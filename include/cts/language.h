#ifndef CTS_LANGUAGE_H
#define CTS_LANGUAGE_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include <cts/common.h>

namespace ts {

namespace detail {

// Converts tree-sitter's raw symbol ids to ts::Symbol one element at a time.
struct ToSymbol {
  static constexpr Symbol
  operator()(TSSymbol symbol) noexcept {
    return Symbol{symbol};
  }
};

}


// Custom range for accessing wrapped instances of values that tree-sitter
// APIs return as a buffer of raw types.
using SymbolRange = detail::MappedRange<TSSymbol, detail::ToSymbol>;


// Symbol classifications like tree-sitter's TSSymbolType. Regular and
// Anonymous symbols appear in trees. Supertype symbols never appear in trees
// but act as an entry point into the supertype/subtype metadata. Auxiliary
// symbols are internal.
enum class SymbolType : uint8_t {
  Regular   = TSSymbolTypeRegular,
  Anonymous = TSSymbolTypeAnonymous,
  Supertype = TSSymbolTypeSupertype,
  Auxiliary = TSSymbolTypeAuxiliary,
};


struct Language {
  // NOTE: Allowing implicit conversions from TSLanguage to Language
  // improves the ergonomics a bit, as clients will still make use of the
  // custom language functions.

  /* implicit */ Language(TSLanguage const* language)
    : impl{language}
      { }

  friend constexpr bool operator==(Language, Language) = default;


  [[nodiscard]] uint32_t
  getNumSymbols() const {
    return ts_language_symbol_count(impl);
  }


  [[nodiscard]] std::optional<std::string_view>
  getSymbolName(Symbol symbol) const {
    char const* name =
      ts_language_symbol_name(impl, std::to_underlying(symbol));
    if (name == nullptr) {
      return std::nullopt;
    }
    return std::string_view{name};
  }


  [[nodiscard]] std::optional<Symbol>
  getSymbolForName(std::string_view name, bool isNamed) const {
    // data() may be null for a default-constructed view; the C API wants a
    // non-null pointer even when the length is zero.
    char const* buffer = name.data() != nullptr ? name.data() : "";
    Symbol symbol{ts_language_symbol_for_name(
      impl, buffer, static_cast<uint32_t>(name.size()), isNamed)};
    if (symbol == Symbol::None) {
      return std::nullopt;
    }
    return symbol;
  }


  [[nodiscard]] SymbolType
  getSymbolType(Symbol symbol) const {
    return static_cast<SymbolType>(
      ts_language_symbol_type(impl, std::to_underlying(symbol)));
  }


  // Supertype introspection requires language ABI >= 15. Grammars from an
  // older CLI return empty ranges even when their node-types.json declares
  // supertypes.
  //
  // The returned SymbolRange is valid only while the TSLanguage behind this
  // language is. If the grammar library is statically linked, that lifetime
  // is static.
  [[nodiscard]] SymbolRange
  getSupertypes() const {
    uint32_t length = 0;
    TSSymbol const* data = ts_language_supertypes(impl, &length);
    return detail::makeRange<SymbolRange>(data, length);
  }


  // The returned SymbolRange is valid only while the TSLanguage behind this
  // language is. If the grammar library is statically linked, that lifetime
  // is static.
  [[nodiscard]] SymbolRange
  getSubtypes(Symbol supertype) const {
    uint32_t length = 0;
    TSSymbol const* data = ts_language_subtypes(
      impl, std::to_underlying(supertype), &length);
    return detail::makeRange<SymbolRange>(data, length);
  }


  [[nodiscard]] Version
  getAbiVersion() const {
    return ts_language_abi_version(impl);
  }


  [[nodiscard]] std::optional<std::string_view>
  getName() const {
    char const* name = ts_language_name(impl);
    if (name == nullptr) {
      return std::nullopt;
    }
    return std::string_view{name};
  }


  [[nodiscard]] uint32_t
  getNumFields() const {
    return ts_language_field_count(impl);
  }


  [[nodiscard]] std::optional<std::string_view>
  getFieldName(FieldId id) const {
    char const* name =
      ts_language_field_name_for_id(impl, std::to_underlying(id));
    if (name == nullptr) {
      return std::nullopt;
    }
    return std::string_view{name};
  }


  [[nodiscard]] std::optional<FieldId>
  getFieldId(std::string_view name) const {
    // The C API wants a non-null pointer even when the length is zero.
    char const* buffer = name.data() != nullptr ? name.data() : "";
    FieldId id{ts_language_field_id_for_name(
      impl, buffer, static_cast<uint32_t>(name.size()))};
    if (id == FieldId::None) {
      return std::nullopt;
    }
    return id;
  }

  TSLanguage const* impl;
};

}

#endif
