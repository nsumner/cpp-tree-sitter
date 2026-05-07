#include "pch.hpp"

// cpp-tree-sitter
#include <cpp-tree-sitter.hpp>

// Catch2
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

// STL
#include <cstdint>
#include <string>
#include <utility>

extern "C" const TSLanguage *tree_sitter_json();

TEST_CASE("TreeCursor navigation", "[cursor]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);
    std::string  code = "[1, 2]";
    ts::Tree     tree = parser.parseString(code);

    ts::TreeCursor cursor = tree.getRootNode().getCursor();

    SECTION("Walking the tree")
    {
        CHECK(cursor.getCurrentNode().getType().compare("document") == 0);

        REQUIRE(cursor.gotoFirstChild());
        CHECK(cursor.getCurrentNode().getType().compare("array") == 0);

        REQUIRE(cursor.gotoFirstChild()); // '['
        CHECK(cursor.getCurrentNode().getType().compare("[") == 0);

        REQUIRE(cursor.gotoNextSibling()); // '1'
        CHECK(cursor.getCurrentNode().getType().compare("number") == 0);

        REQUIRE(cursor.gotoParent());
        CHECK(cursor.getCurrentNode().getType().compare("array") == 0);
    }
}

TEST_CASE("TreeCursor Advanced Movements", "[cursor]")
{
    ts::Language   lang = tree_sitter_json();
    ts::Parser     parser(lang);
    std::string    code   = "[1, [2, 3], 4]";
    ts::Tree       tree   = parser.parseString(code);
    ts::TreeCursor cursor = tree.getRootNode().getCursor();

    SECTION("Deep Navigation")
    {
        REQUIRE(cursor.gotoFirstChild());  // array
        REQUIRE(cursor.gotoFirstChild());  // [
        REQUIRE(cursor.gotoNextSibling()); // 1
        REQUIRE(cursor.gotoNextSibling()); // ,
        REQUIRE(cursor.gotoNextSibling()); // inner array

        CHECK(cursor.getCurrentNode().getType().compare("array") == 0);

        REQUIRE(cursor.gotoFirstChild());  // inner [
        REQUIRE(cursor.gotoNextSibling()); // 2

        CHECK(cursor.getCurrentNode().getSourceText(code) == "2");
        CHECK(cursor.getDepthFromOrigin() == 3); // root -> doc -> array -> array -> 2
    }
}

TEST_CASE("TreeCursor Invalid State Handling", "[cursor][exceptions]")
{
    ts::Node       null_node = ts::Node::null();
    ts::TreeCursor cursor    = null_node.getCursor();

    SECTION("Validation flags")
    {
        CHECK_FALSE(cursor.isValid());
    }

    SECTION("Navigation methods safely return false or -1")
    {
        CHECK_FALSE(cursor.gotoParent());
        CHECK_FALSE(cursor.gotoFirstChild());
        CHECK_FALSE(cursor.gotoLastChild());
        CHECK_FALSE(cursor.gotoNextSibling());
        CHECK_FALSE(cursor.gotoPreviousSibling());

        CHECK(cursor.gotoFirstChildForByte(0) == -1);
        CHECK(cursor.gotoFirstChildForPoint({ 0, 0 }) == -1);
    }

    SECTION("Attribute methods return safe defaults")
    {
        CHECK(cursor.getCurrentNode().isNull());
        CHECK(cursor.getCurrentFieldName().empty());
        CHECK(cursor.getCurrentFieldID() == 0);
        CHECK(cursor.getCurrentDescendantIndex() == 0);
        CHECK(cursor.getDepthFromOrigin() == 0);
    }

    SECTION("Reset operations on invalid cursor do not crash")
    {
        // Should return early and do nothing
        ts::TreeCursor other_cursor = null_node.getCursor();
        CHECK_NOTHROW(cursor.reset(null_node));
        CHECK_NOTHROW(cursor.reset(other_cursor));
    }
}

