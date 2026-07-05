#include <algorithm>
#include <concepts>
#include <map>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include <doctest/doctest.h>
#include <cpp-tree-sitter.h>

#include <stringify.h>

extern "C" TSLanguage* tree_sitter_json();
extern "C" TSLanguage* tree_sitter_ctstest();


// Point must stay a drop-in for TSPoint at the C boundary.
static_assert(sizeof(ts::Point) == sizeof(TSPoint));
static_assert(alignof(ts::Point) == alignof(TSPoint));
static_assert(std::is_trivially_copyable_v<ts::Point>);
static_assert(std::is_standard_layout_v<ts::Point>);
static_assert(std::is_convertible_v<TSPoint, ts::Point>);
static_assert(std::is_convertible_v<ts::Point, TSPoint>);


TEST_CASE("Point converts implicitly to and from TSPoint") {
  TSPoint const raw{3, 7};
  ts::Point const point = raw;
  CHECK(point.row == 3);
  CHECK(point.column == 7);

  TSPoint const roundTrip = point;
  CHECK(roundTrip.row == 3);
  CHECK(roundTrip.column == 7);
}


TEST_CASE("Point orders lexicographically and compares for equality") {
  CHECK(ts::Point{1, 2} == ts::Point{1, 2});
  CHECK(ts::Point{1, 2} != ts::Point{1, 3});
  CHECK(ts::Point{1, 2} < ts::Point{1, 3});
  CHECK(ts::Point{1, 9} < ts::Point{2, 0});
  CHECK(ts::Point{} == ts::Point{0, 0});
}


TEST_CASE("Point works as a key in ordered and unordered containers") {
  std::map<ts::Point, int> ordered;
  ordered[ts::Point{2, 0}] = 20;
  ordered[ts::Point{1, 5}] = 15;
  CHECK(ordered.begin()->first == ts::Point{1, 5});

  std::unordered_map<ts::Point, int> unordered;
  unordered[ts::Point{3, 4}] = 34;
  CHECK(unordered.at(ts::Point{3, 4}) == 34);
  CHECK(unordered.find(ts::Point{9, 9}) == unordered.end());
}


// FieldId is an opaque identifier rather than an integer, so it forbids
// arithmetic and silent substitution for the underlying uint16_t.
static_assert(sizeof(ts::FieldId) == sizeof(TSFieldId));
static_assert(!std::is_convertible_v<ts::FieldId, uint16_t>);
static_assert(!std::is_convertible_v<uint16_t, ts::FieldId>);


TEST_CASE("FieldId round-trips through the language and to the raw API") {
  ts::Language const language = tree_sitter_json();

  auto keyField = language.getFieldId("key");
  REQUIRE(keyField.has_value());
  CHECK(language.getFieldName(*keyField) == "key");

  CHECK(std::to_underlying(*keyField) != 0);
  CHECK(*keyField != ts::FieldId::None);

  CHECK(!language.getFieldId("no_such_field").has_value());
}


static_assert(sizeof(ts::Symbol) == sizeof(TSSymbol));
static_assert(!std::is_convertible_v<ts::Symbol, uint16_t>);
static_assert(!std::is_convertible_v<uint16_t, ts::Symbol>);

static_assert(!std::is_convertible_v<ts::Symbol, ts::FieldId>);
static_assert(!std::is_convertible_v<ts::FieldId, ts::Symbol>);


TEST_CASE("Symbol round-trips through the language and to the raw API") {
  ts::Language const language = tree_sitter_json();

  auto arraySymbol = language.getSymbolForName("array", true);
  REQUIRE(arraySymbol.has_value());
  CHECK(*arraySymbol != ts::Symbol::None);
  CHECK(language.getSymbolName(*arraySymbol) == "array");
  CHECK(language.getSymbolType(*arraySymbol) == ts::SymbolType::Regular);
  CHECK(std::to_underlying(*arraySymbol) != 0);

  CHECK(!language.getSymbolForName("no_such_node", true).has_value());
}


TEST_CASE("Symbol works as a key in unordered containers") {
  ts::Language const language = tree_sitter_json();
  auto arraySymbol = language.getSymbolForName("array", true);
  REQUIRE(arraySymbol.has_value());

  std::unordered_map<ts::Symbol, int> counts;
  counts[*arraySymbol] = 1;
  ++counts[*arraySymbol];
  CHECK(counts.at(*arraySymbol) == 2);
  CHECK(counts.size() == 1);
}


