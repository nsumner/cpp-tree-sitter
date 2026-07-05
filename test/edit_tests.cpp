#include <string>

#include <doctest/doctest.h>
#include <cpp-tree-sitter.h>

#include <stringify.h>

extern "C" TSLanguage* tree_sitter_json();


TEST_CASE("InputEdit converts to the C struct field-for-field") {
  ts::InputEdit const edit{
    .start  = {.byte=6, .point={0, 6}},
    .oldEnd = {.byte=7, .point={0, 7}},
    .newEnd = {.byte=8, .point={0, 8}},
  };
  TSInputEdit const raw = edit.toRaw();
  CHECK(raw.start_byte == 6);
  CHECK(raw.old_end_byte == 7);
  CHECK(raw.new_end_byte == 8);
  CHECK(raw.start_point.column == 6);
  CHECK(raw.old_end_point.column == 7);
  CHECK(raw.new_end_point.column == 8);
}


static_assert(std::is_trivially_copyable_v<ts::Location>);
static_assert(std::is_trivially_copyable_v<ts::InputEdit>);


TEST_CASE("incremental re-parse matches a fresh parse after an edit") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();

  std::string const oldSource = R"({"a": 1})";
  auto oldTree = parser.parse(oldSource).value();

  // Replace the value `1` (byte 6, width 1) with `42` (width 2).
  std::string const newSource = R"({"a": 42})";
  ts::InputEdit const edit{
    .start  = {.byte=6, .point={0, 6}},
    .oldEnd = {.byte=7, .point={0, 7}},
    .newEnd = {.byte=8, .point={0, 8}},
  };
  oldTree.edit(edit);

  const auto incremental = parser.parse(newSource, oldTree).value();
  const auto fresh = parser.parse(newSource).value();
  CHECK(incremental.getRootNode().getSExpr() == fresh.getRootNode().getSExpr());

  // ts_tree_get_changed_ranges diffs node types and shifted extents rather
  // than text content, so a same-type edit like this reports nothing even
  // though the digits differ.
  CHECK(oldTree.getChangedRanges(incremental).empty());
}


TEST_CASE("a type-changing edit yields a changed range") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();

  std::string const oldSource = R"({"a": 1})";
  auto oldTree = parser.parse(oldSource).value();

  // Replace the `number` value `1` (byte 6, width 1) with the `true` literal
  // (width 4). The node type changes, so the edit is a structural one.
  std::string const newSource = R"({"a": true})";
  ts::InputEdit const edit{
    .start  = {.byte=6, .point={0, 6}},
    .oldEnd = {.byte=7, .point={0, 7}},
    .newEnd = {.byte=10, .point={0, 10}},
  };
  oldTree.edit(edit);

  const auto incremental = parser.parse(newSource, oldTree).value();
  const auto fresh = parser.parse(newSource).value();
  CHECK(incremental.getRootNode().getSExpr() == fresh.getRootNode().getSExpr());

  auto changed = oldTree.getChangedRanges(incremental);
  REQUIRE(!changed.empty());
  CHECK(changed.front().start.byte <= 6);
  CHECK(changed.front().end.byte >= 7);
}


TEST_CASE("identical trees have no changed ranges") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  constexpr std::string_view source = R"([1])";
  const auto a = parser.parse(source).value();
  const auto b = parser.parse(source, a).value();
  CHECK(a.getChangedRanges(b).empty());
}
