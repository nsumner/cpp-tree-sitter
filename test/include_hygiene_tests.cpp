// This file's include list is the test. Only <cpp-tree-sitter.h> and
// <cts/format.h> may appear below, and neither may pull in the query
// subsystem, so a parse-only or formatting-only caller pays nothing for
// queries. Both macros are checked, so a leak through either the umbrella
// (cts/query.h) or the subsystem header (cts/query/query.h) is caught.
#include <cpp-tree-sitter.h>
#include <cts/format.h>

#if defined(CTS_QUERY_H) || defined(CTS_QUERY_QUERY_H)
#  error "Neither <cpp-tree-sitter.h> nor <cts/format.h> may include the query subsystem"
#endif

// The loader follows the same rule, plus one of its own. Its formatter
// specializations are opt-in, so <cts/loader.h> must not drag them in behind
// the caller's back.
//
// The guard checks CTS_LOADER_FORMAT_H rather than <format> itself, because
// <cts/loader.h> includes <filesystem>, which on libstdc++ pulls in <format>
// on its own to supply std::formatter<std::filesystem::path>.
#include <cts/loader.h>

#if defined(CTS_QUERY_H) || defined(CTS_QUERY_QUERY_H)
#  error "<cts/loader.h> may not include the query subsystem"
#endif
#if defined(CTS_LOADER_FORMAT_H)
#  error "<cts/loader.h> may not include the loader formatters"
#endif

#include <doctest/doctest.h>

extern "C" TSLanguage* tree_sitter_json();


TEST_CASE("The core umbrella alone is enough to parse and navigate") {
  auto parser = ts::Parser::create(tree_sitter_json());
  REQUIRE(parser.has_value());
  auto tree = parser->parse(R"({"a": 1})");
  REQUIRE(tree.has_value());
  CHECK(tree->getRootNode().getNumChildren() == 1);
}
