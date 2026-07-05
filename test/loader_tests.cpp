#include <filesystem>
#include <fstream>
#include <string>

// Including the header at all is part of the test. It fails to compile unless
// cts::cpp-tree-sitter-dynamic is linked, which is the point of the opt-in
// guard.
#include <cts/loader.h>

#include <cpp-tree-sitter.h>
#include <doctest/doctest.h>


// The CMake helper is under test here as much as the C++ is. A module must be
// named <lang>.so without a "lib" prefix because nvim-treesitter and the
// tree-sitter CLI follow that convention. So we will, too.
TEST_CASE("add_grammar_module produces an unprefixed <lang>.so") {
  std::filesystem::path const module{CTS_TEST_CTSTEST_MODULE};

  std::error_code code;
  CHECK(std::filesystem::is_regular_file(module, code));
  CHECK(!code);
  CHECK(module.filename() == "ctstest.so");
}


TEST_CASE("A grammar module loads and parses") {
  auto language = ts::loadGrammar(CTS_TEST_CTSTEST_MODULE);
  REQUIRE(language.has_value());

  auto parser = ts::Parser::create(*language);
  REQUIRE(parser.has_value());
  auto tree = parser->parse("1");
  REQUIRE(tree.has_value());
  CHECK(tree->getRootNode().getNumChildren() > 0);
}


TEST_CASE("The entry point defaults to tree_sitter_<stem>") {
  CHECK(ts::detail::entryPointName("/parsers/json.so") == "tree_sitter_json");
  // Grammar names use underscores while file names sometimes use dashes.
  CHECK(ts::detail::entryPointName("/parsers/c-sharp.so") == "tree_sitter_c_sharp");
}


TEST_CASE("An explicit entry point is accepted and resolves") {
  auto language =
    ts::loadGrammar(CTS_TEST_CTSTEST_MODULE, ts::EntryPoint{"tree_sitter_ctstest"});
  CHECK(language.has_value());
}


TEST_CASE("Loading the same file twice yields the same language") {
  auto first = ts::loadGrammar(CTS_TEST_CTSTEST_MODULE);
  auto second = ts::loadGrammar(CTS_TEST_CTSTEST_MODULE);
  REQUIRE(first.has_value());
  REQUIRE(second.has_value());
  CHECK(*first == *second);
}


TEST_CASE("A missing file is reported as a missing file instead of a load failure") {
  auto language = ts::loadGrammar("/nonexistent/json.so");
  REQUIRE(!language.has_value());
  CHECK(language.error().kind == ts::GrammarLoadError::Kind::FileMissing);
  CHECK(language.error().path == "/nonexistent/json.so");
}


TEST_CASE("A file that is not a library reports the OS message") {
  std::filesystem::path const scratch{CTS_TEST_SCRATCH_DIR};
  std::error_code code;
  std::filesystem::create_directories(scratch, code);
  REQUIRE(!code);

  auto const notALibrary = scratch / "not-a-library.so";
  {
    std::ofstream out{notALibrary};
    REQUIRE(out.is_open());
    out << "this is not an ELF file\n";
  }

  auto language = ts::loadGrammar(notALibrary);
  REQUIRE(!language.has_value());
  CHECK(language.error().kind == ts::GrammarLoadError::Kind::LibraryOpenFailed);
  CHECK(!language.error().detail.empty());
}


TEST_CASE("A missing entry point names the symbol it looked for") {
  auto language =
    ts::loadGrammar(CTS_TEST_CTSTEST_MODULE, ts::EntryPoint{"tree_sitter_nope"});
  REQUIRE(!language.has_value());
  CHECK(language.error().kind == ts::GrammarLoadError::Kind::EntryPointMissing);
  CHECK(language.error().detail.find("tree_sitter_nope") != std::string::npos);
}


TEST_CASE("An entry point returning no language is reported") {
  auto language = ts::loadGrammar(CTS_TEST_NULLLANG_MODULE);
  REQUIRE(!language.has_value());
  CHECK(language.error().kind == ts::GrammarLoadError::Kind::LanguageNull);
}


TEST_CASE("A stale grammar ABI is rejected at load time") {
  auto language = ts::loadGrammar(CTS_TEST_OLDABI_MODULE);
  REQUIRE(!language.has_value());
  CHECK(language.error().kind == ts::GrammarLoadError::Kind::AbiIncompatible);
  CHECK(language.error().detail.find("12") != std::string::npos);
}
