#include <algorithm>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <doctest/doctest.h>
#include <cpp-tree-sitter.h>
#include <cts/query.h>

#include <stringify.h>

extern "C" TSLanguage* tree_sitter_json();
extern "C" TSLanguage* tree_sitter_ctstest();


TEST_CASE("Query::create compiles a valid query and exposes metadata") {
  auto query = ts::Query::create(tree_sitter_json(),
                                 "(pair key: (string) @key value: (_) @value)");
  REQUIRE(query.has_value());
  CHECK(query->getNumPatterns() == 1);
  CHECK(query->getNumCaptures() == 2);
  CHECK(query->getCaptureNameForId(ts::CaptureId{0}) == "key");
  CHECK(query->getCaptureNameForId(ts::CaptureId{1}) == "value");
}


TEST_CASE("Query::create reports precise errors") {
  SUBCASE("unknown node type") {
    auto query = ts::Query::create(tree_sitter_json(), "(nonexistent) @x");
    REQUIRE(!query.has_value());
    CHECK(query.error().kind == ts::ErrorKind::QueryNodeType);
    CHECK(query.error().offset == 1);
  }
  SUBCASE("syntax error") {
    auto query = ts::Query::create(tree_sitter_json(), "(pair");
    REQUIRE(!query.has_value());
    CHECK(query.error().kind == ts::ErrorKind::QuerySyntax);
  }
  SUBCASE("unknown field") {
    auto query = ts::Query::create(tree_sitter_json(), "(pair nope: (_) @v)");
    REQUIRE(!query.has_value());
    CHECK(query.error().kind == ts::ErrorKind::QueryField);
  }
  SUBCASE("empty query source is a valid empty query") {
    auto query = ts::Query::create(tree_sitter_json(), "");
    REQUIRE(query.has_value());
    CHECK(query->getNumPatterns() == 0);
  }
}


TEST_CASE("getPredicateToken exposes the tokens of a predicate") {
  auto query = ts::Query::create(tree_sitter_json(),
                                 R"(((string) @s (#match? @s "abc")))").value();
  CHECK(query.getNumPredicateTokens() == 2);
  CHECK(query.getPredicateToken(ts::PredicateTokenId{0}) == "match?");
  CHECK(query.getPredicateToken(ts::PredicateTokenId{1}) == "abc");
  CHECK(query.getPredicateToken(ts::PredicateTokenId{2}) == std::nullopt);
}


TEST_CASE("id accessors reject out-of-range ids instead of truncating") {
  auto query = ts::Query::create(tree_sitter_json(),
                                 "(pair key: (string) @key value: (_) @value)").value();
  REQUIRE(query.getNumCaptures() == 2);

  CHECK(query.getCaptureNameForId(ts::CaptureId{0}) == "key");
  CHECK(query.getCaptureNameForId(ts::CaptureId{2}) == std::nullopt);

  CHECK(query.getCaptureNameForId(ts::CaptureId{65536}) == std::nullopt);
  CHECK(query.getPredicateToken(ts::PredicateTokenId{65536}) == std::nullopt);
}


TEST_CASE("getCaptureQuantifier separates an unknown capture from an absent one") {
  auto query = ts::Query::create(
    tree_sitter_json(),
    "(pair key: (string) @key)\n(array (number) @num)").value();
  REQUIRE(query.getNumPatterns() == 2);
  REQUIRE(query.getNumCaptures() == 2);

  constexpr auto pattern0 = ts::PatternIndex{0};
  constexpr auto pattern1 = ts::PatternIndex{1};
  constexpr auto key = ts::CaptureId{0};
  constexpr auto num = ts::CaptureId{1};

  CHECK(query.getCaptureQuantifier(pattern0, key) == ts::Quantifier::One);
  CHECK(query.getCaptureQuantifier(pattern1, num) == ts::Quantifier::One);

  CHECK(query.getCaptureQuantifier(pattern0, num) == ts::Quantifier::Zero);
  CHECK(query.getCaptureQuantifier(pattern1, key) == ts::Quantifier::Zero);

  CHECK(query.getCaptureQuantifier(pattern0, ts::CaptureId{2}) == std::nullopt);
  CHECK(query.getCaptureQuantifier(ts::PatternIndex{2}, key) == std::nullopt);
}


