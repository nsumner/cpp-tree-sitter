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
#include <utility>
#include <vector>

extern "C" const TSLanguage *tree_sitter_json();

TEST_CASE("Language Metadata and Versioning", "[language]")
{
    ts::Language lang = tree_sitter_json();

    SECTION("Basic Properties")
    {
        CHECK(lang.getSymbolsCount() > 0);
        CHECK(lang.getVersion() > 0);
        // CHECK(lang.getName().compare("json") == 0); This grammar don't have name
        CHECK(lang.getName().compare("") == 0);
    }

    SECTION("Symbol Resolution")
    {
        ts::Symbol sym = lang.getSymbolForName("object", true);
        CHECK(sym != 0);
        CHECK(lang.getSymbolName(sym).compare("object") == 0);
        CHECK(lang.getSymbolType(sym) == ts::SymbolType::TypeRegular);
    }
}

TEST_CASE("Memory Management - Copying", "[lifecycle]")
{
    ts::Language lang = tree_sitter_json();
    {
        ts::Language lang_copy = lang; // Copy constructor test (ts_language_copy)
        CHECK(lang_copy.getSymbolsCount() == lang.getSymbolsCount());

        ts::Language lang_moved = std::move(lang_copy); // Move test
        CHECK(lang_moved.getSymbolsCount() > 0);
    }
    CHECK(lang.getSymbolsCount() > 0);
}

TEST_CASE("Language Deep Dive", "[language]")
{
    ts::Language lang = tree_sitter_json();

    SECTION("Count Accessors")
    {
        CHECK(lang.getSymbolsCount() > 0);
        CHECK(lang.getStatesCount() > 0);
        CHECK(lang.getFieldsCount() > 0);
    }

    SECTION("Field Resolution")
    {
        ts::FieldID key_id = lang.getFieldIDForName("key");
        CHECK(key_id > 0);

        auto field_name = lang.getFieldNameForID(key_id);
        CHECK(field_name.compare("key") == 0);

        CHECK(lang.getFieldIDForName("non_existent_field") == 0);
        CHECK(lang.getFieldNameForID(0).compare("") == 0);

        auto fields = lang.getAllFieldsNames();
        for (const auto &field : fields)
        {
            CHECK(field.compare("") != 0);
        }
    }

    SECTION("Type Hierarchy")
    {
        auto super_types = lang.getAllSuperTypes();

        if (!super_types.empty())
        {
            ts::Symbol first_super = super_types[0];
            auto       sub_types   = lang.getAllSubTypesForSuperType(first_super);
            CHECK_FALSE(sub_types.empty());
        }
    }

    SECTION("State Navigation and Lookahead")
    {
        ts::StateID initial_state = 0;
        ts::Symbol  document_sym  = lang.getSymbolForName("document", true);

        [[maybe_unused]] ts::StateID next_state = lang.getNextState(initial_state, document_sym);

        ts::LookaheadIterator li = lang.getLookaheadIterator(initial_state);
        CHECK(li.getLanguage().operator const TSLanguage *() == lang.operator const TSLanguage *());
    }

    SECTION("Metadata")
    {
#if TS_TEST_HAS_CXX17
        auto meta = lang.getMetadata();
        if (meta)
        {
            CHECK((meta->major_version >= 0));
        }
#else
        ts::LanguageMetadata meta = lang.getMetadata();
#endif

#if defined(CPP_TS_TEST_FEATURE_WASM)
        CHECK_FALSE(lang.isWasm());
#endif
    }
}

TEST_CASE("Language Operators and Assignments", "[language][lifecycle]")
{
    ts::Language lang1 = tree_sitter_json();
    ts::Language lang2 = tree_sitter_json();

    SECTION("Assignment operators")
    {
        ts::Language lang3 = lang1; // Copy
        CHECK(lang3.getSymbolsCount() == lang1.getSymbolsCount());

        ts::Language lang4(nullptr); // Null Object
        lang4 = lang1;               // Swap
        CHECK(lang4.getSymbolsCount() == lang1.getSymbolsCount());

        ts::Language lang5 = std::move(lang1);
        CHECK(lang5.getSymbolsCount() > 0);
    }

    SECTION("C-API Interop")
    {
        const TSLanguage *raw = lang2;
        CHECK(raw != nullptr);
        CHECK(ts_language_symbol_count(raw) == lang2.getSymbolsCount());
    }
}

TEST_CASE("Language Invalid State Handling", "[language][error_handling]")
{
    // Language construct with nullptr
    ts::Language invalid_lang(nullptr);

    SECTION("Validation Flags")
    {
        CHECK_FALSE(invalid_lang.isValid());
        CHECK(invalid_lang.operator const TSLanguage *() == nullptr);
    }

    SECTION("Count Accessors on Invalid State")
    {
        CHECK(invalid_lang.getSymbolsCount() == 0);
        CHECK(invalid_lang.getStatesCount() == 0);
        CHECK(invalid_lang.getFieldsCount() == 0);
    }

    SECTION("Resolutions on Invalid State")
    {
        // Symbol Resolution
        CHECK(invalid_lang.getSymbolName(1).empty());
        CHECK(invalid_lang.getSymbolType(1) == ts::SymbolType::TypeRegular);
        CHECK(invalid_lang.getSymbolForName("test", true) == 0);

        // Field Resolution
        CHECK(invalid_lang.getFieldNameForID(1).empty());
        CHECK(invalid_lang.getFieldIDForName("test") == 0);
    }

    SECTION("Type Hierarchy on Invalid State")
    {
        CHECK(invalid_lang.getAllSuperTypes().empty());
        CHECK(invalid_lang.getAllSubTypesForSuperType(1).empty());
    }

    SECTION("Navigation and Exceptions on Invalid State")
    {
        CHECK(invalid_lang.getNextState(0, 0) == 0);
        CHECK_THROWS_AS(invalid_lang.getLookaheadIterator(0), std::logic_error);
    }

    SECTION("Metadata on Invalid State")
    {
        CHECK(invalid_lang.getName().empty());
        CHECK(invalid_lang.getVersion() == 0);

#if TS_HAS_CXX17
        CHECK_FALSE(invalid_lang.getMetadata().has_value());
#else
        ts::LanguageMetadata meta = invalid_lang.getMetadata();
        CHECK(meta.major_version == 0);
        CHECK(meta.minor_version == 0);
        CHECK(meta.patch_version == 0);
#endif
    }
}

TEST_CASE("Language Edge Cases", "[language]")
{
    ts::Language lang = tree_sitter_json();

    SECTION("Self Assignment")
    {
        ts::Language *ptr_lang = &lang;
        lang                   = *ptr_lang;
        CHECK(lang.isValid());
        CHECK(lang.getSymbolsCount() > 0);
    }

    SECTION("Invalid lookups on valid language")
    {
        // Checking the behavior for a symbol ID that is definitely not a SuperType (e.g., 9999).
        // We assume that the C API will correctly return null/0 and the wrapper will throw an empty vector.
        CHECK(lang.getAllSubTypesForSuperType(9999).empty());
    }

    SECTION("Throw exception on size overflow")
    {
        // We bypass the real 4GB allocation by creating an artificial view, but with a given gigantic size.
        const char      *fake_str      = "fake";
        constexpr size_t overflow_size = static_cast<size_t>(std::numeric_limits<uint32_t>::max()) + 1;

        ts::details::StringViewParameter huge_view(fake_str, overflow_size);

        CHECK_THROWS_AS(lang.getSymbolForName(huge_view, true), std::length_error);
        CHECK_THROWS_AS(lang.getFieldIDForName(huge_view), std::length_error);
    }
}
