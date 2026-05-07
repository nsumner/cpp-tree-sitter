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
#include <functional>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#if TS_TEST_HAS_CXX20
#include <span>
#endif

#if TS_TEST_HAS_CXX23
#include <expected>
#endif

extern "C" const TSLanguage *tree_sitter_json();

TEST_CASE("Parser and Tree lifecycle", "[parser]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);

    std::string code = R"({"key": [1, 2, null]})";

    SECTION("Parsing valid code")
    {
        ts::Tree tree = parser.parseString(code);
        ts::Node root = tree.getRootNode();

        CHECK_FALSE(root.isNull());
        CHECK(root.getType().compare("document") == 0);
        CHECK_FALSE(tree.hasError());
    }

    SECTION("Handling syntax errors")
    {
        ts::Tree tree = parser.parseString("{ \"key\": }"); // Error: No value
        CHECK(tree.hasError());
    }
}

TEST_CASE("Parser Logging", "[parser][debug]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);

    std::vector<std::string> log_messages;

    parser.setLogger([&](ts::LogType type [[maybe_unused]], const char *message) { log_messages.push_back(message); });

    ts::Tree tree = parser.parseString("[1]");

    CHECK_FALSE(log_messages.empty());

    parser.removeLogger();
    size_t count_after_removal = log_messages.size();
    tree                       = parser.parseString("[1, 2]");

    CHECK(log_messages.size() == count_after_removal);
}

TEST_CASE("Parser Progress Callback", "[parser]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);

    int              calls = 0;
    ts::ParseOptions options;
    options.progress_callback = [&](ts::ParseState *state [[maybe_unused]])
    {
        ++calls;
        return false;
    };

    // We are generating large, deeply nested JSON that will force the parser to do a lot of work.
    std::string code = "[";
    for (int i = 0; i < 1000; ++i)
    {
        code += "[";
    }
    for (int i = 0; i < 1000; ++i)
    {
        code += "1,";
    }
    code += "1";
    for (int i = 0; i < 1000; ++i)
    {
        code += "]";
    }
    code += "]";

    ts::Input input;
    input.encoding = ts::InputEncoding::UTF8;
    input.read     = [&](uint32_t byte, ts::Point p [[maybe_unused]], uint32_t *read)
    {
        *read = (byte < code.size()) ? 1 : 0;
        return ts::details::make_view(code.c_str() + byte, *read);
    };
#if TS_TEST_HAS_CXX17
    ts::Tree tree = parser.parse(input, {}, options);
#else
    ts::Tree tree = parser.parse(input, nullptr, &options);
#endif
    CHECK(calls > 0);
}

TEST_CASE("Parser Included Ranges", "[parser][range]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);

    ts::Range r1{ ts::Extent<ts::Point>{ { 0, 0 }, { 0, 5 } }, ts::Extent<uint32_t>{ 0, 5 } };
    ts::Range r2{ ts::Extent<ts::Point>{ { 0, 10 }, { 0, 15 } }, ts::Extent<uint32_t>{ 10, 15 } };

    std::vector<ts::Range> ranges = { r2, r1 };

    CHECK(parser.setIncludedRanges(ranges));
    auto back = parser.getIncludedRanges();

    REQUIRE(back.size() >= 1);
    CHECK(back[0].byte.start == 0);
}

TEST_CASE("Parser Configuration and Language", "[parser]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);

    SECTION("Language management")
    {
        CHECK(parser.getCurrentLanguage().operator const TSLanguage *() == lang.operator const TSLanguage *());

#if TS_TEST_HAS_CXX23
        auto res = parser.setLanguage(lang);
        CHECK(res.has_value());
        CHECK(res.value());
#else
        CHECK(parser.setLanguage(lang));
#endif

        parser.reset();
        CHECK(parser.getCurrentLanguage().operator const TSLanguage *() == lang.operator const TSLanguage *());
    }
}