TEST_CASE("getCaptureQuantifier reports repetition") {
  auto zeroOrMore = ts::Query::create(
    tree_sitter_json(), "(array (number)* @nums)").value();
  CHECK(zeroOrMore.getCaptureQuantifier(ts::PatternIndex{0}, ts::CaptureId{0})
        == ts::Quantifier::ZeroOrMore);

  auto oneOrMore = ts::Query::create(
    tree_sitter_json(), "(array (number)+ @nums)").value();
  CHECK(oneOrMore.getCaptureQuantifier(ts::PatternIndex{0}, ts::CaptureId{0})
        == ts::Quantifier::OneOrMore);

  auto zeroOrOne = ts::Query::create(
    tree_sitter_json(), "(array (number)? @nums)").value();
  CHECK(zeroOrOne.getCaptureQuantifier(ts::PatternIndex{0}, ts::CaptureId{0})
        == ts::Quantifier::ZeroOrOne);
}


TEST_CASE("getPatternByteRange locates a pattern in the query source") {
  constexpr std::string_view source =
    "(pair key: (string) @key)\n(array (number) @num)";
  auto query = ts::Query::create(tree_sitter_json(), source).value();
  REQUIRE(query.getNumPatterns() == 2);

  auto first = query.getPatternByteRange(ts::PatternIndex{0});
  REQUIRE(first.has_value());
  CHECK(*first == ts::Extent<uint32_t>{0, 26});
  CHECK(source.substr(first->start, first->end - first->start)
        == "(pair key: (string) @key)\n");

  auto second = query.getPatternByteRange(ts::PatternIndex{1});
  REQUIRE(second.has_value());
  CHECK(*second == ts::Extent<uint32_t>{26, 47});
  CHECK(source.substr(second->start, second->end - second->start)
        == "(array (number) @num)");

  CHECK(query.getPatternByteRange(ts::PatternIndex{2}) == std::nullopt);
}


TEST_CASE("getCaptureId round-trips with getCaptureNameForId") {
  auto query = ts::Query::create(
    tree_sitter_json(), "(pair key: (string) @key value: (_) @value)").value();

  auto key = query.getCaptureId("key");
  REQUIRE(key.has_value());
  CHECK(query.getCaptureNameForId(*key) == "key");

  auto value = query.getCaptureId("value");
  REQUIRE(value.has_value());
  CHECK(*key != *value);

  CHECK(query.getCaptureId("nonexistent") == std::nullopt);
  CHECK(query.getCaptureId("") == std::nullopt);
}


TEST_CASE("getNodeFor finds a match's node by capture id") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  constexpr std::string_view source = R"({"a": 1})";
  const auto tree = parser.parse(source).value();
  auto query = ts::Query::create(
    tree_sitter_json(), "(pair key: (string) @key value: (_) @value)").value();

  auto key = query.getCaptureId("key").value();
  auto value = query.getCaptureId("value").value();

  ts::QueryCursor cursor;
  size_t matches = 0;
  for (const ts::QueryMatch& match :
       cursor.getMatches(query, tree.getRootNode()).value()) {
    auto keyNode = match.getNodeFor(key);
    REQUIRE(keyNode.has_value());
    CHECK(keyNode->getSourceRange(source) == R"("a")");

    auto valueNode = match.getNodeFor(value);
    REQUIRE(valueNode.has_value());
    CHECK(valueNode->getSourceRange(source) == "1");
    ++matches;
  }
  CHECK(matches == 1);
}


TEST_CASE("getNodeFor reports an absent optional capture as nullopt") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  // The array holds no numbers, so @num never binds even though the pattern
  // matches.
  constexpr std::string_view source = R"({"a": ["x"]})";
  const auto tree = parser.parse(source).value();
  auto query = ts::Query::create(
    tree_sitter_json(), "(array (number)? @num) @arr").value();

  auto num = query.getCaptureId("num").value();
  auto arr = query.getCaptureId("arr").value();

  ts::QueryCursor cursor;
  size_t matches = 0;
  for (const ts::QueryMatch& match :
       cursor.getMatches(query, tree.getRootNode()).value()) {
    CHECK(match.getNodeFor(arr).has_value());
    CHECK(match.getNodeFor(num) == std::nullopt);
    ++matches;
  }
  CHECK(matches == 1);
}


