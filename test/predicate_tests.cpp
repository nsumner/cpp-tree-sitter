// This include comes first deliberately. cts/query/predicates.h must compile
// with nothing else included, which shows ids.h and node.h are reachable
// from it and that it does not need query.h.
#include <cts/query/predicates.h>

#include <algorithm>
#include <functional>
#include <optional>
#include <ranges>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include <doctest/doctest.h>
#include <cpp-tree-sitter.h>
#include <cts/query.h>

#include <stringify.h>

extern "C" TSLanguage* tree_sitter_json();


static_assert(!std::is_convertible_v<ts::CaptureId, ts::PredicateTokenId>);
static_assert(!std::is_convertible_v<ts::CaptureId, uint32_t>);
static_assert(std::is_same_v<std::underlying_type_t<ts::PatternIndex>, uint16_t>);

// classifyPredicate reads the quantifier and the negation out of a predicate
// name. An "any-" prefix means the predicate holds when any captured node
// satisfies it, but any-of? has no such prefix and quantifies its literal
// argument list instead, so it still matches all captured nodes. "not" also
// appears in more than one position.
static_assert(ts::detail::classifyPredicate("eq?")->matchAll);
static_assert(!ts::detail::classifyPredicate("any-eq?")->matchAll);
static_assert(ts::detail::classifyPredicate("any-of?")->matchAll);
static_assert(!ts::detail::classifyPredicate("not-any-of?")->positive);
static_assert(!ts::detail::classifyPredicate("any-not-match?")->positive);
static_assert(!ts::detail::classifyPredicate("any-not-match?")->matchAll);

// Unrecognized predicates and directives are ignored
static_assert(!ts::detail::classifyPredicate("set!"));
static_assert(!ts::detail::classifyPredicate("lua-match?"));
static_assert(!ts::detail::classifyPredicate(""));


