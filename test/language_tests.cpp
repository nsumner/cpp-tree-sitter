#include <algorithm>
#include <iterator>
#include <optional>
#include <ranges>
#include <string_view>
#include <type_traits>
#include <utility>

#include <doctest/doctest.h>
#include <cpp-tree-sitter.h>

extern "C" TSLanguage* tree_sitter_json();
extern "C" TSLanguage* tree_sitter_ctstest();


namespace {
constexpr TSSymbol symbolTable[3]{4, 5, 6};
constexpr ts::SymbolRange compileTimeSymbols =
  ts::detail::makeRange<ts::SymbolRange>(symbolTable, 3);
}  // namespace


static_assert(compileTimeSymbols.size() == 3);
static_assert(compileTimeSymbols[1] == ts::Symbol{5});

static_assert(std::random_access_iterator<std::ranges::iterator_t<ts::SymbolRange>>);
static_assert(std::ranges::random_access_range<ts::SymbolRange>);
static_assert(std::ranges::sized_range<ts::SymbolRange>);
static_assert(std::ranges::borrowed_range<ts::SymbolRange>);
static_assert(!std::ranges::contiguous_range<ts::SymbolRange>);

static_assert(std::is_same_v<decltype(std::declval<ts::Language>().getNumSymbols()),
                             uint32_t>);
static_assert(std::is_same_v<decltype(std::declval<ts::Language>().getNumFields()),
                             uint32_t>);


TEST_CASE("Language exposes ABI version, fields, and symbol lookup") {
  ts::Language const language = tree_sitter_json();

  CHECK(language.getAbiVersion() >= 13);
  CHECK(language.getNumSymbols() > 0);

  auto arraySymbol = language.getSymbolForName("array", true);
  REQUIRE(arraySymbol.has_value());
  CHECK(language.getSymbolName(*arraySymbol) == "array");

  REQUIRE(language.getNumSymbols() < 60000);
  CHECK(!language.getSymbolName(ts::Symbol{60000}).has_value());

  CHECK(language.getSymbolName(ts::Symbol{65535}) == "ERROR");
  CHECK(language.getSymbolName(ts::Symbol{65534}) == "_ERROR");

  CHECK(!language.getSymbolForName("no_such_node", true).has_value());
  CHECK(!language.getSymbolForName("", false).has_value());
  auto emptyNamed = language.getSymbolForName("", true);
  REQUIRE(emptyNamed.has_value());
  CHECK(language.getSymbolName(*emptyNamed) == "ERROR");

  CHECK(language.getNumFields() >= 2);
  auto keyField = language.getFieldId("key");
  REQUIRE(keyField.has_value());
  CHECK(language.getFieldName(*keyField) == "key");
  CHECK(!language.getFieldId("no_such_field").has_value());
  CHECK(!language.getFieldId("").has_value());
  CHECK(!language.getFieldName(ts::FieldId{60000}).has_value());
}


TEST_CASE("getName reflects the grammar's language ABI") {
  ts::Language const language = tree_sitter_json();
  auto name = language.getName();
  if (language.getAbiVersion() >= 15) {
    REQUIRE(name.has_value());
    CHECK(*name == "json");
  } else {
    CHECK(!name.has_value());
  }
}


TEST_CASE("default-constructed string_view lookups are memory-safe") {
  ts::Language const language = tree_sitter_json();
  std::string_view const nullView{};  // data() == nullptr, size() == 0

  CHECK(!language.getSymbolForName(nullView, false).has_value());
  auto emptyNamed = language.getSymbolForName(nullView, true);
  REQUIRE(emptyNamed.has_value());
  CHECK(language.getSymbolName(*emptyNamed) == "ERROR");
  CHECK(!language.getFieldId(nullView).has_value());
}


TEST_CASE("symbol types classify visible and internal symbols") {
  ts::Language const language{tree_sitter_json()};
  auto object = language.getSymbolForName("object", true);
  REQUIRE(object.has_value());
  CHECK(language.getSymbolType(*object) == ts::SymbolType::Regular);

  auto lbrace = language.getSymbolForName("{", false);
  REQUIRE(lbrace.has_value());
  CHECK(language.getSymbolType(*lbrace) == ts::SymbolType::Anonymous);

  bool sawSupertypeValue = false;
  for (size_t s = 0; s < language.getNumSymbols(); ++s) {
    auto const symbol = static_cast<ts::Symbol>(s);
    if (language.getSymbolName(symbol) == "_value") {
      sawSupertypeValue |=
        language.getSymbolType(symbol) == ts::SymbolType::Supertype;
      CHECK(language.getSubtypes(symbol).empty());
    }
  }
  CHECK(sawSupertypeValue);
}


TEST_CASE("supertype introspection is empty on an ABI-14 grammar") {
  ts::Language const language{tree_sitter_json()};
  CHECK(language.getAbiVersion() == 14);
  CHECK(language.getSupertypes().empty());
}


TEST_CASE("ctstest fixture exposes ABI-15 supertype metadata") {
  ts::Language const language{tree_sitter_ctstest()};
  CHECK(language.getAbiVersion() >= 15);
  CHECK(language.getSupertypes().size() == 5);

  auto literal = language.getSymbolForName("_literal", true);
  REQUIRE(literal.has_value());
  CHECK(language.getSymbolType(*literal) == ts::SymbolType::Supertype);

  bool hasNestedSupertype = false;
  for (ts::Symbol const sub : language.getSubtypes(*literal)) {
    hasNestedSupertype |=
      language.getSymbolType(sub) == ts::SymbolType::Supertype;
  }
  CHECK(hasNestedSupertype);
}


TEST_CASE("ctstest fixture surfaces its alias node type") {
  auto parser = ts::Parser::create(tree_sitter_ctstest()).value();
  const ts::Tree tree = parser.parse("a: 1").value();
  ts::Node const pair = tree.getRootNode().getNamedChild(0).value();
  CHECK(pair.getType() == "pair");
  CHECK(pair.getNamedChild(0).value().getType() == "key");
}


TEST_CASE("Supertypes from a temporary range keep usable iterators") {
  ts::Language language{tree_sitter_ctstest()};
  REQUIRE(language.getAbiVersion() >= 15);

  auto found = std::ranges::find_if(
    language.getSupertypes(),
    [&](ts::Symbol symbol) { return language.getSymbolName(symbol) == "_literal"; });
  static_assert(!std::is_same_v<decltype(found), std::ranges::dangling>);
  REQUIRE(found != language.getSupertypes().end());
  CHECK(language.getSymbolName(*found) == "_literal");
}
