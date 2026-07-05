#include <concepts>
#include <ranges>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

#include <doctest/doctest.h>
#include <cpp-tree-sitter.h>
#include <cts/typed.h>

extern "C" TSLanguage* tree_sitter_json();
extern "C" TSLanguage* tree_sitter_ctstest();

namespace {

// Callable signatures used by the compile-time checks below.
struct EnterAction { ts::VisitAction operator()(ts::Node) const { return ts::VisitAction::Continue; } };
struct EnterVoid { void operator()(ts::Node) const { } };
struct WalkSig { void operator()(ts::Node, ts::Walk&) const { } };
struct LeafInt { int operator()(ts::Node) const { return 0; } };
struct FoldWalkInt { int operator()(ts::Node, ts::WalkFold<int>&) const { return 0; } };


ts::Tree parseJson(std::string_view source) {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  return parser.parse(source).value();
}


ts::Tree parseCtstest(std::string_view source) {
  auto parser = ts::Parser::create(tree_sitter_ctstest()).value();
  return parser.parse(source).value();
}

}  // namespace


static_assert(ts::detail::VisitEnterCallable<EnterAction>);
static_assert(ts::detail::VisitEnterCallable<EnterVoid>);
static_assert(ts::detail::VisitWalkCallable<WalkSig>);
static_assert(ts::detail::VisitCallable<EnterAction>);
static_assert(ts::detail::VisitCallable<WalkSig>);

static_assert(!ts::detail::VisitEnterCallable<LeafInt>);
static_assert(!ts::detail::VisitEnterCallable<WalkSig>);
static_assert(!ts::detail::VisitWalkCallable<EnterAction>);
static_assert(!ts::detail::VisitCallable<LeafInt>);

static_assert(ts::detail::FoldLeafCallable<LeafInt, int>);
static_assert(ts::detail::FoldWalkCallable<FoldWalkInt, int>);
static_assert(ts::detail::FoldCallable<LeafInt, int>);
static_assert(!ts::detail::FoldCallable<EnterAction, int>);
static_assert(!ts::detail::FoldCallable<EnterVoid, int>);

// Neither walk proxy may be copied or moved out of the handler.
static_assert(!std::copyable<ts::Walk>);
static_assert(!std::movable<ts::Walk>);
static_assert(!std::copyable<ts::WalkFold<int>>);
static_assert(!std::movable<ts::WalkFold<int>>);

static_assert(std::is_constructible_v<ts::Walk, void*, ts::Walk::RunFn, bool*>);
static_assert(std::is_constructible_v<ts::WalkFold<int>,
                                      void*, ts::WalkFold<int>::RunFn>);

static_assert(ts::detail::isEnterSpec<decltype(ts::on<"pair">(EnterVoid{}))>);
static_assert(ts::detail::isLeaveSpec<decltype(ts::onLeave<"pair">(EnterVoid{}))>);
static_assert(ts::detail::isOtherwiseSpec<decltype(ts::otherwise(EnterVoid{}))>);
static_assert(!ts::detail::isEnterSpec<decltype(ts::otherwise(EnterVoid{}))>);
static_assert(decltype(ts::on<"true", "false">(EnterVoid{}))::names.size() == 2);
static_assert(decltype(ts::on<"pair">(EnterVoid{}))::names[0] == "pair");

// specNames() surfaces the same names through the type-erased accessor
// used by the dispatch tables, and is empty for otherwise (it binds none).
static_assert(
  ts::detail::specNames<decltype(ts::on<"true", "false">(EnterVoid{}))>()
    .size()
  == 2);
static_assert(
  ts::detail::specNames<decltype(ts::on<"true", "false">(EnterVoid{}))>()[0]
  == "true");
static_assert(
  ts::detail::specNames<decltype(ts::otherwise(EnterVoid{}))>().empty());

// otherwiseIndexOf() locates the otherwise spec in a pack, or reports
// "not found" as the pack size (a valid one-past-the-end sentinel).
static_assert(
  ts::detail::otherwiseIndexOf<
    decltype(ts::on<"pair">(EnterVoid{})),
    decltype(ts::otherwise(EnterVoid{})),
    decltype(ts::on<"array">(EnterVoid{}))>()
  == 1);
static_assert(
  ts::detail::otherwiseIndexOf<
    decltype(ts::on<"pair">(EnterVoid{})),
    decltype(ts::on<"array">(EnterVoid{}))>()
  == 2);