TEST_CASE("getPredicates groups the raw steps without interpreting them") {
  // Group the pattern and its predicates in one outer paren. A predicate
  // written at top level after a pattern is parsed as its own pattern-less
  // entry.
  auto query = ts::Query::create(tree_sitter_json(),
    R"(((pair key: (string) @k value: (_) @v)
        (#eq? @k "name")
        (#set! priority 10)))");
  REQUIRE(query.has_value());

  auto predicates = query->getPredicates(ts::PatternIndex{0});
  REQUIRE(predicates.has_value());

  std::vector<std::string> names;
  std::vector<size_t> counts;
  for (ts::Predicate const predicate : *predicates) {
    names.emplace_back(predicate.name);
    counts.push_back(static_cast<size_t>(std::ranges::distance(predicate.args)));
  }
  REQUIRE(names.size() == 2);
  CHECK(names[0] == "eq?");     // '#' stripped, '?' kept
  CHECK(names[1] == "set!");    // a reported but uninterpreted directive
  CHECK(counts[0] == 2);
  CHECK(counts[1] == 2);
}


TEST_CASE("A predicate argument is a capture or a token and never both") {
  auto query = ts::Query::create(tree_sitter_json(),
    R"(((pair key: (string) @k) (#eq? @k "name")))");
  REQUIRE(query.has_value());
  auto predicates = query->getPredicates(ts::PatternIndex{0});
  REQUIRE(predicates.has_value());

  auto first = *std::ranges::begin(*predicates);
  auto args = first.args | std::ranges::to<std::vector<ts::PredicateArg>>();
  REQUIRE(args.size() == 2);
  REQUIRE(std::holds_alternative<ts::CaptureId>(args[0]));
  CHECK(std::get<ts::CaptureId>(args[0]) == *query->getCaptureId("k"));
  REQUIRE(std::holds_alternative<ts::PredicateTokenId>(args[1]));
  CHECK(query->getPredicateToken(std::get<ts::PredicateTokenId>(args[1])) == "name");
}


TEST_CASE("A predicate-free pattern yields an empty range") {
  auto query = ts::Query::create(tree_sitter_json(), "(pair) @p");
  REQUIRE(query.has_value());
  auto predicates = query->getPredicates(ts::PatternIndex{0});
  REQUIRE(predicates.has_value());
  CHECK(std::ranges::begin(*predicates) == std::ranges::end(*predicates));
}


// tree-sitter always terminates a predicate with a Done step, so this test
// fakes the pieces out to anchor the behavior.
TEST_CASE("A predicate array missing its final Done step still terminates") {
  std::vector<TSQueryPredicateStep> steps{
    {TSQueryPredicateStepTypeString, 0},
    {TSQueryPredicateStepTypeCapture, 1},
    {TSQueryPredicateStepTypeDone, 0},
    {TSQueryPredicateStepTypeString, 2},   // second, unclosed predicate
    {TSQueryPredicateStepTypeCapture, 3},
  };
  TSQueryPredicateStep const* const first = steps.data();
  TSQueryPredicateStep const* const last = first + steps.size();

  ts::detail::PredicateIterator it{nullptr, first, last};
  ts::detail::PredicateIterator const end{nullptr, last, last};

  REQUIRE(it != end);
  ++it;                  // past the well-formed predicate
  REQUIRE(it != end);
  ++it;                  // past the unterminated one, must clamp not overrun
  CHECK(it == end);
}


TEST_CASE("An out-of-range pattern index is a lookup miss") {
  auto query = ts::Query::create(tree_sitter_json(), "(pair) @p");
  REQUIRE(query.has_value());
  CHECK(!query->getPredicates(ts::PatternIndex{1}).has_value());
  CHECK(!query->getPredicates(ts::PatternIndex{9999}).has_value());
}


TEST_CASE("Query::create rejects a recognised predicate it cannot compile") {
  auto tooFew = ts::Query::create(tree_sitter_json(),
                                  R"(((pair) @p (#eq? @p)))");
  REQUIRE(!tooFew.has_value());
  CHECK(tooFew.error().kind == ts::ErrorKind::QueryPredicate);

  auto literalFirst = ts::Query::create(tree_sitter_json(),
                                        R"(((pair) @p (#eq? "a" @p)))");
  REQUIRE(!literalFirst.has_value());
  CHECK(literalFirst.error().kind == ts::ErrorKind::QueryPredicate);

  auto captureInAnyOf = ts::Query::create(tree_sitter_json(),
    R"(((pair key: (string) @k value: (_) @v) (#any-of? @k @v)))");
  REQUIRE(!captureInAnyOf.has_value());
  CHECK(captureInAnyOf.error().kind == ts::ErrorKind::QueryPredicate);

  auto matchNeedsLiteral = ts::Query::create(tree_sitter_json(),
    R"(((pair key: (string) @k value: (_) @v) (#match? @k @v)))");
  REQUIRE(!matchNeedsLiteral.has_value());
  CHECK(matchNeedsLiteral.error().kind == ts::ErrorKind::QueryPredicate);
}


TEST_CASE("A predicate error points at the pattern that contains it") {
  constexpr std::string_view source =
    "(object) @o\n"
    "((pair) @p (#eq? @p))";
  auto query = ts::Query::create(tree_sitter_json(), source);
  REQUIRE(!query.has_value());
  CHECK(query.error().kind == ts::ErrorKind::QueryPredicate);
  // >= rather than ==, since whether ts_query_start_byte_for_pattern reports
  // the group's open paren or the inner pattern's start is tree-sitter's
  // business. What matters is that it names the offending pattern.
  CHECK(query.error().offset >= source.find("((pair)"));
  CHECK(query.error().hasOffset());
}


TEST_CASE("An unrecognised predicate name is visible and filters nothing") {
  auto query = ts::Query::create(tree_sitter_json(),
    R"(((pair) @p (#lua-match? @p "x") (#set! kind pair)))");
  REQUIRE(query.has_value());

  // Neither name is evaluated.
  CHECK(!query->hasTextPredicates());
  CHECK(!query->hasUncompiledRegexes());

  // Both stay visible for a caller evaluating its own predicates.
  auto predicates = query->getPredicates(ts::PatternIndex{0});
  REQUIRE(predicates.has_value());
  auto names = *predicates
    | std::views::transform([](ts::Predicate p) { return std::string{p.name}; })
    | std::ranges::to<std::vector>();
  CHECK(names == std::vector<std::string>{"lua-match?", "set!"});
}


TEST_CASE("A query advertises what its predicates need") {
  auto plain = ts::Query::create(tree_sitter_json(), "(pair) @p");
  REQUIRE(plain.has_value());
  CHECK(!plain->hasTextPredicates());
  CHECK(!plain->hasUncompiledRegexes());

  auto text = ts::Query::create(tree_sitter_json(),
                                R"(((pair) @p (#eq? @p "x")))");
  REQUIRE(text.has_value());
  CHECK(text->hasTextPredicates());
  CHECK(!text->hasUncompiledRegexes());

  auto regexNoCompiler = ts::Query::create(tree_sitter_json(),
                                           R"(((pair) @p (#match? @p "x")))");
  REQUIRE(regexNoCompiler.has_value());
  CHECK(regexNoCompiler->hasTextPredicates());
  CHECK(regexNoCompiler->hasUncompiledRegexes());
}


TEST_CASE("The regex compiler runs once per distinct pattern") {
  int calls = 0;
  auto query = ts::Query::create(tree_sitter_json(),
    R"(((pair key: (string) @k value: (string) @v)
        (#match? @k "^\"a")
        (#match? @v "^\"a")
        (#not-match? @k "z")))",
    [&calls](std::string_view) -> std::optional<ts::RegexMatcher> {
      ++calls;
      return ts::RegexMatcher{[](std::string_view) { return true; }};
    });
  REQUIRE(query.has_value());
  CHECK(!query->hasUncompiledRegexes());
  CHECK(calls == 2);   // "^\"a" appears twice and compiles once, "z" is the second
}


TEST_CASE("A regex the compiler rejects fails query creation") {
  auto query = ts::Query::create(tree_sitter_json(),
    R"(((pair) @p (#match? @p "(")))",
    [](std::string_view) -> std::optional<ts::RegexMatcher> {
      return std::nullopt;
    });
  REQUIRE(!query.has_value());
  CHECK(query.error().kind == ts::ErrorKind::QueryPredicateRegex);
  CHECK(query.error().hasOffset());
}


namespace {

// Runs `pattern` over `source` and reports which matches satisfy its
// predicates, using the per-match entry point so this exercises evaluation
// without involving the cursor's filtering.
std::vector<bool>
verdicts(std::string_view pattern, std::string_view source) {
  auto parser = ts::Parser::create(tree_sitter_json());
  REQUIRE(parser.has_value());
  auto tree = parser->parse(source);
  REQUIRE(tree.has_value());
  auto query = ts::Query::create(tree_sitter_json(), pattern);
  REQUIRE(query.has_value());

  ts::QueryCursor cursor;
  auto matches = cursor.getMatches(*query, tree->getRootNode(),
                                   {.predicates = ts::PredicateMode::Ignore});
  REQUIRE(matches.has_value());

  std::vector<bool> results;
  for (const ts::QueryMatch& match : *matches) {
    results.push_back(query->satisfies(match, source));
  }
  return results;
}

}


// For every case below, the length of the expected vector is a property of
// the pattern and the JSON grammar rather than predicate evaluation. The
// true and false values are the invariants under test.


TEST_CASE("eq against a literal filters on capture text") {
  constexpr std::string_view source = R"({"a": 1, "b": 2})";
  CHECK(verdicts(R"(((pair key: (string) @k) (#eq? @k "\"a\"")))", source)
        == std::vector<bool>{true, false});
  CHECK(verdicts(R"(((pair key: (string) @k) (#not-eq? @k "\"a\"")))", source)
        == std::vector<bool>{false, true});
}


TEST_CASE("eq between two captures compares their texts") {
  constexpr std::string_view source = R"({"a": "a", "b": "c"})";
  CHECK(verdicts(
          R"(((pair key: (string) @k value: (string) @v) (#eq? @k @v)))", source)
        == std::vector<bool>{true, false});
}


TEST_CASE("any-of tests membership of a literal set") {
  constexpr std::string_view source = R"({"a": 1, "b": 2, "c": 3})";
  CHECK(verdicts(
          R"(((pair key: (string) @k) (#any-of? @k "\"a\"" "\"c\"")))", source)
        == std::vector<bool>{true, false, true});
  CHECK(verdicts(
          R"(((pair key: (string) @k) (#not-any-of? @k "\"a\"" "\"c\"")))", source)
        == std::vector<bool>{false, true, false});
}


