#include "pch.hpp"

// cpp-tree-sitter
#include <cpp-tree-sitter.hpp>

// Catch2
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

// STL
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

extern "C" const TSLanguage *tree_sitter_json();

TEST_CASE("Null Node Handling", "[node]")
{
    ts::Node null_node = ts::Node::null();

    SECTION("Basic null checks")
    {
        CHECK(null_node.isNull());
        CHECK(null_node.getType().compare("") == 0);
        CHECK(null_node.getSymbol() == 0);
        CHECK(null_node.getGrammarType().compare("") == 0);
        CHECK(null_node.getGrammarSymbol() == 0);
        CHECK(null_node.getChildCount() == 0);
    }

    SECTION("Identification")
    {
        CHECK(null_node.getID() == 0);
        CHECK(null_node.getLanguage().operator const TSLanguage *() == nullptr);
    }

    SECTION("Navigation from null node")
    {
        CHECK(null_node.getParent().isNull());
        CHECK(null_node.getNextSibling().isNull());
    }

    SECTION("Advanced Navigation")
    {
        CHECK(null_node.getParent().getParent().isNull());
        CHECK(null_node.getNextNamedSibling().isNamed() == false);
        CHECK(null_node.getChildCount() == 0);
        CHECK(null_node.getChild(0).isNull());
    }

    SECTION("Source text on null")
    {
        CHECK(null_node.getSourceRange("any source").empty());
        CHECK(null_node.getSourceText("any source").empty());
    }
}

TEST_CASE("Node navigation and attributes", "[node]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);
    std::string  code = R"({"id": 123})";
    ts::Tree     tree = parser.parseString(code);
    ts::Node     root = tree.getRootNode();

    // Search for object in document
    ts::Node obj = root.getNamedChild(0);

    SECTION("Source text access")
    {
        CHECK(obj.getType().compare("object") == 0);
        CHECK(obj.getSourceText(code) == code);
    }

    SECTION("Children iteration")
    {
        ts::Node pair = obj.getNamedChild(0);
        CHECK(pair.getType().compare("pair") == 0);
        CHECK(pair.getNamedChildCount() == 2); // "id" and 123
    }

    SECTION("Field names")
    {
        ts::Node pair     = obj.getNamedChild(0);
        ts::Node key_node = pair.getChildByFieldName("key");
        CHECK_FALSE(key_node.isNull());
        CHECK(key_node.getSourceText(code) == "\"id\"");
    }
}

TEST_CASE("Node Search by Position", "[node][search]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);
    std::string  code = R"({
  "key": "value"
})";
    ts::Tree     tree = parser.parseString(code);
    ts::Node     root = tree.getRootNode();

    SECTION("Search by Byte Offset")
    {
        ts::Node value_node = root.getNamedDescendantForByteRange({ 11, 16 });
        CHECK(value_node.getType().compare("string") == 0);
        CHECK(value_node.getSourceText(code) == "\"value\"");
    }

    SECTION("Search by Point")
    {
        ts::Node value_node = root.getNamedDescendantForPointRange({ { 1, 9 }, { 1, 16 } });
        CHECK(value_node.getSourceText(code) == "\"value\"");
    }
}

TEST_CASE("Node identification and language", "[node]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);
    std::string  code     = "42";
    ts::Tree     tree     = parser.parseString(code);
    ts::Node     root     = tree.getRootNode();
    ts::Node     num_node = root.getNamedChild(0);

    SECTION("ID and Language")
    {
        CHECK(num_node.getID() != 0);
        CHECK(num_node.getLanguage().operator const TSLanguage *() == lang.operator const TSLanguage *());
    }

    SECTION("Grammar types")
    {
        CHECK(num_node.getType().compare("number") == 0);
        CHECK(num_node.getGrammarType().compare("number") == 0);
        CHECK(num_node.getGrammarSymbol() == num_node.getSymbol());
    }
}

TEST_CASE("Node flags and states", "[node]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);

    SECTION("Normal node flags")
    {
        std::string code  = "[1, null]";
        ts::Tree    tree  = parser.parseString(code);
        ts::Node    root  = tree.getRootNode();
        ts::Node    array = root.getNamedChild(0);

        CHECK(array.isNamed());
        CHECK_FALSE(array.isMissing());
        CHECK_FALSE(array.isExtra());
        CHECK_FALSE(array.isError());
        CHECK_FALSE(array.hasError());
        CHECK_FALSE(array.hasChanges());
    }

    SECTION("Missing and Error nodes")
    {
        std::string code = "[1, ";
        ts::Tree    tree = parser.parseString(code);
        ts::Node    root = tree.getRootNode();

        CHECK(root.hasError());

        ts::visit(root,
                  [](ts::Node n) -> bool
                  {
                      if (n.isMissing())
                      {
                          SUCCEED("Found missing node");
                          return true;
                      }
                      return false;
                  });
    }
}