// VisitSpecPack requires every spec to carry a signature ts::typed accepts,
// and allows at most one otherwise.
static_assert(ts::detail::VisitSpecPack<
  decltype(ts::on<"pair">(EnterVoid{})),
  decltype(ts::otherwise(EnterVoid{}))>);
static_assert(!ts::detail::VisitSpecPack<
  decltype(ts::otherwise(EnterVoid{})),
  decltype(ts::otherwise(EnterVoid{}))>);
static_assert(!ts::detail::VisitSpecPack<
  decltype(ts::on<"x">(LeafInt{})),
  decltype(ts::otherwise(EnterVoid{}))>);

// FoldSpecPack requires R to be a real result type and every spec to carry a
// signature ts::typedFold accepts. onLeave is never valid in fold context, and
// exactly one otherwise is required, since every node must produce an R.
static_assert(ts::detail::FoldSpecPack<
  int,
  decltype(ts::on<"x">(LeafInt{})),
  decltype(ts::otherwise(LeafInt{}))>);
static_assert(!ts::detail::FoldSpecPack<
  int,
  decltype(ts::on<"x">(LeafInt{}))>);
static_assert(!ts::detail::FoldSpecPack<
  int,
  decltype(ts::onLeave<"x">(EnterVoid{})),
  decltype(ts::otherwise(LeafInt{}))>);
static_assert(!ts::detail::FoldSpecPack<
  int,
  decltype(ts::on<"x">(EnterAction{})),
  decltype(ts::otherwise(LeafInt{}))>);

namespace {
// SFINAE probe for the typed() constraint (negative signature tests).
template <typename... Ss>
concept CanTyped = requires(ts::Language language, Ss... ss) {
  ts::typed(language, ss...);
};
}

static_assert(CanTyped<decltype(ts::on<"number">(EnterVoid{}))>);
static_assert(CanTyped<decltype(ts::on<"number">(EnterAction{})),
                       decltype(ts::otherwise(EnterVoid{}))>);
// Value-returning handlers are not ts::typed handlers.
static_assert(!CanTyped<decltype(ts::on<"number">(LeafInt{}))>);
// At most one otherwise.
static_assert(!CanTyped<decltype(ts::otherwise(EnterVoid{})),
                        decltype(ts::otherwise(EnterAction{}))>);


TEST_CASE("typed dispatches exact type handlers and defaults to Continue") {
  const ts::Tree tree = parseJson(R"({"a": [1, 2], "b": 3})");
  int numbers = 0;
  int arrays = 0;
  auto visitor = ts::typed(ts::Language{tree_sitter_json()},
    ts::on<"number">([&](ts::Node) { ++numbers; }),
    ts::on<"array">([&](ts::Node) { ++arrays; }));
  REQUIRE(visitor.has_value());
  static_assert(ts::TreeVisitor<std::remove_reference_t<decltype(*visitor)>>);
  ts::visit(tree.getRootNode(), *visitor);
  CHECK(numbers == 3);   // unhandled object/pair/string types still descend
  CHECK(arrays == 1);
}


TEST_CASE("one handler can bind several type names") {
  const ts::Tree tree = parseJson(R"([true, false, true])");
  int booleans = 0;
  auto visitor = ts::typed(ts::Language{tree_sitter_json()},
    ts::on<"true", "false">([&](ts::Node) { ++booleans; }));
  REQUIRE(visitor.has_value());
  ts::visit(tree.getRootNode(), *visitor);
  CHECK(booleans == 3);
}


TEST_CASE("handler VisitActions pass through to the engine") {
  const ts::Tree tree = parseJson(R"({"a": [1, 2], "b": 3})");
  int numbers = 0;
  auto pruning = ts::typed(ts::Language{tree_sitter_json()},
    ts::on<"array">([](ts::Node) { return ts::VisitAction::SkipChildren; }),
    ts::on<"number">([&](ts::Node) { ++numbers; }));
  REQUIRE(pruning.has_value());
  ts::visit(tree.getRootNode(), *pruning);
  CHECK(numbers == 1);   // the two numbers inside the array were pruned

  int entered = 0;
  auto stopping = ts::typed(ts::Language{tree_sitter_json()},
    ts::on<"number">([&](ts::Node) {
      ++entered;
      return ts::VisitAction::Stop;
    }));
  REQUIRE(stopping.has_value());
  ts::visit(tree.getRootNode(), *stopping);
  CHECK(entered == 1);
}