TEST_CASE("An empty literal set rejects every node") {
  constexpr std::string_view source = R"({"a": 1})";
  CHECK(verdicts(R"(((pair key: (string) @k) (#any-of? @k)))", source)
        == std::vector<bool>{false});
}


TEST_CASE("An all-form over an empty capture accepts vacuously") {
  // @v never binds, because the alternation takes the (number) branch, so the
  // predicate's node set is empty. A universal over nothing holds. This
  // agrees with the reference binding.
  constexpr std::string_view source = R"({"a": 1})";
  CHECK(verdicts(
          R"(((pair value: [(number) @n (string) @v]) (#eq? @v "\"x\"")))", source)
        == std::vector<bool>{true});
}


TEST_CASE("An any-form over an empty capture rejects") {
  // Deliberately diverge from tree-sitter. The reference bindings accept
  // here. An existential quantifier satisfied by nothing is false. Maybe
  // refine later when we know the use cases better?
  constexpr std::string_view source = R"({"a": 1})";
  CHECK(verdicts(
          R"(((pair value: [(number) @n (string) @v]) (#any-eq? @v "\"x\"")))",
          source)
        == std::vector<bool>{false});
}


TEST_CASE("any-of over an empty capture accepts because it is not an any-form") {
  // #any-of?'s "any" quantifies the literal list, so it is a universal over
  // nodes and the witness rule misses it. Over an empty node set it accepts.
  constexpr std::string_view source = R"({"a": 1})";
  CHECK(verdicts(
          R"(((pair value: [(number) @n (string) @v]) (#any-of? @v "\"x\"")))",
          source)
        == std::vector<bool>{true});
}


