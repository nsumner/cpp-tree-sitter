#ifndef CTS_QUERY_QUERY_H
#define CTS_QUERY_QUERY_H

// Compiled tree-sitter queries and their result streams.
//
// This file is ordered by dependency:
//
//   1. Match results
//   2. Compiled queries
//   3. Execution options
//   4. Execution state
//   5. Result views
//   6. Query cursor

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <string_view>
#include <type_traits>
#include <utility>

#include <tree_sitter/api.h>

#include <cts/common.h>
#include <cts/language.h>
#include <cts/node.h>
#include <cts/query/ids.h>
#include <cts/query/predicates.h>


namespace ts {

namespace detail {

[[nodiscard]] inline Error
queryError(TSQueryError kind, uint32_t offset) {
  switch (kind) {
    case TSQueryErrorSyntax:    return Error{.kind=ErrorKind::QuerySyntax,    .offset=offset};
    case TSQueryErrorNodeType:  return Error{.kind=ErrorKind::QueryNodeType,  .offset=offset};
    case TSQueryErrorField:     return Error{.kind=ErrorKind::QueryField,     .offset=offset};
    case TSQueryErrorCapture:   return Error{.kind=ErrorKind::QueryCapture,   .offset=offset};
    case TSQueryErrorStructure: return Error{.kind=ErrorKind::QueryStructure, .offset=offset};
    case TSQueryErrorLanguage:  return Error{.kind=ErrorKind::QueryLanguage,  .offset=offset};
    case TSQueryErrorNone:      break;
  }
  return Error{.kind=ErrorKind::QuerySyntax, .offset=offset};
}

}


/////////////////////////////////////////////////////////////////////////////
// 1. Match results.
/////////////////////////////////////////////////////////////////////////////


// One capture of a query match. `id` indexes the query's capture-name table.
struct QueryCapture {
  Node node;
  CaptureId id;
};


namespace detail {

// Converts tree-sitter's raw captures to ts::QueryCapture.
struct ToQueryCapture {
  static QueryCapture
  operator()(TSQueryCapture const& capture) noexcept {
    return QueryCapture{.node=Node{capture.node}, .id=CaptureId{capture.index}};
  }
};

}


// A borrowed view of one match's captures.
using CaptureRange = detail::MappedRange<TSQueryCapture, detail::ToQueryCapture>;


// One query match. A QueryMatch and its CaptureRange remain valid until the
// cursor advances. Copy out any captures you need to keep.
class QueryMatch {
public:
  explicit QueryMatch(TSQueryMatch match)
    : impl{match}
      { }


  // Match instance id used by ts_query_cursor_remove_match.
  [[nodiscard]] uint32_t getId() const { return impl.id; }

  [[nodiscard]] PatternIndex
  getPatternIndex() const {
    return PatternIndex{impl.pattern_index};
  }

  [[nodiscard]] CaptureRange
  getCaptures() const {
    return detail::makeRange<CaptureRange>(impl.captures, impl.capture_count);
  }


  // The first node captured under `id`, or nullopt when the capture is absent.
  // For a capture under `+` or `*` this returns only the first of several.
  // Filter getCaptures() to inspect every node captured under the same id.
  //
  //   auto all = match.getCaptures()
  //     | std::views::filter([id](ts::QueryCapture c) { return c.id == id; });
  //
  // Query::getCaptureQuantifier tells you whether the capture can repeat.
  [[nodiscard]] std::optional<Node>
  getNodeFor(CaptureId id) const {
    for (QueryCapture const capture : getCaptures()) {
      if (capture.id == id) {
        return capture.node;
      }
    }
    return std::nullopt;
  }

private:
  TSQueryMatch impl;
};


// One result from capture-order iteration.
struct CaptureResult {
  QueryMatch match;

  // Offset into this match's capture array. This is not a CaptureId.
  uint32_t position;

