// Read query_demo.cpp first.
//
// This example demonstrates controlling how a query runs, restricting what
// it searches, stopping one that is taking too long, reading captures in
// source order, and switching off parts of a query.
//
// query_demo.cpp covers the basics of compiling a query, turning capture
// names into ids, and reading matches back.
//
//
// Options apply to a single query execution
// ==========================================================================
// QueryOptions defines the properties for one full query execution. This
// differs from baseline tree-sitter, where the options persist for a cursor.
// Any option you do not define takes its default setting, so
// cursor.getMatches(query, node) searches the entire subtree regardless of
// what the previous call did.
//
// Four options restrict a match by where it is in the file based on Ranges.
// Ranges are half-open, so {0, 10} covers bytes 0 through 9. byteRange and
// pointRange keep any match that overlaps the range at all, and you get the
// whole match back even when only part of it was inside. containingByteRange
// and containingPointRange are stricter and keep only matches entirely
// within the range. Ranges constraints intersect, so a byte range and a
// point range over different parts of the file return nothing. A range whose
// start is past its end is an error. An empty range like {n, n} selects
// matches that straddle offset n.
//
// maxStartDepth limits how deep a match is allowed to start, counting from the
// node you ran the query against. A value of 0 means a match may only start at
// that node itself.
//
// matchLimit caps how many matches the cursor can have half-built at a time,
// not how many matches an execution returns. A match releases its slot as
// soon as it completes. If the limit is hit, tree-sitter discards the
// in-progress match that started earliest, and calling didExceedMatchLimit()
// afterwards shows whether any in-progress matches were lost.
//
//
// Stopping a long query
// ==========================================================================
// When you pass an onProgress callback, tree-sitter calls it every so often
// while the query runs, and returning ProgressAction::Cancel ends that
// execution. What you have already received is the front of the full result
// set in order. There is no way to resume.
//
// A cancelled query loop ends normally, so call wasCancelled() whenever you
// supply onProgress. Because it is a C callback, it must not throw.
//
//
// Reading captures instead of matches
// ==========================================================================
// getCaptureStream yields captures one at a time in source order across all
// patterns (not grouped by the match they belong to).
//
// If your query uses predicates, the capture stream can yield a capture from
// a match that is still being assembled, so a predicate referring to a
// capture recorded later in the match gets tested against an incomplete
// match. When results depend on predicates, prefer getMatches.
//
//
// Results are single pass
// ==========================================================================
// A QueryMatch and the nodes and text extracted stay valid until the cursor
// moves on. tree-sitter reuses one internal buffer for captures, so results
// must be copied.
//
// Query, QueryCursor, the views they return, and the iterators over those
// views are all move-only because copying one would leave two handles onto a
// stream that can only be read once.
//
// Dereferencing a query iterator gives you a value rather than a reference,
// so bind it with const&, auto&&, or by value. `for (auto& match : matches)`
// will not compile.
//
// A cursor runs one query at a time. Calling getMatches or getCaptureStream
// again restarts it, invalidating iterators. Use separate cursors for
// concurrent queries.

#include <cstdint>
#include <cstdlib>
#include <print>
#include <string>
#include <string_view>
#include <vector>

#include <cpp-tree-sitter.h>
#include <cts/query.h>
// Formatting for query types. It includes <cts/format.h> itself.
#include <cts/query/format.h>

extern "C" TSLanguage* tree_sitter_json();


namespace {

// Pausing only shows up on a document large enough for tree-sitter to call the
// progress hook, so build one.
std::string
makeDocument(int pairs) {
  std::string document = "[";
  for (int i = 0; i < pairs; ++i) {
    if (i != 0) {
      document += ", ";
    }
    document += R"({"k": )" + std::to_string(i) + "}";
  }
  document += "]";
  return document;
}

}


