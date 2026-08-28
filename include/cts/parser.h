#ifndef CTS_PARSER_H
#define CTS_PARSER_H

#include <concepts>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <initializer_list>
#include <memory>
#include <ranges>
#include <string_view>
#include <utility>
#include <vector>

#include <tree_sitter/api.h>

#if defined(_WIN32)
#  include <io.h>
#else
#  include <unistd.h>
#endif

#include <cts/common.h>
#include <cts/language.h>
#include <cts/tree.h>

namespace ts {

namespace detail {

// A sequence of source Ranges. (Helper concept for better errors.)
template <typename R>
concept RangeSequence =
  std::ranges::input_range<R>
  && std::convertible_to<std::ranges::range_reference_t<R>, Range>;

}


class Parser {
public:
  // Use this factory for creating Parser instances. Construction can fail,
  // e.g. from ABI mismatches, so the constructor is hidden behind a private
  // API.
  [[nodiscard]] static std::expected<Parser, Error>
  create(Language language) {
    Parser parser{ts_parser_new()};
    if (!language.impl
        || !ts_parser_set_language(parser.impl.get(), language.impl)) {
      return std::unexpected{Error{.kind=ErrorKind::LanguageIncompatible}};
    }
    return parser;
  }


  [[nodiscard]] std::expected<Tree, Error>
  parse(std::string_view source) {
    // data() may be null, but the C API forbids this.
    char const* buffer = source.data() != nullptr ? source.data() : "";
    TSTree* raw = ts_parser_parse_string(
      impl.get(), nullptr, buffer, static_cast<uint32_t>(source.size()));
    if (raw == nullptr) {
      return std::unexpected{Error{.kind=ErrorKind::ParseFailed}};
    }
    return Tree{raw};
  }


  // Incrementally parse `source` where `oldTree` comes from previous version
  // of `source` and has been informed of edits via Tree::edit.
  [[nodiscard]] std::expected<Tree, Error>
  parse(std::string_view source, const Tree& oldTree) {
    char const* buffer = source.data() != nullptr ? source.data() : "";
    TSTree* rawTree = ts_parser_parse_string(
      impl.get(), oldTree.raw(), buffer, static_cast<uint32_t>(source.size()));
    if (rawTree == nullptr) {
      return std::unexpected{Error{.kind=ErrorKind::ParseFailed}};
    }
    return Tree{rawTree};
  }


  // Restricts parsing to the given ranges. Text outside those ranges is
  // ignored. This is used for language injection like parsing embedded code
  // blocks in Markdown. Returned nodes still use coordinates in the original
  // source. Ranges must be nonoverlapping, ordered by byte offset, with
  // ordered bounds. An empty range clears the filter.
  template <detail::RangeSequence R>
  [[nodiscard]] std::expected<void, Error>
  setIncludedRanges(R&& ranges) {
    return assignIncludedRanges(std::forward<R>(ranges));
  }

  // initializer_lists still need a separate overload.
  [[nodiscard]] std::expected<void, Error>
  setIncludedRanges(std::initializer_list<Range> ranges) {
    return assignIncludedRanges(ranges);
  }


  // Statefully configures a Parser to stream a DOT graph of parser activity
  // to `file` during parses. To disable, pass a nullptr.
  //
  // tree-sitter takes ownership over the descriptor and closes it when graphs
  // are disabled or the parser is destroyed.
  void
  printDotGraphs(std::FILE* file) {
    ts_parser_print_dot_graphs(impl.get(), file != nullptr ? dup(fileno(file)) : -1);
  }

private:
  // Shared by both setIncludedRanges overloads. Kept separate so the
  // initializer_list one can reach it without recursing back into itself.
  template <detail::RangeSequence R>
  [[nodiscard]] std::expected<void, Error>
  assignIncludedRanges(R&& ranges) {
    std::vector<TSRange> raw;
    if constexpr (std::ranges::sized_range<R>) {
      raw.reserve(std::ranges::size(ranges));
    }
    for (Range const range : ranges) {
      raw.push_back(detail::toRaw(range));
    }
    if (!ts_parser_set_included_ranges(impl.get(), raw.data(),
                                       static_cast<uint32_t>(raw.size()))) {
      return std::unexpected{
        Error{.kind=ErrorKind::ParserIncludedRangesUnordered}};
    }
    return {};
  }


  explicit Parser(TSParser* parser)
    : impl{parser, ts_parser_delete}
      { }

  std::unique_ptr<TSParser, decltype(&ts_parser_delete)> impl;
};

}

#endif