TEST_CASE("disableCapture stops a capture without invalidating its id") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  constexpr std::string_view source = R"({"a": 1})";
  const auto tree = parser.parse(source).value();
  auto query = ts::Query::create(
    tree_sitter_json(), "(pair key: (string) @key value: (_) @value)").value();

  auto key = query.getCaptureId("key").value();
  auto value = query.getCaptureId("value").value();

  // An id naming nothing in this query is a no-op. That is checked
  // before the real disableCapture below changes it.
  REQUIRE(query.getNumCaptures() == 2);
  query.disableCapture(ts::CaptureId{query.getNumCaptures()});
  CHECK(query.getNumCaptures() == 2);
  {
    ts::QueryCursor cursor;
    size_t outOfRangeMatches = 0;
    for (const ts::QueryMatch& match :
         cursor.getMatches(query, tree.getRootNode()).value()) {
      CHECK(match.getNodeFor(key).has_value());
      CHECK(match.getNodeFor(value).has_value());
      ++outOfRangeMatches;
    }
    CHECK(outOfRangeMatches == 1);
  }

  query.disableCapture(key);

  // Disabling strips capture recording from the pattern steps but leaves the
  // symbol table alone, so the id keeps resolving but stops appearing.
  CHECK(query.getNumCaptures() == 2);
  CHECK(query.getCaptureNameForId(key) == "key");
  CHECK(query.getCaptureId("key") == key);

  ts::QueryCursor cursor;
  size_t matches = 0;
  for (const ts::QueryMatch& match :
       cursor.getMatches(query, tree.getRootNode()).value()) {
    CHECK(match.getNodeFor(key) == std::nullopt);
    CHECK(match.getNodeFor(value).has_value());
    ++matches;
  }
  CHECK(matches == 1);
}


TEST_CASE("disablePattern stops one pattern and ignores an unknown one") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  constexpr std::string_view source = R"({"a": [1]})";
  const auto tree = parser.parse(source).value();
  auto query = ts::Query::create(
    tree_sitter_json(),
    "(pair key: (string) @key)\n(array (number) @num)").value();
  REQUIRE(query.getNumPatterns() == 2);

  auto countMatches = [&](ts::Query const& q) {
    ts::QueryCursor cursor;
    size_t count = 0;
    for ([[maybe_unused]] const ts::QueryMatch& match :
         cursor.getMatches(q, tree.getRootNode()).value()) {
      ++count;
    }
    return count;
  };
  REQUIRE(countMatches(query) == 2);

  query.disablePattern(ts::PatternIndex{7});
  CHECK(countMatches(query) == 2);

  query.disablePattern(ts::PatternIndex{0});
  CHECK(countMatches(query) == 1);
  CHECK(query.getNumPatterns() == 2);
}


static_assert(!std::is_default_constructible_v<ts::QueryMatch>);
static_assert(std::is_constructible_v<ts::QueryMatch, TSQueryMatch>);

static_assert(
  std::is_same_v<std::iter_reference_t<ts::MatchIterator>, ts::QueryMatch>);
static_assert(std::is_same_v<std::iter_reference_t<ts::CaptureIterator>,
                             ts::CaptureResult>);
static_assert(std::input_iterator<ts::MatchIterator>);
static_assert(std::input_iterator<ts::CaptureIterator>);

static_assert(std::random_access_iterator<std::ranges::iterator_t<ts::CaptureRange>>);
static_assert(std::ranges::random_access_range<ts::CaptureRange>);
static_assert(std::ranges::sized_range<ts::CaptureRange>);
static_assert(std::ranges::borrowed_range<ts::CaptureRange>);
static_assert(!std::ranges::contiguous_range<ts::CaptureRange>);

static_assert(sizeof(ts::QueryCapture) == sizeof(TSQueryCapture));


TEST_CASE("raw exposes the underlying TSQuery without transferring ownership") {
  auto query = ts::Query::create(tree_sitter_json(), "(pair) @p").value();
  TSQuery const* rawHandle = query.raw();
  REQUIRE(rawHandle != nullptr);
  CHECK(ts_query_pattern_count(rawHandle) == query.getNumPatterns());

  static_assert(std::is_same_v<decltype(query.raw()), TSQuery*>);
  static_assert(std::is_same_v<decltype(std::as_const(query).raw()),
                               TSQuery const*>);
  CHECK(std::as_const(query).raw() == rawHandle);
}


static_assert(!std::copyable<ts::Query>);
static_assert(std::movable<ts::Query>);

static_assert(!std::copyable<ts::QueryCursor>);
static_assert(std::movable<ts::QueryCursor>);


TEST_CASE("getMatches yields every pair with named captures") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  constexpr std::string_view source = R"({"a": 1, "b": [2, 3], "c": {"d": 4}})";
  const auto tree = parser.parse(source).value();
  auto query = ts::Query::create(tree_sitter_json(),
                                 "(pair key: (string) @key value: (_) @value)").value();

  ts::QueryCursor cursor;
  std::vector<std::string> keys;
  for (const ts::QueryMatch& match : cursor.getMatches(query, tree.getRootNode()).value()) {
    CHECK(match.getPatternIndex() == ts::PatternIndex{0});
    auto captures = match.getCaptures();
    REQUIRE(captures.size() == 2);
    CHECK(query.getCaptureNameForId(captures[0].id) == "key");
    keys.emplace_back(captures[0].node.getSourceRange(source));
  }
  CHECK(keys == std::vector<std::string>{R"("a")", R"("b")", R"("c")", R"("d")"});
}