TEST_CASE("any-eq between two captures with no equal pair rejects") {
  // Again diverge. The reference accepts here. Both captures bind two nodes
  // and no pair is equal, so there is no witness.
  constexpr std::string_view source = R"({"a": "z", "b": "y"})";
  CHECK(verdicts(
          R"(((object (pair key: (string) @k value: (string) @v)
                      (pair key: (string) @k value: (string) @v))
              (#any-eq? @k @v)))",
          source)
        == std::vector<bool>{false});
}


TEST_CASE("eq between captures of unequal length rejects") {
  constexpr std::string_view source = R"({"a": "a"})";
  CHECK(verdicts(
          R"(((pair key: (string) @k value: [(number) @v (string) @k]) (#eq? @k @v)))",
          source)
        == std::vector<bool>{false});
}


TEST_CASE("getMatches filters on predicates by default") {
  constexpr std::string_view source = R"({"a": 1, "b": 2})";
  auto parser = ts::Parser::create(tree_sitter_json());
  REQUIRE(parser.has_value());
  auto tree = parser->parse(source);
  REQUIRE(tree.has_value());
  auto query = ts::Query::create(tree_sitter_json(),
    R"(((pair key: (string) @k) (#eq? @k "\"a\"")))");
  REQUIRE(query.has_value());

  ts::QueryCursor cursor;
  auto matches = cursor.getMatches(*query, tree->getRootNode(),
                                   {.source = source});
  REQUIRE(matches.has_value());
  CHECK(std::ranges::distance(*matches) == 1);
}