TEST_CASE("Subtype queries yield Symbols from a lazy, sized range") {
  ts::Language const language = tree_sitter_ctstest();
  REQUIRE(language.getAbiVersion() >= 15);

  auto literal = language.getSymbolForName("_literal", true);
  REQUIRE(literal.has_value());

  ts::SymbolRange const subtypes = language.getSubtypes(*literal);
  CHECK(std::ranges::size(subtypes) > 0);

  for (ts::Symbol const sub : subtypes) {
    auto name = language.getSymbolName(sub);
    REQUIRE(name.has_value());
    CHECK(!name->empty());
  }

  // The range is a view, so re-querying gives the same values without
  // copying anything.
  auto again = language.getSubtypes(*literal);
  CHECK(std::ranges::equal(subtypes, again));
}


static_assert(sizeof(ts::NodeID) == sizeof(uintptr_t));
static_assert(!std::is_convertible_v<ts::NodeID, uintptr_t>);
static_assert(std::is_same_v<std::underlying_type_t<ts::NodeID>, uintptr_t>);


TEST_CASE("NodeID identifies nodes without behaving like a number") {
  auto parser = ts::Parser::create(tree_sitter_json());
  REQUIRE(parser.has_value());
  std::string_view const source = "[1, null]";
  auto tree = parser->parse(source);
  REQUIRE(tree.has_value());

  ts::Node const root = tree->getRootNode();
  ts::Node const array = root.getNamedChild(0).value();

  CHECK(root.getID() == root.getID());
  CHECK(root.getID() != array.getID());

  std::unordered_map<ts::NodeID, int> seen;
  seen[root.getID()] = 1;
  seen[array.getID()] = 2;
  CHECK(seen.size() == 2);
  CHECK(seen.at(root.getID()) == 1);
}


static_assert(std::equality_comparable<ts::Node>);

TEST_CASE("Node compares by identity within one tree") {
  auto parser = ts::Parser::create(tree_sitter_json());
  REQUIRE(parser.has_value());
  std::string_view const source = "[1, null]";
  auto tree = parser->parse(source);
  REQUIRE(tree.has_value());

  ts::Node const root = tree->getRootNode();
  ts::Node const array = root.getNamedChild(0).value();

  CHECK(root == root);
  CHECK(root != array);

  // The same node reached by two different routes is one node.
  CHECK(array == root.getNamedChild(0).value());
  CHECK(array.getCursor().getCurrentNode() == array);
  CHECK(array.getParent().value() == root);

  // Node is comparable enough for the standard algorithms.
  ts::Node const first = array.getNamedChild(0).value();
  auto children = array.getNamedChildren();
  CHECK(std::ranges::find(children, first) != std::ranges::end(children));
  CHECK(std::ranges::count(children, first) == 1);
}


TEST_CASE("Nodes from different trees are never equal") {
  auto parser = ts::Parser::create(tree_sitter_json());
  REQUIRE(parser.has_value());
  std::string_view const source = "[1, null]";

  auto first = parser->parse(source);
  auto second = parser->parse(source);
  REQUIRE(first.has_value());
  REQUIRE(second.has_value());

  ts::Node const rootOfFirst = first->getRootNode();
  ts::Node const rootOfSecond = second->getRootNode();

  REQUIRE(rootOfFirst.getSExpr() == rootOfSecond.getSExpr());
  CHECK(rootOfFirst != rootOfSecond);
  CHECK(rootOfFirst == rootOfFirst);
}


TEST_CASE("Node::getRange reports both coordinate systems together") {
  auto parser = ts::Parser::create(tree_sitter_json());
  REQUIRE(parser.has_value());
  std::string_view const source = "[\n  1\n]";
  auto tree = parser->parse(source);
  REQUIRE(tree.has_value());

  ts::Node const root = tree->getRootNode();
  ts::Range const range = root.getRange();

  // It is exactly the two halves the node already exposes, paired up.
  CHECK(ts::Extent<uint32_t>{range.start.byte, range.end.byte}
        == root.getByteRange());
  CHECK(ts::Extent<ts::Point>{range.start.point, range.end.point}
        == root.getPointRange());
}


