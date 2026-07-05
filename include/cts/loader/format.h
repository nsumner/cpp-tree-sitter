#ifndef CTS_LOADER_FORMAT_H
#define CTS_LOADER_FORMAT_H

// Opt-in std::formatter specializations just for runtime grammar loading.

#include <format>
#include <ranges>
#include <string_view>

#include <cts/format.h>
#include <cts/loader.h>


namespace ts::detail {

constexpr std::string_view
enumName(GrammarLoadError::Kind value) {
  switch (value) {
    case GrammarLoadError::Kind::FileMissing:          return "FileMissing";
    case GrammarLoadError::Kind::LibraryOpenFailed:    return "LibraryOpenFailed";
    case GrammarLoadError::Kind::EntryPointMissing:    return "EntryPointMissing";
    case GrammarLoadError::Kind::LanguageNull:         return "LanguageNull";
    case GrammarLoadError::Kind::AbiIncompatible:      return "AbiIncompatible";
    case GrammarLoadError::Kind::NotFoundInSearchPath: return "NotFoundInSearchPath";
  }
  return "Unknown";
}

}  // namespace ts::detail


template <>
struct std::formatter<ts::GrammarLoadError::Kind, char>
  : ts::detail::EnumFormatter<ts::GrammarLoadError::Kind, &ts::detail::enumName> { };


template <>
struct std::formatter<ts::GrammarLoadError, char> : ts::detail::TextSpec {
  template <typename Context>
  auto
  format(ts::GrammarLoadError const& error, Context& context) const {
    return ts::detail::TextSpec::format(
      [&error](auto out) {
        out = std::ranges::copy(error.message(), out).out;
        if (!error.path.empty()) {
          out = std::format_to(out, ": {}", error.path);
        }
        if (!error.detail.empty()) {
          out = std::format_to(out, " ({})", error.detail);
        }
        return out;
      },
      context);
  }
};

#endif
