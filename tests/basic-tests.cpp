#include "pch.hpp"

// cpp-tree-sitter
#include <cpp-tree-sitter.hpp>

// Catch2
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

// STL
#include <cstdint>
#include <string>
#include <vector>

extern "C" const TSLanguage *tree_sitter_json();

TEST_CASE("Basic data structures work correctly", "[types]")
{
    SECTION("Point comparisons")
    {
        ts::Point p1(1, 5);
        ts::Point p2(1, 10);
        ts::Point p3(2, 0);

        CHECK(p1 < p2);
        CHECK(p2 < p3);
        CHECK(p1 != p2);
        CHECK(p1 == ts::Point(1, 5));
    }

    SECTION("Extent (Range) representation")
    {
        ts::Extent<uint32_t> range{ 10, 20 };
        CHECK(range.start == 10);
        CHECK(range.end == 20);
    }
}

TEST_CASE("Basic End-to-End Parsing", "[integration][parser][tree][node]")
{
    // 1. Initialize language and parser
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);

    // 2. Parse a simple JSON string
    std::string code = "[1, 2, 3]";
    ts::Tree    tree = parser.parseString(code);

    SECTION("Tree and Root Node validation")
    {
        CHECK_FALSE(tree.hasError());

        ts::Node root = tree.getRootNode();
        REQUIRE_FALSE(root.isNull());
        CHECK(root.getType().compare("document") == 0);
    }

    SECTION("Basic node traversal and text extraction")
    {
        ts::Node root = tree.getRootNode();

        // The first named child of the document should be the array
        ts::Node array_node = root.getNamedChild(0);
        REQUIRE(array_node.getType().compare("array") == 0);

        // The array should have 3 named children (the numbers 1, 2, and 3)
        REQUIRE(array_node.getNamedChildCount() == 3);

        ts::Node first_number = array_node.getNamedChild(0);
        CHECK(first_number.getType().compare("number") == 0);
        CHECK(first_number.getSourceText(code) == "1");

        ts::Node last_number = array_node.getLastNamedChild();
        CHECK(last_number.getSourceText(code) == "3");
    }
}

TEST_CASE("Basic Object and Field Navigation", "[integration][node]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);

    std::string code = R"({"name": "Mark"})";
    ts::Tree    tree = parser.parseString(code);
    ts::Node    root = tree.getRootNode();

    SECTION("Extracting data by field names")
    {
        // document -> object -> pair
        ts::Node object_node = root.getNamedChild(0);
        ts::Node pair_node   = object_node.getNamedChild(0);

        REQUIRE(pair_node.getType().compare("pair") == 0);

        // Fetch children using grammar-defined field names
        ts::Node key_node   = pair_node.getChildByFieldName("key");
        ts::Node value_node = pair_node.getChildByFieldName("value");

        REQUIRE_FALSE(key_node.isNull());
        REQUIRE_FALSE(value_node.isNull());

        CHECK(key_node.getSourceText(code) == "\"name\"");
        CHECK(value_node.getSourceText(code) == "\"Mark\"");
    }
}

TEST_CASE("Basic Query Execution", "[integration][query]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);

    std::string code = R"([{"id": 1}, {"id": 2}, {"id": 3}])";
    ts::Tree    tree = parser.parseString(code);

    SECTION("Find all numbers using a Query")
    {
        // Create a query that looks for number nodes and captures them as @num
        std::string query_source = "(number) @num";
        ts::Query   query(lang, query_source);

        ts::QueryCursor cursor;
        cursor.exec(query, tree.getRootNode());

        ts::QueryMatch           match;
        std::vector<std::string> captured_numbers;

        // Iterate through all matches
        while (cursor.nextMatch(match))
        {
            for (const auto &capture : match.captures)
            {
                captured_numbers.push_back(capture.node.getSourceText(code));
            }
        }

        // We expect to find 1, 2, and 3
        REQUIRE(captured_numbers.size() == 3);
        CHECK(captured_numbers[0] == "1");
        CHECK(captured_numbers[1] == "2");
        CHECK(captured_numbers[2] == "3");
    }
}
