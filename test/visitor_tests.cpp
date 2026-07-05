#include <ranges>
#include <span>
#include <string>
#include <vector>

#include <doctest/doctest.h>
#include <cpp-tree-sitter.h>

extern "C" TSLanguage* tree_sitter_json();

namespace {

ts::Tree parseJson(std::string_view source) {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  return parser.parse(source).value();
}


// Recording enter and leave events for test cases.
struct TracingVisitor {
  std::vector<std::string> events;

  ts::VisitAction onEnter(this TracingVisitor& self, ts::Node node) {
    self.events.push_back("+" + std::string{node.getType()});
    return ts::VisitAction::Continue;
  }
  void onLeave(this TracingVisitor& self, ts::Node node) {
    self.events.push_back("-" + std::string{node.getType()});
  }
};


// Enter-only visitors.
struct PruningVisitor {
  std::vector<std::string> entered;
  ts::VisitAction onEnter(this PruningVisitor& self, ts::Node node) {
    self.entered.emplace_back(node.getType());
    return node.getType() == "object" ? ts::VisitAction::SkipChildren
                                      : ts::VisitAction::Continue;
  }
};

struct StoppingVisitor {
  std::vector<std::string> entered;
  ts::VisitAction onEnter(this StoppingVisitor& self, ts::Node node) {
    self.entered.emplace_back(node.getType());
    return node.getType() == "number" ? ts::VisitAction::Stop
                                      : ts::VisitAction::Continue;
  }
};

struct WrongReturn {
  void onEnter(this WrongReturn&, ts::Node) { }
};


// TracingVisitor plus a pruning/stopping decision, so the interaction
// between a VisitAction and onLeave is observable.
struct PruningTracingVisitor {
  std::vector<std::string> events;

  ts::VisitAction onEnter(this PruningTracingVisitor& self, ts::Node node) {
    self.events.push_back("+" + std::string{node.getType()});
    return node.getType() == "array" ? ts::VisitAction::SkipChildren
                                     : ts::VisitAction::Continue;
  }
  void onLeave(this PruningTracingVisitor& self, ts::Node node) {
    self.events.push_back("-" + std::string{node.getType()});
  }
};


struct StoppingTracingVisitor {
  std::vector<std::string> events;

  ts::VisitAction onEnter(this StoppingTracingVisitor& self, ts::Node node) {
    self.events.push_back("+" + std::string{node.getType()});
    return node.getType() == "number" ? ts::VisitAction::Stop
                                      : ts::VisitAction::Continue;
  }
  void onLeave(this StoppingTracingVisitor& self, ts::Node node) {
    self.events.push_back("-" + std::string{node.getType()});
  }
};


// Rebuilds the tree shape as a string of the form type(child,child,...).
struct TreeToString {
  std::string onNode(this TreeToString&, ts::Node node,
                     std::span<std::string> children) {
    std::string out{node.getType()};
    if (!children.empty()) {
      out += '(';
      for (size_t i = 0; i < children.size(); ++i) {
        if (i != 0) { out += ','; }
        out += children[i];
      }
      out += ')';
    }
    return out;
  }
};


// The motivating use case, synthesizing a domain value from the CST.
struct SumNumbers {
  std::string_view source;

  int onNode(this SumNumbers& self, ts::Node node, std::span<int> children) {
    int total = 0;
    for (int const value : children) { total += value; }
    if (node.getType() == "number") {
      total += std::stoi(std::string{node.getSourceRange(self.source)});
    }
    return total;
  }
};

}  // namespace


static_assert(ts::TreeVisitor<TracingVisitor>);
static_assert(ts::LeavingTreeVisitor<TracingVisitor>);
static_assert(ts::TreeVisitor<PruningVisitor>);
static_assert(!ts::LeavingTreeVisitor<PruningVisitor>);
static_assert(!ts::TreeVisitor<WrongReturn>);
static_assert(!ts::TreeVisitor<int>);

static_assert(ts::LeavingTreeVisitor<PruningTracingVisitor>);
static_assert(ts::LeavingTreeVisitor<StoppingTracingVisitor>);

