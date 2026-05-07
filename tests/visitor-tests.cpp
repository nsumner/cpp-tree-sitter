#include "pch.hpp"

// cpp-tree-sitter
#include <cpp-tree-sitter.hpp>

// Catch2
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

// STL
#include <string>

extern "C" const TSLanguage *tree_sitter_json();

TEST_CASE("Visitor Simply Usage", "[visitor]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);
    std::string  code = "[1, 2]";
    ts::Tree     tree = parser.parseString(code);

    SECTION("Visit all nodes")
    {
        int total_nodes = 0;
        ts::visit(tree.getRootNode(),
                  [&](ts::Node n [[maybe_unused]]) -> bool
                  {
                      ++total_nodes;
                      return false;
                  });

        // document, array, [, 1, ,, 2, ] = 7 nodes
        CHECK(total_nodes == 7);
    }
}

TEST_CASE("Visitor Edge Cases and Subtrees", "[visitor]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);
    std::string  code = "[1, [2, 3]]";
    ts::Tree     tree = parser.parseString(code);
    ts::Node     root = tree.getRootNode();

    SECTION("Null node does not trigger callback")
    {
        int calls = 0;

        // Passing a null node should hit the early return guard
        ts::visit(ts::Node::null(),
                  [&](ts::Node) -> bool
                  {
                      ++calls;
                      return false;
                  });

        CHECK(calls == 0);
    }

    SECTION("Visit restricts traversal to the specific subtree")
    {
        // Navigate to the inner array: [2, 3]
        ts::Node outer_array = root.getNamedChild(0);
        ts::Node inner_array = outer_array.getNamedChild(1);

        int  inner_nodes_count = 0;
        bool escaped_subtree   = false;

        ts::visit(inner_array,
                  [&](ts::Node n) -> bool
                  {
                      ++inner_nodes_count;

                      // If the visitor accidentally traverses up past the inner_array,
                      // it would hit the root, outer_array, or the number '1'.
                      if (n == root || n == outer_array || n.getSourceText(code) == "1")
                      {
                          escaped_subtree = true;
                          return true;
                      }
                      return false;
                  });

        // The expected nodes inside `[2, 3]` are: array, [, 2, ,, 3, ] -> 6 nodes total
        CHECK(inner_nodes_count == 6);
        CHECK_FALSE(escaped_subtree);
    }

    SECTION("Visit a single leaf node")
    {
        // Navigate to the leaf number node: '1'
        ts::Node number_node = root.getNamedChild(0).getNamedChild(0);

        int calls = 0;
        ts::visit(number_node,
                  [&](ts::Node n) -> bool
                  {
                      ++calls;
                      CHECK(n.getType().compare("number") == 0);
                      return false;
                  });

        // A terminal node should be visited exactly once
        CHECK(calls == 1);
    }
}
