#include "pch.hpp"

// cpp-tree-sitter
#include <cpp-tree-sitter.hpp>

// Catch2
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

// STL
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" const TSLanguage *tree_sitter_json();

TEST_CASE("Tree Incremental Parsing (Editing)", "[parser][tree]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);

    std::string code = "[1, 2, 3]";
    ts::Tree    tree = parser.parseString(code);
    ts::Node    root = tree.getRootNode();

    SECTION("Edit and Re-parse")
    {
        std::string new_code = "[1, 20, 3]";

        ts::InputEdit edit{
            4,        // start_byte
            5,        // old_end_byte
            6,        // new_end_byte
            { 0, 4 }, // start_point
            { 0, 5 }, // old_end_point
            { 0, 6 }  // new_end_point
        };

        tree.edit(edit);
#if TS_TEST_HAS_CXX17
        ts::Tree new_tree = parser.parseString(new_code, tree);
#else
        ts::Tree new_tree = parser.parseString(new_code, &tree);
#endif

        CHECK_FALSE(new_tree.hasError());
        ts::Node new_root   = new_tree.getRootNode();
        ts::Node array_node = new_root.getNamedChild(0);
        ts::Node second_num = array_node.getNamedChild(1);

        CHECK(second_num.getType().compare("number") == 0);
        CHECK(second_num.getSourceText(new_code) == "20");
    }
}

TEST_CASE("Tree Lifecycle and Operators", "[tree]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);
    std::string  code = "[1, 2]";
    ts::Tree     tree = parser.parseString(code);

    SECTION("Copy constructor and copy method")
    {
        ts::Tree tree_copy(tree);
        CHECK(tree_copy.getRootNode().getType().compare("document") == 0);

        ts::Tree tree_copy2 = tree.copy();
        CHECK(tree_copy2.getRootNode().getType().compare("document") == 0);
    }

    SECTION("Assignment operators")
    {
        ts::Tree tree2 = parser.parseString("null");
        tree2          = tree; // Copy assignment
        CHECK(tree2.getRootNode().getSourceText(code).find("[1, 2]") != std::string::npos);

        ts::Tree tree3 = std::move(tree2); // Move assignment
        CHECK_FALSE(tree3.getRootNode().isNull());
    }

    SECTION("C-API Conversion")
    {
        const TSTree *raw = tree; // TSTree* operator
        REQUIRE(raw != nullptr);
        CHECK(ts_tree_language(raw) == lang.operator const TSLanguage *());
    }
}

TEST_CASE("Tree Root and Offsets", "[tree]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);
    std::string  code = "  [1]";
    ts::Tree     tree = parser.parseString(code);

    SECTION("Standard root node")
    {
        ts::Node root = tree.getRootNode();
        CHECK(root.getByteRange().start == 2); // Because of spaces
        CHECK(root.getType().compare("document") == 0);
    }

    SECTION("Root node with offset")
    {
        ts::Node offset_root = tree.getRootNodeWithOffset(2, { 0, 2 });

        ts::Node array = offset_root.getNamedChild(0);
        CHECK(array.getType().compare("array") == 0);
    }
}

TEST_CASE("Tree Ranges and Changes", "[tree]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);

    SECTION("Included Ranges")
    {
        std::string code = "[1, 2]";
        ts::Tree    tree = parser.parseString(code);

        auto ranges = tree.getIncludedRanges();
        REQUIRE_FALSE(ranges.empty());

        CHECK(ranges[0].byte.start == 0);
        CHECK(ranges[0].byte.end == std::numeric_limits<uint32_t>::max());
    }

    SECTION("Changed Ranges after edit")
    {
        std::string code1 = "[1, 2]";
        std::string code2 = "[1, 20]";

        ts::Tree tree1 = parser.parseString(code1);
        ts::Tree tree2 = parser.parseString(code1);

        // Edit '2' to '20' (4 bytes)
        ts::InputEdit edit{
            4,        5,        6,       // bytes: start, old_end, new_end
            { 0, 4 }, { 0, 5 }, { 0, 6 } // points
        };

        tree2.edit(edit);
#if TS_TEST_HAS_CXX17
        ts::Tree tree3 = parser.parseString(code2, tree2);
#else
        ts::Tree tree3 = parser.parseString(code2, &tree2);
#endif

        auto changed = ts::Tree::getChangedRanges(tree1, tree3);

        REQUIRE_FALSE(changed.empty());

        CHECK(changed[0].byte.start <= 5);
        CHECK(changed[0].byte.end >= 7);
    }
}

TEST_CASE("Tree Error and Language", "[tree]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);

    SECTION("hasError flag")
    {
        ts::Tree valid_tree = parser.parseString("[1]");
        CHECK_FALSE(valid_tree.hasError());

        ts::Tree invalid_tree = parser.parseString("[1, ]");
        CHECK(invalid_tree.hasError());
    }

    SECTION("getLanguage")
    {
        ts::Tree     tree      = parser.parseString("{}");
        ts::Language tree_lang = tree.getLanguage();
        CHECK(tree_lang.operator const TSLanguage *() == lang.operator const TSLanguage *());
    }
}

