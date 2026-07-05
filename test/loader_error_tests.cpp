#include <set>
#include <string>
#include <string_view>

#include <doctest/doctest.h>
#include <cts/loader.h>
#include <cts/loader/format.h>


TEST_CASE("Every load error kind has its own message") {
  using Kind = ts::GrammarLoadError::Kind;
  constexpr Kind kinds[]{
    Kind::FileMissing,   Kind::LibraryOpenFailed,   Kind::EntryPointMissing,
    Kind::LanguageNull,  Kind::AbiIncompatible,     Kind::NotFoundInSearchPath,
  };

  std::set<std::string_view> messages;
  for (Kind kind : kinds) {
    ts::GrammarLoadError const error{
      .kind = kind, .path = "/p/json.so", .detail = ""};
    CAPTURE(kind);
    CHECK(!error.message().empty());
    messages.insert(error.message());
  }
  CHECK(messages.size() == std::size(kinds));
}


TEST_CASE("Formatting a load error names the path and the detail") {
  ts::GrammarLoadError const error{
    .kind = ts::GrammarLoadError::Kind::LibraryOpenFailed,
    .path = "/parsers/json.so",
    .detail = "wrong ELF class",
  };

  std::string const text = std::format("{}", error);
  CHECK(text.find("/parsers/json.so") != std::string::npos);
  CHECK(text.find("wrong ELF class") != std::string::npos);
}


TEST_CASE("Formatting tolerates an absent detail") {
  ts::GrammarLoadError const error{
    .kind = ts::GrammarLoadError::Kind::FileMissing,
    .path = "/parsers/json.so",
    .detail = "",
  };

  std::string const text = std::format("{}", error);
  CHECK(text.find('(') == std::string::npos);
  CHECK(text.find("/parsers/json.so") != std::string::npos);
}


TEST_CASE("A load error kind formats as its own name") {
  CHECK(std::format("{}", ts::GrammarLoadError::Kind::LanguageNull) == "LanguageNull");
}


static_assert(ts::abiIsSupported(TREE_SITTER_LANGUAGE_VERSION));
static_assert(ts::abiIsSupported(TREE_SITTER_MIN_COMPATIBLE_LANGUAGE_VERSION));
static_assert(!ts::abiIsSupported(TREE_SITTER_LANGUAGE_VERSION + 1));
static_assert(!ts::abiIsSupported(TREE_SITTER_MIN_COMPATIBLE_LANGUAGE_VERSION - 1));

static_assert(!ts::abiIsSupported(12),
  "the oldabi fixture's ABI must stay below the supported range");
