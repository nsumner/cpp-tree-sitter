#ifndef CTS_FORMAT_H
#define CTS_FORMAT_H

// Opt-in std::formatter specializations for the types this library owns.
//
// Deliberately NOT included by <cpp-tree-sitter.h> because it is not a
// cheap include, so paying for it is a choice.

#include <algorithm>
#include <cstdint>
#include <format>
#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <cts/children.h>
#include <cts/common.h>
#include <cts/language.h>
#include <cts/node.h>
#include <cts/visitor.h>


namespace ts::detail {

// Base for every formatter whose value renders as text.
//
// Delegates the format spec to std::formatter<std::string_view>, so
// `{:>20}`, `{:.5}`, `{:s}`, and dynamic widths like `{:{}}` behave as they
// do on a string.
//
// It needs to know whether the format spec was empty to render directly
// without allocating. Nonempty specs force buffering.
struct TextSpec {
  std::formatter<std::string_view, char> padded;
  bool simple = false;

  constexpr auto
  parse(std::format_parse_context& context) {
    auto it = context.begin();
    if (it == context.end() || *it == '}') {
      simple = true;
      return it;
    }
    return padded.parse(context);
  }


  // `render` is any callable taking an output iterator and returning it.
  template <typename Render, typename Context>
  auto
  format(Render&& render, Context& context) const {
    if (simple) {
      return render(context.out());
    }
    std::string buffer;
    render(std::back_inserter(buffer));
    return padded.format(buffer, context);
  }
};


// Shared by the opaque identifiers.
template <typename E>
struct IdFormatter : std::formatter<std::underlying_type_t<E>, char> {
  template <typename Context>
  auto
  format(E value, Context& context) const {
    return std::formatter<std::underlying_type_t<E>, char>::format(
      std::to_underlying(value), context);
  }
};


constexpr std::string_view
enumName(SymbolType value) {
  switch (value) {
    case SymbolType::Regular:   return "Regular";
    case SymbolType::Anonymous: return "Anonymous";
    case SymbolType::Supertype: return "Supertype";
    case SymbolType::Auxiliary: return "Auxiliary";
  }
  return "Unknown";
}


constexpr std::string_view
enumName(ErrorKind value) {
  switch (value) {
    case ErrorKind::LanguageIncompatible: return "LanguageIncompatible";
    case ErrorKind::ParseFailed:          return "ParseFailed";
    case ErrorKind::ParserIncludedRangesUnordered:
      return "ParserIncludedRangesUnordered";
    case ErrorKind::QuerySyntax:          return "QuerySyntax";
    case ErrorKind::QueryNodeType:        return "QueryNodeType";
    case ErrorKind::QueryField:           return "QueryField";
    case ErrorKind::QueryCapture:         return "QueryCapture";
    case ErrorKind::QueryStructure:       return "QueryStructure";
    case ErrorKind::QueryLanguage:        return "QueryLanguage";
    case ErrorKind::QueryInvalidRange:    return "QueryInvalidRange";
    case ErrorKind::QueryNodeLanguage:    return "QueryNodeLanguage";
    case ErrorKind::QueryPredicate:       return "QueryPredicate";
    case ErrorKind::QueryPredicateRegex:  return "QueryPredicateRegex";
    case ErrorKind::QueryPredicatesNeedSource:
      return "QueryPredicatesNeedSource";
    case ErrorKind::QueryPredicatesNeedRegex:
      return "QueryPredicatesNeedRegex";
    case ErrorKind::VisitorNodeType:      return "VisitorNodeType";
    case ErrorKind::VisitorSupertypeAmbiguity:
      return "VisitorSupertypeAmbiguity";
    case ErrorKind::VisitorDuplicate:     return "VisitorDuplicate";
  }
  return "Unknown";
}


constexpr std::string_view
enumName(VisitAction value) {
  switch (value) {
    case VisitAction::Continue:     return "Continue";
    case VisitAction::SkipChildren: return "SkipChildren";
    case VisitAction::Stop:         return "Stop";
  }
  return "Unknown";
}


constexpr std::string_view
enumName(ChildScope value) {
  switch (value) {
    case ChildScope::All:       return "All";
    case ChildScope::NamedOnly: return "NamedOnly";
  }
  return "Unknown";
}


// Shared by the enumerations that render as their enumerator name.
template <typename E, std::string_view (*Name)(E)>
struct EnumFormatter : TextSpec {
  template <typename Context>
  auto
  format(E value, Context& context) const {
    return TextSpec::format(
      [value](auto out) {
        return std::ranges::copy(Name(value), out).out;
      },
      context);
  }
};

}


