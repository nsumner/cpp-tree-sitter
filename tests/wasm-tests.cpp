#include "pch.hpp"

#if defined(CPP_TS_TEST_FEATURE_WASM)

// cpp-tree-sitter
#include <cpp-tree-sitter.hpp>

// wasmtime
#if TS_TEST_HAS_CXX17
#include <wasmtime.hh>
#else
#include <wasm.h>
#endif

// Catch2
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

// STL
#include <stdexcept>
#include <string>

#if TS_TEST_HAS_CXX23
#include <expected>
#endif

TEST_CASE("Wasm Loading with Wasmtime C++ API", "[wasm]")
{
#if TS_TEST_HAS_CXX17
    // C++ Api Wasm Engine
    wasmtime::Engine engineCpp;
    wasm_engine_t   *engine = engineCpp.capi();
#else
    // C Api Wasm Engine
    wasm_engine_t *engine = wasm_engine_new();
#endif

    SECTION("Successful language loading and parsing")
    {
        // Engine will be deleted by tree-sitter
        ts::WasmStore store(engine);

        // Loading compilated tree-spitter-json grammar
        ts::Language json_lang{ nullptr };
        REQUIRE_NOTHROW(json_lang = store.loadLanguage(CPP_TS_TEST_WASM_FILES_DIR "/tree-sitter-json.wasm"));

        // Loaded language tests
        REQUIRE(json_lang.isWasm());
        REQUIRE(store.getLanguageCount() == 1);

        // Parser test using wasm
        ts::Parser parser;

        parser.setWasmStore(store);

        CHECK_FALSE(store.isValid());

#if TS_TEST_HAS_CXX23
        auto res = parser.setLanguage(json_lang);
        CHECK(res.has_value());
        CHECK(res.value());
#else
        CHECK(parser.setLanguage(json_lang));
#endif

        std::string code = R"({"test": 123})";
        ts::Tree    tree = parser.parseString(code);

        REQUIRE_FALSE(tree.hasError());
        REQUIRE(tree.getRootNode().getType().compare("document") == 0);

        store = parser.takeWasmStore();

        CHECK(store.isValid());
        CHECK_FALSE(parser.hasLanguage());
    }

    SECTION("Error handling for invalid Wasm data")
    {
        ts::WasmStore store(engine);

        // Plain text. Not a binary wasm
        std::string garbage = "not a wasm file";

        // WasmErrorHelper::validate error
        REQUIRE_THROWS_AS(store.loadLanguage("invalid", garbage), std::runtime_error);
    }
}

TEST_CASE("WasmStore Lifecycle", "[wasm]")
{
#if TS_TEST_HAS_CXX17
    // C++ Api Wasm Engine
    wasmtime::Engine engineCpp;
    wasm_engine_t   *raw_engine = engineCpp.capi();
#else
    // C Api Wasm Engine
    wasm_engine_t *raw_engine = wasm_engine_new();
#endif

    SECTION("RAII Safety")
    {
        {
            ts::WasmStore store(raw_engine);
            // WasmStore Live here
        }
        SUCCEED("WasmStore destroyed without crashing");
    }
}

TEST_CASE("Parser Wasm Exceptions and Guardrails", "[wasm][parser][exceptions]")
{
#if TS_TEST_HAS_CXX17
    wasmtime::Engine engine;
    ts::WasmStore    store(engine.capi());
#else
    wasm_engine_t *engine = wasm_engine_new();
    ts::WasmStore  store(engine);
#endif

    ts::Language json_lang = store.loadLanguage(CPP_TS_TEST_WASM_FILES_DIR "/tree-sitter-json.wasm");

    SECTION("Setting Wasm language without WasmStore throws logic_error")
    {
        ts::Parser parser;

        CHECK_FALSE(parser.hasWasmStore());

#if TS_TEST_HAS_CXX23
        auto res = parser.setLanguage(json_lang);
        REQUIRE_FALSE(res.has_value());
        CHECK_THAT(res.error(), Catch::Matchers::ContainsSubstring("WasmStore"));
#else
        // The parser must reject a Wasm language if no WasmStore is provided
        CHECK_THROWS_AS(parser.setLanguage(json_lang), std::logic_error);
#endif
    }

    SECTION("Constructor throws on invalid language with WasmStore")
    {
        ts::Language invalid_lang(nullptr);

        // Constructor should validate the language before transferring the store
        CHECK_THROWS_AS(ts::Parser(invalid_lang, store), std::runtime_error);
    }
}

