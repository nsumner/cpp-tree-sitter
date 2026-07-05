# Loading grammars at run time

This page describes loading a module at run time and converting it to a `ts::Language`.

A grammar does not need to be compiled into your program. Tree-sitter parsers are commonly distributed as loadable modules such as `json.so` or `python.so`. Neovim, Helix, and the tree-sitter CLI commonly install such modules. Dynamic grammar loading is opt-in and POSIX-only for now.

The features for loading grammars build on top of each other in three layers: (1) basic grammar loading, (2) search paths for grammars, and (3) common search paths for, e.g., editors.


## Opting in

Link the dynamic-loader target and include its header:

```cmake
add_executable(demo demo.cpp)
target_link_libraries(demo PRIVATE
  cpp-tree-sitter
  cts::cpp-tree-sitter-dynamic
)
```

```cpp
#include <cpp-tree-sitter.h>
#include <cts/loader.h>
#include <cts/loader/format.h>   // optional: formatters for the error type
```

`<cts/loader.h>` is intentionally not included through `<cpp-tree-sitter.h>`. It requires `<filesystem>`, the dynamic loader, and an additional link dependency that are otherwise not required.

Using the header without linking `cts::cpp-tree-sitter-dynamic` will produce an error.


## Layer 1: Loading a named file

`ts::loadGrammar` loads a grammar module from a path:

```cpp
auto language = ts::loadGrammar("/usr/lib/nvim/parser/json.so");
if (!language) {
  std::println(stderr, "error: {}", language.error());
  return EXIT_FAILURE;
}

auto parser = ts::Parser::create(*language);
```

By default, the entry-point name is derived from the file stem:

- `json.so` uses `tree_sitter_json`.
- `c-sharp.so` uses `tree_sitter_c_sharp`.

Dashes become underscores to match common parser naming conventions.

You can specify the entry point explicitly when the file name does not identify the grammar. For example, `libtree-sitter-json.so` would otherwise imply `tree_sitter_libtree_sitter_json`:

```cpp
auto language = ts::loadGrammar("/opt/parsers/libtree-sitter-json.so",
                                ts::EntryPoint{"tree_sitter_json"});
```


### Errors

Loading failures use `ts::GrammarLoadError` instead of `ts::Error` to capture more information about the failure than other `Error`s.

| `Kind` | What happened |
| --- | --- |
| `FileMissing` | Nothing exists at the path. |
| `LibraryOpenFailed` | `dlopen` rejected the module. `detail` contains the loader's message. |
| `EntryPointMissing` | The module opened but does not provide the requested symbol. `detail` names the symbol. |
| `LanguageNull` | The entry point ran and returned no language. |
| `AbiIncompatible` | The grammar ABI is outside the runtime's accepted range. `detail` contains both versions. |
| `NotFoundInSearchPath` | No candidate file was found while searching. `detail` lists every searched directory. |

`message()` returns the fixed message for the error kind. `path` contains the attempted file path. For `NotFoundInSearchPath`, where no single file was attempted, it contains the language name. `detail` contains additional information.

`<cts/loader/format.h>` combines these fields, so:

```cpp
std::println("{}", error)
```

prints:

```text
grammar library could not be opened: /opt/parsers/json.so (…linker text…)
```


### ABI compatibility

`ts::abiIsSupported(version)` can check compatibility before loading.

The accepted range is tree-sitter's `TREE_SITTER_MIN_COMPATIBLE_LANGUAGE_VERSION` through `TREE_SITTER_LANGUAGE_VERSION`. For the tree-sitter version presently supported by this library, the range is 13 through 15. The loader performs the same check so that an incompatible grammar is reported early.


## Layer 2: Finding a grammar by name

`ts::GrammarSearchPath` resolves a language name through an ordered list of directories:

```cpp
ts::GrammarSearchPath const grammars{std::vector<std::filesystem::path>{
  "/home/me/.local/share/nvim/site/parser",
  "/usr/lib/nvim/parser",
}};

auto language = grammars.load("json");
```

Its constructor is `explicit` and takes directories to search by value. A language name must be a single file-name component. Path-like names, including `/opt/parsers/evil`, `../evil`, and `sub/evil`, do not match:

- `find` returns `std::nullopt`.
- `load` returns `NotFoundInSearchPath`.

And the helper APIs for loading a library are then:

- `find(name)` resolves a name to a path without loading it. Directories are searched in order. Within each directory, `.so` is tried first, followed by `.dylib` on macOS.
- `load(name)` finds and loads a grammar, deriving its entry point as in layer 1.
- `load(name, EntryPoint{...})` overrides the derived entry point.
- `available()` lists available grammar candidates.
- `directories()` returns a `std::span<std::filesystem::path const>` over the supplied directories, in order.

The name is just the file stem for the `search`/`load`, for example:

