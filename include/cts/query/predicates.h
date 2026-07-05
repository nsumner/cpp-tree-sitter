#ifndef CTS_QUERY_PREDICATES_H
#define CTS_QUERY_PREDICATES_H

// Query predicates have two representations. Predicate, PredicateArg, and
// PredicateRange expose the predicates attached to a pattern as tree-sitter
// stores them. PredicateProgram is the internal form used to evaluate the
// supported text predicates during query execution.
//
// query.h includes this file, so the internal code below avoids depending on
// Query, QueryCursor, or the public result types.

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <expected>
#include <functional>
#include <iterator>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <tree_sitter/api.h>

#include <cts/common.h>
#include <cts/node.h>
#include <cts/query/ids.h>

namespace ts {

/////////////////////////////////////////////////////////////////////////////
// Layer 1: what a pattern declares.
/////////////////////////////////////////////////////////////////////////////


// A predicate argument is either
// 1) a capture or
// 2) an entry in tree-sitter's predicate-token table.
// The token table contains both quoted literals and bare symbols, such as
// the two arguments of (#set! foo bar).
using PredicateArg = std::variant<CaptureId, PredicateTokenId>;


namespace detail {

[[nodiscard]] inline std::string_view
tokenText(TSQuery const* query, uint32_t id) {
  uint32_t length = 0;
  char const* text = ts_query_string_value_for_id(query, id, &length);
  return std::string_view{text, length};
}


// Converts tree-sitter's raw step into the public argument type.
struct ToPredicateArg {
  static PredicateArg
  operator()(TSQueryPredicateStep const& step) noexcept {
    return step.type == TSQueryPredicateStepTypeCapture
      ? PredicateArg{CaptureId{step.value_id}}
      : PredicateArg{PredicateTokenId{step.value_id}};
  }
};


// Returns the Done step that ends this predicate, or `last` if none is present.
[[nodiscard]] inline TSQueryPredicateStep const*
findPredicateEnd(TSQueryPredicateStep const* first,
                 TSQueryPredicateStep const* last) noexcept {
  while (first != last && first->type != TSQueryPredicateStepTypeDone) {
    ++first;
  }
  return first;
}

}


// A borrowed view of one predicate's arguments.
using PredicateArgRange =
  detail::MappedRange<TSQueryPredicateStep, detail::ToPredicateArg>;


struct Predicate {
  // The leading '#' is stripped; suffixes such as '?' and '!' are kept.
  std::string_view name;
  PredicateArgRange args;
};


namespace detail {

// Iterates over predicates in tree-sitter's flat, sentinel-delimited array.
class PredicateIterator {
public:
  using value_type = Predicate;
  using difference_type = std::ptrdiff_t;
  using iterator_concept = std::forward_iterator_tag;

  PredicateIterator() = default;

  PredicateIterator(TSQuery const* query,
                    TSQueryPredicateStep const* name,
                    TSQueryPredicateStep const* last) noexcept
    : query{query}, name{name}, done{findPredicateEnd(name, last)}, last{last}
      { }


  [[nodiscard]] Predicate
  operator*() const {
    return Predicate{
      .name=tokenText(query, name->value_id),
      .args=makeRange<PredicateArgRange>(
        name + 1, static_cast<uint32_t>(done - (name + 1)))};
  }

  PredicateIterator&
  operator++() noexcept {
    // Clamp to the end defensively for symmetry.
    name = done == last ? last : done + 1;
    done = findPredicateEnd(name, last);
    return *this;
  }

  PredicateIterator
  operator++(int) noexcept {
    PredicateIterator copy = *this;
    ++*this;
    return copy;
  }


