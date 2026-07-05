#include <array>
#include <concepts>
#include <expected>
#include <initializer_list>
#include <ranges>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <doctest/doctest.h>
#include <cpp-tree-sitter.h>

#include <stringify.h>

extern "C" TSLanguage* tree_sitter_json();

TEST_CASE("parsing the README example yields the documented tree") {
  ts::Language const language = tree_sitter_json();
  auto parser = ts::Parser::create(language).value();

  constexpr std::string_view source = "[1, null]";
  const ts::Tree tree = parser.parse(source).value();

  ts::Node const root = tree.getRootNode();
  REQUIRE(!root.isNull());
  CHECK(root.getType() == "document");
  CHECK(root.getNumChildren() == 1);

  ts::Node const array = root.getNamedChild(0).value();
  CHECK(array.getType() == "array");
  CHECK(array.getNumChildren() == 5);
  CHECK(array.getNumNamedChildren() == 2);

  ts::Node const number = array.getNamedChild(0).value();
  CHECK(number.getType() == "number");
  CHECK(number.getNumChildren() == 0);
  CHECK(number.getSourceRange(source) == "1");

  CHECK(!tree.hasError());
}


TEST_CASE("malformed input is reported via hasError") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  constexpr std::string_view source = "[1, ";
  const ts::Tree tree = parser.parse(source).value();
  CHECK(tree.hasError());
}


TEST_CASE("byte and point ranges reflect the source layout") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  constexpr std::string_view source = "[1, null]";
  const ts::Tree tree = parser.parse(source).value();
  ts::Node const array = tree.getRootNode().getNamedChild(0).value();

  auto const end = static_cast<uint32_t>(source.size());
  CHECK(array.getByteRange() == ts::Extent<uint32_t>{0, end});
  CHECK(array.getPointRange()
        == ts::Extent<ts::Point>{ts::Point{0, 0}, ts::Point{0, end}});
}


TEST_CASE("parse accepts empty and default-constructed string_views") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();

  const auto empty = parser.parse("");
  REQUIRE(empty.has_value());
  CHECK(empty->getRootNode().getNumChildren() == 0);

  const auto defaulted = parser.parse(std::string_view{});
  REQUIRE(defaulted.has_value());
  CHECK(defaulted->getRootNode().getNumChildren() == 0);
}


namespace {
// The sources below are single lines, so a byte offset and a column are the
// same number and a range can be written from the offsets alone.
ts::Range byteRange(uint32_t start, uint32_t end) {
  return ts::Range{{.byte=start, .point=ts::Point{0, start}},
                   {.byte=end,   .point=ts::Point{0, end}}};
}
}  // namespace


TEST_CASE("Included ranges parse a window of a document in its own coordinates") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  constexpr std::string_view source = "xxx[1, null]yyy";

  REQUIRE(parser.setIncludedRanges({byteRange(3, 12)}).has_value());
  const ts::Tree tree = parser.parse(source).value();
  ts::Node const root = tree.getRootNode();

  CHECK(!root.hasError());
  CHECK(root.getSExpr() == "(document (array (number) (null)))");

  CHECK(root.getByteRange() == ts::Extent<uint32_t>{3, 12});
  CHECK(root.getSourceRange(source) == "[1, null]");
}


TEST_CASE("Several included ranges are parsed as one contiguous text") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  constexpr std::string_view source = "[1, JUNKnull]";

  REQUIRE(parser.setIncludedRanges({byteRange(0, 4), byteRange(8, 13)})
            .has_value());
  const ts::Tree tree = parser.parse(source).value();

  CHECK(!tree.getRootNode().hasError());
  CHECK(tree.getRootNode().getSExpr() == "(document (array (number) (null)))");
}


TEST_CASE("Included ranges must be ordered and must not overlap") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  constexpr std::string_view source = "xxx[1, null]yyy";

  REQUIRE(parser.setIncludedRanges({byteRange(3, 12)}).has_value());

  CHECK(parser.setIncludedRanges({byteRange(3, 7), byteRange(7, 12)})
          .has_value());
  REQUIRE(parser.setIncludedRanges({byteRange(3, 12)}).has_value());

  auto const overlapping =
    parser.setIncludedRanges({byteRange(3, 9), byteRange(6, 12)});
  REQUIRE(!overlapping.has_value());
  CHECK(overlapping.error().kind
        == ts::ErrorKind::ParserIncludedRangesUnordered);

  auto const unordered =
    parser.setIncludedRanges({byteRange(8, 12), byteRange(3, 7)});
  REQUIRE(!unordered.has_value());
  CHECK(unordered.error().kind
        == ts::ErrorKind::ParserIncludedRangesUnordered);

  auto const inverted = parser.setIncludedRanges({byteRange(12, 3)});
  REQUIRE(!inverted.has_value());
  CHECK(inverted.error().kind
        == ts::ErrorKind::ParserIncludedRangesUnordered);

  const ts::Tree tree = parser.parse(source).value();
  CHECK(!tree.getRootNode().hasError());
  CHECK(tree.getRootNode().getByteRange() == ts::Extent<uint32_t>{3, 12});
}


