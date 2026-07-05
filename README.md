# cpp-tree-sitter

... is a simple C++ and CMake wrapper around tree-sitter. This project provides
CMake definitions and a C++ wrapper that help with
* managing tree-sitter and tree-sitter grammars as dependencies
* accessing basic tree-sitter APIs for parse tree inspection

There is also opt-in support for smoothing out C++ interactions with
* formatters for tree-sitter wrapper types
* queries
* visitor and fold operations over tree-sitter trees


## Requirements

... for version 0.1.0+ include
* C++23 (gcc 14+, clang 18+ with libstdc++) (for `std::expected`, `std::print`, ...)
* CMake 3.28+

Tag v0.0.3 should still support older versions of C++ and CMake.


## Using in a CMake project

... requires the [CPM](https://github.com/cpm-cmake/CPM.cmake) CMake module
for fetching and managing dependencies from github. Adding `cpp-tree-sitter`
as a CPM dependency makes `cpp-tree-sitter` available as a library and
provides a function, `add_grammar_from_repo`, that will download and
make available a standard tree-sitter grammar on GitHub as a library.

The tree-sitter parser
[example](https://tree-sitter.github.io/tree-sitter/using-parsers#an-example-program)
can be reproduced in a CMake project with CPM by including the following in
`CMakeLists.txt`:

```cmake
include(cmake/CPM.cmake)

# Downloads this wrapper library and tree-sitter.
# Makes them available via the `cpp-tree-sitter` CMake library target.
CPMAddPackage(
  NAME cpp-tree-sitter
  GIT_REPOSITORY https://github.com/nsumner/cpp-tree-sitter.git
  GIT_TAG v0.1.0
)

# Downloads a tree-sitter grammar from github and makes it available as a
# cmake library target.
add_grammar_from_repo(
  NAME tree-sitter-json                                       # Defines the library name for a grammar
  REPO https://github.com/tree-sitter/tree-sitter-json.git    # Repository URL of a tree-sitter grammar
  VERSION 0.21.0                                              # Version tag for the grammar
)

# Use the library in a demo program.
add_executable(demo)
target_sources(demo
  PRIVATE
    demo.cpp
)
target_link_libraries(demo
  PRIVATE
    tree-sitter-json
    cts::cpp-tree-sitter
)
```

Translating the parsing and tree inspection operations from the example to
use the C++ wrappers then yields a `demo.cpp` like:

```cpp
#include <cstdlib>
#include <print>
#include <string_view>

#include <cpp-tree-sitter.h>
#include <cts/format.h>

extern "C" TSLanguage* tree_sitter_json();

int
main() {
  auto parser = ts::Parser::create(tree_sitter_json());
  if (!parser) {
    std::println(stderr, "error: {}", parser.error());
    return EXIT_FAILURE;
  }

  constexpr std::string_view sourcecode = "[1, null]";
  const auto tree = parser->parse(sourcecode);
  if (!tree) {
    std::println(stderr, "error: {}", tree.error());
    return EXIT_FAILURE;
  }

  const ts::Node root = tree->getRootNode();
  std::println("root: {}", root.getType());

  for (const ts::Node child : root.getNamedChild(0)->getNamedChildren()) {
    std::println("  {}: '{}'", child, child.getSourceRange(sourcecode));
  }

  std::println("Syntax tree: {}", root.getSExpr());
  return EXIT_SUCCESS;
}
```

In particular, some of the underlying APIs now use method calls for
easier discoverability, and resource cleaning is automatic.

Core functionality can be included with just `<cpp-tree-sitter.h>`, but extra
features that might be more expensive are opt-in via:
* `<cts/format.h>` - modern C++ formatters
* `<cts/query.h>` - query processing
* `<cts/typed.h>` - a type-dispatching visitor
* `<cts/loader.h>` - dynamically loading grammar plugins


## Examples

... can be found in the [`examples/`](examples/) directory. Each one builds
and runs, and each carries details and explanation in comments at the top of
the file.

* [`parse_demo.cpp`](examples/parse_demo.cpp) — basic parsing and tree inspection
* [`query_demo.cpp`](examples/query_demo.cpp) — running a query, and how predicates are evaluated
* [`query_advanced_demo.cpp`](examples/query_advanced_demo.cpp) — restricting, limiting, cancelling, and narrowing a query
* [`visitor_demo.cpp`](examples/visitor_demo.cpp) — walking a tree with `ts::visit` and `ts::preorder`
* [`fold_demo.cpp`](examples/fold_demo.cpp) — computing a value with `ts::fold`
* [`typed_demo.cpp`](examples/typed_demo.cpp) — dispatching on node type with `ts::typed`
* [`edit_demo.cpp`](examples/edit_demo.cpp) — incremental parsing after an edit
* [`load_demo.cpp`](examples/load_demo.cpp) — loading a grammar an editor already installed

[`examples/CMakeLists.txt`](examples/CMakeLists.txt) shows how to wire them up
against `cts::cpp-tree-sitter` and a grammar fetched from github or against
`cts::cpp-tree-sitter-dynamic` and a dynamically loaded grammar plugin.

Longer explanations that do not fit in an example live in [`docs/`](docs/):
[build and install configurations](docs/cmake.md),
[lifetimes and types](docs/lifetimes-and-types.md),
[formatting](docs/formatting.md), and
[loading grammars at run time](docs/dynamic-grammars.md).


## Using the API


### Iterating over children

... can be done with `Node::getChildren()` and `Node::getNamedChildren()`
ranges. They return a `ChildrenView`, a lazy `std::ranges::view` over a
node's children:

```cpp
for (ts::Node child : root.getNamedChild(0)->getNamedChildren()) {
  std::println("  {}: '{}'", child.getType(), child.getSourceRange(sourcecode));
}

auto newList = node.getChildren()
  | std::views::filter(...)
  | std::views::transform(...)
  | std::ranges::to<std::vector>();
```

A `ChildrenView` holds its parent `Node` by value, so it is copyable and
behaves intuitively.


### Errors and lifetimes

... are managed through API types. Operations that can fail return
`std::expected<T, ts::Error>`. Questions about whether something exists
return `std::optional<T>`.

```cpp
auto parser = ts::Parser::create(tree_sitter_json());
if (!parser) {
  std::println(stderr, "error: {}", parser.error());
  return EXIT_FAILURE;
}
```

`Tree` and `Parser` own their tree-sitter resources and free them when
destroyed. `Node` borrows from a `Tree`. Keep the owning `Tree` alive for
as long as you hold `Node`s from it.

[`docs/lifetimes-and-types.md`](docs/lifetimes-and-types.md) lists which
operations can fail, why, borrowing details.


### Visiting, walking, and folding

... operations help with traversing and computing values from a parse tree.

`ts::visit` walks a subtree with a visitor object. Any object with `onEnter()`
can be used as a visitor.

```cpp
struct CountNamed {
  int count = 0;

  ts::VisitAction onEnter(ts::Node node) {
    count += node.isNamed() ? 1 : 0;
    return ts::VisitAction::Continue;   // or SkipChildren, or Stop
  }
};

CountNamed counter;
ts::visit(root, counter);
```

An optional `onLeave(ts::Node)` fires on the way back out. `visit` is
cursor-based, so it neither recurses nor allocates. For the same walk as a lazy
range, `ts::preorder` composes with `<ranges>`:

```cpp
auto numbers = ts::preorder(root)
  | std::views::filter([](ts::Node n) { return n.getType() == "number"; });
```

`ts::fold` computes a value bottom-up instead, building each node's result from
its children's results:

```cpp
struct MaxDepth {
  int onNode(ts::Node node, std::span<int> children) {
    int deepest = 0;
    for (int depth : children) { deepest = std::max(deepest, depth); }
    return node.isNamed() ? deepest + 1 : deepest;
  }
};

MaxDepth folder;
int depth = ts::fold<int>(root, folder);
```

The fold result type is explicit at the call site. Leaves in the tree receive
an empty `children` span. Unlike `visit`, `fold` allocates to keep a stack of
child-result frames.

See [`examples/visitor_demo.cpp`](examples/visitor_demo.cpp) and
[`examples/fold_demo.cpp`](examples/fold_demo.cpp).


### Typed visitors

... with `ts::typed` enable dispatching to different methods based on the
type of the node being visited.

```cpp
#include <cts/typed.h>

auto visitor = ts::typed(language,
  ts::on<"number">([&](ts::Node node) { numbers.push_back(node); }),
  ts::on<"comment", "string">([](ts::Node) {
    return ts::VisitAction::SkipChildren;      // one handler, several types
  }),
  ts::otherwise([](ts::Node) { /* everything else, punctuation included */ }));
if (!visitor) { /* error.name is the handler or node type at fault */ }

ts::visit(root, *visitor);                     // an ordinary TreeVisitor
```

Handler names are checked against the language when the visitor is built, so
mistyped grammar labels fail up front. Supertypes are supported, and matching
uses a lookup table that is most-specific-first. A visitor first tries the
exact type, then the nearest supertype like `_expression`, then `otherwise`.

`ts::typedFold<R>` combines that dispatch with `fold`-style value production.
Every handler returns an `R`, and child results are pulled on demand:

```cpp
auto sum = ts::typedFold<int>(language,
  ts::on<"number">([&](ts::Node node) { return toInt(node); }),
  ts::otherwise([](ts::Node node, ts::WalkFold<int>& walk) {
    int total = 0;
    for (ts::Node child : node.getNamedChildren()) { total += walk(child); }
    return total;
  }));
int result = sum->run(root);
```

A handler can also take a `ts::Walk&` and drive the traversal itself.
[`examples/typed_demo.cpp`](examples/typed_demo.cpp) shows more.


### Formatting

... is opted into with `<cts/format.h>` and provides expected formatting for
wrapper types.

```cpp
#include <cts/format.h>        // core types
#include <cts/query/format.h>  // query types (extra opt-in for queries)
```

`{}` on a `ts::Node` renders as `number [1, 2)` and allocates nothing, so it is
safe to use inside a visitor. [`docs/formatting.md`](docs/formatting.md) has
the full table of what renders as what.


### Running queries

... is opted-into through `<cts/query.h>`. `ts::Query::create` compiles one
`Query` against a language, and a `ts::QueryCursor` runs it:

```cpp
#include <cts/query.h>

auto query = ts::Query::create(
  tree_sitter_json(),
  "(pair key: (string) @key value: (_) @value)");
if (!query) { /* handle error */ }

// Resolve capture names to ids once ahead of time.
auto key = query->getCaptureId("key");

ts::QueryCursor cursor;
auto matches = cursor.getMatches(*query, root);
if (!matches) { /* wrong language for this tree, or bad options */ }

for (const ts::QueryMatch& match : *matches) {
  if (auto node = match.getNodeFor(*key)) {
    std::println("key: '{}'", node->getSourceRange(source));
  }
}
```

Three rules cover most uses:

* A match and its captures are valid only until the cursor advances. Copy out
  whatever you want to keep.
* Text predicates like `#eq?` and `#match?` are evaluated for you but need
  the source text. Pass `{.source = source}`. A `#match?` also needs a regex
  engine, which you can choose yourself and supply at `create`.
* One cursor runs one query at a time. Use a separate cursor for each
  concurrent iteration.

A query over a large tree can be bounded with a progress callback, which
tree-sitter invokes periodically during execution. Returning
`ts::ProgressAction::Cancel` ends the execution with the results so far.

```cpp
auto matches = cursor.getMatches(*query, root, {
  .onProgress = [deadline](uint32_t /*byteOffset*/) {
    return std::chrono::steady_clock::now() > deadline
      ? ts::ProgressAction::Cancel
      : ts::ProgressAction::Continue;
  }});

for (const ts::QueryMatch& match : *matches) { /* ... */ }

if (cursor.wasCancelled()) { /* ... */ }
```

[`examples/query_demo.cpp`](examples/query_demo.cpp) covers predicates.
More advanced options are shown in
[`examples/query_advanced_demo.cpp`](examples/query_advanced_demo.cpp).


## Building and installing

... depends on CPM as shown at the top of this README.
[`docs/cmake.md`](docs/cmake.md) covers alternative use cases like

* building a grammar you already have on disk, with `add_grammar_from_path`
* repositories holding several grammars, with `SUBDIRECTORY`
* installing the library once and consuming it with `find_package`
* linking against a system tree-sitter, with `-DCTS_SYSTEM_TREE_SITTER=ON`
* providing your own `tree-sitter` target
* building a grammar as a loadable module instead, with `add_grammar_module`


## Loading grammars at run time

... allows you to reuse grammar plugins that you might have installed with
tree-sitter or that might be used with an IDE.

```cpp
#include <cts/loader.h>

ts::GrammarSearchPath const grammars{ts::layout::nvimTreesitter()};
auto language = grammars.load("json");
if (!language) { /* the error names every directory that was searched */ }
```

Support is opt-in and POSIX-only behind a second CMake target,
`cts::cpp-tree-sitter-dynamic`. A loaded grammar is never unloaded, so the
`ts::Language` stays valid for the lifetime of the process.

[`docs/dynamic-grammars.md`](docs/dynamic-grammars.md) shows APIs that can
help to find and load a particular grammar, and
[`examples/load_demo.cpp`](examples/load_demo.cpp) shows them in action.