// Byte layout of R"({"a": 1, "b": 2})".
//   0:{  1:"  2:a  3:"  4::  5:sp 6:1  7:,
//   8:sp 9:"  10:b 11:" 12:: 13:sp 14:2 15:}
// pair1 spans bytes [1,7), pair2 spans [9,15).
static constexpr std::string_view kTwoPairs = R"({"a": 1, "b": 2})";


TEST_CASE("QueryOptions restricts one execution") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  std::string const source{kTwoPairs};
  const auto tree = parser.parse(source).value();
  auto query = ts::Query::create(tree_sitter_json(), "(pair) @p").value();
  ts::QueryCursor cursor;

  // A bound like [0,8) cannot tell byteRange from containingByteRange, since
  // pair1 [1,7) is both contained in it and intersects it, so a swap of the
  // two options in applyOptions would still answer 1. [0,4) discriminates.
  // pair1 intersects it (bytes 1-4 overlap) but is not contained in it (7 > 4).
  SUBCASE("a byte range keeps the matches it merely intersects") {
    auto matches = cursor.getMatches(query, tree.getRootNode(),
                                     {.byteRange = ts::Extent<uint32_t>{.start=0, .end=4}});
    REQUIRE(matches.has_value());
    CHECK(std::ranges::distance(*matches) == 1);
  }

  SUBCASE("a containing range keeps only the fully contained matches") {
    // [0,4) intersects pair1 but does not contain it, so this must yield 0.
    // A byteRange and containingByteRange swap would answer 1 instead.
    auto matches = cursor.getMatches(
      query, tree.getRootNode(),
      {.containingByteRange = ts::Extent<uint32_t>{.start=0, .end=4}});
    REQUIRE(matches.has_value());
    CHECK(std::ranges::distance(*matches) == 0);
  }

  SUBCASE("a containing point range keeps only the fully contained matches") {
    // kTwoPairs is single-line, so column tracks byte offset and the same
    // discriminating bound applies, with Point{0,4} at byte 4.
    auto matches = cursor.getMatches(
      query, tree.getRootNode(),
      {.containingPointRange =
         ts::Extent<ts::Point>{.start=ts::Point{0, 0}, .end=ts::Point{0, 4}}});
    REQUIRE(matches.has_value());
    CHECK(std::ranges::distance(*matches) == 0);
  }

  SUBCASE("maxStartDepth of zero prevents descending to the pairs") {
    auto matches = cursor.getMatches(query, tree.getRootNode(),
                                     {.maxStartDepth = 0});
    REQUIRE(matches.has_value());
    CHECK(std::ranges::distance(*matches) == 0);
  }
}


TEST_CASE("an absent option resets the previous execution's restriction") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  std::string const source{kTwoPairs};
  const auto tree = parser.parse(source).value();
  auto query = ts::Query::create(tree_sitter_json(), "(pair) @p").value();

  ts::QueryCursor cursor;
  auto restricted = cursor.getMatches(query, tree.getRootNode(),
                                      {.byteRange = ts::Extent<uint32_t>{.start=0, .end=8}});
  REQUIRE(restricted.has_value());
  CHECK(std::ranges::distance(*restricted) == 1);

  auto unrestricted = cursor.getMatches(query, tree.getRootNode());
  REQUIRE(unrestricted.has_value());
  CHECK(std::ranges::distance(*unrestricted) == 2);
}


TEST_CASE("an empty extent means no matches rather than a full scan") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  std::string const source{kTwoPairs};
  const auto tree = parser.parse(source).value();
  auto query = ts::Query::create(tree_sitter_json(), "(pair) @p").value();
  ts::QueryCursor cursor;

  SUBCASE("{0,0} does not become unbounded") {
    auto matches = cursor.getMatches(query, tree.getRootNode(),
                                     {.byteRange = ts::Extent<uint32_t>{.start=0, .end=0}});
    REQUIRE(matches.has_value());
    CHECK(std::ranges::distance(*matches) == 0);
  }

  SUBCASE("an empty extent inside a match still straddles it") {
    auto matches = cursor.getMatches(query, tree.getRootNode(),
                                     {.byteRange = ts::Extent<uint32_t>{.start=3, .end=3}});
    REQUIRE(matches.has_value());
    CHECK(std::ranges::distance(*matches) == 1);
  }

  SUBCASE("an empty extent between matches straddles nothing") {
    auto matches = cursor.getMatches(query, tree.getRootNode(),
                                     {.byteRange = ts::Extent<uint32_t>{.start=8, .end=8}});
    REQUIRE(matches.has_value());
    CHECK(std::ranges::distance(*matches) == 0);
  }
}