TEST_CASE("PredicateMode::Ignore runs a predicated query unfiltered") {
  constexpr std::string_view source = R"({"a": 1, "b": 2})";
  auto parser = ts::Parser::create(tree_sitter_json());
  REQUIRE(parser.has_value());
  auto tree = parser->parse(source);
  REQUIRE(tree.has_value());
  auto query = ts::Query::create(tree_sitter_json(),
    R"(((pair key: (string) @k) (#eq? @k "\"a\"")))");
  REQUIRE(query.has_value());

  ts::QueryCursor cursor;
  auto matches = cursor.getMatches(*query, tree->getRootNode(),
                                   {.predicates = ts::PredicateMode::Ignore});
  REQUIRE(matches.has_value());
  CHECK(std::ranges::distance(*matches) == 2);
}


TEST_CASE("A predicated query without source is an error rather than a silent pass") {
  auto parser = ts::Parser::create(tree_sitter_json());
  REQUIRE(parser.has_value());
  auto tree = parser->parse(R"({"a": 1})");
  REQUIRE(tree.has_value());
  auto query = ts::Query::create(tree_sitter_json(),
    R"(((pair key: (string) @k) (#eq? @k "\"a\"")))");
  REQUIRE(query.has_value());

  ts::QueryCursor cursor;
  auto matches = cursor.getMatches(*query, tree->getRootNode());
  REQUIRE(!matches.has_value());
  CHECK(matches.error().kind == ts::ErrorKind::QueryPredicatesNeedSource);
  CHECK(!matches.error().hasOffset());
}


TEST_CASE("A match query without a regex compiler is an error at execution") {
  constexpr std::string_view source = R"({"a": 1})";
  auto parser = ts::Parser::create(tree_sitter_json());
  REQUIRE(parser.has_value());
  auto tree = parser->parse(source);
  REQUIRE(tree.has_value());
  auto query = ts::Query::create(tree_sitter_json(),
    R"(((pair key: (string) @k) (#match? @k "a")))");
  REQUIRE(query.has_value());

  ts::QueryCursor cursor;
  auto matches = cursor.getMatches(*query, tree->getRootNode(),
                                   {.source = source});
  REQUIRE(!matches.has_value());
  CHECK(matches.error().kind == ts::ErrorKind::QueryPredicatesNeedRegex);

  auto ignored = cursor.getMatches(*query, tree->getRootNode(),
                                   {.predicates = ts::PredicateMode::Ignore});
  REQUIRE(ignored.has_value());
  CHECK(std::ranges::distance(*ignored) == 1);
}


TEST_CASE("An injected regex compiler makes match filter") {
  constexpr std::string_view source = R"({"apple": 1, "berry": 2})";
  auto parser = ts::Parser::create(tree_sitter_json());
  REQUIRE(parser.has_value());
  auto tree = parser->parse(source);
  REQUIRE(tree.has_value());
  auto query = ts::Query::create(tree_sitter_json(),
    R"(((pair key: (string) @k) (#match? @k "app")))",
    [](std::string_view pattern) -> std::optional<ts::RegexMatcher> {
      return ts::RegexMatcher{
        [needle = std::string{pattern}](std::string_view text) {
          return text.find(needle) != std::string_view::npos;
        }};
    });
  REQUIRE(query.has_value());

  ts::QueryCursor cursor;
  auto matches = cursor.getMatches(*query, tree->getRootNode(),
                                   {.source = source});
  REQUIRE(matches.has_value());
  CHECK(std::ranges::distance(*matches) == 1);
}