TEST_CASE("otherwise fires for every unhandled type, anonymous included") {
  const ts::Tree tree = parseJson("[1]");   // document array [ number ]
  int handled = 0;
  int rest = 0;
  auto visitor = ts::typed(ts::Language{tree_sitter_json()},
    ts::on<"number">([&](ts::Node) { ++handled; }),
    ts::otherwise([&](ts::Node) { ++rest; }));
  REQUIRE(visitor.has_value());
  ts::visit(tree.getRootNode(), *visitor);
  CHECK(handled == 1);
  CHECK(rest == 4);      // document, array, "[", "]"
}


TEST_CASE("anonymous type names bind punctuation nodes") {
  const ts::Tree tree = parseCtstest("1 + 2");
  int plus = 0;
  auto visitor = ts::typed(ts::Language{tree_sitter_ctstest()},
    ts::on<"+">([&](ts::Node) { ++plus; }));
  REQUIRE(visitor.has_value());
  ts::visit(tree.getRootNode(), *visitor);
  CHECK(plus == 1);
}


TEST_CASE("alias node types dispatch by their surfaced name") {
  const ts::Tree tree = parseCtstest("a: 1");
  int keys = 0;
  int identifiers = 0;
  auto visitor = ts::typed(ts::Language{tree_sitter_ctstest()},
    ts::on<"key">([&](ts::Node) { ++keys; }),
    ts::on<"identifier">([&](ts::Node) { ++identifiers; }));
  REQUIRE(visitor.has_value());
  ts::visit(tree.getRootNode(), *visitor);
  CHECK(keys == 1);
  CHECK(identifiers == 0);   // the only identifier token is aliased to key
}


TEST_CASE("typed rejects unknown node type names") {
  auto visitor = ts::typed(ts::Language{tree_sitter_json()},
    ts::on<"nubmer">([](ts::Node) { }));
  REQUIRE(!visitor.has_value());
  CHECK(visitor.error().kind == ts::ErrorKind::VisitorNodeType);
}


TEST_CASE("supertype names are unknown on an ABI-14 grammar") {
  // json's _value is a supertype in node-types.json, but ABI 14 carries no
  // runtime supertype metadata, so the name cannot resolve.
  auto visitor = ts::typed(ts::Language{tree_sitter_json()},
    ts::on<"_value">([](ts::Node) { }));
  REQUIRE(!visitor.has_value());
  CHECK(visitor.error().kind == ts::ErrorKind::VisitorNodeType);
}


TEST_CASE("typed rejects duplicate handlers for one type") {
  auto visitor = ts::typed(ts::Language{tree_sitter_json()},
    ts::on<"number">([](ts::Node) { }),
    ts::on<"number">([](ts::Node) { }));
  REQUIRE(!visitor.has_value());
  CHECK(visitor.error().kind == ts::ErrorKind::VisitorDuplicate);
  REQUIRE(visitor.error().hasName());
  CHECK(visitor.error().name == "number");
}


TEST_CASE("unknown node type errors name the handler name that failed") {
  // Several handlers with one bad name. The error must report the name that
  // failed to resolve rather than whichever was listed first.
  auto visitor = ts::typed(ts::Language{tree_sitter_json()},
    ts::on<"string">([](ts::Node) { }),
    ts::on<"nubmer">([](ts::Node) { }));
  REQUIRE(!visitor.has_value());
  REQUIRE(visitor.error().hasName());
  CHECK(visitor.error().name == "nubmer");
}


TEST_CASE("supertype handlers fire for every concrete subtype") {
  const ts::Tree tree = parseCtstest(R"(1 2.5 "s")");
  int literals = 0;
  auto visitor = ts::typed(ts::Language{tree_sitter_ctstest()},
    ts::on<"_literal">([&](ts::Node) { ++literals; }));
  REQUIRE(visitor.has_value());
  ts::visit(tree.getRootNode(), *visitor);
  CHECK(literals == 3);  // number and float via nested _numeric, plus string
}


TEST_CASE("nested supertype handlers win over outer ones") {
  const ts::Tree tree = parseCtstest(R"(1 2.5 "s")");
  int numerics = 0;
  int literals = 0;
  auto visitor = ts::typed(ts::Language{tree_sitter_ctstest()},
    ts::on<"_literal">([&](ts::Node) { ++literals; }),
    ts::on<"_numeric">([&](ts::Node) { ++numerics; }));
  REQUIRE(visitor.has_value());
  ts::visit(tree.getRootNode(), *visitor);
  CHECK(numerics == 2);  // number, float
  CHECK(literals == 1);  // string only, _numeric types went to the inner handler
}