TEST_CASE("Tree Debugging", "[tree]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);
    ts::Tree     tree = parser.parseString("true");

    SECTION("printDotGraph to file")
    {
        const char *temp_file = "tree_graph.dot";
#if defined(_WIN32)
        FILE *f = nullptr;
        fopen_s(&f, temp_file, "w");
#else
        FILE *f = fopen(temp_file, "w");
#endif
        REQUIRE(f != nullptr);

        tree.printDotGraph(f);
        fclose(f);

        // Check if file is not empty
#if defined(_WIN32)
        FILE *f_check = nullptr;
        fopen_s(&f_check, temp_file, "r");
#else
        FILE *f_check = fopen(temp_file, "r");
#endif
        REQUIRE(f_check != nullptr);

        fseek(f_check, 0, SEEK_END);
        long size = ftell(f_check);
        fclose(f_check);

        CHECK(size > 0);
        std::remove(temp_file);
    }
}

TEST_CASE("Tree printDotGraph by path", "[tree][debug]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);
    std::string  code     = "{\"test\": 123}";
    ts::Tree     tree     = parser.parseString(code);
    std::string  filename = "tree_path_graph.dot";

    SECTION("Generate DOT graph via filename")
    {
        CHECK_NOTHROW(tree.printDotGraph(filename));

        std::ifstream f(filename);
        REQUIRE(f.is_open());

        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

        CHECK(content.find("digraph") != std::string::npos);
        CHECK(content.find("object") != std::string::npos);
        CHECK(content.find("pair") != std::string::npos);

        f.close();

        CHECK(std::remove(filename.c_str()) == 0);
    }
}

TEST_CASE("Tree Invalid State Handling", "[tree][exceptions]")
{
    // Create an invalid tree by passing nullptr
    ts::Tree invalid_tree(nullptr);

    SECTION("Validation flags and basic getters")
    {
        CHECK_FALSE(invalid_tree.isValid());
        CHECK_FALSE(invalid_tree.hasError());
        CHECK(invalid_tree.getLanguage().operator const TSLanguage *() == nullptr);
        CHECK(invalid_tree.operator TSTree *() == nullptr);
    }

    SECTION("Node access on invalid tree returns null nodes")
    {
        CHECK(invalid_tree.getRootNode().isNull());
        CHECK(invalid_tree.getRootNodeWithOffset(0, { 0, 0 }).isNull());
    }

    SECTION("Editing an invalid tree throws logic_error")
    {
        ts::InputEdit dummy_edit{};
        // The edit method has an explicit guard against modifying an invalid tree
        CHECK_THROWS_AS(invalid_tree.edit(dummy_edit), std::logic_error);
    }

    SECTION("Ranges on invalid tree return empty vectors")
    {
        CHECK(invalid_tree.getIncludedRanges().empty());

        ts::Language lang = tree_sitter_json();
        ts::Parser   parser(lang);
        ts::Tree     valid_tree = parser.parseString("{}");

        // If either tree is invalid, getChangedRanges should safely return an empty vector
        CHECK(ts::Tree::getChangedRanges(invalid_tree, valid_tree).empty());
        CHECK(ts::Tree::getChangedRanges(valid_tree, invalid_tree).empty());
        CHECK(ts::Tree::getChangedRanges(invalid_tree, invalid_tree).empty());
    }

    SECTION("Debugging methods on invalid tree do not crash")
    {
        // These should hit the early return guards and do nothing without throwing
        CHECK_NOTHROW(invalid_tree.printDotGraph(-1));
        CHECK_NOTHROW(invalid_tree.printDotGraph("dummy_output.dot"));

        FILE *dummy_file = nullptr;
        CHECK_NOTHROW(invalid_tree.printDotGraph(dummy_file));
    }
}

TEST_CASE("Tree Lifecycle Edge Cases", "[tree][lifecycle]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);
    std::string  code = "[1, 2, 3]";
    ts::Tree     tree = parser.parseString(code);

    SECTION("Self Assignment")
    {
        // Suppress compiler warnings about self-assignment by using a pointer
        ts::Tree *ptr_tree = &tree;
        tree               = *ptr_tree;

        CHECK(tree.isValid());
        CHECK(tree.getRootNode().getType().compare("document") == 0);
    }

    SECTION("Copying an invalid tree results in an invalid tree")
    {
        ts::Tree invalid_tree(nullptr);
        ts::Tree invalid_copy = invalid_tree.copy();

        CHECK_FALSE(invalid_copy.isValid());
    }
}

TEST_CASE("Tree Unchanged Ranges", "[tree]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);
    std::string  code = "{\"key\": \"value\"}";

    SECTION("Changed ranges are empty if no edits occurred")
    {
        ts::Tree tree1 = parser.parseString(code);
        ts::Tree tree2 = parser.parseString(code);

        // Even though they are different tree instances, no structural edits
        // were recorded, so changed ranges should be empty.
        auto changed = ts::Tree::getChangedRanges(tree1, tree2);
        CHECK(changed.empty());
    }
}