TEST_CASE("Parser Encoded Strings", "[parser]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);

    // JSON in UTF-8 format
    std::string utf8_code = "[1, 2, 3]";

    SECTION("Parse UTF-8 explicitly")
    {
        ts::Tree tree = parser.parseStringEncoded(utf8_code, ts::InputEncoding::UTF8);
        CHECK_FALSE(tree.hasError());
        CHECK(tree.getRootNode().getNamedChild(0).getNamedChildCount() == 3);
    }

    SECTION("Parse with Old Tree (Incremental Encoded)")
    {
        ts::Tree old_tree = parser.parseString(utf8_code);

        // Simulate small change in text
        std::string   new_code = "[1, 5, 3]";
        ts::InputEdit edit{ 4,        5,        5, // '2' to '5' (same length)
                            { 0, 4 }, { 0, 5 }, { 0, 5 } };
        old_tree.edit(edit);

#if TS_TEST_HAS_CXX17
        ts::Tree new_tree = parser.parseStringEncoded(new_code, ts::InputEncoding::UTF8, old_tree);
#else
        ts::Tree new_tree = parser.parseStringEncoded(new_code, ts::InputEncoding::UTF8, &old_tree);
#endif
        CHECK_FALSE(new_tree.hasError());
        CHECK(new_tree.getRootNode().getNamedChild(0).getNamedChild(1).getSourceText(new_code) == "5");
    }
}