TEST_CASE("exact handlers win over supertype handlers") {
  const ts::Tree tree = parseCtstest("1 2.5");
  int numbers = 0;
  int numerics = 0;
  auto visitor = ts::typed(ts::Language{tree_sitter_ctstest()},
    ts::on<"number">([&](ts::Node) { ++numbers; }),
    ts::on<"_numeric">([&](ts::Node) { ++numerics; }));
  REQUIRE(visitor.has_value());
  ts::visit(tree.getRootNode(), *visitor);
  CHECK(numbers == 1);
  CHECK(numerics == 1);  // only the float
}


TEST_CASE("incomparable supertype overlap is rejected") {
  // _literal and _stringy both contain string, and neither nests in the other.
  auto visitor = ts::typed(ts::Language{tree_sitter_ctstest()},
    ts::on<"_literal">([](ts::Node) { }),
    ts::on<"_stringy">([](ts::Node) { }));
  REQUIRE(!visitor.has_value());
  CHECK(visitor.error().kind == ts::ErrorKind::VisitorSupertypeAmbiguity);
  // The error names the contested concrete type rather than either handler,
  // since the handler names are ones the caller already wrote.
  REQUIRE(visitor.error().hasName());
  CHECK(visitor.error().name == "string");
}


TEST_CASE("diamond overlap resolves to the innermost handler in any spec order") {
  // _numeric is nested in both _literal and _scalar, which are incomparable.
  // The _numeric handler must win for number and float whether it is listed
  // first or last.
  const ts::Tree tree = parseCtstest(R"(1 2.5 "s" [x])");
  ts::Language const language{tree_sitter_ctstest()};

  int numerics = 0;
  int literals = 0;
  int scalars = 0;
  auto innermostLast = ts::typed(language,
    ts::on<"_literal">([&](ts::Node) { ++literals; }),
    ts::on<"_scalar">([&](ts::Node) { ++scalars; }),
    ts::on<"_numeric">([&](ts::Node) { ++numerics; }));
  REQUIRE(innermostLast.has_value());
  ts::visit(tree.getRootNode(), *innermostLast);
  CHECK(numerics == 2);  // number, float
  CHECK(literals == 1);  // string
  CHECK(scalars == 1);   // identifier inside index

  numerics = literals = scalars = 0;
  auto innermostFirst = ts::typed(language,
    ts::on<"_numeric">([&](ts::Node) { ++numerics; }),
    ts::on<"_literal">([&](ts::Node) { ++literals; }),
    ts::on<"_scalar">([&](ts::Node) { ++scalars; }));
  REQUIRE(innermostFirst.has_value());
  ts::visit(tree.getRootNode(), *innermostFirst);
  CHECK(numerics == 2);
  CHECK(literals == 1);
  CHECK(scalars == 1);
}


TEST_CASE("diamond overlap without the inner handler is ambiguous") {
  // number and float sit under both _literal and _scalar with no handler
  // below, so the minimal covering binds span two handlers.
  ts::Language const language{tree_sitter_ctstest()};
  auto oneOrder = ts::typed(language,
    ts::on<"_literal">([](ts::Node) { }),
    ts::on<"_scalar">([](ts::Node) { }));
  REQUIRE(!oneOrder.has_value());
  CHECK(oneOrder.error().kind == ts::ErrorKind::VisitorSupertypeAmbiguity);

  auto otherOrder = ts::typed(language,
    ts::on<"_scalar">([](ts::Node) { }),
    ts::on<"_literal">([](ts::Node) { }));
  REQUIRE(!otherOrder.has_value());
  CHECK(otherOrder.error().kind == ts::ErrorKind::VisitorSupertypeAmbiguity);
}