TEST_CASE("an inverted extent is an error rather than a silent full scan") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  std::string const source{kTwoPairs};
  const auto tree = parser.parse(source).value();
  auto query = ts::Query::create(tree_sitter_json(), "(pair) @p").value();
  ts::QueryCursor cursor;

  SUBCASE("plainly inverted") {
    auto matches = cursor.getMatches(query, tree.getRootNode(),
                                     {.byteRange = ts::Extent<uint32_t>{.start=9, .end=3}});
    REQUIRE(!matches.has_value());
    CHECK(matches.error().kind == ts::ErrorKind::QueryInvalidRange);
  }

  SUBCASE("inverted with a zero end, which tree-sitter would accept") {
    auto matches = cursor.getMatches(query, tree.getRootNode(),
                                     {.byteRange = ts::Extent<uint32_t>{.start=5, .end=0}});
    REQUIRE(!matches.has_value());
    CHECK(matches.error().kind == ts::ErrorKind::QueryInvalidRange);
  }

  SUBCASE("an inverted point range is rejected too") {
    auto matches = cursor.getMatches(
      query, tree.getRootNode(),
      {.pointRange = ts::Extent<ts::Point>{.start=ts::Point{5, 0}, .end=ts::Point{2, 0}}});
    REQUIRE(!matches.has_value());
    CHECK(matches.error().kind == ts::ErrorKind::QueryInvalidRange);
  }

  SUBCASE("a rejected options object leaves the cursor untouched") {
    auto rejected = cursor.getMatches(query, tree.getRootNode(),
                                      {.byteRange = ts::Extent<uint32_t>{.start=9, .end=3}});
    REQUIRE(!rejected.has_value());
    auto matches = cursor.getMatches(query, tree.getRootNode());
    REQUIRE(matches.has_value());
    CHECK(std::ranges::distance(*matches) == 2);
  }
}


TEST_CASE("executing a query against another language's tree is an error") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  const auto tree = parser.parse(R"({"a": 1})").value();
  auto foreign = ts::Query::create(tree_sitter_ctstest(), "(_) @d").value();

  ts::QueryCursor cursor;

  SUBCASE("getMatches rejects it") {
    auto matches = cursor.getMatches(foreign, tree.getRootNode());
    REQUIRE(!matches.has_value());
    CHECK(matches.error().kind == ts::ErrorKind::QueryNodeLanguage);
  }

  SUBCASE("getCaptureStream rejects it") {
    auto captures = cursor.getCaptureStream(foreign, tree.getRootNode());
    REQUIRE(!captures.has_value());
    CHECK(captures.error().kind == ts::ErrorKind::QueryNodeLanguage);
  }

  SUBCASE("the matching language still succeeds") {
    auto native = ts::Query::create(tree_sitter_json(), "(pair) @p").value();
    auto matches = cursor.getMatches(native, tree.getRootNode());
    REQUIRE(matches.has_value());
    CHECK(std::ranges::distance(*matches) == 1);
  }
}


namespace {

std::string
makeManyPairs(int count) {
  std::string source = "[";
  for (int i = 0; i < count; ++i) {
    if (i > 0) { source += ","; }
    source += "{\"k" + std::to_string(i) + "\": " + std::to_string(i) + "}";
  }
  source += "]";
  return source;
}


// Identifies a match by the start byte of its first capture, which is stable
// across runs.
std::vector<uint32_t>
collect(ts::MatchesView view) {
  std::vector<uint32_t> starts;
  for (const ts::QueryMatch& match : view) {
    auto captures = match.getCaptures();
    starts.push_back(captures[0].node.getByteRange().start);
  }
  return starts;
}


// As collect(ts::MatchesView), but for the capture stream, identified by its
// own start byte.
std::vector<uint32_t>
collect(ts::CapturesView view) {
  std::vector<uint32_t> starts;
  for (const ts::CaptureResult& result : view) {
    starts.push_back(result.getCapture().node.getByteRange().start);
  }
  return starts;
}


// A callback that asks to cancel once and then continues.
ts::ProgressCallback
cancelOnce(int& calls, int cancelAt) {
  return [&calls, cancelAt](uint32_t) {
    return ++calls == cancelAt ? ts::ProgressAction::Cancel
                               : ts::ProgressAction::Continue;
  };
}

}