TEST_CASE("An empty range list restores whole-document parsing") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  constexpr std::string_view source = "xxx[1, null]yyy";

  REQUIRE(parser.setIncludedRanges({byteRange(3, 12)}).has_value());
  REQUIRE(!parser.parse(source).value().getRootNode().hasError());

  REQUIRE(parser.setIncludedRanges({}).has_value());
  const ts::Tree whole = parser.parse(source).value();

  ts::Node const root = whole.getRootNode();
  CHECK(root.hasError());
  CHECK(root.getByteRange() == ts::Extent<uint32_t>{0, 15});
}


TEST_CASE("Included ranges over a multi-line document from byte offsets") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  constexpr std::string_view source = "prelude\n[1,\n null]\ntrailer";

  ts::Range const json = ts::rangeForBytes(source, {8, 18});
  REQUIRE(json.start == ts::Location{8, ts::Point{1, 0}});
  REQUIRE(json.end == ts::Location{18, ts::Point{2, 6}});

  REQUIRE(parser.setIncludedRanges({json}).has_value());
  const ts::Tree tree = parser.parse(source).value();
  ts::Node const root = tree.getRootNode();

  CHECK(!root.hasError());
  CHECK(root.getSExpr() == "(document (array (number) (null)))");
  CHECK(root.getSourceRange(source) == "[1,\n null]");
  CHECK(root.getRange() == json);
}


TEST_CASE("A node's own range can be fed straight back as an included range") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  constexpr std::string_view source = "[1,\n [2, 3]]";

  const ts::Tree host = parser.parse(source).value();
  ts::Node const outer = host.getRootNode().getNamedChild(0).value();
  ts::Node const inner = outer.getNamedChild(1).value();
  REQUIRE(inner.getSourceRange(source) == "[2, 3]");

  REQUIRE(parser.setIncludedRanges({inner.getRange()}).has_value());
  const ts::Tree injected = parser.parse(source).value();

  CHECK(!injected.getRootNode().hasError());
  CHECK(injected.getRootNode().getSExpr()
        == "(document (array (number) (number)))");
  CHECK(injected.getRootNode().getRange() == inner.getRange());
}


static_assert(ts::detail::RangeSequence<std::vector<ts::Range>>);
static_assert(ts::detail::RangeSequence<std::array<ts::Range, 2>>);
static_assert(ts::detail::RangeSequence<std::span<ts::Range const>>);
static_assert(ts::detail::RangeSequence<std::initializer_list<ts::Range>>);
static_assert(!ts::detail::RangeSequence<std::vector<int>>);
static_assert(!ts::detail::RangeSequence<ts::Range>);


TEST_CASE("Included ranges accept a lazy pipeline over the host tree") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  constexpr std::string_view source = "[1,\n [2, 3]]";

  const ts::Tree host = parser.parse(source).value();
  ts::Node const outer = host.getRootNode().getNamedChild(0).value();

  auto injected = outer.getNamedChildren()
    | std::views::filter([](ts::Node node) { return node.getType() == "array"; })
    | std::views::transform([](ts::Node node) { return node.getRange(); });

  REQUIRE(parser.setIncludedRanges(injected).has_value());
  const ts::Tree tree = parser.parse(source).value();

  CHECK(!tree.getRootNode().hasError());
  CHECK(tree.getRootNode().getSExpr()
        == "(document (array (number) (number)))");
  CHECK(tree.getRootNode().getSourceRange(source) == "[2, 3]");
}


TEST_CASE("Included ranges accept a temporary view of computed ranges") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  constexpr std::string_view source = "[1, JUNKnull]";
  std::vector<ts::Extent<uint32_t>> const regions{{0, 4}, {8, 13}};

  REQUIRE(parser.setIncludedRanges(
    regions | std::views::transform([&](ts::Extent<uint32_t> bytes) {
      return ts::rangeForBytes(source, bytes);
    })).has_value());

  const ts::Tree tree = parser.parse(source).value();
  CHECK(!tree.getRootNode().hasError());
  CHECK(tree.getRootNode().getSExpr() == "(document (array (number) (null)))");
}


TEST_CASE("Included ranges accept any contiguous container of Range") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();

  std::vector<ts::Range> const fromVector{byteRange(3, 12)};
  CHECK(parser.setIncludedRanges(fromVector).has_value());

  std::array<ts::Range, 2> const fromArray{byteRange(0, 4), byteRange(8, 13)};
  CHECK(parser.setIncludedRanges(fromArray).has_value());

  CHECK(parser.setIncludedRanges(std::span{fromVector}).has_value());
}


static_assert(!std::copyable<ts::Parser>);
static_assert(!std::copyable<ts::Tree>);
static_assert(std::movable<ts::Parser>);
static_assert(std::movable<ts::Tree>);


TEST_CASE("raw exposes the underlying TSTree without transferring ownership") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  ts::Tree tree = parser.parse("[1, null]").value();

  TSTree const* rawHandle = tree.raw();
  REQUIRE(rawHandle != nullptr);
  CHECK(ts_node_eq(ts_tree_root_node(rawHandle), tree.getRootNode().impl));

  static_assert(std::is_same_v<decltype(tree.raw()), TSTree*>);
  static_assert(std::is_same_v<decltype(std::as_const(tree).raw()),
                               TSTree const*>);
  CHECK(std::as_const(tree).raw() == rawHandle);
}