  [[nodiscard]] QueryCapture
  getCapture() const {
    return match.getCaptures()[static_cast<std::ptrdiff_t>(position)];
  }
};


/////////////////////////////////////////////////////////////////////////////
// 2. Compiled queries.
/////////////////////////////////////////////////////////////////////////////


// A compiled query. Execute it with QueryCursor.
class Query {
public:
  // `regex` compiles the query's #match? patterns at construction. Without
  // one, #match? predicates can be inspected or ignored but cannot be
  // evaluated by a cursor.
  [[nodiscard]] static std::expected<Query, Error>
  create(Language language, std::string_view source, const RegexCompiler& regex = {}) {
    uint32_t errorOffset = 0;
    TSQueryError errorType = TSQueryErrorNone;
    char const* buffer = source.data() != nullptr ? source.data() : "";
    TSQuery* rawQuery = ts_query_new(
      language.impl, buffer, static_cast<uint32_t>(source.size()),
      &errorOffset, &errorType);
    if (rawQuery == nullptr) {
      return std::unexpected{detail::queryError(errorType, errorOffset)};
    }

    Query query{rawQuery, language};
    auto program = detail::compilePredicates(query.impl.get(), regex);
    if (!program) {
      return std::unexpected{program.error()};
    }

    query.program = std::move(*program);
    return query;
  }


  // The language this query was compiled against.
  [[nodiscard]] Language
  getLanguage() const {
    return language;
  }


  [[nodiscard]] uint32_t
  getNumPatterns() const {
    return ts_query_pattern_count(impl.get());
  }

  [[nodiscard]] uint32_t
  getNumCaptures() const {
    return ts_query_capture_count(impl.get());
  }

  // Number of entries in the query's predicate-token table.
  [[nodiscard]] uint32_t
  getNumPredicateTokens() const {
    return ts_query_string_count(impl.get());
  }


  // The name a capture id refers to or nullopt for an unknown capture id.
  // The bound check prevents tree-sitter from narrowing a large id and
  // accidentally looking up a different capture.
  [[nodiscard]] std::optional<std::string_view>
  getCaptureNameForId(CaptureId id) const {
    if (std::to_underlying(id) >= getNumCaptures()) {
      return std::nullopt;
    }

    uint32_t length = 0;
    char const* name = ts_query_capture_name_for_id(
      impl.get(), std::to_underlying(id), &length);
    return std::string_view{name, length};
  }


  // The id a capture name refers to, or nullopt when this query has no such
  // capture. Prefer resolving names once before iterating matches.
  [[nodiscard]] std::optional<CaptureId>
  getCaptureId(std::string_view name) const {
    uint32_t const count = getNumCaptures();
    for (uint32_t id = 0; id < count; ++id) {
      if (getCaptureNameForId(CaptureId{id}) == name) {
        return CaptureId{id};
      }
    }
    return std::nullopt;
  }


  // The text of one predicate-token table entry or nullopt for an unknown id.
  // Predicate tokens include predicate names, quoted string literals, and
  // bare symbol arguments.
  [[nodiscard]] std::optional<std::string_view>
  getPredicateToken(PredicateTokenId id) const {
    if (std::to_underlying(id) >= getNumPredicateTokens()) {
      return std::nullopt;
    }
    uint32_t length = 0;
    char const* value = ts_query_string_value_for_id(
      impl.get(), std::to_underlying(id), &length);
    return std::string_view{value, length};
  }


  // The quantifier for `capture` within `pattern` or nullopt for an unknown
  // id. nullopt means the id is unknown. Quantifier::Zero means the capture
  // exists but is not used by this pattern.
  [[nodiscard]] std::optional<Quantifier>
  getCaptureQuantifier(PatternIndex pattern, CaptureId capture) const {
    if (!hasPattern(pattern)) {
      return std::nullopt;
    }

    if (std::to_underlying(capture) >= getNumCaptures()) {
      return std::nullopt;
    }

    return static_cast<Quantifier>(ts_query_capture_quantifier_for_id(
      impl.get(), std::to_underlying(pattern), std::to_underlying(capture)));
  }


  // Where one pattern sits in the query source or nullopt for an unknown
  // pattern.
  [[nodiscard]] std::optional<Extent<uint32_t>>
  getPatternByteRange(PatternIndex pattern) const {
    if (!hasPattern(pattern)) {
      return std::nullopt;
    }

    return Extent<uint32_t>{
      .start=ts_query_start_byte_for_pattern(impl.get(), std::to_underlying(pattern)),
      .end=ts_query_end_byte_for_pattern(impl.get(), std::to_underlying(pattern))};
  }