TEST_CASE("pair handler spanning a nested and an incomparable supertype is still ambiguous") {
  // _expr contains _literal but is incomparable to _scalar. A handler on
  // <_expr, _scalar> is minimal via _scalar for number and float, and the
  // other handler's _literal is minimal too. That is two minimal covering
  // binds owned by two handlers, so this stays ambiguous even though _expr
  // subsumes _literal.
  ts::Language const language{tree_sitter_ctstest()};
  auto oneOrder = ts::typed(language,
    ts::on<"_expr", "_scalar">([](ts::Node) { }),
    ts::on<"_literal">([](ts::Node) { }));
  REQUIRE(!oneOrder.has_value());
  CHECK(oneOrder.error().kind == ts::ErrorKind::VisitorSupertypeAmbiguity);

  auto otherOrder = ts::typed(language,
    ts::on<"_literal">([](ts::Node) { }),
    ts::on<"_expr", "_scalar">([](ts::Node) { }));
  REQUIRE(!otherOrder.has_value());
  CHECK(otherOrder.error().kind == ts::ErrorKind::VisitorSupertypeAmbiguity);
}


TEST_CASE("supertype resolution ignores name order within one handler") {
  // One handler covers number via both _scalar and _literal, while _expr
  // contains _literal but not _scalar. The pair handler is nearer via
  // _literal, so it must win no matter which of its names is listed first.
  const ts::Tree tree = parseCtstest("1 [x] (2)");
  ts::Language const language{tree_sitter_ctstest()};

  int pairCount = 0;
  int exprCount = 0;
  auto scalarFirst = ts::typed(language,
    ts::on<"_scalar", "_literal">([&](ts::Node) { ++pairCount; }),
    ts::on<"_expr">([&](ts::Node) { ++exprCount; }));
  REQUIRE(scalarFirst.has_value());
  ts::visit(tree.getRootNode(), *scalarFirst);
  CHECK(pairCount == 3);  // 1, x, 2
  CHECK(exprCount == 1);  // the paren node

  pairCount = exprCount = 0;
  auto literalFirst = ts::typed(language,
    ts::on<"_literal", "_scalar">([&](ts::Node) { ++pairCount; }),
    ts::on<"_expr">([&](ts::Node) { ++exprCount; }));
  REQUIRE(literalFirst.has_value());
  ts::visit(tree.getRootNode(), *literalFirst);
  CHECK(pairCount == 3);
  CHECK(exprCount == 1);
}


TEST_CASE("diamond overlap resolves to the innermost handler in onLeave") {
  // The diamond from the `on` cases above, written with onLeave instead,
  // innermost listed last.
  const ts::Tree tree = parseCtstest(R"(1 2.5 "s" [x])");
  int numericLeft = 0;
  int literalLeft = 0;
  int scalarLeft = 0;
  auto visitor = ts::typed(ts::Language{tree_sitter_ctstest()},
    ts::onLeave<"_literal">([&](ts::Node) { ++literalLeft; }),
    ts::onLeave<"_scalar">([&](ts::Node) { ++scalarLeft; }),
    ts::onLeave<"_numeric">([&](ts::Node) { ++numericLeft; }));
  REQUIRE(visitor.has_value());
  ts::visit(tree.getRootNode(), *visitor);
  CHECK(numericLeft == 2);
  CHECK(literalLeft == 1);
  CHECK(scalarLeft == 1);
}


TEST_CASE("ERROR is a matchable type name, preferred over otherwise") {
  const ts::Tree tree = parseCtstest("(((");   // (source (ERROR))
  int errors = 0;
  int rest = 0;
  auto visitor = ts::typed(ts::Language{tree_sitter_ctstest()},
    ts::on<"ERROR">([&](ts::Node) { ++errors; }),
    ts::otherwise([&](ts::Node) { ++rest; }));
  REQUIRE(visitor.has_value());
  ts::visit(tree.getRootNode(), *visitor);
  CHECK(errors == 1);
  CHECK(rest >= 1);   // source, plus any tokens inside the ERROR node
}


TEST_CASE("ERROR nodes fall through to otherwise or the default") {
  const ts::Tree tree = parseCtstest("(((");   // (source (ERROR))

  int numbers = 0;
  auto plain = ts::typed(ts::Language{tree_sitter_ctstest()},
    ts::on<"number">([&](ts::Node) { ++numbers; }));
  REQUIRE(plain.has_value());
  ts::visit(tree.getRootNode(), *plain);
  CHECK(numbers == 0);

  // With otherwise, the ERROR node reaches it.
  bool sawError = false;
  auto withOtherwise = ts::typed(ts::Language{tree_sitter_ctstest()},
    ts::otherwise([&](ts::Node node) {
      sawError |= node.getType() == "ERROR";
    }));
  REQUIRE(withOtherwise.has_value());
  ts::visit(tree.getRootNode(), *withOtherwise);
  CHECK(sawError);
}