TEST_CASE("cancelling truncates a query to a prefix of its results") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  const std::string source = makeManyPairs(1000);
  const auto tree = parser.parse(source).value();
  auto query = ts::Query::create(tree_sitter_json(), "(pair) @p").value();

  ts::QueryCursor cursor;
  auto baseline = collect(cursor.getMatches(query, tree.getRootNode()).value());
  REQUIRE(baseline.size() == 1000);
  CHECK(!cursor.wasCancelled());

  int calls = 0;
  ts::QueryOptions const options{.onProgress = cancelOnce(calls, 5)};
  auto truncated = collect(
    cursor.getMatches(query, tree.getRootNode(), options).value());

  REQUIRE(calls >= 5);
  REQUIRE(cursor.wasCancelled());
  CHECK(!truncated.empty());
  CHECK(truncated.size() < baseline.size());
  CHECK(std::ranges::equal(
    truncated, baseline | std::views::take(std::ssize(truncated))));
}


TEST_CASE("cancellation is per-execution and clears on the next one") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  const std::string source = makeManyPairs(1000);
  const auto tree = parser.parse(source).value();
  auto query = ts::Query::create(tree_sitter_json(), "(pair) @p").value();

  ts::QueryCursor cursor;
  int calls = 0;
  ts::QueryOptions const options{.onProgress = cancelOnce(calls, 5)};
  auto truncated = collect(
    cursor.getMatches(query, tree.getRootNode(), options).value());
  REQUIRE(cursor.wasCancelled());
  REQUIRE(truncated.size() < 1000);

  auto again = collect(cursor.getMatches(query, tree.getRootNode()).value());
  CHECK(!cursor.wasCancelled());
  CHECK(again.size() == 1000);
}


TEST_CASE("a callback that never cancels does not change the results") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  const std::string source = makeManyPairs(200);
  const auto tree = parser.parse(source).value();
  auto query = ts::Query::create(tree_sitter_json(), "(pair) @p").value();

  ts::QueryCursor cursor;
  int calls = 0;
  uint32_t maxOffsetSeen = 0;
  ts::QueryOptions const options{
    .onProgress = [&](uint32_t byteOffset) {
      maxOffsetSeen = std::max(maxOffsetSeen, byteOffset);
      ++calls;
      return ts::ProgressAction::Continue;
    }};

  auto matches = cursor.getMatches(query, tree.getRootNode(), options);
  REQUIRE(matches.has_value());
  CHECK(std::ranges::distance(*matches) == 200);
  CHECK(calls > 0);
  CHECK(maxOffsetSeen <= source.size());
  CHECK(!cursor.wasCancelled());
}


TEST_CASE("cancelling truncates the capture stream to a prefix too") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  const std::string source = makeManyPairs(500);
  const auto tree = parser.parse(source).value();
  auto query = ts::Query::create(tree_sitter_json(), "(string) @s").value();

  ts::QueryCursor cursor;
  auto baseline = collect(cursor.getCaptureStream(query, tree.getRootNode()).value());
  REQUIRE(baseline.size() == 500);
  CHECK(!cursor.wasCancelled());

  int calls = 0;
  ts::QueryOptions const options{.onProgress = cancelOnce(calls, 5)};
  auto truncated = collect(
    cursor.getCaptureStream(query, tree.getRootNode(), options).value());

  REQUIRE(calls >= 5);
  REQUIRE(cursor.wasCancelled());
  CHECK(!truncated.empty());
  CHECK(truncated.size() < baseline.size());
  CHECK(std::ranges::equal(
    truncated, baseline | std::views::take(std::ssize(truncated))));
}


TEST_CASE("Query remembers the language it was compiled against") {
  auto query = ts::Query::create(tree_sitter_json(), "(pair) @p").value();
  CHECK(query.getLanguage() == ts::Language{tree_sitter_json()});
}


TEST_CASE("a point range restricts by row and column") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  constexpr std::string_view source = "{\n  \"a\": 1,\n  \"b\": 2\n}";  // pair per row, rows 1 and 2
  const auto tree = parser.parse(source).value();
  auto query = ts::Query::create(tree_sitter_json(), "(pair) @p").value();

  ts::QueryCursor cursor;
  auto matches = cursor.getMatches(
    query, tree.getRootNode(),
    {.pointRange = ts::Extent<ts::Point>{.start=ts::Point{1, 0}, .end=ts::Point{2, 0}}});
  REQUIRE(matches.has_value());
  CHECK(std::ranges::distance(*matches) == 1);
}


TEST_CASE("one QueryOptions object serves several executions") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  std::string const source{kTwoPairs};
  const auto tree = parser.parse(source).value();
  auto query = ts::Query::create(tree_sitter_json(), "(pair) @p").value();

  const ts::QueryOptions options{.byteRange = ts::Extent<uint32_t>{.start=0, .end=8}};
  ts::QueryCursor cursor;
  auto first = cursor.getMatches(query, tree.getRootNode(), options);
  REQUIRE(first.has_value());
  CHECK(std::ranges::distance(*first) == 1);
  auto second = cursor.getMatches(query, tree.getRootNode(), options);
  REQUIRE(second.has_value());
  CHECK(std::ranges::distance(*second) == 1);
}