TEST_CASE("The capture stream filters too") {
  constexpr std::string_view source = R"({"a": 1, "b": 2})";
  auto parser = ts::Parser::create(tree_sitter_json());
  REQUIRE(parser.has_value());
  auto tree = parser->parse(source);
  REQUIRE(tree.has_value());
  auto query = ts::Query::create(tree_sitter_json(),
    R"(((pair key: (string) @k) (#eq? @k "\"a\"")))");
  REQUIRE(query.has_value());

  ts::QueryCursor cursor;
  auto captures = cursor.getCaptureStream(*query, tree->getRootNode(),
                                          {.source = source});
  REQUIRE(captures.has_value());
  CHECK(std::ranges::distance(*captures) == 1);
}


TEST_CASE("A cancelled execution keeps filtering") {
  // Filtering and cancellation share one loop, so this guards against that
  // loop dropping the predicate check once cancellation is in play. The
  // truncated results must be a prefix of the filtered results.
  //
  // The source must be large enough for the progress callback to fire at all,
  // since tree-sitter only consults it every hundred operations. The
  // REQUIRE(cursor.wasCancelled()) below guards against a vacuous test.
  std::string source = "[";
  for (int i = 0; i < 400; ++i) {
    if (i > 0) { source += ","; }
    source += R"({"a": 1, "b": 2})";
  }
  source += "]";

  auto parser = ts::Parser::create(tree_sitter_json());
  REQUIRE(parser.has_value());
  auto tree = parser->parse(source);
  REQUIRE(tree.has_value());
  auto query = ts::Query::create(tree_sitter_json(),
    R"(((pair key: (string) @k) (#eq? @k "\"a\"")))");
  REQUIRE(query.has_value());

  // Every "a" key, in order, with the "b" pairs filtered out.
  auto keyStarts = [](auto&& matches) {
    std::vector<uint32_t> starts;
    for (const ts::QueryMatch& match : matches) {
      starts.push_back(match.getCaptures()[0].node.getByteRange().start);
    }
    return starts;
  };

  ts::QueryCursor cursor;
  auto baseline = keyStarts(
    cursor.getMatches(*query, tree->getRootNode(), {.source = source}).value());
  REQUIRE(baseline.size() == 400);
  CHECK(!cursor.wasCancelled());

  int calls = 0;
  auto matches = cursor.getMatches(*query, tree->getRootNode(), {
    .source = source,
    .onProgress = [&calls](uint32_t) {
      return ++calls == 3 ? ts::ProgressAction::Cancel
                          : ts::ProgressAction::Continue;
    }});
  REQUIRE(matches.has_value());
  auto truncated = keyStarts(*matches);

  REQUIRE(cursor.wasCancelled());
  CHECK(truncated.size() < baseline.size());
  CHECK(std::ranges::equal(
    truncated, baseline | std::views::take(std::ssize(truncated))));
}


TEST_CASE("A failed preflight leaves the cursor untouched") {
  constexpr std::string_view source = R"({"a": 1, "b": 2})";
  auto parser = ts::Parser::create(tree_sitter_json());
  REQUIRE(parser.has_value());
  auto tree = parser->parse(source);
  REQUIRE(tree.has_value());
  auto plain = ts::Query::create(tree_sitter_json(), "(pair) @p");
  REQUIRE(plain.has_value());
  auto predicated = ts::Query::create(tree_sitter_json(),
    R"(((pair key: (string) @k) (#eq? @k "\"a\"")))");
  REQUIRE(predicated.has_value());

  ts::QueryCursor cursor;
  auto good = cursor.getMatches(*plain, tree->getRootNode());
  REQUIRE(good.has_value());

  auto bad = cursor.getMatches(*predicated, tree->getRootNode());
  REQUIRE(!bad.has_value());

  // The first execution is still the one in flight.
  CHECK(std::ranges::distance(*good) == 2);
}