static_assert(ts::TreeFolder<TreeToString, std::string>);
static_assert(!ts::TreeFolder<TreeToString, int>);
static_assert(ts::TreeFolder<SumNumbers, int>);


TEST_CASE("visit performs a full pre/post-order walk") {
  const ts::Tree tree = parseJson("[1]");
  TracingVisitor visitor;
  ts::visit(tree.getRootNode(), visitor);

  // document over array over [ , number, ], so enters and leaves must nest.
  std::vector<std::string> expected{
    "+document", "+array", "+[", "-[", "+number", "-number", "+]", "-]",
    "-array", "-document",
  };
  CHECK(visitor.events == expected);
}


TEST_CASE("SkipChildren prunes a subtree; the walk continues elsewhere") {
  const ts::Tree tree = parseJson(R"([{"a": 1}, 2])");
  PruningVisitor visitor;
  ts::visit(tree.getRootNode(), visitor);

  // The object is entered but not descended into.
  CHECK(visitor.entered
        == std::vector<std::string>{"document", "array", "[", "object",
                                    ",", "number", "]"});
}


TEST_CASE("Stop halts the walk immediately") {
  const ts::Tree tree = parseJson("[1, 2, 3]");
  StoppingVisitor visitor;
  ts::visit(tree.getRootNode(), visitor);
  CHECK(visitor.entered
        == std::vector<std::string>{"document", "array", "[", "number"});
}


TEST_CASE("visiting a leaf node runs enter then leave once") {
  const ts::Tree tree = parseJson("[1]");
  ts::Node const number = tree.getRootNode()
                        .getNamedChild(0).value()
                        .getNamedChild(0).value();
  TracingVisitor visitor;
  ts::visit(number, visitor);
  CHECK(visitor.events == std::vector<std::string>{"+number", "-number"});
}


TEST_CASE("onLeave fires for a pruned (SkipChildren) node") {
  const ts::Tree tree = parseJson("[1]");
  PruningTracingVisitor visitor;
  ts::visit(tree.getRootNode(), visitor);

  // The array's children are never entered, but onLeave still fires for the
  // pruned "array" node and the walk goes on to leave "document".
  std::vector<std::string> expected{"+document", "+array", "-array", "-document"};
  CHECK(visitor.events == expected);
}


TEST_CASE("Stop suppresses onLeave for the stopping node and its ancestors") {
  const ts::Tree tree = parseJson("[1]");
  StoppingTracingVisitor visitor;
  ts::visit(tree.getRootNode(), visitor);

  std::vector<std::string> expected{"+document", "+array", "+[", "-[", "+number"};
  CHECK(visitor.events == expected);
}


TEST_CASE("preorder visits every node exactly once, root first") {
  const ts::Tree tree = parseJson("[1]");
  std::vector<std::string> types;
  for (ts::Node const node : ts::preorder(tree.getRootNode())) {
    types.emplace_back(node.getType());
  }
  CHECK(types == std::vector<std::string>{"document", "array", "[", "number", "]"});
}


TEST_CASE("preorder composes with range adaptors") {
  const ts::Tree tree = parseJson(R"({"a": 1, "b": 2})");
  auto numbers = ts::preorder(tree.getRootNode())
    | std::views::filter([](ts::Node n) { return n.getType() == "number"; });
  CHECK(std::ranges::distance(numbers) == 2);
}


TEST_CASE("fold rebuilds structure bottom-up") {
  const ts::Tree tree = parseJson("[1]");
  TreeToString folder;
  CHECK(ts::fold<std::string>(tree.getRootNode(), folder)
        == "document(array([,number,]))");
}


TEST_CASE("fold on a leaf node receives an empty child span") {
  const ts::Tree tree = parseJson("[1]");
  ts::Node const number = tree.getRootNode()
                        .getNamedChild(0).value()
                        .getNamedChild(0).value();
  TreeToString folder;
  CHECK(ts::fold<std::string>(number, folder) == "number");
}


TEST_CASE("fold synthesizes domain values across nesting") {
  constexpr std::string_view source = "[1, [2, 3], {\"a\": 4}]";
  const ts::Tree tree = parseJson(source);
  SumNumbers folder{source};
  CHECK(ts::fold<int>(tree.getRootNode(), folder) == 10);
}