  // Every predicate of one pattern, in source order. This is an
  // uninterpreted predicate list. Supported text predicates are also
  // evaluated during execution unless PredicateMode::Ignore is used.
  [[nodiscard]] std::optional<PredicateRange>
  getPredicates(PatternIndex pattern) const {
    if (!hasPattern(pattern)) {
      return std::nullopt;
    }

    uint32_t count = 0;
    TSQueryPredicateStep const* steps = ts_query_predicates_for_pattern(
      impl.get(), std::to_underlying(pattern), &count);
    // tree-sitter returns null for an empty predicate list.
    return PredicateRange{
      detail::PredicateIterator{impl.get(), steps, steps + count},
      detail::PredicateIterator{impl.get(), steps + count, steps + count}};
  }


  // True when this query has predicates that need source text to evaluate.
  [[nodiscard]] bool
  hasTextPredicates() const {
    return program.hasText;
  }

  // True when this query has a #match? predicate without a compiled regex.
  [[nodiscard]] bool
  hasUncompiledRegexes() const {
    return program.hasUncompiledRegexes;
  }


  // Whether `match` satisfies every supported text predicate of its pattern.
  // `match` must come from this query.
  //
  // QueryCursor applies this automatically unless PredicateMode::Ignore is
  // set. Call it directly to combine built-in text predicates with custom
  // predicate handling.
  //
  // An uncompiled #match? predicate rejects every node.
  [[nodiscard]] bool
  satisfies(const QueryMatch& match, std::string_view source) const {
    return program.accepts(match.getPatternIndex(), match.getCaptures(), source);
  }


  // Disable a capture or pattern for the rest of this Query's life.
  //
  //   if (auto key = query.getCaptureId("key")) { query.disableCapture(*key); }
  //
  // Call before starting any execution. It is unsafe to mutate a query while
  // a Cursor uses it. Disabled ids stay valid and appear in query metadata.
  void
  disableCapture(CaptureId id) {
    auto name = getCaptureNameForId(id);
    if (!name) {
      return;
    }
    ts_query_disable_capture(
      impl.get(), name->data(), static_cast<uint32_t>(name->size()));
  }

  void
  disablePattern(PatternIndex pattern) {
    ts_query_disable_pattern(impl.get(), std::to_underlying(pattern));
  }


  // Escape hatch to the C API. Query retains ownership.
  // Do not call ts_query_delete on this pointer.
  [[nodiscard]] TSQuery const*
  raw() const {
    return impl.get();
  }

  [[nodiscard]] TSQuery*
  raw() {
    return impl.get();
  }


private:
  Query(TSQuery* query, Language language)
    : impl{query, ts_query_delete}, language{language}
      { }


  [[nodiscard]] bool
  hasPattern(PatternIndex pattern) const {
    return static_cast<uint32_t>(std::to_underlying(pattern)) < getNumPatterns();
  }


  std::unique_ptr<TSQuery, decltype(&ts_query_delete)> impl;
  Language language;
  detail::PredicateProgram program;
};


/////////////////////////////////////////////////////////////////////////////
// 3. Execution options.
/////////////////////////////////////////////////////////////////////////////


// A progress callback returns a ProgressAction to tell a Cursor what to do.
// Cancel ends the execution permanently. Results already produced are a prefix
// of the uncancelled result set.
enum class ProgressAction : uint8_t { Continue, Cancel };


// The owning type of a progress callback. Do not rely on
// std::function-specific APIs.
// TODO: decide on a better wrapper type?
using ProgressCallback = std::function<ProgressAction(uint32_t byteOffset)>;

static_assert(std::copy_constructible<ProgressCallback>);
static_assert(std::invocable<const ProgressCallback&, uint32_t>);
static_assert(std::is_nothrow_default_constructible_v<ProgressCallback>);


// Per-execution configuration for a query cursor. This is the complete
// configuration for one execution. An omitted option resets that setting to
// unrestricted. Unlike base tree-sitter, options do not persist.
//
// Ranges are half-open [start, end) filters. byteRange and pointRange keep
// matches that intersect the range. containingByteRange and
// containingPointRange only keep matches fully contained in the range. All
// active ranges are ANDed.
//
// An extent with start > end is an error. An empty extent is valid.
// {n, n} selects matches straddling offset n, while {0, 0} selects nothing.
struct QueryOptions {
  std::optional<Extent<uint32_t>> byteRange            = std::nullopt;
  std::optional<Extent<Point>>    pointRange           = std::nullopt;
  std::optional<Extent<uint32_t>> containingByteRange  = std::nullopt;
  std::optional<Extent<Point>>    containingPointRange = std::nullopt;

