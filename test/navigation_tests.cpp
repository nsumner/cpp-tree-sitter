#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include <doctest/doctest.h>
#include <cpp-tree-sitter.h>

#include <stringify.h>

extern "C" TSLanguage* tree_sitter_json();


namespace {
ts::Tree parseJson(std::string_view source) {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  return parser.parse(source).value();
}
}  // namespace


TEST_CASE("getChildren works on temporaries and composes with views") {
  constexpr std::string_view source = "[1, null]";
  const ts::Tree tree = parseJson(source);

  // Iterating over a temporary Node is safe, since ChildrenView holds it by
  // value.
  size_t total = 0;
  for (ts::Node const child : tree.getRootNode().getNamedChild(0)->getChildren()) {
    CHECK(!child.isNull());
    ++total;
  }
  CHECK(total == 5);

  auto named = tree.getRootNode().getNamedChild(0)->getNamedChildren();
  std::vector<ts::Node> values = named | std::ranges::to<std::vector>();
  REQUIRE(values.size() == 2);
  CHECK(values[0].getType() == "number");
  CHECK(values[1].getType() == "null");

  auto numbers = tree.getRootNode().getNamedChild(0)->getChildren()
    | std::views::filter([](ts::Node n) { return n.getType() == "number"; });
  CHECK(std::ranges::distance(numbers) == 1);
}


TEST_CASE("children compose through a stacked adaptor pipeline into a vector") {
  constexpr std::string_view source = R"([1, "two", 33])";
  const ts::Tree tree = parseJson(source);
  std::string_view const text = source;

  // transform moves the element type away from ts::Node, so a transform_view
  // over a filter_view over ChildrenView has to hold up through
  // materialization.
  std::vector<std::string_view> const named =
    tree.getRootNode().getNamedChild(0)->getChildren()
      | std::views::filter(&ts::Node::isNamed)
      | std::views::transform([text](ts::Node n) { return n.getSourceRange(text); })
      | std::ranges::to<std::vector>();

  CHECK(named == std::vector<std::string_view>{"1", R"("two")", "33"});
}


TEST_CASE("Cursor navigates down, across, and back up") {
  constexpr std::string_view source = "[1, null]";
  const ts::Tree tree = parseJson(source);
  ts::Node const root = tree.getRootNode();

  ts::Cursor cursor = root.getCursor();
  CHECK(cursor.getCurrentNode().getID() == root.getID());
  REQUIRE(cursor.gotoFirstChild());          // array
  CHECK(cursor.getCurrentNode().getType() == "array");
  REQUIRE(cursor.gotoFirstChild());          // '['
  REQUIRE(cursor.gotoNextSibling());         // number
  CHECK(cursor.getCurrentNode().getType() == "number");
  CHECK(cursor.getDepthFromOrigin() == 2);
  REQUIRE(cursor.gotoParent());
  CHECK(cursor.getCurrentNode().getType() == "array");
  CHECK(!ts::Cursor{root.impl}.gotoParent()); // cannot climb above origin
}


TEST_CASE("sibling getters hit within a list and miss at the edges") {
  constexpr std::string_view source = "[1, null]";
  const ts::Tree tree = parseJson(source);
  ts::Node const root = tree.getRootNode();

  CHECK(!root.getPreviousSibling().has_value());
  CHECK(!root.getNextSibling().has_value());

  ts::Node const number = root.getNamedChild(0)->getNamedChild(0).value();
  REQUIRE(number.getType() == "number");
  auto next = number.getNextSibling();       // the ',' token
  REQUIRE(next.has_value());
  CHECK(next->getType() == ",");
  auto prev = number.getPreviousSibling();   // the '[' token
  REQUIRE(prev.has_value());
  CHECK(prev->getType() == "[");
}


TEST_CASE("Cursor walks to the last child and backwards across siblings") {
  constexpr std::string_view source = "[1, null]";
  const ts::Tree tree = parseJson(source);
  ts::Node const root = tree.getRootNode();

  ts::Cursor cursor = root.getCursor();
  CHECK(!cursor.gotoPreviousSibling());      // no sibling at the origin
  REQUIRE(cursor.gotoFirstChild());          // array
  REQUIRE(cursor.gotoLastChild());           // ']'
  CHECK(cursor.getCurrentNode().getType() == "]");
  CHECK(!cursor.gotoLastChild());            // ']' is a leaf
  REQUIRE(cursor.gotoPreviousSibling());     // null
  CHECK(cursor.getCurrentNode().getType() == "null");
}


TEST_CASE("Cursor reset overloads restore position") {
  constexpr std::string_view source = "[1, null]";
  const ts::Tree tree = parseJson(source);
  ts::Node const root = tree.getRootNode();

  ts::Cursor walker = root.getCursor();
  REQUIRE(walker.gotoFirstChild());          // array
  REQUIRE(walker.gotoFirstChild());          // '['

  walker.reset(root);                        // reset to a node
  CHECK(walker.getCurrentNode().getID() == root.getID());

  ts::Cursor other = root.getCursor();
  REQUIRE(other.gotoFirstChild());           // array
  walker.reset(other);                       // reset to another cursor
  CHECK(walker.getCurrentNode().getID() == other.getCurrentNode().getID());
}