  friend bool
  operator==(PredicateIterator lhs, PredicateIterator rhs) noexcept {
    return lhs.name == rhs.name;
  }

private:
  TSQuery const* query = nullptr;
  TSQueryPredicateStep const* name = nullptr;
  TSQueryPredicateStep const* done = nullptr;
  TSQueryPredicateStep const* last = nullptr;
};

}


// A borrowed view of a pattern's predicates.
using PredicateRange = std::ranges::subrange<detail::PredicateIterator>;

static_assert(std::forward_iterator<detail::PredicateIterator>);
static_assert(std::ranges::forward_range<PredicateRange>);
static_assert(std::ranges::borrowed_range<PredicateRange>);
static_assert(!std::ranges::sized_range<PredicateRange>);

static_assert(std::random_access_iterator<
                std::ranges::iterator_t<PredicateArgRange>>);
static_assert(std::ranges::random_access_range<PredicateArgRange>);
static_assert(std::ranges::sized_range<PredicateArgRange>);
static_assert(std::ranges::borrowed_range<PredicateArgRange>);


/////////////////////////////////////////////////////////////////////////////
// Injected regex support. This library does not provide a regex engine.
// Callers that want #match? predicates must provide one.
/////////////////////////////////////////////////////////////////////////////


// One compiled #match? pattern. Matchers may be called concurrently through
// separate cursors sharing the same Query, so captured state must be safe
// for concurrent uses if they occur. Exceptions propagate out of the
// enclosing query operation.
using RegexMatcher = std::function<bool(std::string_view text)>;

// Compiles one #match? pattern. Returning nullopt makes Query::create fail
// with ErrorKind::QueryPredicateRegex.
using RegexCompiler =
  std::function<std::optional<RegexMatcher>(std::string_view pattern)>;


// Whether an execution evaluates the query's text predicates.
//
// Ignore returns unfiltered structural matches. Use it when handling predicates
// yourself through Query::getPredicates, or when no source text is available.
//
// With QueryCursor::getCaptureStream, Evaluate can see a partial match because
// tree-sitter may return captures before the whole match is assembled. Use
// getMatches when results depend on predicate filtering.
enum class PredicateMode : uint8_t { Evaluate, Ignore };


/////////////////////////////////////////////////////////////////////////////
// Layer 2: the compiled program. Everything below is for internal use.
/////////////////////////////////////////////////////////////////////////////


namespace detail {

// A compiled text predicate.
struct TextPredicate {
  enum class Op : uint8_t { EqualCapture, EqualToken, Match, AnyOf };

  Op        op           = Op::EqualToken;
  bool      positive     = true;   // false for not- forms
  bool      matchAll     = true;   // false for any- forms. AnyOf ignores it
  CaptureId capture      = CaptureId{0};
  CaptureId otherCapture = CaptureId{0};   // EqualCapture only

  // For EqualToken and AnyOf this indexes PredicateProgram::operands.
  // For Match it indexes PredicateProgram::matchers.
  uint32_t  operandStart = 0;
  uint32_t  operandCount = 0;
};


// Compiled text predicates for every pattern.
// patternStarts has numPatterns + 1 offsets into `predicates`
// pattern i owns [patternStarts[i], patternStarts[i + 1]).
struct PredicateProgram {
  std::vector<TextPredicate>    predicates;
  std::vector<uint32_t>         patternStarts;
  std::vector<std::string_view> operands;
  std::vector<RegexMatcher>     matchers;

  bool hasText              = false;
  bool hasUncompiledRegexes = false;


  // Literals tested by an EqualToken or AnyOf predicate.
  [[nodiscard]] std::span<std::string_view const>
  operandsOf(TextPredicate const& predicate) const {
    return std::span{operands}.subspan(predicate.operandStart,
                                       predicate.operandCount);
  }


  // Whether a Match predicate's regex accepts `text`. A missing matcher rejects.
  [[nodiscard]] bool
  matches(TextPredicate const& predicate, std::string_view text) const {
    if (predicate.operandCount == 0) {
      return false;
    }
    return matchers[predicate.operandStart](text);
  }