TEST_CASE("Parser Constructor and WasmStore Transfer", "[wasm][parser]")
{
#if TS_TEST_HAS_CXX17
    wasmtime::Engine engine;
    ts::WasmStore    store(engine.capi());
#else
    wasm_engine_t *engine = wasm_engine_new();
    ts::WasmStore  store(engine);
#endif

    ts::Language json_lang = store.loadLanguage(CPP_TS_TEST_WASM_FILES_DIR "/tree-sitter-json.wasm");

    SECTION("Initialize Parser directly with WasmStore and Language")
    {
        // The constructor should take ownership of the store and set the language safely
        ts::Parser parser(json_lang, store);

        CHECK(parser.hasLanguage());
        CHECK(parser.hasWasmStore());

        // The original store object should be invalidated after the transfer
        CHECK_FALSE(store.isValid());

        // Verify that parsing works correctly using the transferred store
        std::string code = "[1, 2, 3]";
        ts::Tree    tree = parser.parseString(code);
        CHECK_FALSE(tree.hasError());
    }
}

TEST_CASE("Replacing WasmStore in Parser", "[wasm][parser]")
{
#if TS_TEST_HAS_CXX17
    wasmtime::Engine engine1;
    wasmtime::Engine engine2;
    ts::WasmStore    store1(engine1.capi());
    ts::WasmStore    store2(engine2.capi());
#else
    wasm_engine_t *engine1 = wasm_engine_new();
    wasm_engine_t *engine2 = wasm_engine_new();
    ts::WasmStore  store1(engine1);
    ts::WasmStore  store2(engine2);
#endif

    ts::Parser parser;

    SECTION("Setting a new WasmStore safely discards the old one")
    {
        parser.setWasmStore(store1);
        CHECK(parser.hasWasmStore());
        CHECK_FALSE(store1.isValid());

        // Set a second store without taking the first one back.
        // The parser should safely delete the previous store internally.
        parser.setWasmStore(store2);
        CHECK(parser.hasWasmStore());
        CHECK_FALSE(store2.isValid());

        // Taking the store should now return the second one (which is valid)
        ts::WasmStore retrieved_store = parser.takeWasmStore();
        CHECK(retrieved_store.isValid());
        CHECK_FALSE(parser.hasWasmStore());
    }
}

#if TS_TEST_HAS_CXX23
TEST_CASE("Wasm C++23 Features", "[wasm][cxx23]")
{
    wasmtime::Engine engine;
    wasm_engine_t   *raw_engine = engine.capi();

    SECTION("WasmStore::create success")
    {
        auto store_res = ts::WasmStore::create(raw_engine);
        REQUIRE(store_res.has_value());
        CHECK(store_res->isValid());
    }

    SECTION("loadLanguageExpected from file success")
    {
        auto store_res = ts::WasmStore::create(raw_engine);
        auto lang_res  = store_res->loadLanguageExpected(CPP_TS_TEST_WASM_FILES_DIR "/tree-sitter-json.wasm");

        REQUIRE(lang_res.has_value());
        CHECK(lang_res->isValid());
        CHECK(lang_res->isWasm());
    }

    SECTION("loadLanguageExpected failure (invalid file)")
    {
        auto store_res = ts::WasmStore::create(raw_engine);
        auto lang_res  = store_res->loadLanguageExpected("non_existent.wasm");

        CHECK_FALSE(lang_res.has_value());
        CHECK_THAT(lang_res.error(), Catch::Matchers::ContainsSubstring("Failed to open"));
    }

    SECTION("Parser::create with WasmStore success")
    {
        auto store_res = ts::WasmStore::create(raw_engine);
        auto lang      = store_res->loadLanguage(CPP_TS_TEST_WASM_FILES_DIR "/tree-sitter-json.wasm");

        // Testowanie statycznej fabryki parsera przyjmuj¹cej store
        auto parser_res = ts::Parser::create(lang, *store_res);

        REQUIRE(parser_res.has_value());
        CHECK(parser_res->hasWasmStore());
        CHECK_FALSE(store_res->isValid()); // Store powinien byæ przeniesiony (invalidated)
    }
}
#endif

#endif
