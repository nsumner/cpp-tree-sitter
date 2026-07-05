#ifndef CTS_COMMON_H
#define CTS_COMMON_H

#include <algorithm>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iterator>
#include <ranges>
#include <string_view>
#include <type_traits>
#include <tree_sitter/api.h>


namespace ts {

struct FreeHelper{
  template <typename T>
  void
  operator()(T* rawPointer) const {
    std::free(rawPointer);
  }
};


// A half-open [start, end) range representation
template<typename T>
struct Extent {
  T start;
  T end;

  friend constexpr auto operator<=>(Extent const&, Extent const&) = default;
};


/////////////////////////////////////////////////////////////////////////////
// Aliases.
// Create slightly stricter aliases for some of the core tree-sitter types.
/////////////////////////////////////////////////////////////////////////////


// A position in tree-sitter's row/column coordinate system. Point is
// compatible with the original TSPoint, but it supports C++ niceties like
// comparison, hashing, and optionally formatting.
struct Point {
  uint32_t row = 0;
  uint32_t column = 0;

  constexpr Point() = default;

  constexpr Point(uint32_t rowIn, uint32_t columnIn) noexcept
    : row{rowIn}, column{columnIn}
      { }

  // Implicitly convertible with TSPoints.

  /* implicit */ constexpr Point(TSPoint point) noexcept
    : row{point.row}, column{point.column}
      { }

  [[nodiscard]] /* implicit */ constexpr operator TSPoint() const noexcept {
    return TSPoint{row, column};
  }

  friend constexpr auto operator<=>(Point, Point) = default;
};


// Using enum classes allows us to create transparently usable wrappers for
// some of the other built in tree-sitter types as well. These create strong
// type aliases, unlike "using ... =", so they provide better type safety
// and are still handled by data structures, formatting, etc. It also
// cleanly flags tree-sitter's sentinel 0s.
//
// std::to_underlying() converts to the actual type if needed.

enum class Symbol : uint16_t { None = 0 };

enum class FieldId : uint16_t { None = 0 };

enum class NodeID : uintptr_t {};


using Version = uint32_t;


/////////////////////////////////////////////////////////////////////////////
// Locations and edits.
/////////////////////////////////////////////////////////////////////////////


// One position expressed in both tree-sitter coordinate systems.
struct Location {
  uint32_t byte;
  Point point;

  friend constexpr auto operator<=>(Location, Location) = default;
};


// A contiguous region of source.
using Range = Extent<Location>;


namespace detail {

[[nodiscard]] constexpr TSRange
toRaw(Range range) noexcept {
  return TSRange{.start_point=range.start.point,
                 .end_point=range.end.point,
                 .start_byte=range.start.byte,
                 .end_byte=range.end.byte};
}


[[nodiscard]] constexpr Range
toRange(TSRange const& raw) noexcept {
  return Range{{.byte=raw.start_byte, .point=raw.start_point},
               {.byte=raw.end_byte,   .point=raw.end_point}};
}


// Walks `source` from `from` to byte offset `target`. The target must not
// precede from.
[[nodiscard]] constexpr Location
advanceLocation(std::string_view source, Location from, uint32_t target) {
  for (uint32_t i = from.byte; i < target; ++i) {
    if (source[i] == '\n') {
      ++from.point.row;
      from.point.column = 0;
    } else {
      ++from.point.column;
    }
  }
  from.byte = target;
  return from;
}

}


// Converts a byte offset in `source` to a Location. Tries to matches
// tree-sitter's coordinate system. '\n' starts a new row, and columns count
// bytes rather than characters. Offsets past the end of `source` resolve to
// the end. Prefer Node::getRange() when the location comes from a node.
[[nodiscard]] constexpr Location
locationForByte(std::string_view source, uint32_t byte) {
  uint32_t const clamped =
    std::min(byte, static_cast<uint32_t>(source.size()));
  return detail::advanceLocation(source, Location{.byte=0, .point={}}, clamped);
}


// Resolves a byte extent in `source` into a Range, following the same rules
// as locationForByte.
[[nodiscard]] constexpr Range
rangeForBytes(std::string_view source, Extent<uint32_t> bytes) {
  Location const start = locationForByte(source, bytes.start);
  uint32_t const end =
    std::min(bytes.end, static_cast<uint32_t>(source.size()));
  return Range{start, end >= start.byte
                        ? detail::advanceLocation(source, start, end)
                        : locationForByte(source, end)};
}


// A source text change for incremental parsing.
// start/oldEnd are pre-edit coordinates of the replaced region.
// newEnd is the post-edit coordinate of the replacement's end.
struct InputEdit {
  Location start;
  Location oldEnd;
  Location newEnd;