static_assert(std::copyable<ts::QueryOptions>);


TEST_CASE("a query for a structure absent from the tree yields no matches") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  const auto tree = parser.parse("[1, 2]").value();
  auto query = ts::Query::create(tree_sitter_json(), "(pair) @p").value();
  ts::QueryCursor cursor;
  auto matches = cursor.getMatches(query, tree.getRootNode()).value();
  CHECK(std::ranges::distance(matches) == 0);
}


TEST_CASE("getCaptureStream yields captures in source order across patterns") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  constexpr std::string_view source = R"({"a": [1], "b": 2})";
  const auto tree = parser.parse(source).value();
  auto query = ts::Query::create(tree_sitter_json(),
                                 "(string) @s (number) @n").value();

  ts::QueryCursor cursor;
  std::vector<uint32_t> starts;
  for (const ts::CaptureResult& result :
       cursor.getCaptureStream(query, tree.getRootNode()).value()) {
    starts.push_back(result.getCapture().node.getByteRange().start);
  }
  REQUIRE(starts.size() == 4);  // "a", 1, "b", 2
  CHECK(std::ranges::is_sorted(starts));
}


TEST_CASE("getCaptures yields every capture of a match in order") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  constexpr std::string_view source = R"({"a": 1})";
  const auto tree = parser.parse(source).value();
  auto query = ts::Query::create(
    tree_sitter_json(), "(pair key: (string) @key value: (_) @value)").value();

  ts::QueryCursor cursor;
  std::vector<std::pair<std::string, std::string>> byIndexing;
  std::vector<std::string> byIteration;
  for (const ts::QueryMatch& match : cursor.getMatches(query, tree.getRootNode()).value()) {
    auto captures = match.getCaptures();
    REQUIRE(captures.size() == 2);

    // Use constant indices to avoid signedness conversion.
    byIndexing.emplace_back(
      std::string{*query.getCaptureNameForId(captures[0].id)},
      std::string{captures[0].node.getSourceRange(source)});
    byIndexing.emplace_back(
      std::string{*query.getCaptureNameForId(captures[1].id)},
      std::string{captures[1].node.getSourceRange(source)});

    for (const ts::QueryCapture& capture : captures) {
      byIteration.emplace_back(capture.node.getSourceRange(source));
    }
  }

  CHECK(byIndexing == std::vector<std::pair<std::string, std::string>>{
    {"key", R"("a")"}, {"value", "1"}});
  CHECK(byIteration == std::vector<std::string>{R"("a")", "1"});
}


TEST_CASE("Captures from a temporary match keep usable iterators") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  constexpr std::string_view source = R"({"a": 1})";
  const auto tree = parser.parse(source).value();
  auto query = ts::Query::create(
    tree_sitter_json(), "(pair key: (string) @key value: (_) @value)").value();

  ts::QueryCursor cursor;
  size_t matches = 0;
  for (const ts::QueryMatch& match : cursor.getMatches(query, tree.getRootNode()).value()) {
    auto found = std::ranges::find_if(
      match.getCaptures(), [&](const ts::QueryCapture& capture) {
        return query.getCaptureNameForId(capture.id) == "value";
      });
    static_assert(!std::is_same_v<decltype(found), std::ranges::dangling>);
    REQUIRE(found != match.getCaptures().end());
    CHECK((*found).node.getSourceRange(source) == "1");
    ++matches;
  }
  CHECK(matches == 1);
}


TEST_CASE("query result views compose with range adaptors") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  constexpr std::string_view source = R"({"a": 1, "b": 2, "c": 3})";
  const auto tree = parser.parse(source).value();
  auto query = ts::Query::create(tree_sitter_json(),
                                 "(pair key: (string) @key)").value();

  SUBCASE("views::take truncates the stream") {
    ts::QueryCursor cursor;
    std::vector<std::string> keys;
    for (const ts::QueryMatch& match :
         cursor.getMatches(query, tree.getRootNode()).value() | std::views::take(2)) {
      keys.emplace_back(match.getCaptures()[0].node.getSourceRange(source));
    }
    CHECK(keys == std::vector<std::string>{R"("a")", R"("b")"});
  }

  SUBCASE("views::filter selects from the stream") {
    ts::QueryCursor cursor;
    auto isC = [&](const ts::QueryMatch& match) {
      return match.getCaptures()[0].node.getSourceRange(source) == R"("c")";
    };
    std::vector<std::string> keys;
    for (const ts::QueryMatch& match :
         cursor.getMatches(query, tree.getRootNode()).value() | std::views::filter(isC)) {
      keys.emplace_back(match.getCaptures()[0].node.getSourceRange(source));
    }
    CHECK(keys == std::vector<std::string>{R"("c")"});
  }
}


