#ifndef CTS_QUERY_FORMAT_H
#define CTS_QUERY_FORMAT_H

// Opt-in std::formatter specializations for the query subsystem.
//
// Separate from <cts/format.h> so that formatting a Node does not compile
// queries. Include this only if you format query types.

#include <format>
#include <ranges>
#include <string_view>
#include <variant>

#include <cts/format.h>
#include <cts/query.h>


namespace ts::detail {

constexpr std::string_view
enumName(Quantifier value) {
  switch (value) {
    case Quantifier::Zero:       return "Zero";
    case Quantifier::ZeroOrOne:  return "ZeroOrOne";
    case Quantifier::ZeroOrMore: return "ZeroOrMore";
    case Quantifier::One:        return "One";
    case Quantifier::OneOrMore:  return "OneOrMore";
  }
  return "Unknown";
}


constexpr std::string_view
enumName(ProgressAction value) {
  switch (value) {
    case ProgressAction::Continue: return "Continue";
    case ProgressAction::Cancel:   return "Cancel";
  }
  return "Unknown";
}

}


template <>
struct std::formatter<ts::CaptureId, char>
  : ts::detail::IdFormatter<ts::CaptureId> { };

template <>
struct std::formatter<ts::PredicateTokenId, char>
  : ts::detail::IdFormatter<ts::PredicateTokenId> { };

template <>
struct std::formatter<ts::PatternIndex, char>
  : ts::detail::IdFormatter<ts::PatternIndex> { };

template <>
struct std::formatter<ts::Quantifier, char>
  : ts::detail::EnumFormatter<ts::Quantifier, &ts::detail::enumName> { };

template <>
struct std::formatter<ts::ProgressAction, char>
  : ts::detail::EnumFormatter<ts::ProgressAction, &ts::detail::enumName> { };


// A QueryCapture knows a CaptureId but not the Query, so this shows the id and
// the captured node. To print the capture's name, resolve it yourself with
//   std::format("@{}", *query.getCaptureNameForId(capture.id))
template <>
struct std::formatter<ts::QueryCapture, char> : ts::detail::TextSpec {
  template <typename Context>
  auto
  format(ts::QueryCapture const& capture, Context& context) const {
    return ts::detail::TextSpec::format(
      [&capture](auto out) {
        return std::format_to(out, "@{} {}", capture.id, capture.node);
      },
      context);
  }
};


// A QueryMatch knows only its pattern and how many captures it holds.
template <>
struct std::formatter<ts::QueryMatch, char> : ts::detail::TextSpec {
  template <typename Context>
  auto
  format(ts::QueryMatch const& match, Context& context) const {
    return ts::detail::TextSpec::format(
      [&match](auto out) {
        return std::format_to(out, "pattern {}, {} captures",
                              match.getPatternIndex(),
                              match.getCaptures().size());
      },
      context);
  }
};


// A CaptureResult knows a match and its position.
template <>
struct std::formatter<ts::CaptureResult, char> : ts::detail::TextSpec {
  template <typename Context>
  auto
  format(ts::CaptureResult const& result, Context& context) const {
    return ts::detail::TextSpec::format(
      [&result](auto out) {
        return std::format_to(out, "{}, capture {}",
                              result.match, result.position);
      },
      context);
  }
};


// A PredicateArg knows an id but not the Query, so this shows which table the
// id belongs to rather than resolving it. To resolve one yourself, use
//   std::format("@{}", *query.getCaptureNameForId(id))
//   std::format("{}", *query.getPredicateToken(id))
template <>
struct std::formatter<ts::PredicateArg, char> : ts::detail::TextSpec {
  template <typename Context>
  auto
  format(ts::PredicateArg const& arg, Context& context) const {
    return ts::detail::TextSpec::format(
      [&arg](auto out) {
        if (auto const* capture = std::get_if<ts::CaptureId>(&arg)) {
          return std::format_to(out, "@{}", *capture);
        }
        return std::format_to(out, "token {}",
                              std::get<ts::PredicateTokenId>(arg));
      },
      context);
  }
};


// A Predicate shows the name as written and how many arguments it carries.
// The arguments themselves are a range, so format them with {::} if you want
// them, or resolve them through the Query.
template <>
struct std::formatter<ts::Predicate, char> : ts::detail::TextSpec {
  template <typename Context>
  auto
  format(ts::Predicate const& predicate, Context& context) const {
    return ts::detail::TextSpec::format(
      [&predicate](auto out) {
        return std::format_to(out, "#{} with {} arguments", predicate.name,
                              std::ranges::distance(predicate.args));
      },
      context);
  }
};

#endif
