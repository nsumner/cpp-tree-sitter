#include <ranges>
#include <string>
#include <utility>

#include <doctest/doctest.h>
#include <cpp-tree-sitter.h>
#include <cts/query.h>

extern "C" TSLanguage* tree_sitter_json();


TEST_CASE("moved-from Parser/Tree/Cursor destruct safely and moves preserve state") {
  constexpr std::string_view source = "[1, null]";

  SUBCASE("Parser move") {
    auto parser = ts::Parser::create(tree_sitter_json()).value();
    ts::Parser moved = std::move(parser);
    const ts::Tree tree = moved.parse(source).value();
    CHECK(!tree.getRootNode().isNull());
  }  // both parsers destroyed here, sanitizers arbitrate

  SUBCASE("Tree move") {
    auto parser = ts::Parser::create(tree_sitter_json()).value();
    ts::Tree tree = parser.parse(source).value();
    const ts::Tree moved = std::move(tree);
    CHECK(moved.getRootNode().getType() == "document");
  }

  SUBCASE("Cursor move construction and assignment") {
    auto parser = ts::Parser::create(tree_sitter_json()).value();
    const ts::Tree tree = parser.parse(source).value();
    ts::Cursor a = tree.getRootNode().getCursor();
    ts::Cursor b = std::move(a);
    CHECK(b.getCurrentNode().getType() == "document");
    ts::Cursor c = tree.getRootNode().getCursor();
    c = std::move(b);
    CHECK(c.getCurrentNode().getType() == "document");
  }  // moved-from a and b destroyed here

  SUBCASE("Cursor self-move-assignment is harmless") {
    auto parser = ts::Parser::create(tree_sitter_json()).value();
    const ts::Tree tree = parser.parse(source).value();
    ts::Cursor cursor = tree.getRootNode().getCursor();
    ts::Cursor* alias = &cursor;   // defeat -Wself-move
    cursor = std::move(*alias);
    CHECK(cursor.getCurrentNode().getType() == "document");
  }

  SUBCASE("assigning into a moved-from Cursor revives it") {
    auto parser = ts::Parser::create(tree_sitter_json()).value();
    const ts::Tree tree = parser.parse(source).value();
    ts::Cursor a = tree.getRootNode().getCursor();
    ts::Cursor const b = std::move(a);
    a = tree.getRootNode().getCursor();
    CHECK(a.getCurrentNode().getType() == "document");
    CHECK(b.getCurrentNode().getType() == "document");
  }
}


TEST_CASE("Cursor copy() yields an independent cursor") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  constexpr std::string_view source = "[1, null]";
  const ts::Tree tree = parser.parse(source).value();

  ts::Cursor const original = tree.getRootNode().getCursor();
  ts::Cursor copy = original.copy();
  REQUIRE(copy.gotoFirstChild());
  CHECK(copy.getCurrentNode().getType() == "array");
  CHECK(original.getCurrentNode().getType() == "document");  // unaffected
}


TEST_CASE("query result views move without aliasing the cursor") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  constexpr std::string_view source = R"({"a": 1, "b": 2})";
  const auto tree = parser.parse(source).value();
  auto query = ts::Query::create(tree_sitter_json(), "(pair) @p").value();

  SUBCASE("move construction transfers the iteration") {
    ts::QueryCursor cursor;
    auto original = cursor.getMatches(query, tree.getRootNode()).value();
    auto moved = std::move(original);
    CHECK(std::ranges::distance(moved) == 2);
  }  // moved-from view destroyed here, sanitizers arbitrate

  SUBCASE("move assignment transfers the iteration") {
    ts::QueryCursor first;
    ts::QueryCursor second;
    auto target = first.getMatches(query, tree.getRootNode()).value();
    target = second.getMatches(query, tree.getRootNode()).value();
    CHECK(std::ranges::distance(target) == 2);
  }

  SUBCASE("self-move-assignment is harmless") {
    ts::QueryCursor cursor;
    auto view = cursor.getMatches(query, tree.getRootNode()).value();
    auto* alias = &view;          // defeat -Wself-move
    view = std::move(*alias);
    CHECK(std::ranges::distance(view) == 2);
  }

  SUBCASE("moving the cursor itself leaves an outstanding view valid") {
    // The view points at the QueryCursor's detail::ExecState, so moving the
    // cursor only moves the unique_ptr<ExecState>.
    ts::QueryCursor cursor;
    auto view = cursor.getMatches(query, tree.getRootNode()).value();
    ts::QueryCursor const moved = std::move(cursor);
    CHECK(std::ranges::distance(view) == 2);
  }
}


TEST_CASE("query iterators move without aliasing the stream") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  constexpr std::string_view source = R"({"a": 1, "b": 2, "c": 3})";
  const auto tree = parser.parse(source).value();
  auto query = ts::Query::create(tree_sitter_json(), "(pair) @p").value();

  SUBCASE("move construction transfers the position") {
    ts::QueryCursor cursor;
    auto view = cursor.getMatches(query, tree.getRootNode()).value();
    auto original = view.begin();
    ++original;                        // consumed one of three
    auto moved = std::move(original);
    size_t remaining = 0;
    for (; moved != std::default_sentinel; ++moved) { ++remaining; }
    CHECK(remaining == 2);
  }  // moved-from iterator destroyed here, sanitizers arbitrate

  SUBCASE("move assignment revives a moved-from iterator at the position") {
    ts::QueryCursor cursor;
    auto view = cursor.getMatches(query, tree.getRootNode()).value();
    auto target = view.begin();
    auto moved = std::move(target);
    ++moved;                           // consumed one of three
    target = std::move(moved);         // and back into the moved-from one
    size_t remaining = 0;
    for (; target != std::default_sentinel; ++target) { ++remaining; }
    CHECK(remaining == 2);
  }
}