TEST_CASE("typed visitors satisfy LeavingTreeVisitor iff leave handlers exist") {
  auto withLeave = ts::typed(ts::Language{tree_sitter_json()},
    ts::onLeave<"array">([](ts::Node) { }));
  REQUIRE(withLeave.has_value());
  static_assert(
    ts::LeavingTreeVisitor<std::remove_reference_t<decltype(*withLeave)>>);

  auto enterOnly = ts::typed(ts::Language{tree_sitter_json()},
    ts::on<"array">([](ts::Node) { }));
  REQUIRE(enterOnly.has_value());
  static_assert(
    ts::TreeVisitor<std::remove_reference_t<decltype(*enterOnly)>>);
  static_assert(
    !ts::LeavingTreeVisitor<std::remove_reference_t<decltype(*enterOnly)>>);
}


TEST_CASE("enter and leave handlers bracket a subtree") {
  const ts::Tree tree = parseJson("[1]");
  std::vector<std::string> events;
  auto visitor = ts::typed(ts::Language{tree_sitter_json()},
    ts::on<"array">([&](ts::Node) { events.emplace_back("+array"); }),
    ts::on<"number">([&](ts::Node) { events.emplace_back("+number"); }),
    ts::onLeave<"array">([&](ts::Node) { events.emplace_back("-array"); }),
    ts::onLeave<"number">([&](ts::Node) { events.emplace_back("-number"); }));
  REQUIRE(visitor.has_value());
  ts::visit(tree.getRootNode(), *visitor);
  CHECK(events == std::vector<std::string>{
    "+array", "+number", "-number", "-array"});
}


TEST_CASE("onLeave fires for a node whose enter handler pruned children") {
  const ts::Tree tree = parseJson("[1]");
  std::vector<std::string> events;
  auto visitor = ts::typed(ts::Language{tree_sitter_json()},
    ts::on<"array">([&](ts::Node) {
      events.emplace_back("+array");
      return ts::VisitAction::SkipChildren;
    }),
    ts::on<"number">([&](ts::Node) { events.emplace_back("+number"); }),
    ts::onLeave<"array">([&](ts::Node) { events.emplace_back("-array"); }));
  REQUIRE(visitor.has_value());
  ts::visit(tree.getRootNode(), *visitor);
  CHECK(events == std::vector<std::string>{"+array", "-array"});
}


TEST_CASE("supertype names work in onLeave") {
  const ts::Tree tree = parseCtstest("1 2.5");
  int left = 0;
  auto visitor = ts::typed(ts::Language{tree_sitter_ctstest()},
    ts::onLeave<"_numeric">([&](ts::Node) { ++left; }));
  REQUIRE(visitor.has_value());
  ts::visit(tree.getRootNode(), *visitor);
  CHECK(left == 2);
}


TEST_CASE("onLeave fires for ERROR nodes when registered") {
  const ts::Tree tree = parseCtstest("(((");
  int left = 0;
  auto visitor = ts::typed(ts::Language{tree_sitter_ctstest()},
    ts::onLeave<"ERROR">([&](ts::Node) { ++left; }));
  REQUIRE(visitor.has_value());
  ts::visit(tree.getRootNode(), *visitor);
  CHECK(left == 1);
}


TEST_CASE("a type may have a leave handler without an enter handler") {
  const ts::Tree tree = parseJson("[1, 2]");
  int left = 0;
  auto visitor = ts::typed(ts::Language{tree_sitter_json()},
    ts::onLeave<"number">([&](ts::Node) { ++left; }));
  REQUIRE(visitor.has_value());
  ts::visit(tree.getRootNode(), *visitor);
  CHECK(left == 2);
}


TEST_CASE("a walk handler controls descent order and interleaves logic") {
  const ts::Tree tree = parseCtstest("x = y");
  std::vector<std::string> events;
  constexpr std::string_view source = "x = y";
  auto visitor = ts::typed(ts::Language{tree_sitter_ctstest()},
    ts::on<"assignment">([&](ts::Node node, ts::Walk& walk) {
      events.emplace_back("begin");
      // ChildIterator is move-only (it owns a Cursor), so collect via a
      // range-for rather than the vector iterator-pair constructor.
      std::vector<ts::Node> children;
      for (ts::Node const child : node.getNamedChildren()) {
        children.push_back(child);
      }
      for (auto& it : std::ranges::reverse_view(children)) {
        walk(it);
      }
      events.emplace_back("end");
    }),
    ts::on<"identifier">([&](ts::Node node) {
      events.emplace_back(node.getSourceRange(source));
    }));
  REQUIRE(visitor.has_value());
  ts::visit(tree.getRootNode(), *visitor);
  CHECK(events == std::vector<std::string>{"begin", "y", "x", "end"});
}


