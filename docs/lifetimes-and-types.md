# Lifetimes and types

This library provides a handful of types and wrappers for underlying `tree-sitter` types to provide additional safety or functionality.


## Errors

Operations that can fail return `std::expected<T, ts::Error>`. Operations whose result depends on whether something exists return `std::optional<T>`. The library does not throw exceptions or use sentinel return values.

Errors can arise in a handful of ways, including
- `ts::Parser::create(language)` fails when the grammar ABI does not match the linked tree-sitter runtime.
- `parser->parse(source)` fails when tree-sitter cannot produce a tree.
- `ts::Query::create(language, source)` fails when the query does not compile. The error includes the byte offset in the query source.
- `ts::typed(...)` and `ts::typedFold<R>(...)` fail when a handler names a node type not in the language.
- `QueryCursor::getMatches` and `getCaptureStream` fail when mixing a `Query` compiled for one `Language` with a `Node` from another.

`ts::Error` is formattable through `<cts/format.h>`, so `std::println("error: {}", result.error())` prints a complete message, including an offset or name when the error provides one.


## Ownership

`Tree` and `Parser` are move-only RAII owners. They own the underlying tree-sitter resources and release them on destruction. `Query` and `QueryCursor` own their resources in the same way.

`Node` and `Language` are non-owning borrows, analogous to a `std::string_view`, so

- A `Node` is valid while its `Tree` remains alive.
- A `Language` is valid while the grammar library that provides it remains loaded.

A `Tree` should be kept alive while using its `Node` values.

A `Language` remains valid while its grammar library stays loaded. Statically linked grammars are always valid. Grammars loaded through [`<cts/loader.h>`](dynamic-grammars.md) should also be valid for the process lifetime. You are responsible for this lifetime when you load a grammar yourself. After unloading its library, do not access anything obtained from it.

Query results have a shorter lifetime. A `QueryMatch` and its captures remain valid until the cursor advances. See [`examples/query_demo.cpp`](../examples/query_demo.cpp).


## Strong identifier types

`ts::Symbol`, `ts::FieldId`, and `ts::NodeID` are opaque identifiers. Each is a distinct `enum class` with no implicit conversion to or from its underlying type. Accidentally passing one when another is expected produces a compile time error. Use `std::to_underlying` to obtain an identifier's raw integer value:

```cpp
TSSymbol raw = std::to_underlying(symbol);
```

`ts::Point` converts implicitly to and from `TSPoint`, so it passes through the C API unchanged.


## Ranges that yield values

`Language::getSupertypes()` and `Language::getSubtypes()` return `ts::SymbolRange`. Similarly, `QueryMatch::getCaptures()` returns `ts::CaptureRange`. Both types are lazy, sized, allocation-free views. They convert elements from tree-sitter arrays into well typed wrappers on demand, which has some consequences for usage:

- Neither range is contiguous. No array of `Symbol` or `QueryCapture` exists, so neither provides `.data()` nor can be passed where a pointer is required.
- Iterators yield values rather than references, so use `(*it)` where `it->` would otherwise be used. `operator[]`, `front()`, and `back()` also return prvalues, so neither `auto& first = subtypes[0]` nor `&subtypes[0]` compiles.
- Both are borrowed ranges. `std::ranges` algorithms operate on them directly, including when the range object is temporary.