  // 0 visits only the root node.
  uint32_t maxStartDepth = std::numeric_limits<uint32_t>::max();

  // Bounds the number of in-progress capture lists. When exceeded, tree-sitter
  // drops matches and didExceedMatchLimit() reports that results are incomplete.
  std::optional<uint32_t> matchLimit = std::nullopt;

  // Source text used for predicate evaluation. Required when evaluating text
  // predicates. The view must remain valid for the whole execution. It must
  // be the text used to parse the tree. If it is too short,
  // Node::getSourceRange may throw or abort during iteration.
  std::optional<std::string_view> source = std::nullopt;

  // The mode determines whether to evaluate the query's text predicates.
  PredicateMode predicates = PredicateMode::Evaluate;

  // Called periodically during execution. Returning Cancel stops the
  // execution. Check QueryCursor::wasCancelled() after iteration if partial
  // results matter. The callback must not throw. The cursor copies and owns
  // the callback for the query execution.
  ProgressCallback onProgress = {};
};


/////////////////////////////////////////////////////////////////////////////
// 4. Execution state.
/////////////////////////////////////////////////////////////////////////////


namespace detail {

inline constexpr uint32_t maxByte = std::numeric_limits<uint32_t>::max();
inline constexpr Point maxPoint{maxByte, maxByte};


// Whether an extent has its ends in the wrong order.
template <typename T>
[[nodiscard]] constexpr bool
isInverted(std::optional<Extent<T>> const& range) {
  return range && range->start > range->end;
}


// Converts a public range into the range passed to tree-sitter. tree-sitter
// treats an end of 0 as unbounded. This API treats {0, 0} as empty, so it is
// rewritten to {max, max}.
template <typename T>
[[nodiscard]] constexpr Extent<T>
normalizeRange(std::optional<Extent<T>> const& range, T zero, T max) {
  if (!range) {
    return Extent<T>{zero, max};
  }
  if (range->end == zero) {
    return Extent<T>{max, max};
  }
  return *range;
}


// Execution state shared by the cursor and its current result view.
struct ExecState {
  ExecState()
    : cursor{ts_query_cursor_new(), ts_query_cursor_delete}
      { }


  // tree-sitter keeps a pointer to rawOptions for the whole execution.
  std::unique_ptr<TSQueryCursor, decltype(&ts_query_cursor_delete)> cursor;
  ProgressCallback onProgress = {};
  TSQueryCursorOptions rawOptions{};

  // Latched for the whole execution and cleared by starting another one.
  bool cancelled = false;

  // Borrowed for the whole execution.
  Query const* query = nullptr;
  std::string_view source;
  bool evaluate = false;


  [[nodiscard]] bool
  accepts(TSQueryMatch const& match) const {
    if (!evaluate) {
      return true;
    }
    return query->satisfies(QueryMatch{match}, source);
  }


  // Invoked from C. noexcept turns a throwing callback into std::terminate.
  static bool
  onProgressTrampoline(TSQueryCursorState* progress) noexcept {
    auto* self = static_cast<ExecState*>(progress->payload);
    if (self->onProgress(progress->current_byte_offset)
        == ProgressAction::Cancel) {
      self->cancelled = true;
      return true;   // the C API's "true means stop"
    }
    return false;
  }
};


// Step policies describing how one cursor pull produces one result. Each policy
// applies predicate filtering for its stream.
//
// Keep the cancellation checks inside the retry loops. A single pull may skip
// many predicate-rejected matches, and cancellation must stop that work before
// the next tree-sitter call.


struct MatchStep {
  using value_type = QueryMatch;
  using raw_type = TSQueryMatch;