TEST_CASE("navigation misses are absent optionals, hits are engaged") {
  constexpr std::string_view source = "[1, null]";
  const ts::Tree tree = parseJson(source);
  ts::Node const root = tree.getRootNode();

  CHECK(!root.getParent().has_value());
  CHECK(!root.getChild(root.getNumChildren()).has_value());
  CHECK(!root.getChildByFieldName("no_such_field").has_value());
  CHECK(!root.getChildByFieldName("").has_value());
  CHECK(!root.getChildByFieldName(std::string_view{}).has_value());  // null data(), memory-safe

  auto array = root.getNamedChild(0);
  REQUIRE(array.has_value());
  CHECK(array->getType() == "array");
  auto parent = array->getParent();
  REQUIRE(parent.has_value());
  CHECK(parent->getID() == root.getID());
}


// Byte offsets into "[1, null]":
//
//   [  1  ,     n  u  l  l  ]
//   0  1  2  3  4  5  6  7  8
//
// An empty range is how you ask what sits at a single offset.
TEST_CASE("Descendant lookup finds the innermost node covering a range") {
  constexpr std::string_view source = "[1, null]";
  const ts::Tree tree = parseJson(source);
  ts::Node const root = tree.getRootNode();
  ts::Node const array = root.getNamedChild(0).value();

  auto const number = root.getDescendantForByteRange({1, 1});
  REQUIRE(number.has_value());
  CHECK(number->getType() == "number");
  CHECK(number->getSourceRange(source) == "1");

  // A range straddling two children stops at their common parent rather
  // than picking one of them.
  auto const spanning = root.getDescendantForByteRange({1, 8});
  REQUIRE(spanning.has_value());
  CHECK(*spanning == array);

  // The lookup starts from the node it is called on.
  auto const fromArray = array.getDescendantForByteRange({4, 8});
  REQUIRE(fromArray.has_value());
  CHECK(fromArray->getType() == "null");
}


TEST_CASE("The named form skips anonymous nodes the plain form reports") {
  constexpr std::string_view source = "[1, null]";
  const ts::Tree tree = parseJson(source);
  ts::Node const root = tree.getRootNode();
  ts::Node const array = root.getNamedChild(0).value();

  // Offset 2 is the comma, which is an anonymous node.
  auto const anonymous = root.getDescendantForByteRange({2, 2});
  REQUIRE(anonymous.has_value());
  CHECK(anonymous->getType() == ",");
  CHECK(!anonymous->isNamed());

  auto const named = root.getNamedDescendantForByteRange({2, 2});
  REQUIRE(named.has_value());
  CHECK(named->isNamed());
  CHECK(*named == array);
}


TEST_CASE("Point and byte lookup agree on the same location") {
  constexpr std::string_view source = "[1, null]";
  const ts::Tree tree = parseJson(source);
  ts::Node const root = tree.getRootNode();

  // The source is a single line, so column and byte offset coincide.
  ts::Extent<ts::Point> const atNull{.start=ts::Point{0, 4},
                                     .end=ts::Point{0, 4}};

  auto const byPoint = root.getDescendantForPointRange(atNull);
  auto const byByte = root.getDescendantForByteRange({4, 4});
  REQUIRE(byPoint.has_value());
  REQUIRE(byByte.has_value());
  CHECK(byPoint->getType() == "null");
  CHECK(*byPoint == *byByte);

  auto const namedByPoint = root.getNamedDescendantForPointRange(atNull);
  auto const namedByByte = root.getNamedDescendantForByteRange({4, 4});
  REQUIRE(namedByPoint.has_value());
  REQUIRE(namedByByte.has_value());
  CHECK(*namedByPoint == *namedByByte);
}


TEST_CASE("Descendant lookup is empty only for an inverted range") {
  constexpr std::string_view source = "[1, null]";
  const ts::Tree tree = parseJson(source);
  ts::Node const root = tree.getRootNode();
  ts::Node const number = root.getDescendantForByteRange({1, 1}).value();

  // start past end is the one empty result.
  CHECK(!root.getDescendantForByteRange({8, 1}).has_value());
  CHECK(!root.getNamedDescendantForByteRange({8, 1}).has_value());
  CHECK(!root.getDescendantForPointRange(
    {.start=ts::Point{0, 8}, .end=ts::Point{0, 1}}).has_value());
  CHECK(!root.getNamedDescendantForPointRange(
    {.start=ts::Point{0, 8}, .end=ts::Point{0, 1}}).has_value());

  // A range no child covers yields the node itself. `number` has
  // no children, so a lookup outside it still comes back as `number`.
  auto const outside = number.getDescendantForByteRange({4, 8});
  REQUIRE(outside.has_value());
  CHECK(*outside == number);

  // Past the end of the source behaves the same way.
  auto const beyond = root.getDescendantForByteRange({500, 900});
  REQUIRE(beyond.has_value());
  CHECK(*beyond == root);
}
