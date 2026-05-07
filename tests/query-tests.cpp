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

#if TS_TEST_HAS_CXX23
#include <expected>
#endif
extern "C" const TSLanguage *tree_sitter_json();

TEST_CASE("Query API", "[query]")
{
    ts::Language lang = tree_sitter_json();
    // Search for all numbers in file
    std::string query_source = "((number) @num)";

    CHECK_NOTHROW(ts::Query(lang, query_source));

    SECTION("Invalid query throws")
    {
        CHECK_THROWS_AS(ts::Query(lang, "((invalid_node)"), std::runtime_error);
    }
}

TEST_CASE("Detailed Query Execution", "[query][cursor]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);
    std::string  code = R"({"name": "Gemini", "type": "AI"})";
    ts::Tree     tree = parser.parseString(code);

    std::string     query_source = "(pair key: (string) @key_name)";
    ts::Query       query(lang, query_source);
    ts::QueryCursor cursor;
    cursor.exec(query, tree.getRootNode());

    SECTION("Iterate Matches and Captures")
    {
        ts::QueryMatch           match;
        std::vector<std::string> keys;

        while (cursor.nextMatch(match))
        {
            for (const auto &capture : match.captures)
            {
                keys.push_back(capture.node.getSourceText(code));
            }
        }

        REQUIRE(keys.size() == 2);
        CHECK(keys[0] == "\"name\"");
        CHECK(keys[1] == "\"type\"");
    }
}

TEST_CASE("Query Metadata and Information", "[query]")
{
    ts::Language lang         = tree_sitter_json();
    std::string  query_source = "((pair key: (string) @key) (#match? @key \"name\"))\n(number)+ @num";

    ts::Query query(lang, query_source);

    SECTION("Counts")
    {
        CHECK(query.getPatternCount() == 2);
        CHECK(query.getCaptureCount() == 2); // @key, @num
        CHECK(query.getStringCount() >= 1);  // "match?"
    }

    SECTION("Capture and String Resolution")
    {
        CHECK(query.getCaptureNameForID(0).compare("key") == 0);
        CHECK(query.getCaptureNameForID(1).compare("num") == 0);

        bool found_match_pred = false;
        for (uint32_t i = 0; i < query.getStringCount(); ++i)
        {
            if (query.getStringValueForID(i).compare("match?") == 0)
            {
                found_match_pred = true;
            }
        }
        CHECK(found_match_pred);
    }

    SECTION("Pattern Properties")
    {
        CHECK(query.isPatternRooted(0) == true);
        auto range = query.getByteRangeForPattern(0);
        CHECK(range.end > range.start);
    }

    SECTION("Quantifiers")
    {
        // JSON number
        CHECK(query.getCaptureQuantifierForID(1, 1) == ts::Quantifier::OneOrMore);
    }
}

TEST_CASE("Query Predicates", "[query]")
{
    ts::Language lang         = tree_sitter_json();
    std::string  query_source = "((pair key: (string) @k) (#eq? @k \"\\\"target\\\"\"))";
    ts::Query    query(lang, query_source);

    SECTION("Examine Predicate Steps")
    {
        auto steps = query.getAllPredicatesForPattern(0);
        REQUIRE(steps.size() >= 3); // "eq?", "@k", "\"target\"", "Done"

        CHECK(steps[0].type == ts::QueryPredicateStepType::String);

        // string "eq?"
        CHECK(query.getStringValueForID(steps[0].value_id).compare("eq?") == 0);

        // Capture (@k)
        CHECK(steps[1].type == ts::QueryPredicateStepType::Capture);

        // Last is always Done
        CHECK(steps.back().type == ts::QueryPredicateStepType::Done);
    }
}

TEST_CASE("Query Management", "[query]")
{
    ts::Language lang = tree_sitter_json();

    SECTION("Verify disableCapture only removes tags")
    {
        ts::Query query(lang, "((string) @s) ((number) @n)");
        query.disableCapture("n"); // Disable tag @n

        ts::Parser      parser(lang);
        ts::Tree        tree = parser.parseString("123");
        ts::QueryCursor cursor;
        cursor.exec(query, tree.getRootNode());

        ts::QueryMatch match;
        if (cursor.nextMatch(match))
        {
            CHECK(match.captures.empty());
        }
    }

    SECTION("Disable Patterns")
    {
        ts::Query query(lang, "((string) @s) ((number) @n)");
        query.disablePattern(0);
        query.disablePattern(1);

        ts::Parser      parser(lang);
        std::string     code = "{\"a\": 1}";
        ts::Tree        tree = parser.parseString(code);
        ts::QueryCursor cursor;
        cursor.exec(query, tree.getRootNode());

        ts::QueryMatch match;
        CHECK_FALSE(cursor.nextMatch(match));
    }
}

