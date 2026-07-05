// Running a tree-sitter query and reading its matches.
// ==========================================================================
// The query subsystem is opt-in. Include <cts/query.h> to use it and
// <cts/query/format.h> if you want to format query types. Advanced controls
// for how a query runs are covered in query_advanced_demo.cpp.
//
//
// Work with capture ids rather than names
// ==========================================================================
// A query identifies each capture with an integer. Call
// query->getCaptureId(name) before you start iterating results to compare
// ids inside the loop. getCaptureNameForId does the reverse, but costs a
// string comparison in a loop.
//
// getNodeFor returns the first node a match captured under an id or
// nullopt. Quantifiers affect inperpreting captures. A capture with `?` can
// indicate no capture. Under `+` or `*` a match can hold several nodes for
// one id and getNodeFor returns the first, so filter over all of the
// captures to reach others.
//
//   auto all = match.getCaptures()
//     | std::views::filter([&](ts::QueryCapture c) { return c.id == *key; });
//
//
// Predicates are evaluated for you
// ==========================================================================
// #eq?, #not-eq?, #any-of?, #match?, and their any- and not- variants all act
// as filters, so getMatches yields only the matches that satisfy them.
//
// These predicates compare source text, but the library does not keep a copy
// of the source. You have to provide it on each execution. That is what
// {.source = source} does in the call below. Running a query that has text
// predicates without giving it the source is an error. If you want matches
// unfiltered, you can ignore predicates:
//
//   cursor.getMatches(*query, root, {.predicates = ts::PredicateMode::Ignore});
//
// #match? needs a regular expression engine. You can supply one to
// Query::create and each #match? pattern is compiled once at that point. An
// invalid pattern fails at creation instead of partway through an iteration.
// A query containing #match? still builds without a regex engine, and you
// can inspect it or run it under PredicateMode::Ignore, but executing it
// with predicates enabled fails with QueryPredicatesNeedRegex. Whatever
// matcher you return must be threadsafe if you run queries across threads.
//
// When an any- predicate covers a capture with no bound nodes, this library
// calls evaluates it as false where tree-sitter calls it true.
// TODO: Re-evaluate.
//
// Other predicates like #set! or #is? or something a query file invents,
// compile fine, filter nothing, are not an error, and can be read back.
//
//   for (ts::Predicate predicate : query->getPredicates(pattern).value()) {
//     if (predicate.name == "set!") { /* CaptureId or PredicateTokenId args */ }
//   }
//
//
// Lifetime
// ==========================================================================
// A match and the captures you get out of it stay valid only until the
// cursor moves on. tree-sitter reuses one internal buffer for captures, so
// copy out anything you want to keep.

#include <cstdlib>
#include <optional>
#include <print>
#include <regex>
#include <string_view>

#include <cpp-tree-sitter.h>
#include <cts/query.h>
#include <cts/format.h>

extern "C" TSLanguage* tree_sitter_json();

int
main() {
  auto parser = ts::Parser::create(tree_sitter_json());
  if (!parser) {
    std::println(stderr, "error: {}", parser.error());
    return EXIT_FAILURE;
  }

  constexpr std::string_view source = R"({"name": "cpp-tree-sitter", "stars": 100})";
  auto tree = parser->parse(source);
  if (!tree) {
    std::println(stderr, "error: {}", tree.error());
    return EXIT_FAILURE;
  }

  // The library ships no regex engine. Supply one and it is compiled once per
  // distinct #match? pattern, at create, so an invalid pattern is an error
  // here rather than a surprise later.
  auto query = ts::Query::create(
    tree_sitter_json(),
    R"(((pair key: (string) @key value: (_) @value)
        (#not-eq? @key "\"stars\"")
        (#match? @key "^\"[a-z]")))",
    [](std::string_view pattern) -> std::optional<ts::RegexMatcher> {
      try {
        return ts::RegexMatcher{
          [expression = std::regex{std::string{pattern}}](std::string_view text) {
            return std::regex_search(text.begin(), text.end(), expression);
          }};
      } catch (const std::regex_error&) {
        return std::nullopt;
      }
    });
  if (!query) {
    std::println(stderr, "error: {}", query.error());
    return EXIT_FAILURE;
  }

  // Resolve capture names to ids once here rather than comparing strings
  // inside the loop below.
  auto key = query->getCaptureId("key");
  auto value = query->getCaptureId("value");
  if (!key || !value) {
    std::println(stderr, "error: query is missing an expected capture");
    return EXIT_FAILURE;
  }

  // This query has text predicates, so evaluating them needs the source the
  // tree was parsed from. Leaving it out here would be an error rather than an
  // unfiltered result set.
  ts::QueryCursor cursor;
  auto matches = cursor.getMatches(*query, tree->getRootNode(),
                                   {.source = source});
  if (!matches) {
    std::println(stderr, "error: {}", matches.error());
    return EXIT_FAILURE;
  }
  for (const ts::QueryMatch& match : *matches) {
    auto keyNode = match.getNodeFor(*key);
    auto valueNode = match.getNodeFor(*value);
    if (keyNode && valueNode) {
      std::println("{} = {}",
                   keyNode->getSourceRange(source),
                   valueNode->getSourceRange(source));
    }
  }
  return EXIT_SUCCESS;
}