TEST_CASE("Node ranges and source", "[node]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);
    std::string  code     = "  \"hello\"";
    ts::Tree     tree     = parser.parseString(code);
    ts::Node     root     = tree.getRootNode();
    ts::Node     str_node = root.getNamedChild(0);

    SECTION("Byte and Point ranges")
    {
        auto bytes  = str_node.getByteRange();
        auto points = str_node.getPointRange();

        CHECK(bytes.start == 2);
        CHECK(bytes.end == 9);
        CHECK(points.start.column == 2);
        CHECK(points.end.column == 9);
    }

    SECTION("Source Range Validation")
    {
        std::string short_source = "  ";
        CHECK(str_node.getSourceRange(short_source).empty());
    }
}

TEST_CASE("Node navigation (extended)", "[node]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);
    std::string  code  = "[1, 2, 3]";
    ts::Tree     tree  = parser.parseString(code);
    ts::Node     array = tree.getRootNode().getNamedChild(0);

    SECTION("Siblings and children")
    {
        ts::Node first  = array.getChild(1); // It is '1' (0 is '[')
        ts::Node second = array.getChild(3); // It is '2' (2 is ',')

        CHECK(first.getNextSibling() == array.getChild(2)); // comma
        CHECK(second.getPreviousSibling() == array.getChild(2));

        CHECK(array.getFirstChild().getType().compare("[") == 0);
        CHECK(array.getLastChild().getType().compare("]") == 0);
    }

    SECTION("Named children navigation")
    {
        ts::Node first_named  = array.getNamedChild(0); // 1
        ts::Node second_named = array.getNamedChild(1); // 2

        CHECK(first_named.getNextNamedSibling() == second_named);
        CHECK(second_named.getPreviousNamedSibling() == first_named);

        CHECK(array.getFirstNamedChild().getSourceText(code) == "1");
        CHECK(array.getLastNamedChild().getSourceText(code) == "3");
    }

    SECTION("Child with descendant")
    {
        ts::Node root   = tree.getRootNode();
        ts::Node target = array.getNamedChild(2); // '3'

        ts::Node found = root.getChildWithDescendant(target);
        CHECK(found == array);
    }
}

TEST_CASE("Node Search and Fields (extended)", "[node]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);
    std::string  code = "{\"item\": 42}";
    ts::Tree     tree = parser.parseString(code);
    ts::Node     pair = tree.getRootNode().getNamedChild(0).getNamedChild(0);

    SECTION("Search for children")
    {
        ts::Node found = pair.getFirstChildForByte(9);
        CHECK(found.getType().compare("number") == 0);

        ts::Node found_named = pair.getFirstNamedChildForByte(2);
        CHECK(found_named.getType().compare("string") == 0);
    }

    SECTION("Field access")
    {
        CHECK(pair.getFieldNameForChild(0).compare("key") == 0);
        // "value" is second named child after "key"
        CHECK(pair.getFieldNameForNamedChild(1).compare("value") == 0);

        uint16_t field_id = lang.getFieldIDForName("value");
        CHECK_FALSE(pair.getChildByFieldID(field_id).isNull());
    }
}

TEST_CASE("Node Parsing State", "[node]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);
    ts::Tree     tree = parser.parseString("{\"a\": 1}");
    ts::Node     root = tree.getRootNode();

    ts::Node obj  = root.getNamedChild(0);
    ts::Node pair = obj.getNamedChild(0);

    SECTION("State access for internal nodes")
    {
        CHECK(obj.getParseState() != 0);

        ts::Node key = pair.getNamedChild(0);
        CHECK(key.getNextParseState() != 0);
    }
}

TEST_CASE("Node Mutation", "[node]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);
    std::string  code = "[1]";
    ts::Tree     tree = parser.parseString(code);
    ts::Node     node = tree.getRootNode();

    SECTION("Edit node")
    {
        // Move node one byte to right
        ts::InputEdit edit{ 0, 0, 1, { 0, 0 }, { 0, 0 }, { 0, 1 } };

        uint32_t old_start = node.getByteRange().start;
        node.edit(edit);
        CHECK(node.getByteRange().start == old_start + 1);
    }
}