  [[nodiscard]] TSInputEdit
  toRaw() const {
    return TSInputEdit{
      start.byte,  oldEnd.byte,  newEnd.byte,
      start.point, oldEnd.point, newEnd.point,
    };
  }
};


/////////////////////////////////////////////////////////////////////////////
// Error reporting.
/////////////////////////////////////////////////////////////////////////////


enum class ErrorKind : uint8_t {
  LanguageIncompatible,
  ParseFailed,
  ParserIncludedRangesUnordered,
  QuerySyntax,
  QueryNodeType,
  QueryField,
  QueryCapture,
  QueryStructure,
  QueryLanguage,
  QueryInvalidRange,
  QueryNodeLanguage,
  QueryPredicate,
  QueryPredicateRegex,
  QueryPredicatesNeedSource,
  QueryPredicatesNeedRegex,
  VisitorNodeType,
  VisitorSupertypeAmbiguity,
  VisitorDuplicate,
};


// TODO: Consider separating error types out so the only carry the state
// they need.
struct Error {
  ErrorKind kind;
  uint32_t offset = 0;
  std::string_view name = {};


  [[nodiscard]] constexpr std::string_view message() const {
    switch (kind) {
      case ErrorKind::LanguageIncompatible:
        return "language ABI version is incompatible with this tree-sitter runtime";
      case ErrorKind::ParseFailed:
        return "parsing produced no tree";
      case ErrorKind::ParserIncludedRangesUnordered:
        return "parser included ranges must be ordered by byte offset and "
               "must not overlap";
      case ErrorKind::QuerySyntax:
        return "query syntax error";
      case ErrorKind::QueryNodeType:
        return "query references an unknown node type";
      case ErrorKind::QueryField:
        return "query references an unknown field";
      case ErrorKind::QueryCapture:
        return "query references an undefined capture name";
      case ErrorKind::QueryStructure:
        return "query pattern has invalid structure";
      case ErrorKind::QueryLanguage:
        return "query was compiled for a different language";
      case ErrorKind::QueryInvalidRange:
        return "query option range has start greater than end";
      case ErrorKind::QueryNodeLanguage:
        return "query and node come from different languages";
      case ErrorKind::QueryPredicate:
        return "query predicate has the wrong number or kind of arguments";
      case ErrorKind::QueryPredicateRegex:
        return "query predicate regex could not be compiled";
      case ErrorKind::QueryPredicatesNeedSource:
        return "query has text predicates but no source was supplied";
      case ErrorKind::QueryPredicatesNeedRegex:
        return "query has a #match? predicate but no regex compiler was "
               "supplied at creation";
      case ErrorKind::VisitorNodeType:
        return "typed visitor references an unknown node type";
      case ErrorKind::VisitorSupertypeAmbiguity:
        return "typed visitor has incomparable nearest-supertype handlers "
               "for the same node type";
      case ErrorKind::VisitorDuplicate:
        return "typed visitor has duplicate handlers for the same node type";
    }
    return "unknown error";
  }


  [[nodiscard]] constexpr bool
  hasOffset() const {
    switch (kind) {
      case ErrorKind::QuerySyntax:
      case ErrorKind::QueryNodeType:
      case ErrorKind::QueryField:
      case ErrorKind::QueryCapture:
      case ErrorKind::QueryStructure:
      case ErrorKind::QueryLanguage:
      case ErrorKind::QueryPredicate:
      case ErrorKind::QueryPredicateRegex:
        return true;
      case ErrorKind::LanguageIncompatible:
      case ErrorKind::ParseFailed:
      case ErrorKind::ParserIncludedRangesUnordered:
      case ErrorKind::VisitorNodeType:
      case ErrorKind::VisitorSupertypeAmbiguity:
      case ErrorKind::QueryInvalidRange:
      case ErrorKind::QueryNodeLanguage:
      case ErrorKind::VisitorDuplicate:
      case ErrorKind::QueryPredicatesNeedSource:
      case ErrorKind::QueryPredicatesNeedRegex:
        return false;
    }
    return false;
  }