TEST_CASE("TreeCursor Reset and Copy", "[cursor][lifecycle]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);
    std::string  code = "[1, 2]";
    ts::Tree     tree = parser.parseString(code);
    ts::Node     root = tree.getRootNode();

    ts::TreeCursor cursor = root.getCursor();

    SECTION("Copying a cursor replicates its state")
    {
        REQUIRE(cursor.gotoFirstChild()); // Move to 'array' node

        ts::TreeCursor cursor_copy = cursor.copy();
        CHECK(cursor_copy.isValid());
        CHECK(cursor_copy.getCurrentNode() == cursor.getCurrentNode());
    }

    SECTION("Resetting cursor to node")
    {
        REQUIRE(cursor.gotoFirstChild()); // Move to 'array' node
        CHECK(cursor.getCurrentNode() != root);

        cursor.reset(root); // Reset back to root
        CHECK(cursor.getCurrentNode() == root);
    }

    SECTION("Resetting cursor to another cursor")
    {
        ts::TreeCursor root_cursor = root.getCursor();
        REQUIRE(cursor.gotoFirstChild()); // Move to 'array' node

        cursor.reset(root_cursor); // Reset state to match root_cursor
        CHECK(cursor.getCurrentNode() == root_cursor.getCurrentNode());
    }
}

TEST_CASE("TreeCursor Extended Navigation and Fields", "[cursor][navigation]")
{
    ts::Language   lang = tree_sitter_json();
    ts::Parser     parser(lang);
    std::string    code   = R"({"key": 42})";
    ts::Tree       tree   = parser.parseString(code);
    ts::Node       root   = tree.getRootNode();
    ts::TreeCursor cursor = root.getCursor();

    SECTION("Navigate backwards (Last Child, Previous Sibling)")
    {
        REQUIRE(cursor.gotoFirstChild()); // object
        REQUIRE(cursor.gotoLastChild());  // '}'

        CHECK(cursor.getCurrentNode().getType().compare("}") == 0);

        REQUIRE(cursor.gotoPreviousSibling()); // pair
        CHECK(cursor.getCurrentNode().getType().compare("pair") == 0);
    }

    SECTION("Descendant indexing and Field Names")
    {
        REQUIRE(cursor.gotoFirstChild());  // object
        REQUIRE(cursor.gotoFirstChild());  // '{'
        REQUIRE(cursor.gotoNextSibling()); // pair

        // Move into the pair
        REQUIRE(cursor.gotoFirstChild()); // string ("key")

        // Check field attributes
        CHECK(cursor.getCurrentFieldName().compare("key") == 0);
        CHECK(cursor.getCurrentFieldID() > 0);

        // Get descendant index relative to the parent
        uint32_t index = cursor.getCurrentDescendantIndex();

        // Verify we can jump directly back to this descendant
        REQUIRE(cursor.gotoParent()); // back to pair
        CHECK_NOTHROW(cursor.gotoDescendant(index));
        CHECK(cursor.getCurrentFieldName().compare("key") == 0);
    }

    SECTION("Navigate by Byte and Point")
    {
        REQUIRE(cursor.gotoFirstChild());  // object
        REQUIRE(cursor.gotoFirstChild());  // '{'
        REQUIRE(cursor.gotoNextSibling()); // pair

        // 9 is the byte offset of the number '42'
        int64_t index_byte = cursor.gotoFirstChildForByte(9);
        CHECK(index_byte >= 0);
        CHECK(cursor.getCurrentNode().getType().compare("number") == 0);

        cursor.reset(root);
        REQUIRE(cursor.gotoFirstChild());  // object
        REQUIRE(cursor.gotoFirstChild());  // '{'
        REQUIRE(cursor.gotoNextSibling()); // pair

        // Point navigation (row 0, column 9)
        int64_t index_point = cursor.gotoFirstChildForPoint({ 0, 9 });
        CHECK(index_point >= 0);
        CHECK(cursor.getCurrentNode().getType().compare("number") == 0);
    }
}

TEST_CASE("TreeCursor Move Semantics", "[cursor][lifecycle]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);
    std::string  code = "{}";
    ts::Tree     tree = parser.parseString(code);

    ts::TreeCursor cursor1 = tree.getRootNode().getCursor();

    SECTION("Move Assignment Operator")
    {
        ts::TreeCursor cursor2 = ts::Node::null().getCursor();
        CHECK_FALSE(cursor2.isValid());

        cursor2 = std::move(cursor1); // Move ownership

        CHECK(cursor2.isValid());
        CHECK(cursor2.getCurrentNode().getType().compare("document") == 0);
    }

    SECTION("Move Constructor")
    {
        ts::TreeCursor cursor2(std::move(cursor1));

        CHECK(cursor2.isValid());
        CHECK(cursor2.getCurrentNode().getType().compare("document") == 0);
    }
}
