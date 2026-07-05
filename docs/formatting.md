# Formatting

`std::formatter` specializations are opt-in. Include `<cts/format.h>` when formatting support is required:

```cpp
#include <cpp-tree-sitter.h>
#include <cts/format.h>
```

Programs that do not use formatting do not incur the cost of `<format>`.

Query formatting is provided separately by `<cts/query/format.h>`. This matches the query subsystem's opt-in design. (Formatting a `Node` does not require query support.) `<cts/query/format.h>` includes `<cts/format.h>`, so it is sufficient on its own.


## Rendered values

`<cts/format.h>` provides formatters for the following types:

| Type | Renders as | Example |
| --- | --- | --- |
| `Symbol`, `FieldId`, `NodeID` | the underlying integer | `{:#06x}` on a `Symbol` renders `0x002a` |
| `Point` | `row:column` | `3:5` |
| `Extent<T>`, for any formattable `T` | `[start, end)` | `[4, 8)` |
| `Location` | `byte (point)` | `12 (0:12)` |
| `Error` | `message()`, followed by ` at offset N` and `: name` when present | `query syntax error at offset 9` |
| `SymbolType`, `ErrorKind`, `VisitAction`, `ChildScope` | the enumerator name | `Supertype`, `VisitorDuplicate` |
| `Node` | `type [start, end)` | `number [1, 2)` |

`<cts/query/format.h>` provides formatters for these query types:

| Type | Renders as |
| --- | --- |
| `CaptureId`, `PredicateTokenId`, `PatternIndex` | the underlying integer |
| `Quantifier`, `ProgressAction` | the enumerator name |
| `QueryCapture`, `QueryMatch`, `CaptureResult` | their contents |
| `PredicateArg`, `Predicate` | their contents |


## Format specifications

Each formatter accepts the standard format specification for its rendered value. Types rendered as integers accept the integer format-spec grammar, including `{:x}` and `{:#06x}`. All other types render as text and accept the string format-spec grammar, including `{:>20}`, `{:.5}`, and `{:{}}`.


## Allocation behavior

Formatting a `ts::Node` with `{}` does not allocate. Any non-empty format specification, including width, precision, or alignment, requires the renderer to measure the text first, so it buffers the text in a `std::string` and may allocate.


## Formatter ownership

Avoiding `<cts/format.h>` to provide your own formatter. To avoid ODR, this requires that no translation unit in the program includes `<cts/format.h>`.