  [[nodiscard]] static bool
  next(ExecState* state, raw_type& out) {
    while (true) {
      if (state->cancelled) {
        return false;
      }
      TSQueryMatch match;
      if (!ts_query_cursor_next_match(state->cursor.get(), &match)) {
        return false;
      }
      if (state->accepts(match)) {
        out = match;
        return true;
      }
    }
  }

  [[nodiscard]] static value_type
  wrap(raw_type const& raw) {
    return QueryMatch{raw};
  }
};


struct CaptureStep {
  using value_type = CaptureResult;

  struct raw_type {
    TSQueryMatch match;
    uint32_t position;
  };

  // Capture-order iteration can evaluate predicates against a partial
  // match, so a capture may be emitted from a match that later fails its
  // predicates. Use getMatches() when predicate filtering must be exact.
  [[nodiscard]] static bool
  next(ExecState* state, raw_type& out) {
    for (;;) {
      if (state->cancelled) {
        return false;
      }
      TSQueryMatch match;
      uint32_t position = 0;
      if (!ts_query_cursor_next_capture(
            state->cursor.get(), &match, &position)) {
        return false;
      }
      if (state->accepts(match)) {
        out = raw_type{.match=match, .position=position};
        return true;
      }
      // Do not remove the whole match here The predicate verdict may be
      // based on a partial capture list.
    }
  }

  [[nodiscard]] static value_type
  wrap(raw_type const& raw) {
    return CaptureResult{.match=QueryMatch{raw.match}, .position=raw.position};
  }
};

}


/////////////////////////////////////////////////////////////////////////////
// 5. Result views.
/////////////////////////////////////////////////////////////////////////////


// Single-pass iterator over query results. The QueryCursor must outlive it.
// The iterator is move-only because it is a handle on one cursor stream.
template <typename Step>
class QueryIterator {
public:
  using value_type = typename Step::value_type;
  using difference_type = std::ptrdiff_t;
  using iterator_concept = std::input_iterator_tag;

  explicit QueryIterator(detail::ExecState* state)
    : state{state} {
    advance();
  }

  QueryIterator(const QueryIterator&) = delete;
  QueryIterator& operator=(const QueryIterator&) = delete;

  QueryIterator(QueryIterator&& other) noexcept
    : state{std::exchange(other.state, nullptr)},
      current{other.current},
      atEnd{other.atEnd}
      { }


  QueryIterator&
  operator=(QueryIterator&& other) noexcept {
    state = std::exchange(other.state, nullptr);
    current = other.current;
    atEnd = other.atEnd;
    return *this;
  }


  // Returns a wrapper around the current raw tree-sitter result.
  [[nodiscard]] value_type operator*() const { return Step::wrap(current); }
  QueryIterator& operator++() { advance(); return *this; }
  void operator++(int) { advance(); }

  friend bool operator==(const QueryIterator& it, std::default_sentinel_t) {
    return it.atEnd;
  }

private:
  void advance() {
    assert(state != nullptr && "using a moved-from iterator");
    atEnd = !Step::next(state, current);
  }


  detail::ExecState* state;
  typename Step::raw_type current{};
  bool atEnd = false;
};

using MatchIterator = QueryIterator<detail::MatchStep>;
using CaptureIterator = QueryIterator<detail::CaptureStep>;


// Single-pass view over one cursor execution. The view borrows its
// QueryCursor. Iterators remain valid if the view object is destroyed, but
// not if the cursor advances or is destroyed. begin() may be called at most
// once.
template <typename Iterator>
class QueryResultsView
  : public std::ranges::view_interface<QueryResultsView<Iterator>> {
public:
  explicit QueryResultsView(detail::ExecState* state)
    : state{state}
      { }

  QueryResultsView(const QueryResultsView&) = delete;
  QueryResultsView& operator=(const QueryResultsView&) = delete;

  QueryResultsView(QueryResultsView&& other) noexcept
    : state{std::exchange(other.state, nullptr)}
      { }


  QueryResultsView&
  operator=(QueryResultsView&& other) noexcept {
    state = std::exchange(other.state, nullptr);
    return *this;
  }


  // Pulling the first result mutates the cursor.
  [[nodiscard]] Iterator
  begin() {
    assert(state != nullptr && "iterating a moved-from view");
    return Iterator{state};
  }

  [[nodiscard]] std::default_sentinel_t end() const { return {}; }


private:
  detail::ExecState* state;
};

using MatchesView = QueryResultsView<MatchIterator>;
using CapturesView = QueryResultsView<CaptureIterator>;

}  // namespace ts