TEST_CASE("QueryCursor Limits and Ranges", "[query][cursor]")
{
    ts::Language    lang = tree_sitter_json();
    ts::Parser      parser(lang);
    std::string     code = "[1, 2, 3, 4, 5]";
    ts::Tree        tree = parser.parseString(code);
    ts::Query       query(lang, "(number) @n");
    ts::QueryCursor cursor;

    SECTION("Match Limit")
    {
        ts::Query query_complex(lang, "(_ (_)) @match");

        cursor.setMatchLimit(1);
        CHECK(cursor.getMatchLimit() == 1);

        std::string complex_code = "[[[[[[[[[[1]]]]]]]]]]";
        ts::Tree    complex_tree = parser.parseString(complex_code);

        cursor.exec(query_complex, complex_tree.getRootNode());

        ts::QueryMatch m;
        int            count = 0;
        while (cursor.nextMatch(m))
        {
            ++count;
        }

        CHECK(count == 6);
        CHECK(cursor.didExceedMatchLimit() == true);
    }

    SECTION("Byte Range")
    {
        bool ok = cursor.setByteRange({ 3, 8 });

        REQUIRE(ok == true);

        cursor.exec(query, tree.getRootNode());

        ts::QueryMatch m;
        uint32_t       count = 0;
        while (cursor.nextMatch(m))
        {
            ++count;
        }
        CHECK(count == 2);
    }
}

TEST_CASE("QueryCursor NextCapture", "[query][cursor]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);
    std::string  code = "{\"a\": 1}";
    ts::Tree     tree = parser.parseString(code);

    ts::Query       query(lang, "(pair key: (string) @k value: (number) @v)");
    ts::QueryCursor cursor;
    cursor.exec(query, tree.getRootNode());

    SECTION("Iterate via captures")
    {
        ts::QueryMatch match;
        uint32_t       capture_index;
        int            count = 0;

        while (cursor.nextCapture(match, capture_index))
        {
            ++count;
            CHECK(match.captures[capture_index].node.isNull() == false);
        }
        CHECK(count == 2); // One @k and one @v
    }
}

TEST_CASE("QueryCursor Progress Callback", "[query][cursor]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);

    // Large JSON to make cursor work
    std::string code = "[";
    for (int i = 0; i < 500; ++i)
    {
        code += "1,";
    }
    code += "1]";

    ts::Tree        tree = parser.parseString(code);
    ts::Query       query(lang, "(number) @n");
    ts::QueryCursor cursor;

    int                    progress_calls = 0;
    ts::QueryCursorOptions options;
    options.progress_callback = [&](ts::QueryCursorState *state)
    {
        ++progress_calls;
        CHECK(state->current_byte_offset <= code.size());
        return false; // Continue
    };

    SECTION("Execution with progress")
    {
#if TS_TEST_HAS_CXX17
        cursor.exec(query, tree.getRootNode(), options);
#else
        cursor.exec(query, tree.getRootNode(), &options);
#endif
        ts::QueryMatch m;
        int            matches = 0;
        while (cursor.nextMatch(m))
        {
            ++matches;
        }

        CHECK(progress_calls > 0);
        CHECK(matches != progress_calls);
        CHECK(matches > progress_calls);
    }
}

TEST_CASE("QueryCursor Remove Match", "[query][cursor]")
{
    ts::Language    lang = tree_sitter_json();
    ts::Parser      parser(lang);
    ts::Tree        tree = parser.parseString("[1, 2]");
    ts::Query       query(lang, "(number) @n");
    ts::QueryCursor cursor;
    cursor.exec(query, tree.getRootNode());

    SECTION("Remove active match")
    {
        ts::QueryMatch m;
        REQUIRE(cursor.nextMatch(m));
        uint32_t first_id = m.id;

        cursor.removeMatch(first_id);

        CHECK(cursor.nextMatch(m));
    }
}