TEST_CASE("walked subtrees reproduce engine enter/leave bracketing") {
  constexpr std::string_view source = "(1 + 2)";
  const ts::Tree tree = parseCtstest(source);
  ts::Language const language{tree_sitter_ctstest()};

  auto record = [](std::vector<std::string>& events) {
    return std::tuple{
      ts::otherwise([&events](ts::Node node) {
        events.push_back("+" + std::string{node.getType()});
      }),
      ts::onLeave<"source", "binary", "paren", "number", "+", "(", ")">(
        [&events](ts::Node node) {
          events.push_back("-" + std::string{node.getType()});
        })};
  };

  std::vector<std::string> engineEvents;
  auto [engineOtherwise, engineLeave] = record(engineEvents);
  auto engineVisitor = ts::typed(language, engineOtherwise, engineLeave);
  REQUIRE(engineVisitor.has_value());
  ts::visit(tree.getRootNode(), *engineVisitor);

  std::vector<std::string> walkEvents;
  auto [walkOtherwise, walkLeave] = record(walkEvents);
  auto walkVisitor = ts::typed(language,
    ts::on<"paren">([&walkEvents](ts::Node node, ts::Walk& walk) {
      walkEvents.push_back("+" + std::string{node.getType()});
      for (ts::Node const child : node.getChildren()) {
        walk(child);
      }
      // "-paren" comes from the engine calling onLeave after SkipChildren.
    }),
    walkOtherwise, walkLeave);
  REQUIRE(walkVisitor.has_value());
  ts::visit(tree.getRootNode(), *walkVisitor);

  CHECK(walkEvents == engineEvents);
}


TEST_CASE("Stop from inside a walked subtree ends the whole traversal") {
  constexpr std::string_view source = "(1 + 2) 3";
  const ts::Tree tree = parseCtstest(source);
  std::vector<std::string> numbers;
  auto visitor = ts::typed(ts::Language{tree_sitter_ctstest()},
    ts::on<"paren">([&](ts::Node node, ts::Walk& walk) {
      for (ts::Node const child : node.getChildren()) {
        walk(child);
      }
      numbers.emplace_back("after-walk");  // still runs, the handler isn't preempted
    }),
    ts::on<"number">([&](ts::Node node) {
      numbers.emplace_back(node.getSourceRange(source));
      return ts::VisitAction::Stop;   // first number stops
    }));
  REQUIRE(visitor.has_value());
  ts::visit(tree.getRootNode(), *visitor);
  // "1" is recorded and Stop propagates, so "2" is never visited. The walk
  // handler still finishes its own body, and the adapter returns Stop to the
  // engine, so the trailing "3" outside the paren is never reached.
  CHECK(numbers == std::vector<std::string>{"1", "after-walk"});
}


TEST_CASE("walk.stop ends the traversal from inside a walk handler") {
  constexpr std::string_view source = "(1 + 2) 3";
  const ts::Tree tree = parseCtstest(source);
  std::vector<std::string> numbers;
  auto visitor = ts::typed(ts::Language{tree_sitter_ctstest()},
    ts::on<"paren">([&](ts::Node node, ts::Walk& walk) {
      auto first = node.getNamedChild(0);   // the binary node
      REQUIRE(first.has_value());
      walk(*first);
      walk.stop();
      walk(*first);   // no-op after stop
    }),
    ts::on<"number">([&](ts::Node node) {
      numbers.emplace_back(node.getSourceRange(source));
    }));
  REQUIRE(visitor.has_value());
  ts::visit(tree.getRootNode(), *visitor);
  CHECK(numbers == std::vector<std::string>{"1", "2"});  // "3" never reached
}


