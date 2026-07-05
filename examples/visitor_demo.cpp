// Walking a tree with ts::visit, and the same walk as a lazy range.
//
// A visitor is any object with an onEnter(ts::Node) that returns a
// ts::VisitAction. There is no base class to inherit from. The returned
// action controls the traversal of the walk:
//
//   Continue      descend into this node's children
//   SkipChildren  prune this subtree and move on to the next sibling
//   Stop          end the entire traversal immediately
//
// onLeave(ts::Node) is optional to define. When defined, it fires after
// traversing the children if a node. It still fires for nodes whose
// children were skipped, so enter/leave pairing survives pruning.
//
// visit is cursor-based and does not use recursion or allocation, so tree
// depth does not cost extra space.
//
// ts::preorder exposes a similar API as a lazy range. It is single-pass
// and move-only, but because it is a range, you can materialize it with
// std::ranges::to<std::vector>() as usual.

#include <cstdlib>
#include <print>
#include <ranges>
#include <string_view>

#include <cpp-tree-sitter.h>
#include <cts/format.h>

extern "C" TSLanguage* tree_sitter_json();


namespace {
struct IndentPrinter {
  std::string_view source;
  int depth = 0;

  ts::VisitAction onEnter(ts::Node node) {
    if (node.isNamed()) {
      // An empty string padded to `depth * 2` supplies the indentation.
      std::println("{:{}}{}: {}", "", depth * 2,
                   node.getType(), node.getSourceRange(source));
    }
    ++depth;
    return ts::VisitAction::Continue;
  }
  void onLeave(ts::Node) { --depth; }
};
}


int
main() {
  auto parser = ts::Parser::create(tree_sitter_json());
  if (!parser) {
    std::println(stderr, "error: {}", parser.error());
    return EXIT_FAILURE;
  }

  constexpr std::string_view source = R"({"a": [1, 2], "b": null})";
  auto tree = parser->parse(source);
  if (!tree) {
    std::println(stderr, "error: {}", tree.error());
    return EXIT_FAILURE;
  }

  IndentPrinter printer{.source=source};
  ts::visit(tree->getRootNode(), printer);

  // The same walk as a range that composes with <ranges>. Nothing moves until
  // the range is iterated.
  auto numbers = ts::preorder(tree->getRootNode())
    | std::views::filter([](ts::Node node) {
        return node.getType() == "number";
      });
  for (ts::Node const node : numbers) {
    std::println("number: {}", node.getSourceRange(source));
  }
  return EXIT_SUCCESS;
}