  // Whether every text predicate of `pattern` holds for `captures`.
  template <std::ranges::random_access_range Captures>
  [[nodiscard]] bool
  accepts(PatternIndex pattern, Captures const& captures,
          std::string_view source) const;
};


/////////////////////////////////////////////////////////////////////////////
// Compiling predicates of a query.
/////////////////////////////////////////////////////////////////////////////


// The properties determined by a supported predicate name. The eq family
// starts as EqualToken and becomes EqualCapture later if the second argument
// is a capture.
struct PredicateShape {
  TextPredicate::Op op = TextPredicate::Op::EqualToken;
  bool positive        = true;
  bool matchAll        = true;
};


// The predicate names this wrapper evaluates. Unknown names are ignored.
[[nodiscard]] constexpr std::optional<PredicateShape>
classifyPredicate(std::string_view name) {
  using Op = TextPredicate::Op;
  struct Entry { std::string_view name; PredicateShape shape; };
  static constexpr Entry table[] = {
    {.name="eq?",            .shape={.op=Op::EqualToken, .positive=true,  .matchAll=true }},
    {.name="not-eq?",        .shape={.op=Op::EqualToken, .positive=false, .matchAll=true }},
    {.name="any-eq?",        .shape={.op=Op::EqualToken, .positive=true,  .matchAll=false}},
    {.name="any-not-eq?",    .shape={.op=Op::EqualToken, .positive=false, .matchAll=false}},
    {.name="match?",         .shape={.op=Op::Match,      .positive=true,  .matchAll=true }},
    {.name="not-match?",     .shape={.op=Op::Match,      .positive=false, .matchAll=true }},
    {.name="any-match?",     .shape={.op=Op::Match,      .positive=true,  .matchAll=false}},
    {.name="any-not-match?", .shape={.op=Op::Match,      .positive=false, .matchAll=false}},
    {.name="any-of?",        .shape={.op=Op::AnyOf,      .positive=true,  .matchAll=true }},
    {.name="not-any-of?",    .shape={.op=Op::AnyOf,      .positive=false, .matchAll=true }},
  };

  auto const found = std::ranges::find(table, name, &Entry::name);
  if (found == std::ranges::end(table)) {
    return std::nullopt;
  }
  return found->shape;
}


// Builds a PredicateProgram for a Query.
class PredicateCompiler {
public:
  PredicateCompiler(TSQuery const* query, RegexCompiler const& regex)
    : query{query}, regex{regex}
      { }


  [[nodiscard]] std::expected<PredicateProgram, Error>
  compileQuery() {
    uint32_t const patterns = ts_query_pattern_count(query);
    program.patternStarts.reserve(patterns + 1);
    for (uint32_t pattern = 0; pattern < patterns; ++pattern) {
      if (auto compiled = compilePattern(pattern); !compiled) {
        return std::unexpected{compiled.error()};
      }
    }
    program.patternStarts.push_back(predicateCount());
    return std::move(program);
  }


private:
  [[nodiscard]] std::expected<void, Error>
  compilePattern(uint32_t pattern) {
    program.patternStarts.push_back(predicateCount());

    uint32_t count = 0;
    TSQueryPredicateStep const* const steps =
      ts_query_predicates_for_pattern(query, pattern, &count);
    TSQueryPredicateStep const* const end = steps + count;

    for (TSQueryPredicateStep const* step = steps; step != end; ) {
      TSQueryPredicateStep const* const done = findPredicateEnd(step, end);
      if (auto compiled = compilePredicate(step, done); !compiled) {
        return std::unexpected{Error{
          .kind=compiled.error(),
          .offset=ts_query_start_byte_for_pattern(query, pattern)}};
      }
      if (done == end) {
        break;   // no closing Done step
      }
      step = done + 1;
    }
    return {};
  }


  // Compiles one predicate. Unknown names emit nothing.
  [[nodiscard]] std::expected<void, ErrorKind>
  compilePredicate(TSQueryPredicateStep const* first,
                   TSQueryPredicateStep const* last) {
    if (first == last) {
      return {};
    }
    if (first->type != TSQueryPredicateStepTypeString) {
      return std::unexpected{ErrorKind::QueryPredicate};
    }

    std::optional<PredicateShape> const shape =
      classifyPredicate(tokenText(query, first->value_id));
    if (!shape) {
      return {};
    }

    // Supported text predicates take a capture as their first argument.
    std::span<TSQueryPredicateStep const> const args{first + 1, last};
    if (args.empty() || args[0].type != TSQueryPredicateStepTypeCapture) {
      return std::unexpected{ErrorKind::QueryPredicate};
    }

    TextPredicate compiled{.op=shape->op,
                           .positive=shape->positive,
                           .matchAll=shape->matchAll,
                           .capture=CaptureId{args[0].value_id}};

    std::expected<void, ErrorKind> bound;
    switch (shape->op) {
      case TextPredicate::Op::EqualToken:
        bound = bindEqual(args, compiled);
        break;
      case TextPredicate::Op::Match:
        bound = bindMatch(args, compiled);
        break;
      case TextPredicate::Op::AnyOf:
        bound = bindLiteralSet(args, compiled);
        break;
      case TextPredicate::Op::EqualCapture:
        break;
    }
    if (!bound) {
      return bound;
    }

    program.hasText = true;
    program.predicates.push_back(compiled);
    return {};
  }