- `load("c++")` searches for `c++.so` but does not find `cpp.so`.
- `load("json")` does not match `libjson.so`.

`available()` returns sorted, deduplicated stems for every regular file in the search path that looks like it might be a module. This supports programs that adapt to parsers installed by a user:

```cpp
for (std::string const& name : grammars.available()) {
  std::println("maybe a grammar: {}", name);
}
```

When a name is not found, `detail` lists every searched directory so they can be inspected.


## Layer 3: Editor directory layouts

`ts::layout` provides presets for directories commonly used by editors. Each returns a plain `std::vector<std::filesystem::path>`.

| Preset | Covers |
| --- | --- |
| `nvimTreesitter()` | `parser/` under the XDG configuration and data directories read by Neovim, distribution and local-prefix paths, and `$VIMRUNTIME/parser` for bundled parsers |
| `helix()` | `grammars/` under `$HELIX_RUNTIME` and the XDG configuration home |
| `treeSitterCli()` | `tree-sitter/lib` under the XDG cache home, where the CLI stores compiled grammars |

Each preset returns a value that can be extended as desired.

```cpp
auto directories = ts::layout::nvimTreesitter();
directories.emplace_back("/home/me/.local/share/nvim/lazy/nvim-treesitter/parser");
ts::GrammarSearchPath const grammars{std::move(directories)};
```

For Neovim, the preset is an estimate of a `runtimepath` it cannot inspect. Neovim can provide the authoritative result:

```sh
nvim --headless -c 'lua io.write(table.concat(vim.api.nvim_get_runtime_file("parser", true), "\n"))' -c quit
```

Pass those directories directly to `GrammarSearchPath` when you need the exact paths for a configuration.

[`examples/load_demo.cpp`](../examples/load_demo.cpp) shows all three layers in a runnable program.


## Limitations

* Presets will include locations like `XDG_DATA_DIRS`, `XDG_CONFIG_HOME`, `XDG_CACHE_HOME`, `VIMRUNTIME`, and `HELIX_RUNTIME`. These variables are assumed to be trusted.
* A grammar module contains only a parser. Highlight, locals, and tags queries remain in the grammar's source repository. If you need queries, get them from a source checkout.
* `load` resolves a name to the first matching file and loads that file. If it is corrupt, ABI-incompatible, or not a grammar, `load` reports that error. It does not silently try a later directory.
* IDE plugin locations are determined by user configuration and inherently best-effort only.


## Unresolved `ts_current_malloc`

A plugin built with `TREE_SITTER_REUSE_ALLOCATOR` can produce the following error:

```text
grammar library could not be opened: /opt/parsers/json.so
  (/opt/parsers/json.so: undefined symbol: ts_current_malloc)
```

Building with `TREE_SITTER_REUSE_ALLOCATOR` routes allocations through `ts_current_malloc`, a pointer in the tree-sitter runtime. This allows the grammar and host to share an allocator. When linked statically, the symbol resolves within the program. When loaded as a module, it must be available from the running process. Programs that link tree-sitter statically do not export their symbols by default, so the symbol is unavailable.

`loadGrammar` just reports this as `LibraryOpenFailed` with the dynamic loader's text, rather than allowing later misbehavior. Build the module without `TREE_SITTER_REUSE_ALLOCATOR` to support it.


## Building loadable modules

`add_grammar_module` builds a grammar as a `MODULE` library. The result is position-independent, loadable with `dlopen`, and named after the grammar rather than the CMake target.

```cmake
add_grammar_module(
  NAME tree-sitter-json
  REPO https://github.com/tree-sitter/tree-sitter-json.git
  VERSION 0.21.0
)
```

This produces `json.so` with no `lib` prefix and `tree-sitter-` removed from `NAME`. This matches the file names installed by the ecosystem and probed by `GrammarSearchPath`.

`OUTPUT_NAME` overrides the derived file name.

`add_grammar_module` accepts the same arguments as its static counterparts:

- `PATH` specifies an existing checkout.
- `REPO` with `VERSION` fetches a checkout.
- `SUBDIRECTORY` selects a grammar within a repository containing several grammars.

Exactly one of `PATH` or `REPO`must be specified.

CMake target names are global. Building the same grammar statically and as a module therefore needs distinct target names. Use `OUTPUT_NAME` to retain the module's intended file name:

```cmake
add_grammar_from_repo(NAME tree-sitter-json REPO ... VERSION 0.21.0)
add_grammar_module(NAME json-module PATH ... OUTPUT_NAME json)
```

A `MODULE` library is not linked by another target, so it does not automatically build before the program that loads it. Add the dependency explicitly and pass the module path to the program:

```cmake
add_dependencies(demo json-module)
target_compile_definitions(demo PRIVATE
  DEMO_JSON_MODULE="$<TARGET_FILE:json-module>")
```