TEST_CASE("onLeave fires for the walked node itself") {
  const ts::Tree tree = parseCtstest("(1)");
  std::vector<std::string> events;
  auto visitor = ts::typed(ts::Language{tree_sitter_ctstest()},
    ts::on<"paren">([&](ts::Node, ts::Walk&) {
      events.emplace_back("walked");
      // deliberately walks nothing
    }),
    ts::onLeave<"paren">([&](ts::Node) { events.emplace_back("-paren"); }));
  REQUIRE(visitor.has_value());
  ts::visit(tree.getRootNode(), *visitor);
  CHECK(events == std::vector<std::string>{"walked", "-paren"});
}


namespace {
template <typename R, typename... Ss>
concept CanTypedFold = requires(ts::Language language, Ss... ss) {
  ts::typedFold<R>(language, ss...);
};
}


static_assert(CanTypedFold<int,
  decltype(ts::on<"number">(LeafInt{})),
  decltype(ts::otherwise(LeafInt{}))>);
static_assert(CanTypedFold<int,
  decltype(ts::on<"binary">(FoldWalkInt{})),
  decltype(ts::otherwise(FoldWalkInt{}))>);
// otherwise is required...
static_assert(!CanTypedFold<int, decltype(ts::on<"number">(LeafInt{}))>);
// ...onLeave is rejected...
static_assert(!CanTypedFold<int,
  decltype(ts::onLeave<"number">(EnterVoid{})),
  decltype(ts::otherwise(LeafInt{}))>);
// ...and so are the ts::typed handler signatures.
static_assert(!CanTypedFold<int,
  decltype(ts::on<"number">(EnterAction{})),
  decltype(ts::otherwise(LeafInt{}))>);


TEST_CASE("typedFold evaluates a tree with per-type handlers") {
  constexpr std::string_view source = "(1 + -2) + 4";
  const ts::Tree tree = parseCtstest(source);
  auto folder = ts::typedFold<int>(ts::Language{tree_sitter_ctstest()},
    ts::on<"number">([&](ts::Node node) {
      return std::stoi(std::string{node.getSourceRange(source)});
    }),
    ts::on<"binary">([](ts::Node node, ts::WalkFold<int>& walk) {
      return walk(node.getNamedChild(0).value())
           + walk(node.getNamedChild(1).value());
    }),
    ts::on<"negation">([](ts::Node node, ts::WalkFold<int>& walk) {
      // named child 0 is the minus token, so the operand is child 1.
      return -walk(node.getNamedChild(1).value());
    }),
    ts::on<"paren">([](ts::Node node, ts::WalkFold<int>& walk) {
      return walk(node.getNamedChild(0).value());
    }),
    ts::otherwise([](ts::Node node, ts::WalkFold<int>& walk) {
      int total = 0;
      for (ts::Node const child : node.getNamedChildren()) {
        total += walk(child);
      }
      return total;
    }));
  REQUIRE(folder.has_value());
  CHECK(folder->run(tree.getRootNode()) == 3);
}


TEST_CASE("typedFold construction validates names like typed") {
  auto folder = ts::typedFold<int>(ts::Language{tree_sitter_ctstest()},
    ts::on<"nubmer">([](ts::Node) { return 0; }),
    ts::otherwise([](ts::Node) { return 0; }));
  REQUIRE(!folder.has_value());
  CHECK(folder.error().kind == ts::ErrorKind::VisitorNodeType);
}


TEST_CASE("typedFold supertype handlers dispatch like typed") {
  constexpr std::string_view source = "1 2.5";
  const ts::Tree tree = parseCtstest(source);
  auto folder = ts::typedFold<int>(ts::Language{tree_sitter_ctstest()},
    ts::on<"_numeric">([](ts::Node) { return 1; }),
    ts::otherwise([](ts::Node node, ts::WalkFold<int>& walk) {
      int total = 0;
      for (ts::Node const child : node.getNamedChildren()) {
        total += walk(child);
      }
      return total;
    }));
  REQUIRE(folder.has_value());
  CHECK(folder->run(tree.getRootNode()) == 2);
}


TEST_CASE("typedFold handles ERROR nodes through otherwise") {
  const ts::Tree tree = parseCtstest("(((");
  auto folder = ts::typedFold<int>(ts::Language{tree_sitter_ctstest()},
    ts::otherwise([](ts::Node node, ts::WalkFold<int>& walk) {
      int total = node.getType() == "ERROR" ? 100 : 1;
      for (ts::Node const child : node.getNamedChildren()) {
        total += walk(child);
      }
      return total;
    }));
  REQUIRE(folder.has_value());
  CHECK(folder->run(tree.getRootNode()) == 101);  // source + ERROR
}
