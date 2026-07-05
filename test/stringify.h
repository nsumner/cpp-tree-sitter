#ifndef CTS_TEST_STRINGIFY_H
#define CTS_TEST_STRINGIFY_H

// doctest StringMaker specializations for the types this library owns.
//
// doctest cannot print a type it has no insertion operator for. A failed
// CHECK(a == b) on one reports `{?} == {?}`, which says nothing about what
// went wrong. cts/format.h already renders each of these types, so the
// specializations below hand that rendering to doctest.
//
// Include this from every test source that puts one of these types in a
// CHECK.  Assertions in a file that does include this header start reporting
// `{?}` because some other file did not.
//
// "doctest renders cts values in failure messages" in test/format_tests.cpp
// is a canary. If a translation unit forgets the include, that test case
// fails rather than the suite quietly degrading.
//
// Scoped enumerations deliberately have no entry. doctest renders every enum
// as its underlying integer through a separate toString overload, so a
// specialization here would be dead code. test/format_tests.cpp pins that
// behavior.

#include <format>

#include <doctest/doctest.h>

#include <cts/common.h>
#include <cts/format.h>
#include <cts/node.h>

namespace cts_test {

// Shared by every specialization below. Delegating to std::format keeps
// doctest's failure output and the library's own rendering from drifting
// apart, since there is only one definition of how a Point looks.
template <typename T>
struct FormatStringMaker {
  static doctest::String
  convert(T const& value) {
    return std::format("{}", value).c_str();
  }
};

}  // namespace cts_test

namespace doctest {

template <>
struct StringMaker<ts::Point> : cts_test::FormatStringMaker<ts::Point> { };

template <>
struct StringMaker<ts::Location>
  : cts_test::FormatStringMaker<ts::Location> { };

template <>
struct StringMaker<ts::Node> : cts_test::FormatStringMaker<ts::Node> { };

template <>
struct StringMaker<ts::Error> : cts_test::FormatStringMaker<ts::Error> { };

// Covers ts::Range, which is Extent<Location>. An Extent of some
// non-formattable T falls back to doctest's primary template and reports
// `{?}` rather than failing to compile.
template <typename T>
  requires std::formattable<T, char>
struct StringMaker<ts::Extent<T>>
  : cts_test::FormatStringMaker<ts::Extent<T>> { };

}  // namespace doctest

#endif