// Iterators borrow the cursor.
template <typename Iterator>
inline constexpr bool
std::ranges::enable_borrowed_range<ts::QueryResultsView<Iterator>> = true;


namespace ts {

namespace detail {

// Shared compile-time contract for both result streams.
template <typename Iterator, typename View>
consteval bool
isSinglePassResultStream() {
  static_assert(std::input_iterator<Iterator>);
  static_assert(std::sentinel_for<std::default_sentinel_t, Iterator>);
  static_assert(!std::copyable<Iterator>);
  static_assert(std::movable<Iterator>);
  static_assert(!std::is_trivially_move_constructible_v<Iterator>);
  static_assert(!std::is_trivially_move_assignable_v<Iterator>);

  static_assert(std::ranges::input_range<View>);
  static_assert(std::ranges::view<View>);
  static_assert(std::ranges::borrowed_range<View>);
  static_assert(!std::copyable<View>);
  static_assert(std::movable<View>);
  static_assert(!std::is_trivially_move_constructible_v<View>);
  static_assert(!std::is_trivially_move_assignable_v<View>);

  // Iterating mutates the cursor.
  static_assert(!std::ranges::range<const View>);
  return true;
}

}

static_assert(detail::isSinglePassResultStream<MatchIterator, MatchesView>());
static_assert(detail::isSinglePassResultStream<CaptureIterator, CapturesView>());

static_assert(!std::copyable<std::expected<MatchesView, Error>>);
static_assert(std::movable<std::expected<MatchesView, Error>>);


/////////////////////////////////////////////////////////////////////////////
// 6. The query cursor.
/////////////////////////////////////////////////////////////////////////////


// A QueryCursor owns query execution state. Pass QueryOptions to each
// execution. This wrapper resets every tree-sitter cursor setting each time,
// so omitted options are unrestricted. Each call to getMatches or
// getCaptureStream starts a fresh execution.
class QueryCursor {
public:
  QueryCursor() = default;

  // Runs `query` over the subtree rooted at `node` and yields matches.
  // Fails if the query and node use different languages, predicates cannot be
  // evaluated under the selected options, or any range is inverted. On failure,
  // the current cursor execution is left untouched.
  // The returned view borrows both this cursor and the query.
  // Starting another execution invalidates the existing views and iterators
  // from this cursor.
  [[nodiscard]] std::expected<MatchesView, Error>
  getMatches(const Query& query, Node node, const QueryOptions& options = {}) {
    return execute<MatchesView>(query, node, options);
  }


  // Like getMatches, but yields captures in source order instead of grouping
  // them by match. Predicate filtering may be in-flight for this stream.
  // Use getMatches when results depend on predicates.
  [[nodiscard]] std::expected<CapturesView, Error>
  getCaptureStream(const Query& query, Node node,
                   const QueryOptions& options = {}) {
    return execute<CapturesView>(query, node, options);
  }


  // True when the current execution was cancelled. Cancellation means the
  // result set is a prefix of the uncancelled result set.
  [[nodiscard]] bool
  wasCancelled() const {
    assert(state != nullptr && "using a moved-from cursor");
    return state->cancelled;
  }


  // True when the current execution exceeded matchLimit and results may be
  // incomplete.
  [[nodiscard]] bool
  didExceedMatchLimit() const {
    assert(state != nullptr && "using a moved-from cursor");
    return ts_query_cursor_did_exceed_match_limit(state->cursor.get());
  }


  // Drops a match from the current execution. `match` must come from this
  // cursor's current execution. Removing it also invalidates that match's
  // captures, so any needed information must be copied first.
  void
  removeMatch(const QueryMatch& match) {
    assert(state != nullptr && "using a moved-from cursor");
    ts_query_cursor_remove_match(state->cursor.get(), match.getId());
  }


  // Escape hatch to the C API. QueryCursor retains ownership. Do not call
  // ts_query_cursor_delete on this pointer. Calling ts_query_cursor_exec
  // directly can lead to inconsistent wrapper state.
  [[nodiscard]] TSQueryCursor const*
  raw() const {
    assert(state != nullptr && "using a moved-from cursor");
    return state->cursor.get();
  }

