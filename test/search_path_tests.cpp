#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <doctest/doctest.h>
#include <cts/loader.h>

namespace {

std::filesystem::path
makeParserDirectory(std::string_view name) {
  auto const root = std::filesystem::path{CTS_TEST_SCRATCH_DIR} / name;
  std::error_code code;
  std::filesystem::remove_all(root, code);
  std::filesystem::create_directories(root, code);
  REQUIRE(!code);
  return root;
}


void
installModule(std::filesystem::path const& directory,
              std::filesystem::path const& source,
              std::string_view name) {
  std::error_code code;
  std::filesystem::copy_file(
    source, directory / (std::string{name} + ".so"),
    std::filesystem::copy_options::overwrite_existing, code);
  REQUIRE(!code);
}

}  // namespace


TEST_CASE("A search path finds and loads a grammar by name") {
  auto const directory = makeParserDirectory("hit");
  installModule(directory, CTS_TEST_CTSTEST_MODULE, "ctstest");

  ts::GrammarSearchPath const grammars{{directory}};

  auto const found = grammars.find("ctstest");
  REQUIRE(found.has_value());
  CHECK(found->filename() == "ctstest.so");

  auto language = grammars.load("ctstest");
  CHECK(language.has_value());
}


TEST_CASE("A search path honours an explicit entry point") {
  auto const directory = makeParserDirectory("entry-point");
  installModule(directory, CTS_TEST_CTSTEST_MODULE, "renamed");

  ts::GrammarSearchPath const grammars{{directory}};

  CHECK(!grammars.load("renamed").has_value());
  CHECK(grammars.load("renamed", ts::EntryPoint{"tree_sitter_ctstest"}).has_value());
}


TEST_CASE("A miss names every directory that was searched") {
  auto const directory = makeParserDirectory("miss");

  ts::GrammarSearchPath const grammars{{directory, "/nonexistent/parsers"}};

  auto language = grammars.load("json");
  REQUIRE(!language.has_value());
  CHECK(language.error().kind == ts::GrammarLoadError::Kind::NotFoundInSearchPath);
  CHECK(language.error().path == "json");
  CHECK(language.error().detail.find(directory.string()) != std::string::npos);
  CHECK(language.error().detail.find("/nonexistent/parsers") != std::string::npos);
}


TEST_CASE("Earlier directories win") {
  auto const first = makeParserDirectory("order-first");
  auto const second = makeParserDirectory("order-second");
  installModule(first, CTS_TEST_CTSTEST_MODULE, "shared");
  installModule(second, CTS_TEST_NULLLANG_MODULE, "shared");

  ts::GrammarSearchPath const grammars{{first, second}};

  auto const found = grammars.find("shared");
  REQUIRE(found.has_value());
  CHECK(found->parent_path() == first);
}


TEST_CASE("A first match that fails to load is not skipped") {
  auto const first = makeParserDirectory("stale-first");
  auto const second = makeParserDirectory("stale-second");
  installModule(first, CTS_TEST_OLDABI_MODULE, "shadowed");
  installModule(second, CTS_TEST_CTSTEST_MODULE, "shadowed");

  ts::GrammarSearchPath const grammars{{first, second}};

  auto language = grammars.load("shadowed", ts::EntryPoint{"tree_sitter_oldabi"});
  REQUIRE(!language.has_value());
  CHECK(language.error().kind == ts::GrammarLoadError::Kind::AbiIncompatible);
}


TEST_CASE("available lists installed grammars, sorted and deduplicated") {
  auto const first = makeParserDirectory("avail-first");
  auto const second = makeParserDirectory("avail-second");
  installModule(first, CTS_TEST_CTSTEST_MODULE, "zebra");
  installModule(first, CTS_TEST_CTSTEST_MODULE, "alpha");
  installModule(second, CTS_TEST_CTSTEST_MODULE, "alpha");
  {
    std::ofstream out{first / "notes.txt"};
    out << "ignored\n";
  }

  ts::GrammarSearchPath const grammars{{first, second, "/nonexistent/parsers"}};

  std::vector<std::string> const expected{"alpha", "zebra"};
  CHECK(grammars.available() == expected);
}


TEST_CASE("A search path reports the directories it was given, in order") {
  ts::GrammarSearchPath const grammars{{"/a", "/b"}};

  auto const directories = grammars.directories();
  REQUIRE(directories.size() == 2);
  CHECK(directories[0] == "/a");
  CHECK(directories[1] == "/b");
}


TEST_CASE("available skips a directory that is named like a module") {
  auto const directory = makeParserDirectory("avail-directory");
  installModule(directory, CTS_TEST_CTSTEST_MODULE, "real");
  std::error_code code;
  std::filesystem::create_directory(directory / "impostor.so", code);
  REQUIRE(!code);

  ts::GrammarSearchPath const grammars{{directory}};

  std::vector<std::string> const expected{"real"};
  CHECK(grammars.available() == expected);
}


TEST_CASE("A name that is a path escapes nothing") {
  auto const inside = makeParserDirectory("confine-inside");
  auto const nested = makeParserDirectory("confine-inside/nested");
  auto const outside = makeParserDirectory("confine-outside");
  installModule(nested, CTS_TEST_CTSTEST_MODULE, "ctstest");
  installModule(outside, CTS_TEST_CTSTEST_MODULE, "ctstest");

  ts::GrammarSearchPath const grammars{{inside}};

  std::error_code code;
  REQUIRE(std::filesystem::is_regular_file(outside / "ctstest.so", code));
  REQUIRE(std::filesystem::is_regular_file(nested / "ctstest.so", code));
  REQUIRE(ts::loadGrammar(outside / "ctstest.so").has_value());

  std::vector<std::string> const escapes{
    (outside / "ctstest").string(),          // absolute, operator/ drops the left side
    "../confine-outside/ctstest",            // relative traversal
    "nested/ctstest",                        // an interior separator
  };

  for (std::string const& name : escapes) {
    CAPTURE(name);
    CHECK(!grammars.find(name).has_value());

    auto language = grammars.load(name);
    CHECK(!language.has_value());
    if (!language.has_value()) {
      CHECK(language.error().kind == ts::GrammarLoadError::Kind::NotFoundInSearchPath);
    }
  }
}


TEST_CASE("A name that is not a file name at all is not found") {
  auto const directory = makeParserDirectory("confine-degenerate");
  installModule(directory, CTS_TEST_CTSTEST_MODULE, "ctstest");

  ts::GrammarSearchPath const grammars{{directory}};

  for (std::string_view const name : {"", ".", ".."}) {
    CAPTURE(name);
    CHECK(!grammars.find(name).has_value());
    CHECK(!grammars.load(name).has_value());
  }

  CHECK(grammars.find("ctstest").has_value());
}