template <>
struct std::formatter<ts::Symbol, char>
  : ts::detail::IdFormatter<ts::Symbol> { };

template <>
struct std::formatter<ts::FieldId, char>
  : ts::detail::IdFormatter<ts::FieldId> { };

template <>
struct std::formatter<ts::NodeID, char>
  : ts::detail::IdFormatter<ts::NodeID> { };


template <>
struct std::formatter<ts::Point, char> : ts::detail::TextSpec {
  template <typename Context>
  auto
  format(ts::Point point, Context& context) const {
    return ts::detail::TextSpec::format(
      [point](auto out) {
        return std::format_to(out, "{}:{}", point.row, point.column);
      },
      context);
  }
};


template <typename T>
  requires std::formattable<T, char>
struct std::formatter<ts::Extent<T>, char> : ts::detail::TextSpec {
  template <typename Context>
  auto
  format(ts::Extent<T> const& extent, Context& context) const {
    return ts::detail::TextSpec::format(
      [&extent](auto out) {
        return std::format_to(out, "[{}, {})", extent.start, extent.end);
      },
      context);
  }
};


template <>
struct std::formatter<ts::Location, char> : ts::detail::TextSpec {
  template <typename Context>
  auto
  format(ts::Location const& location, Context& context) const {
    return ts::detail::TextSpec::format(
      [&location](auto out) {
        return std::format_to(out, "{} ({})", location.byte, location.point);
      },
      context);
  }
};


template <>
struct std::formatter<ts::Error, char> : ts::detail::TextSpec {
  template <typename Context>
  auto
  format(ts::Error const& error, Context& context) const {
    return ts::detail::TextSpec::format(
      [&error](auto out) {
        out = std::ranges::copy(error.message(), out).out;
        if (error.hasOffset()) {
          out = std::format_to(out, " at offset {}", error.offset);
        }
        if (error.hasName() && !error.name.empty()) {
          out = std::format_to(out, ": {}", error.name);
        }
        return out;
      },
      context);
  }
};


template <>
struct std::formatter<ts::SymbolType, char>
  : ts::detail::EnumFormatter<ts::SymbolType, &ts::detail::enumName> { };

template <>
struct std::formatter<ts::ErrorKind, char>
  : ts::detail::EnumFormatter<ts::ErrorKind, &ts::detail::enumName> { };

template <>
struct std::formatter<ts::VisitAction, char>
  : ts::detail::EnumFormatter<ts::VisitAction, &ts::detail::enumName> { };

template <>
struct std::formatter<ts::ChildScope, char>
  : ts::detail::EnumFormatter<ts::ChildScope, &ts::detail::enumName> { };


// Nodes do not carry their source, so the formatter is simple. More
// complex formatting is still easy to access explicitly with
//   std::println("{}", node.getSExpr());
//   std::println("{}", node.getSourceRange(source));
template <>
struct std::formatter<ts::Node, char> : ts::detail::TextSpec {
  template <typename Context>
  auto
  format(ts::Node node, Context& context) const {
    return ts::detail::TextSpec::format(
      [node](auto out) {
        ts::Extent<uint32_t> const bytes = node.getByteRange();
        return std::format_to(out, "{} [{}, {})",
                              node.getType(), bytes.start, bytes.end);
      },
      context);
  }
};

#endif