int
main() {
  auto parser = ts::Parser::create(tree_sitter_json());
  if (!parser) {
    std::println(stderr, "error: {}", parser.error());
    return EXIT_FAILURE;
  }

  const std::string document = makeDocument(500);
  const std::string_view source = document;
  auto tree = parser->parse(source);
  if (!tree) {
    std::println(stderr, "error: {}", tree.error());
    return EXIT_FAILURE;
  }
  ts::Node const root = tree->getRootNode();

  auto query = ts::Query::create(
    tree_sitter_json(),
    "(pair key: (string) @key value: (_) @value)");
  if (!query) {
    std::println(stderr, "error: {}", query.error());
    return EXIT_FAILURE;
  }

  auto key = query->getCaptureId("key");
  if (!key) {
    std::println(stderr, "error: query has no @key capture");
    return EXIT_FAILURE;
  }

  if (auto quantifier = query->getCaptureQuantifier(ts::PatternIndex{0}, *key)) {
    std::println("@key is quantified as {}", *quantifier);
  }

  // getNodeFor returns the first node captured under an id or nullopt when
  // the match captured none. The quantifier tells you about other captures:
  // * under `?` a nullopt is a real answer
  // * under `+` or `*` there may be more nodes that only a filter over
  //   getCaptures() will reach.
  auto keyText = [&](const ts::QueryMatch& match) {
    auto node = match.getNodeFor(*key);
    return node ? std::string{node->getSourceRange(source)} : std::string{};
  };

  ts::QueryCursor cursor;


  ///////////////////////////////////////////////////////////////////////
  // Restricting one execution
  ///////////////////////////////////////////////////////////////////////

  auto all = cursor.getMatches(*query, root);
  if (!all) {
    std::println(stderr, "error: {}", all.error());
    return EXIT_FAILURE;
  }
  int total = 0;
  for ([[maybe_unused]] const ts::QueryMatch& match : *all) {
    ++total;
  }
  std::println("pairs in the whole document: {}", total);

  // Restrict the next execution to the first object. The unrestricted run
  // above is unaffected by this one, and a later unrestricted run would be
  // too: options do not accumulate on the cursor.
  ts::Node const array = *root.getNamedChild(0);
  ts::Node const firstObject = *array.getNamedChild(0);

  auto restricted = cursor.getMatches(*query, root, {
    .byteRange = firstObject.getByteRange(),
    .matchLimit = 8,
  });
  if (!restricted) {
    std::println(stderr, "error: {}", restricted.error());
    return EXIT_FAILURE;
  }
  for (const ts::QueryMatch& match : *restricted) {
    std::println("first object holds key {}", keyText(match));
  }
  // Check whether the limit was exceeded when you set a limit to avoid
  // silent drops.
  if (cursor.didExceedMatchLimit()) {
    std::println("match limit exceeded: those results are incomplete");
  }


  ///////////////////////////////////////////////////////////////////////
  // Cancelling a long query
  ///////////////////////////////////////////////////////////////////////

  int ticks = 0;
  ts::QueryOptions const bounded{
    .onProgress = [&ticks](uint32_t) {
      // Give up after a fixed amount of work (or after a deadline)
      return ++ticks >= 4 ? ts::ProgressAction::Cancel
                          : ts::ProgressAction::Continue;
    },
  };

  std::vector<std::string> keys;
  auto bounded_matches = cursor.getMatches(*query, root, bounded);
  if (!bounded_matches) {
    std::println(stderr, "error: {}", bounded_matches.error());
    return EXIT_FAILURE;
  }
  for (const ts::QueryMatch& match : *bounded_matches) {
    // Copy out because it is invalidated when the cursor advances.
    keys.push_back(keyText(match));
  }

  // We can check to see if the query ended because it was cancelled.
  std::println("collected {} keys ({})", keys.size(),
               cursor.wasCancelled() ? "cancelled early, partial results"
                                     : "complete");


  ///////////////////////////////////////////////////////////////////////
  // Capture-ordered iteration
  ///////////////////////////////////////////////////////////////////////

  // This restarts the cursor. The old iterators and views are invalid.
  auto captures = cursor.getCaptureStream(*query, firstObject);
  if (!captures) {
    std::println(stderr, "error: {}", captures.error());
    return EXIT_FAILURE;
  }
  for (const ts::CaptureResult& result : *captures) {
    ts::QueryCapture capture = result.getCapture();
    std::println("capture id {} covers {}", capture.id,
                 capture.node.getSourceRange(source));
  }


  ///////////////////////////////////////////////////////////////////////
  // Narrowing a query permanently
  ///////////////////////////////////////////////////////////////////////

  // disableCapture and disablePattern have no undo and mutate what a
  // running cursor reads, so neither should be called while an execution
  // is in flight. Ideally, set them right after Query creation.
  auto narrowed = ts::Query::create(
    tree_sitter_json(),
    "(pair key: (string) @key value: (_) @value)");
  if (!narrowed) {
    std::println(stderr, "error: {}", narrowed.error());
    return EXIT_FAILURE;
  }
  if (auto value = narrowed->getCaptureId("value")) {
    narrowed->disableCapture(*value);
  }

  // Nothing is renumbered or removed. It just stops appearing in matches.
  // So in this case, two captures per match become one.
  ts::QueryCursor narrowCursor;
  auto narrowMatches = narrowCursor.getMatches(*narrowed, firstObject);
  if (!narrowMatches) {
    std::println(stderr, "error: {}", narrowMatches.error());
    return EXIT_FAILURE;
  }
  for (const ts::QueryMatch& match : *narrowMatches) {
    std::println("captures per match after disabling @value: {}",
                 match.getCaptures().size());
  }
  return EXIT_SUCCESS;
}