TEST_CASE("locationForByte counts rows and columns the way tree-sitter does") {
  SUBCASE("a column is a byte offset within its row") {
    std::string_view const source = "ab\ncde\nf";
    CHECK(ts::locationForByte(source, 0) == ts::Location{0, ts::Point{0, 0}});
    CHECK(ts::locationForByte(source, 2) == ts::Location{2, ts::Point{0, 2}});
    // Byte 3 is the first of row 1, since the '\n' itself ends row 0.
    CHECK(ts::locationForByte(source, 3) == ts::Location{3, ts::Point{1, 0}});
    CHECK(ts::locationForByte(source, 6) == ts::Location{6, ts::Point{1, 3}});
    CHECK(ts::locationForByte(source, 7) == ts::Location{7, ts::Point{2, 0}});
  }

  SUBCASE("only '\\n' breaks a line, so a CR counts toward its column") {
    std::string_view const source = "ab\r\ncd";
    CHECK(ts::locationForByte(source, 2) == ts::Location{2, ts::Point{0, 2}});
    // The '\r' at byte 2 advances the column rather than the row.
    CHECK(ts::locationForByte(source, 3) == ts::Location{3, ts::Point{0, 3}});
    CHECK(ts::locationForByte(source, 4) == ts::Location{4, ts::Point{1, 0}});
  }

  SUBCASE("a multi-byte character advances the column by its byte length") {
    // "é" is two bytes and "€" is three, so the column at the end is 5 even
    // though only two characters precede it.
    std::string_view const source = "\xC3\xA9\xE2\x82\xAC";
    CHECK(source.size() == 5);
    CHECK(ts::locationForByte(source, 5) == ts::Location{5, ts::Point{0, 5}});
  }

  SUBCASE("an offset past the end clamps to the end") {
    std::string_view const source = "ab\ncd";
    CHECK(ts::locationForByte(source, 500) == ts::locationForByte(source, 5));
    CHECK(ts::locationForByte(source, 500).byte == 5);
  }

  SUBCASE("an empty source has only the origin") {
    CHECK(ts::locationForByte("", 0) == ts::Location{0, ts::Point{0, 0}});
    CHECK(ts::locationForByte("", 9) == ts::Location{0, ts::Point{0, 0}});
  }
}


TEST_CASE("rangeForBytes resolves both endpoints") {
  std::string_view const source = "ab\ncde\nf";

  ts::Range const range = ts::rangeForBytes(source, {1, 6});
  CHECK(range.start == ts::Location{1, ts::Point{0, 1}});
  CHECK(range.end == ts::Location{6, ts::Point{1, 3}});

  // Each endpoint agrees with resolving it on its own.
  CHECK(range.start == ts::locationForByte(source, 1));
  CHECK(range.end == ts::locationForByte(source, 6));

  // An inverted extent stays inverted rather than collapsing onto the start.
  ts::Range const inverted = ts::rangeForBytes(source, {6, 1});
  CHECK(inverted.start == ts::locationForByte(source, 6));
  CHECK(inverted.end == ts::locationForByte(source, 1));
  CHECK(inverted.end < inverted.start);
}


TEST_CASE("rangeForBytes reproduces the points tree-sitter assigns") {
  auto parser = ts::Parser::create(tree_sitter_json());
  REQUIRE(parser.has_value());

  // Newlines, CRLF endings, tabs, and multi-byte characters in one document.
  std::string_view const source =
    "[\r\n\t1,\n  \"\xC3\xA9\xE2\x82\xAC\",\n\tnull,\n  [2, 3]\n]";
  auto tree = parser->parse(source);
  REQUIRE(tree.has_value());
  REQUIRE(!tree->getRootNode().hasError());

  size_t checked = 0;
  auto visit = [&](auto&& self, ts::Node node) -> void {
    CHECK(ts::rangeForBytes(source, node.getByteRange()) == node.getRange());
    ++checked;
    for (ts::Node const child : node.getChildren()) {
      self(self, child);
    }
  };
  visit(visit, tree->getRootNode());
  CHECK(checked > 15);
}


TEST_CASE("Range and TSRange convert without losing a field") {
  ts::Range const range{{.byte=3, .point=ts::Point{1, 2}},
                        {.byte=9, .point=ts::Point{4, 5}}};

  TSRange const raw = ts::detail::toRaw(range);
  CHECK(raw.start_byte == 3);
  CHECK(raw.end_byte == 9);
  CHECK(ts::Point{raw.start_point} == ts::Point{1, 2});
  CHECK(ts::Point{raw.end_point} == ts::Point{4, 5});

  CHECK(ts::detail::toRange(raw) == range);
}


TEST_CASE("Extent compares as a whole value") {
  CHECK(ts::Extent<uint32_t>{0, 9} == ts::Extent<uint32_t>{0, 9});
  CHECK(ts::Extent<uint32_t>{0, 9} != ts::Extent<uint32_t>{0, 8});

  ts::Extent<ts::Point> const span{.start=ts::Point{0, 0}, .end=ts::Point{0, 9}};
  CHECK(span == ts::Extent<ts::Point>{ts::Point{0, 0}, ts::Point{0, 9}});
  CHECK(span != ts::Extent<ts::Point>{ts::Point{0, 0}, ts::Point{1, 0}});

  // Ordering is lexicographic.
  CHECK(ts::Extent<uint32_t>{0, 9} < ts::Extent<uint32_t>{1, 2});
  CHECK(ts::Extent<uint32_t>{0, 8} < ts::Extent<uint32_t>{0, 9});
}


TEST_CASE("Location compares as a whole value") {
  ts::Location const here{.byte=9, .point=ts::Point{0, 9}};
  CHECK(here == ts::Location{9, ts::Point{0, 9}});
  CHECK(here != ts::Location{9, ts::Point{1, 0}});
  CHECK(ts::Location{0, ts::Point{0, 0}} < here);
}