TEST_CASE("Query Exception Handling and Limits", "[query][exceptions]")
{
    ts::Language lang        = tree_sitter_json();
    std::string  valid_query = "(number) @num";

    SECTION("Invalid language throws runtime_error")
    {
        ts::Language invalid_lang(nullptr);
        CHECK_THROWS_AS(ts::Query(invalid_lang, valid_query), std::runtime_error);
    }

    SECTION("Exceeding 4GB source size limit throws length_error")
    {
        // Fake a massive string view to trigger the overflow protection
        const char      *fake_str      = "fake";
        constexpr size_t overflow_size = static_cast<size_t>(std::numeric_limits<uint32_t>::max()) + 1;

        ts::details::StringViewParameter huge_view(fake_str, overflow_size);

        CHECK_THROWS_AS(ts::Query(lang, huge_view), std::length_error);
    }

    SECTION("Exceeding 4GB capture name limit in disableCapture throws length_error")
    {
        ts::Query        query(lang, valid_query);
        const char      *fake_str      = "fake";
        constexpr size_t overflow_size = static_cast<size_t>(std::numeric_limits<uint32_t>::max()) + 1;

        ts::details::StringViewParameter huge_view(fake_str, overflow_size);

        CHECK_THROWS_AS(query.disableCapture(huge_view), std::length_error);
    }
}

TEST_CASE("QueryCursor Depth Limits", "[query][cursor]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);

    // Deeply nested array
    std::string     code = "[[[[1]]]]";
    ts::Tree        tree = parser.parseString(code);
    ts::Query       query(lang, "(number) @n");
    ts::QueryCursor cursor;

    SECTION("Restrict search depth")
    {
        // Number is deeply nested, so restricting max start depth to 1 should prevent matching
        cursor.setMaxStartDepth(1);
        cursor.exec(query, tree.getRootNode());

        ts::QueryMatch m;
        CHECK_FALSE(cursor.nextMatch(m));
    }

    SECTION("Reset search depth allows matching again")
    {
        cursor.setMaxStartDepth(1);
        cursor.resetMaxStartDepth(); // Removes the limit
        cursor.exec(query, tree.getRootNode());

        ts::QueryMatch m;
        CHECK(cursor.nextMatch(m));
    }
}

TEST_CASE("QueryCursor Advanced Ranges", "[query][cursor][range]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);

    // Two objects on different lines
    std::string     code = "{\n\"a\": 1\n}\n{\n\"b\": 2\n}";
    ts::Tree        tree = parser.parseString(code);
    ts::Query       query(lang, "(number) @n");
    ts::QueryCursor cursor;

    SECTION("Point Range restriction")
    {
        // Restrict to the first line (skipping the second object)
        ts::Extent<ts::Point> point_range{ { 0, 0 }, { 2, 0 } };
        bool                  ok = cursor.setPointRange(point_range);
        REQUIRE(ok);

        cursor.exec(query, tree.getRootNode());
        ts::QueryMatch m;
        int            count = 0;
        while (cursor.nextMatch(m))
        {
            ++count;
        }

        // Should only find '1', not '2'
        CHECK(count == 1);
    }

    SECTION("Containing Byte and Point Ranges")
    {
        // Note: These methods return true if the operation succeeds in the C API.
        // We verify that the API accepts valid Extent structs.
        ts::Extent<uint32_t> byte_range{ 0, 5 };
        CHECK(cursor.setContainingByteRange(byte_range));

        ts::Extent<ts::Point> pt_range{ { 0, 0 }, { 0, 5 } };
        CHECK(cursor.setContainingPointRange(pt_range));
    }
}

#if TS_TEST_HAS_CXX23
TEST_CASE("Query C++23 Factory", "[query][cxx23]")
{
    ts::Language lang         = tree_sitter_json();
    std::string  valid_source = "((number) @num)";

    using Catch::Matchers::ContainsSubstring;

    SECTION("Static factory create() success")
    {
        auto result = ts::Query::create(lang, valid_source);
        REQUIRE(result.has_value());

        ts::Query query = std::move(result.value());
        CHECK(query.getCaptureCount() == 1);
        CHECK(query.getCaptureNameForID(0).compare("num") == 0);
    }

    SECTION("Static factory create() invalid syntax error")
    {
        // Missing closing parenthesis
        std::string invalid_source = "((number) @num";
        auto        result         = ts::Query::create(lang, invalid_source);

        REQUIRE_FALSE(result.has_value());
        // Verify that error message contains information about the problem
        CHECK_THAT(result.error(), ContainsSubstring("Syntax"));
    }

    SECTION("Static factory create() invalid node type")
    {
        std::string invalid_source = "((non_existent_node) @tag)";
        auto        result         = ts::Query::create(lang, invalid_source);

        REQUIRE_FALSE(result.has_value());
        CHECK_THAT(result.error(), ContainsSubstring("Node"));
    }

    SECTION("Static factory create() with invalid language")
    {
        ts::Language invalid_lang(nullptr);
        auto         result = ts::Query::create(invalid_lang, valid_source);

        REQUIRE_FALSE(result.has_value());
        CHECK_THAT(result.error(), ContainsSubstring("Language"));
    }
}
#endif