TEST_CASE("Parser Debugging (DOT Graphs)", "[parser][debug]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);

    SECTION("Enable and Disable DOT graphs")
    {
        const char *temp_file = "parser_graph.dot";
#if defined(_WIN32)
        FILE *f = nullptr;
        fopen_s(&f, temp_file, "w");
#else
        FILE *f = fopen(temp_file, "w");
#endif
        REQUIRE(f != nullptr);

        parser.enableDotGraphs(f);
        ts::Tree tree = parser.parseString("[true]");
        parser.disableDotGraphs();
        fclose(f);

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

TEST_CASE("Parser DOT graphs by filename", "[parser][debug]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);
    std::string  filename = "auto_graph.dot";

    SECTION("Enable by path")
    {
        CHECK_NOTHROW(parser.enableDotGraphs(filename));

        ts::Tree tree = parser.parseString("[1, 2, 3]");
        parser.disableDotGraphs();

        std::ifstream f(filename);
        CHECK(f.good());
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        CHECK(content.find("digraph") != std::string::npos);
        f.close();

        CHECK(std::remove(filename.c_str()) == 0);
    }
}

TEST_CASE("Parser Exception Handling (No Language)", "[parser][exceptions]")
{
    // Create a parser without assigning a language
    ts::Parser empty_parser;

    SECTION("Validation flags and getters on empty parser")
    {
        CHECK_FALSE(empty_parser.hasLanguage());
        CHECK_FALSE(empty_parser.getCurrentLanguage().isValid());
    }

    SECTION("Parsing without language throws logic_error")
    {
        std::string code = "{}";

        CHECK_THROWS_AS(empty_parser.parseString(code), std::logic_error);
        CHECK_THROWS_AS(empty_parser.parseStringEncoded(code, ts::InputEncoding::UTF8), std::logic_error);

        ts::Input input;
        input.encoding = ts::InputEncoding::UTF8;
        input.read     = [](uint32_t, ts::Point, uint32_t *read)
        {
            *read = 0;
            return ts::details::StringViewReturn();
        };

        CHECK_THROWS_AS(empty_parser.parse(input), std::logic_error);
    }

    SECTION("Initialization with invalid language throws runtime_error")
    {
        ts::Language invalid_lang(nullptr);
        CHECK_THROWS_AS(ts::Parser(invalid_lang), std::runtime_error);
    }
}

TEST_CASE("Parser Input Limits (4GB Overflow)", "[parser][exceptions]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);

    SECTION("Exceeding 4GB string limit throws length_error")
    {
        // We avoid allocating 4GB by creating a fake view with an artificial size
        const char      *fake_str      = "fake";
        constexpr size_t overflow_size = static_cast<size_t>(std::numeric_limits<uint32_t>::max()) + 1;

        ts::details::StringViewParameter huge_view(fake_str, overflow_size);

        CHECK_THROWS_AS(parser.parseString(huge_view), std::length_error);
        CHECK_THROWS_AS(parser.parseStringEncoded(huge_view, ts::InputEncoding::UTF8), std::length_error);
    }
}

TEST_CASE("Parser Included Ranges Merging Algorithm", "[parser][range]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);

    SECTION("Empty ranges resets included ranges")
    {
        std::vector<ts::Range> empty_ranges;
        CHECK(parser.setIncludedRanges(empty_ranges));
        CHECK_FALSE(parser.getIncludedRanges().empty());
    }

    SECTION("Merge overlapping ranges")
    {
        // Range 1: bytes 0 to 10
        ts::Range r1{ ts::Extent<ts::Point>{ { 0, 0 }, { 0, 10 } }, ts::Extent<uint32_t>{ 0, 10 } };
        // Range 2: bytes 5 to 15 (overlaps with r1)
        ts::Range r2{ ts::Extent<ts::Point>{ { 0, 5 }, { 0, 15 } }, ts::Extent<uint32_t>{ 5, 15 } };

        std::vector<ts::Range> ranges = { r1, r2 };
        CHECK(parser.setIncludedRanges(ranges));

        auto merged = parser.getIncludedRanges();
        REQUIRE(merged.size() == 1); // Should be merged into a single range
        CHECK(merged[0].byte.start == 0);
        CHECK(merged[0].byte.end == 15);
    }

    SECTION("Merge fully enveloped ranges")
    {
        // Range 1: bytes 0 to 20
        ts::Range r1{ ts::Extent<ts::Point>{ { 0, 0 }, { 0, 20 } }, ts::Extent<uint32_t>{ 0, 20 } };
        // Range 2: bytes 5 to 10 (fully inside r1)
        ts::Range r2{ ts::Extent<ts::Point>{ { 0, 5 }, { 0, 10 } }, ts::Extent<uint32_t>{ 5, 10 } };

        // Even if provided in reverse order, the sort algorithm should handle them
        std::vector<ts::Range> ranges = { r2, r1 };
        CHECK(parser.setIncludedRanges(ranges));

        auto merged = parser.getIncludedRanges();
        REQUIRE(merged.size() == 1);
        CHECK(merged[0].byte.start == 0);
        CHECK(merged[0].byte.end == 20);
    }
}

TEST_CASE("Parser Included Ranges - Reset to full document", "[parser][range]")
{
    ts::Language lang = tree_sitter_json();
    ts::Parser   parser(lang);
    std::string  code = "[1, 2, 3]";

    SECTION("Resetting ranges parses the whole document")
    {
        // Restrict the parser to a specific narrow range (e.g., just the number '2')
        ts::Range narrow_range{ ts::Extent<ts::Point>{ { 0, 4 }, { 0, 5 } }, ts::Extent<uint32_t>{ 4, 5 } };

#if TS_TEST_HAS_CXX20
        REQUIRE(parser.setIncludedRanges(std::span<const ts::Range>{ &narrow_range, 1 }));
#else
        REQUIRE(parser.setIncludedRanges({ narrow_range }));
#endif

        // Pass an empty vector to tell the parser to evaluate the entire document again
        std::vector<ts::Range> empty_ranges;
        CHECK(parser.setIncludedRanges(empty_ranges));

        // Parse the code to verify the parser successfully reads the whole string
        ts::Tree tree = parser.parseString(code);
        CHECK_FALSE(tree.hasError());

        // The root node should span the entire length of the code
        ts::Node root = tree.getRootNode();
        CHECK(root.getByteRange().end == code.length());

        // Tree-sitter returns one array when no ranges are restricting it
        CHECK(parser.getIncludedRanges().size() == 1);
    }
}

#if TS_TEST_HAS_CXX23
TEST_CASE("Parser C++23 Specific Features", "[parser][cxx23]")
{
    ts::Language lang = tree_sitter_json();

    SECTION("Static factory create() success")
    {
        auto result = ts::Parser::create(lang);
        REQUIRE(result.has_value());

        ts::Parser parser = std::move(result.value());
        CHECK(parser.hasLanguage());

        ts::Tree tree = parser.parseString("{}");
        CHECK_FALSE(tree.hasError());
    }

    SECTION("Static factory create() with invalid language")
    {
        ts::Language invalid_lang(nullptr);
        auto         result = ts::Parser::create(invalid_lang);

        REQUIRE_FALSE(result.has_value());
        CHECK_THAT(result.error(), Catch::Matchers::ContainsSubstring("invalid"));
    }

    SECTION("setLanguage return type in C++23")
    {
        ts::Parser parser;
        auto       res = parser.setLanguage(lang);

        REQUIRE(res.has_value());
        CHECK(parser.hasLanguage());
    }
}
#endif