TEST_CASE("Node Comparison", "[node]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);
    ts::Tree     tree  = parser.parseString("[1, 1]");
    ts::Node     root  = tree.getRootNode();
    ts::Node     array = root.getNamedChild(0);

    SECTION("Equality")
    {
        ts::Node first  = array.getNamedChild(0);
        ts::Node second = array.getNamedChild(1);

        CHECK(first == first);
        CHECK(first != second);
        CHECK_FALSE(first == second);
    }
}

TEST_CASE("Node S-Expression", "[node]")
{
    ts::Language lang  = tree_sitter_json();
    ts::Tree     tree  = ts::Parser(lang).parseString("[1]");
    auto         sexpr = tree.getRootNode().getSExpr();

    REQUIRE(sexpr != nullptr);
    CHECK(std::string(sexpr.get()).find("array") != std::string::npos);
}

TEST_CASE("Node Exceptions and Boundary Checks", "[node][exceptions]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);
    std::string  code  = "[1, 2, 3]";
    ts::Tree     tree  = parser.parseString(code);
    ts::Node     root  = tree.getRootNode();
    ts::Node     array = root.getNamedChild(0);

    SECTION("Out of bounds child access returns null node")
    {
        uint32_t total_children       = array.getChildCount();
        uint32_t total_named_children = array.getNamedChildCount();

        // Accessing exactly at the count index should return null node
        CHECK(array.getChild(total_children).isNull());
        CHECK(array.getNamedChild(total_named_children).isNull());
    }

    SECTION("Source text bounds check")
    {
        // Provide a source string that is significantly shorter than the actual node length
        std::string short_source = "[";

        // getSourceRange returns an empty string view if extents.end > source.size()
        CHECK(array.getSourceRange(short_source).empty());
    }

    SECTION("Field name size overflow throws length_error")
    {
        const char      *fake_str      = "fake";
        constexpr size_t overflow_size = static_cast<size_t>(std::numeric_limits<uint32_t>::max()) + 1;

        ts::details::StringViewParameter huge_view(fake_str, overflow_size);

        CHECK_THROWS_AS(array.getChildByFieldName(huge_view), std::length_error);
    }

    SECTION("Mutating a null node throws logic_error")
    {
        ts::Node      null_node = ts::Node::null();
        ts::InputEdit valid_edit{ 0, 0, 1, { 0, 0 }, { 0, 0 }, { 0, 1 } };

        CHECK_THROWS_AS(null_node.edit(valid_edit), std::logic_error);
    }
}

TEST_CASE("Node Search and Descendants (Unnamed Variants)", "[node][search]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);
    std::string  code = R"({"key": "value"})";
    ts::Tree     tree = parser.parseString(code);
    ts::Node     root = tree.getRootNode();

    SECTION("Descendants Count")
    {
        CHECK(root.getDescendantsCount() > 0);
    }

    SECTION("Unnamed descendant by byte and point range")
    {
        // Byte 0 is '{', which is an unnamed punctuation node
        ts::Node brace_node = root.getDescendantForByteRange({ 0, 1 });
        CHECK_FALSE(brace_node.isNamed());
        CHECK(brace_node.getType().compare("{") == 0);

        ts::Node brace_node_pt = root.getDescendantForPointRange({ { 0, 0 }, { 0, 1 } });
        CHECK(brace_node_pt == brace_node);
    }
}

TEST_CASE("Extended Null Node Fallbacks", "[node][null]")
{
    ts::Node null_node = ts::Node::null();

    SECTION("Search and Descendants on null")
    {
        CHECK(null_node.getDescendantsCount() == 0);
        CHECK(null_node.getFirstChildForByte(0).isNull());
        CHECK(null_node.getFirstNamedChildForByte(0).isNull());
        CHECK(null_node.getDescendantForByteRange({ 0, 1 }).isNull());
        CHECK(null_node.getNamedDescendantForByteRange({ 0, 1 }).isNull());
        CHECK(null_node.getDescendantForPointRange({ { 0, 0 }, { 0, 1 } }).isNull());
        CHECK(null_node.getNamedDescendantForPointRange({ { 0, 0 }, { 0, 1 } }).isNull());
    }

    SECTION("Fields on null")
    {
        CHECK(null_node.getFieldNameForChild(0).empty());
        CHECK(null_node.getFieldNameForNamedChild(0).empty());
        CHECK(null_node.getChildByFieldName("test").isNull());
        CHECK(null_node.getChildByFieldID(1).isNull());
    }

    SECTION("Parse State on null")
    {
        CHECK(null_node.getParseState() == 0);
        CHECK(null_node.getNextParseState() == 0);
    }

    SECTION("S-Expression on null")
    {
        CHECK(null_node.getSExpr() == nullptr);
    }
}