  [[nodiscard]] TSQueryCursor*
  raw() {
    assert(state != nullptr && "using a moved-from cursor");
    return state->cursor.get();
  }


private:
  // Shared path behind getMatches and getCaptureStream.
  template <typename View>
  [[nodiscard]] std::expected<View, Error>
  execute(const Query& query, Node node, const QueryOptions& options) {
    assert(state != nullptr && "using a moved-from cursor");
    return checkLanguage(query, node)
      .and_then([&] { return checkPredicates(query, options); })
      .and_then([&] { return checkRanges(options); })
      .transform([&] {
        applyOptions(options);
        start(query, node, options);
        return View{state.get()};
      });
  }


  // Preflight checks. A rejected call leaves the current execution untouched.

  [[nodiscard]] static std::expected<void, Error>
  checkLanguage(const Query& query, Node node) {
    if (query.getLanguage() != node.getLanguage()) {
      return std::unexpected{Error{.kind=ErrorKind::QueryNodeLanguage}};
    }
    return {};
  }


  [[nodiscard]] static std::expected<void, Error>
  checkPredicates(const Query& query, const QueryOptions& options) {
    if (options.predicates != PredicateMode::Evaluate) {
      return {};
    }
    if (query.hasTextPredicates() && !options.source) {
      return std::unexpected{Error{.kind=ErrorKind::QueryPredicatesNeedSource}};
    }
    if (query.hasUncompiledRegexes()) {
      return std::unexpected{Error{.kind=ErrorKind::QueryPredicatesNeedRegex}};
    }
    return {};
  }


  [[nodiscard]] static std::expected<void, Error>
  checkRanges(const QueryOptions& options) {
    bool const inverted = detail::isInverted(options.byteRange)
                       || detail::isInverted(options.pointRange)
                       || detail::isInverted(options.containingByteRange)
                       || detail::isInverted(options.containingPointRange);
    if (inverted) {
      return std::unexpected{Error{.kind=ErrorKind::QueryInvalidRange}};
    }
    return {};
  }


  // Writes the full cursor configuration for this execution.
  void
  applyOptions(const QueryOptions& options) {
    applyRange(ts_query_cursor_set_byte_range,
               options.byteRange, uint32_t{0}, detail::maxByte);
    applyRange(ts_query_cursor_set_point_range,
               options.pointRange, Point{0, 0}, detail::maxPoint);
    applyRange(ts_query_cursor_set_containing_byte_range,
               options.containingByteRange, uint32_t{0}, detail::maxByte);
    applyRange(ts_query_cursor_set_containing_point_range,
               options.containingPointRange, Point{0, 0}, detail::maxPoint);

    TSQueryCursor* cursor = state->cursor.get();
    ts_query_cursor_set_max_start_depth(cursor, options.maxStartDepth);
    ts_query_cursor_set_match_limit(
      cursor, options.matchLimit.value_or(detail::maxByte));
  }


  // Applies one range restriction after public ranges have been normalized.
  template <typename T, typename Set>
  void
  applyRange(Set set, std::optional<Extent<T>> const& range, T zero, T max) {
    Extent<T> const extent = detail::normalizeRange(range, zero, max);
    set(state->cursor.get(), extent.start, extent.end);
  }


  // Starts the execution.
  void
  start(const Query& query, Node node, const QueryOptions& options) {
    state->onProgress = options.onProgress;
    state->cancelled = false;
    state->query = &query;
    state->source = options.source.value_or(std::string_view{});
    state->evaluate = options.predicates == PredicateMode::Evaluate
                      && query.hasTextPredicates();

    if (state->onProgress) {
      state->rawOptions = TSQueryCursorOptions{
        state.get(), &detail::ExecState::onProgressTrampoline};
      ts_query_cursor_exec_with_options(
        state->cursor.get(), query.raw(), node.impl, &state->rawOptions);
    } else {
      ts_query_cursor_exec(state->cursor.get(), query.raw(), node.impl);
    }
  }

  std::unique_ptr<detail::ExecState> state =
    std::make_unique<detail::ExecState>();
};

}

#endif
