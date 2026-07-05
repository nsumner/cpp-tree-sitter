#include <string>

#include <doctest/doctest.h>
#include <cpp-tree-sitter.h>

extern "C" TSLanguage* tree_sitter_json();


TEST_CASE("field access resolves names, ids, and per-child field names") {
  ts::Parser parser = ts::Parser::create(tree_sitter_json()).value();
  constexpr std::string_view source = R"({"a": 1})";
  const ts::Tree tree = parser.parse(source).value();

  ts::Node const pair = tree.getRootNode()
                      .getNamedChild(0).value()   // object
                      .getNamedChild(0).value();  // pair
  REQUIRE(pair.getType() == "pair");

  auto key = pair.getChildByFieldName("key");
  REQUIRE(key.has_value());
  CHECK(key->getSourceRange(source) == R"("a")");

  ts::Language const language = tree.getLanguage();
  auto valueId = language.getFieldId("value");
  REQUIRE(valueId.has_value());
  auto value = pair.getChildByFieldId(*valueId);
  REQUIRE(value.has_value());
  CHECK(value->getSourceRange(source) == "1");

  CHECK(pair.getFieldNameForNamedChild(0) == "key");
  CHECK(pair.getFieldNameForNamedChild(1) == "value");
  CHECK(!pair.getFieldNameForNamedChild(99).has_value());
  // The ':' anonymous child carries no field name.
  CHECK(!pair.getFieldNameForChild(1).has_value());
}