  [[nodiscard]] constexpr bool
  hasName() const {
    switch (kind) {
      case ErrorKind::VisitorNodeType:
      case ErrorKind::VisitorSupertypeAmbiguity:
      case ErrorKind::VisitorDuplicate:
        return true;
      case ErrorKind::LanguageIncompatible:
      case ErrorKind::ParseFailed:
      case ErrorKind::ParserIncludedRangesUnordered:
      case ErrorKind::QuerySyntax:
      case ErrorKind::QueryNodeType:
      case ErrorKind::QueryField:
      case ErrorKind::QueryCapture:
      case ErrorKind::QueryStructure:
      case ErrorKind::QueryLanguage:
      case ErrorKind::QueryInvalidRange:
      case ErrorKind::QueryNodeLanguage:
      case ErrorKind::QueryPredicate:
      case ErrorKind::QueryPredicateRegex:
      case ErrorKind::QueryPredicatesNeedSource:
      case ErrorKind::QueryPredicatesNeedRegex:
        return false;
    }
    return false;
  }
};


namespace detail {

  // Walks a C array of `Raw`, yielding values built one element at a time by
  // `Convert`. This is used in a few places where a raw array to the original
  // tree-sitter types would have been returned by the API instead. A custom
  // iterator overcomes some impedence mismatches using, e.g., a
  // transform_view instead.
  template <typename Raw, typename Convert>
  class MappedIterator {
  public:
    using raw_type = Raw;
    using value_type = std::invoke_result_t<Convert, Raw const&>;
    using difference_type = std::ptrdiff_t;

    using iterator_concept = std::random_access_iterator_tag;

    constexpr MappedIterator() = default;

    constexpr explicit MappedIterator(Raw const* raw) noexcept
    : raw{raw}
      { }

    constexpr value_type
    operator*() const noexcept {
      return Convert{}(*raw);
    }

    constexpr value_type
    operator[](difference_type offset) const noexcept {
      return Convert{}(raw[offset]);
    }

    constexpr MappedIterator&
    operator++() noexcept {
      ++raw;
      return *this;
    }

    constexpr MappedIterator
    operator++(int) noexcept {
      MappedIterator copy = *this;
      ++raw;
      return copy;
    }

    constexpr MappedIterator&
    operator--() noexcept {
      --raw;
      return *this;
    }

    constexpr MappedIterator
    operator--(int) noexcept {
      MappedIterator copy = *this;
      --raw;
      return copy;
    }

    constexpr MappedIterator&
    operator+=(difference_type offset) noexcept {
      raw += offset;
      return *this;
    }

    constexpr MappedIterator&
    operator-=(difference_type offset) noexcept {
      raw -= offset;
      return *this;
    }

    friend constexpr MappedIterator
    operator+(MappedIterator iterator, difference_type offset) noexcept {
      return iterator += offset;
    }

    friend constexpr MappedIterator
    operator+(difference_type offset, MappedIterator iterator) noexcept {
      return iterator += offset;
    }

    friend constexpr MappedIterator
    operator-(MappedIterator iterator, difference_type offset) noexcept {
      return iterator -= offset;
    }

    friend constexpr difference_type
    operator-(MappedIterator lhs, MappedIterator rhs) noexcept {
      return lhs.raw - rhs.raw;
    }

    friend bool operator==(MappedIterator, MappedIterator) = default;
    friend auto operator<=>(MappedIterator, MappedIterator) = default;

  private:
    Raw const* raw = nullptr;
  };


  template <typename Raw, typename Convert>
  using MappedRange = std::ranges::subrange<MappedIterator<Raw, Convert>>;


  // Wraps a raw (data, length) pair from the C API as a MappedRange.
  // Spelling the pointer through raw_type makes it a non-deduced context so
  // a mismatched pointer is diagnosed at the call site, and a literal
  // nullptr still converts.
  template <typename Range>
  [[nodiscard]] constexpr Range
  makeRange(typename std::ranges::iterator_t<Range>::raw_type const* data,
            uint32_t length) {
    using Iterator = std::ranges::iterator_t<Range>;
    return Range{Iterator{data}, Iterator{data + length}};
  }

}

}


template <>
struct std::hash<ts::Point> {
  [[nodiscard]] size_t
  operator()(ts::Point point) const noexcept {
    auto const [row, column] = point;
    return row ^ (column + 0x9e3779b9u + (row << 6) + (row >> 2));
  }
};

#endif