  // The eq family compares with either a second capture or a literal.
  [[nodiscard]] std::expected<void, ErrorKind>
  bindEqual(std::span<TSQueryPredicateStep const> args,
            TextPredicate& compiled) {
    if (args.size() != 2) {
      return std::unexpected{ErrorKind::QueryPredicate};
    }
    if (args[1].type == TSQueryPredicateStepTypeCapture) {
      compiled.op = TextPredicate::Op::EqualCapture;
      compiled.otherCapture = CaptureId{args[1].value_id};
      return {};
    }
    compiled.operandStart = addOperand(args[1].value_id);
    compiled.operandCount = 1;
    return {};
  }


  // The match family compiles even without a RegexCompiler but records that
  // the query cannot be run by a cursor in PredicateMode::Evaluate.
  [[nodiscard]] std::expected<void, ErrorKind>
  bindMatch(std::span<TSQueryPredicateStep const> args,
            TextPredicate& compiled) {
    if (args.size() != 2 || args[1].type != TSQueryPredicateStepTypeString) {
      return std::unexpected{ErrorKind::QueryPredicate};
    }
    if (!regex) {
      program.hasUncompiledRegexes = true;
      compiled.operandCount = 0;
      return {};
    }

    auto const matcher = addMatcher(PredicateTokenId{args[1].value_id});
    if (!matcher) {
      return std::unexpected{matcher.error()};
    }
    compiled.operandStart = *matcher;
    compiled.operandCount = 1;
    return {};
  }


  // The any-of family. All remaining arguments must be literals.
  [[nodiscard]] std::expected<void, ErrorKind>
  bindLiteralSet(std::span<TSQueryPredicateStep const> args,
                 TextPredicate& compiled) {
    compiled.operandStart = static_cast<uint32_t>(program.operands.size());
    for (TSQueryPredicateStep const& arg : args.subspan(1)) {
      if (arg.type == TSQueryPredicateStepTypeCapture) {
        return std::unexpected{ErrorKind::QueryPredicate};
      }
      addOperand(arg.value_id);
    }
    compiled.operandCount = static_cast<uint32_t>(args.size() - 1);
    return {};
  }

  uint32_t
  addOperand(uint32_t token) {
    auto const index = static_cast<uint32_t>(program.operands.size());
    program.operands.push_back(tokenText(query, token));
    return index;
  }


  // Compiles one #match? pattern.
  [[nodiscard]] std::expected<uint32_t, ErrorKind>
  addMatcher(PredicateTokenId token) {
    auto const existing = std::ranges::find(matcherTokens, token);
    if (existing != matcherTokens.end()) {
      return static_cast<uint32_t>(existing - matcherTokens.begin());
    }

    std::optional<RegexMatcher> matcher =
      regex(tokenText(query, std::to_underlying(token)));
    if (!matcher) {
      return std::unexpected{ErrorKind::QueryPredicateRegex};
    }
    auto const index = static_cast<uint32_t>(program.matchers.size());
    program.matchers.push_back(std::move(*matcher));
    matcherTokens.push_back(token);
    return index;
  }


  [[nodiscard]] uint32_t
  predicateCount() const {
    return static_cast<uint32_t>(program.predicates.size());
  }