TEST_CASE("MatchesView is a borrowed range") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  constexpr std::string_view source = R"({"a": 1, "b": 2})";
  const auto tree = parser.parse(source).value();
  auto query = ts::Query::create(tree_sitter_json(),
                                 "(pair key: (string) @key)").value();

  ts::QueryCursor cursor;
  auto found = std::ranges::find_if(
    cursor.getMatches(query, tree.getRootNode()).value(),
    [&](const ts::QueryMatch& match) {
      return match.getCaptures()[0].node.getSourceRange(source) == R"("b")";
    });

  static_assert(!std::is_same_v<decltype(found), std::ranges::dangling>);
  REQUIRE(found != std::default_sentinel);
  CHECK((*found).getCaptures()[0].node.getSourceRange(source) == R"("b")");
}


TEST_CASE("CapturesView is a borrowed range") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  constexpr std::string_view source = R"({"a": 1, "b": 2})";
  const auto tree = parser.parse(source).value();
  auto query = ts::Query::create(tree_sitter_json(),
                                 "(string) @s (number) @n").value();

  ts::QueryCursor cursor;
  auto found = std::ranges::find_if(
    cursor.getCaptureStream(query, tree.getRootNode()).value(),
    [&](const ts::CaptureResult& result) {
      return result.getCapture().node.getSourceRange(source) == R"("b")";
    });

  static_assert(!std::is_same_v<decltype(found), std::ranges::dangling>);
  REQUIRE(found != std::default_sentinel);
  CHECK((*found).getCapture().node.getSourceRange(source) == R"("b")");
}


TEST_CASE("the match limit is configurable and its breach is reportable") {
  // The limit bounds concurrently in-progress capture lists and completed
  // states release their lists back to the pool, so it is hard to trip.
  // These cases pin that the option reaches the cursor and the predicate
  // is readable.
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  std::string const source{kTwoPairs};
  const auto tree = parser.parse(source).value();
  auto query = ts::Query::create(tree_sitter_json(), "(pair) @p").value();
  ts::QueryCursor cursor;

  SUBCASE("an ordinary query does not exceed the default limit") {
    auto matches = cursor.getMatches(query, tree.getRootNode());
    REQUIRE(matches.has_value());
    CHECK(std::ranges::distance(*matches) == 2);
    CHECK(!cursor.didExceedMatchLimit());
  }

  SUBCASE("a matchLimit reaches the cursor and an absent one resets it") {
    auto limited = cursor.getMatches(query, tree.getRootNode(),
                                     {.matchLimit = 1});
    REQUIRE(limited.has_value());
    CHECK(ts_query_cursor_match_limit(cursor.raw()) == 1);
    CHECK(std::ranges::distance(*limited) == 2);

    auto unlimited = cursor.getMatches(query, tree.getRootNode());
    REQUIRE(unlimited.has_value());
    CHECK(ts_query_cursor_match_limit(cursor.raw())
          == std::numeric_limits<uint32_t>::max());
  }
}


TEST_CASE("QueryCursor::raw exposes the cursor without transferring ownership") {
  ts::QueryCursor cursor;
  TSQueryCursor const* rawHandle = cursor.raw();
  REQUIRE(rawHandle != nullptr);

  static_assert(std::is_same_v<decltype(cursor.raw()), TSQueryCursor*>);
  static_assert(std::is_same_v<decltype(std::as_const(cursor).raw()),
                               TSQueryCursor const*>);
  CHECK(std::as_const(cursor).raw() == rawHandle);
}


TEST_CASE("removeMatch leaves an in-flight iteration intact") {
  auto parser = ts::Parser::create(tree_sitter_json());
  REQUIRE(parser.has_value());
  auto tree = parser->parse(R"({"a": 1, "b": 2})");
  REQUIRE(tree.has_value());
  auto query = ts::Query::create(tree_sitter_json(), "(pair) @p");
  REQUIRE(query.has_value());

  ts::QueryCursor cursor;
  auto matches = cursor.getMatches(*query, tree->getRootNode());
  REQUIRE(matches.has_value());
  auto it = std::ranges::begin(*matches);
  REQUIRE(it != std::ranges::end(*matches));
  cursor.removeMatch(*it);
  ++it;
  CHECK(it != std::ranges::end(*matches));
}