TEST_CASE("Node Cursor Instantiation", "[node][cursor]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);
    ts::Tree     tree = parser.parseString("[1, 2]");
    ts::Node     root = tree.getRootNode();

    SECTION("Valid node returns valid cursor")
    {
        ts::TreeCursor cursor = root.getCursor();
        CHECK(cursor.isValid());
        CHECK(cursor.getCurrentNode() == root);
    }

    SECTION("Null node returns invalid cursor")
    {
        ts::Node       null_node = ts::Node::null();
        ts::TreeCursor cursor    = null_node.getCursor();
        CHECK_FALSE(cursor.isValid());
    }
}

TEST_CASE("Node Get Child By Type", "[node][search]")
{
    SECTION("Returns null() for an empty/null node")
    {
        ts::Node null_node = ts::Node::null();
        CHECK(null_node.getChildByType("pair").isNull());
    }

    SECTION("Throws std::length_error when input name exceeds 4GB limit")
    {
        ts::Node                         node          = ts::Node::null();
        const char                      *fake_str      = "fake";
        constexpr size_t                 overflow_size = static_cast<size_t>(std::numeric_limits<uint32_t>::max()) + 1;
        ts::details::StringViewParameter huge_view(fake_str, overflow_size);

        CHECK_THROWS_AS(node.getChildByType(huge_view), std::length_error);
    }

    SECTION("Tree-based lookup using JSON grammar")
    {
        ts::Language lang = tree_sitter_json();
        ts::Parser   parser(lang);
        std::string  code = R"({"key": 123})";
        ts::Tree     tree = parser.parseString(code);
        ts::Node     root = tree.getRootNode();

        // The root is 'document', its first named child is 'object'
        ts::Node object_node = root.getNamedChild(0);
        REQUIRE_FALSE(object_node.isNull());
        REQUIRE(object_node.getType().compare("object") == 0);

        SECTION("Successfully finds a Named Node (AST element)")
        {
            // The 'object' node contains a named 'pair' child
            ts::Node pair_node = object_node.getChildByType("pair");
            CHECK_FALSE(pair_node.isNull());
            CHECK(pair_node.getType().compare("pair") == 0);
            CHECK(pair_node.isNamed() == true);
        }

        SECTION("Successfully finds an Anonymous Node / Punctuation (CST element)")
        {
            // The '{' character is an anonymous node representing punctuation in the JSON grammar
            ts::Node brace_node = object_node.getChildByType("{");
            CHECK_FALSE(brace_node.isNull());
            CHECK(brace_node.getType().compare("{") == 0);
            CHECK(brace_node.isNamed() == false);
        }

        SECTION("Returns null() for a valid grammar type that doesn't exist as a child")
        {
            // 'array' is a valid JSON grammar type, but not present inside this 'object'
            ts::Node missing_node = object_node.getChildByType("array");
            CHECK(missing_node.isNull());
        }

        SECTION("Returns null() for a completely fictitious type string")
        {
            ts::Node missing_node = object_node.getChildByType("non_existent_type_123");
            CHECK(missing_node.isNull());
        }
    }
}

