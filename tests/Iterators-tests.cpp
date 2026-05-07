#include "pch.hpp"

// cpp-tree-sitter
#include <cpp-tree-sitter.hpp>

// Catch2
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

// STL
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" const TSLanguage *tree_sitter_json();

TEST_CASE("Lookahead Iterator", "[language][lookahead]")
{
    ts::Language lang = tree_sitter_json();
    ts::Tree     tree = ts::Parser(lang).parseString("{\"a\": 1}");
    ts::Node     root = tree.getRootNode();

    ts::StateID state = root.getNamedChild(0).getNamedChild(0).getNextParseState();

    ts::LookaheadIterator it(lang, state);
    bool                  found_value_symbols = false;

    while (it.next())
    {
        if (it.getCurrentSymbolName().size() > 0)
        {
            found_value_symbols = true;
        }
    }
    CHECK(found_value_symbols);
}

TEST_CASE("STL-like Iterators", "[iterators]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);
    std::string  code       = "[10, 20, 30]";
    ts::Tree     tree       = parser.parseString(code);
    ts::Node     array_node = tree.getRootNode().getNamedChild(0);

    SECTION("Iterating over named children")
    {
        std::vector<std::string> values;
        ts::Children             children{ array_node };

        for (ts::Node child : children)
        {
            if (child.isNamed())
            {
                values.push_back(child.getSourceText(code));
            }
        }

        REQUIRE(values.size() == 3);
        CHECK(values[0] == "10");
        CHECK(values[2] == "30");
    }
}

TEST_CASE("ChildIterator Edge Cases and Operators", "[iterators]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);
    std::string  code = "123";
    ts::Tree     tree = parser.parseString(code);

    // The root node (document) has one child (number), and the number has 0 children
    ts::Node document = tree.getRootNode();
    ts::Node number   = document.getNamedChild(0);

    SECTION("Iteration over a node with no children")
    {
        ts::Children children{ number };
        auto         it  = children.begin();
        auto         end = children.end();

        // Should immediately be equal to the sentinel
        CHECK(it == end);
        CHECK(end == it);
        CHECK_FALSE(it != end);
        CHECK_FALSE(end != it);
    }

    SECTION("Pre-increment vs Post-increment")
    {
        std::string arr_code   = "[1, 2]";
        ts::Tree    arr_tree   = parser.parseString(arr_code);
        ts::Node    array_node = arr_tree.getRootNode().getNamedChild(0);

        ts::Children children{ array_node };
        auto         it = children.begin();

        // Check pre-increment
        CHECK(it->getType().compare("[") == 0);
        ++it;
        CHECK(it->getType().compare("number") == 0);

        // Check post-increment
        it++;
        CHECK(it->getType().compare(",") == 0);
    }
}

TEST_CASE("LookaheadIterator Exceptions and State Control", "[iterators][lookahead]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);
    ts::Tree     tree        = parser.parseString("{}");
    ts::Node     root        = tree.getRootNode();
    ts::StateID  valid_state = root.getParseState();

    SECTION("Constructor throws on invalid language")
    {
        ts::Language invalid_lang(nullptr);
        CHECK_THROWS_AS(ts::LookaheadIterator(invalid_lang, 0), std::runtime_error);
    }

    SECTION("Constructor throws on invalid state")
    {
        // Provide an absurdly high state ID that doesn't exist in the grammar
        constexpr ts::StateID invalid_state = std::numeric_limits<uint16_t>::max();
        CHECK_THROWS_AS(ts::LookaheadIterator(lang, invalid_state), std::runtime_error);
    }

    SECTION("Reset state and language")
    {
        ts::LookaheadIterator it(lang, valid_state);

        // Resetting to the same or another valid state should return true
        CHECK(it.resetState(valid_state) == true);

        // Resetting language and state entirely
        CHECK(it.reset(lang, valid_state) == true);
    }

    SECTION("Property accessors")
    {
        ts::LookaheadIterator it(lang, valid_state);

        // Verify the language getter returns the correct underlying pointer
        CHECK(it.getLanguage().operator const TSLanguage *() == lang.operator const TSLanguage *());
        bool ok = it.next();

        if (ok && it.next())
        {
            // Verify that the underlying C API pointer is accessible
            CHECK(it.operator const TSLookaheadIterator *() != nullptr);

            // Verify symbol integer ID is accessible and valid
            ts::Symbol sym = it.getCurrentSymbol();
            CHECK(sym > 0);
        }
    }

    SECTION("Move semantics")
    {
        ts::LookaheadIterator it1(lang, valid_state);
        ts::LookaheadIterator it2 = std::move(it1);

        // it2 should now be valid and able to iterate
        CHECK(it2.operator const TSLookaheadIterator *() != nullptr);
    }
}