  TSQuery const* query;
  RegexCompiler const& regex;
  PredicateProgram program;
  // Token id for each compiled matcher.
  std::vector<PredicateTokenId> matcherTokens;
};


[[nodiscard]] inline std::expected<PredicateProgram, Error>
compilePredicates(TSQuery const* query, RegexCompiler const& regex) {
  return PredicateCompiler{query, regex}.compileQuery();
}


/////////////////////////////////////////////////////////////////////////////
// Evaluating the compiled program.
/////////////////////////////////////////////////////////////////////////////


// The Captures recorded under one capture id in capture order.
template <std::ranges::random_access_range Captures>
[[nodiscard]] auto
capturesUnder(Captures const& captures, CaptureId id) {
  return captures | std::views::filter(
    [id](auto const& capture) { return capture.id == id; });
}


// Whether one captured node decides a quantified predicate.
enum class Verdict : uint8_t { Undecided, Accepts, Rejects };

[[nodiscard]] constexpr Verdict
verdictFor(TextPredicate const& predicate, bool holds) {
  bool const asExpected = holds == predicate.positive;
  if (predicate.matchAll) {
    return asExpected ? Verdict::Undecided : Verdict::Rejects;
  }
  return asExpected ? Verdict::Accepts : Verdict::Undecided;
}


// The eq family between two captures, compared pairwise in capture order.
template <std::ranges::random_access_range Captures>
[[nodiscard]] bool
acceptsCapturePair(TextPredicate const& predicate, Captures const& captures,
                   std::string_view source) {
  auto lhs = capturesUnder(captures, predicate.capture);
  auto rhs = capturesUnder(captures, predicate.otherCapture);
  auto left = std::ranges::begin(lhs);
  auto right = std::ranges::begin(rhs);

  // The filtered iterator yields prvalues, so use (*it) rather than it->.
  for (; left != std::ranges::end(lhs) && right != std::ranges::end(rhs);
       ++left, ++right) {
    Verdict const verdict = verdictFor(
      predicate, (*left).node.getSourceRange(source)
                   == (*right).node.getSourceRange(source));
    if (verdict != Verdict::Undecided) {
      return verdict == Verdict::Accepts;
    }
  }

  // An all-form succeeds only if both capture lists were fully compared.
  return predicate.matchAll
      && left == std::ranges::end(lhs) && right == std::ranges::end(rhs);
}


// The any-of family. Despite the name, every captured node must be in the
// literal set. For not-any-of?, every captured node must be outside it.
template <std::ranges::random_access_range Captures>
[[nodiscard]] bool
acceptsLiteralSet(PredicateProgram const& program,
                  TextPredicate const& predicate, Captures const& captures,
                  std::string_view source) {
  std::span<std::string_view const> const literals =
    program.operandsOf(predicate);

  for (auto const capture : capturesUnder(captures, predicate.capture)) {
    bool const inSet =
      std::ranges::contains(literals, capture.node.getSourceRange(source));
    if (inSet != predicate.positive) {
      return false;
    }
  }
  return true;
}


// The eq family against a literal and the match family. Both test one captured
// node at a time.
template <std::ranges::random_access_range Captures>
[[nodiscard]] bool
acceptsEachNode(PredicateProgram const& program,
                TextPredicate const& predicate, Captures const& captures,
                std::string_view source) {
  for (auto const capture : capturesUnder(captures, predicate.capture)) {
    std::string_view const text = capture.node.getSourceRange(source);
    bool const holds = predicate.op == TextPredicate::Op::EqualToken
      ? text == program.operandsOf(predicate).front()
      : program.matches(predicate, text);

    Verdict const verdict = verdictFor(predicate, holds);
    if (verdict != Verdict::Undecided) {
      return verdict == Verdict::Accepts;
    }
  }

  // No node settled it. all-forms accept, any-forms reject.
  return predicate.matchAll;
}


template <std::ranges::random_access_range Captures>
[[nodiscard]] bool
acceptsPredicate(PredicateProgram const& program,
                 TextPredicate const& predicate, Captures const& captures,
                 std::string_view source) {
  switch (predicate.op) {
    case TextPredicate::Op::EqualCapture:
      return acceptsCapturePair(predicate, captures, source);
    case TextPredicate::Op::AnyOf:
      return acceptsLiteralSet(program, predicate, captures, source);
    case TextPredicate::Op::EqualToken:
    case TextPredicate::Op::Match:
      break;
  }
  return acceptsEachNode(program, predicate, captures, source);
}


template <std::ranges::random_access_range Captures>
inline bool
PredicateProgram::accepts(PatternIndex pattern, Captures const& captures,
                          std::string_view source) const {
  auto const index = static_cast<uint32_t>(std::to_underlying(pattern));
  assert(index + 1 < patternStarts.size()
         && "match came from a different query");

  for (uint32_t i = patternStarts[index]; i < patternStarts[index + 1]; ++i) {
    if (!acceptsPredicate(*this, predicates[i], captures, source)) {
      return false;
    }
  }
  return true;
}

}

}

#endif