TEST_CASE("Node Get Child By Symbol", "[node][search]")
{
    SECTION("Returns null() for an empty/null node")
    {
        ts::Node null_node = ts::Node::null();
        // Przekazanie dowolnego symbolu (np. 1) do pustego wêz³a
        CHECK(null_node.getChildBySymbol(1).isNull());
    }

    SECTION("Tree-based lookup using JSON grammar")
    {
        ts::Language lang = tree_sitter_json();
        ts::Parser   parser(lang);
        std::string  code = R"({"key": 123})";
        ts::Tree     tree = parser.parseString(code);
        ts::Node     root = tree.getRootNode();

        // The root is 'document', its first named child is 'object'
        ts::Node object_node = root.getNamedChild(0);
        REQUIRE_FALSE(object_node.isNull());

        SECTION("Successfully finds a Named Node (AST element)")
        {
            ts::Symbol pair_symbol = lang.getSymbolForName("pair", true);
            REQUIRE(pair_symbol != 0);

            ts::Node pair_node = object_node.getChildBySymbol(pair_symbol);
            CHECK_FALSE(pair_node.isNull());
            CHECK(pair_node.getType().compare("pair") == 0);
            CHECK(pair_node.isNamed() == true);
        }

        SECTION("Successfully finds an Anonymous Node / Punctuation (CST element)")
        {
            ts::Symbol brace_symbol = lang.getSymbolForName("{", false);
            REQUIRE(brace_symbol != 0);

            ts::Node brace_node = object_node.getChildBySymbol(brace_symbol);
            CHECK_FALSE(brace_node.isNull());
            CHECK(brace_node.getType().compare("{") == 0);
            CHECK(brace_node.isNamed() == false);
        }

        SECTION("Returns null() for a valid grammar symbol that doesn't exist as a child")
        {
            ts::Symbol array_symbol = lang.getSymbolForName("array", true);
            REQUIRE(array_symbol != 0);

            ts::Node missing_node = object_node.getChildBySymbol(array_symbol);
            CHECK(missing_node.isNull());
        }

        SECTION("Returns null() when passing symbol 0")
        {
            ts::Node missing_node = object_node.getChildBySymbol(0);
            CHECK(missing_node.isNull());
        }
    }
}

TEST_CASE("Node Get Named Child By Type", "[node][search]")
{
    SECTION("Returns null() for an empty/null node")
    {
        ts::Node null_node = ts::Node::null();
        CHECK(null_node.getNamedChildByType("pair").isNull());
    }

    SECTION("Throws std::length_error when input name exceeds 4GB limit")
    {
        ts::Node                         node          = ts::Node::null();
        const char                      *fake_str      = "fake";
        constexpr size_t                 overflow_size = static_cast<size_t>(std::numeric_limits<uint32_t>::max()) + 1;
        ts::details::StringViewParameter huge_view(fake_str, overflow_size);

        CHECK_THROWS_AS(node.getNamedChildByType(huge_view), std::length_error);
    }

    SECTION("Tree-based strict AST lookup using JSON grammar")
    {
        ts::Language lang = tree_sitter_json();
        ts::Parser   parser(lang);
        std::string  code = R"({"key": 123})";
        ts::Tree     tree = parser.parseString(code);
        ts::Node     root = tree.getRootNode();

        ts::Node object_node = root.getNamedChild(0);

        SECTION("Successfully finds a Named Node")
        {
            ts::Node pair_node = object_node.getNamedChildByType("pair");
            CHECK_FALSE(pair_node.isNull());
            CHECK(pair_node.getType().compare("pair") == 0);
            CHECK(pair_node.isNamed() == true);
        }

        SECTION("Returns null() and ignores Anonymous Nodes (punctuation)")
        {
            // Even though '{' is a valid child in the Concrete Syntax Tree (CST),
            // getNamedChildByType strictly iterates over AST elements. It must ignore it and return null.
            ts::Node brace_node = object_node.getNamedChildByType("{");
            CHECK(brace_node.isNull());
        }
    }
}

TEST_CASE("Node Get Named Child By Symbol", "[node][search]")
{
    SECTION("Returns null() for an empty/null node")
    {
        ts::Node null_node = ts::Node::null();
        CHECK(null_node.getNamedChildBySymbol(1).isNull());
    }

    SECTION("Tree-based strict AST lookup using JSON grammar")
    {
        ts::Language lang = tree_sitter_json();
        ts::Parser   parser(lang);
        std::string  code = R"({"key": 123})";
        ts::Tree     tree = parser.parseString(code);
        ts::Node     root = tree.getRootNode();

        ts::Node object_node = root.getNamedChild(0);
        REQUIRE_FALSE(object_node.isNull());

        SECTION("Successfully finds a Named Node")
        {
            ts::Symbol pair_symbol = lang.getSymbolForName("pair", true);
            REQUIRE(pair_symbol != 0);

            ts::Node pair_node = object_node.getNamedChildBySymbol(pair_symbol);
            CHECK_FALSE(pair_node.isNull());
            CHECK(pair_node.getType().compare("pair") == 0);
            CHECK(pair_node.isNamed() == true);
        }

        SECTION("Returns null() and ignores Anonymous Nodes (punctuation)")
        {
            ts::Symbol brace_symbol = lang.getSymbolForName("{", false);
            REQUIRE(brace_symbol != 0);

            // getNamedChildBySymbol iterates only over named children,
            // so passing a symbol for an anonymous node should return null.
            ts::Node brace_node = object_node.getNamedChildBySymbol(brace_symbol);
            CHECK(brace_node.isNull());
        }
    }
}
