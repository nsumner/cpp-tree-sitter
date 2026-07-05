# Build and install configurations

`cpp-tree-sitter` is normally fetched with CPM, as described in the [README](../README.md#using-in-a-cmake-project). This page documents the alternative build and installation configurations.


## Using a grammar already on disk

`add_grammar_from_repo` downloads a grammar from GitHub. Use `add_grammar_from_path` when the grammar already exists locally, such as in a Git clone, vendored directory, or [nvim-treesitter](https://github.com/nvim-treesitter/nvim-treesitter) cache. It builds a target from that directory.

```cmake
add_grammar_from_path(
  NAME tree-sitter-json          # The library name for the grammar
  PATH /path/to/tree-sitter-json # A directory containing the grammar's src/
)
```

For nvim-treesitter, set `PATH` to the cached *sources* in `~/.cache/nvim/tree-sitter-<lang>`. The installed parser directory contains only a compiled `.so` file rather than the `src/parser.c` required for compilation.

Repositories may contain multiple grammars. Use `SUBDIRECTORY` to select one. For example, [tree-sitter-typescript](https://github.com/tree-sitter/tree-sitter-typescript) provides both `typescript` and `tsx` grammars:

```cmake
add_grammar_from_path(
  NAME tree-sitter-typescript
  PATH /path/to/tree-sitter-typescript
  SUBDIRECTORY typescript
)
```

`add_grammar_from_repo` also accepts `SUBDIRECTORY`.

Grammar sources compile in place under `PATH`.


## Pinning a grammar without a version tag

`VERSION 0.21.0`, as used in the README, specifies a *version*. CPM infers the `v0.21.0` tag when fetching the grammar.

`VERSION` does not work for projects that do not follow this tag convention. Use `GIT_TAG` to provide the Git reference unchanged. It accepts a tag, branch, or commit SHA:

```cmake
add_grammar_from_repo(
  NAME tree-sitter-somelang
  REPO https://github.com/example/tree-sitter-somelang.git
  GIT_TAG 3f2a1c9e4b7d8a5f6c0b1e2d3a4f5b6c7d8e9f01
)
```

`GIT_TAG` can track branches such as `main`, but pinning a specific revision is recommended.


## Offline and hermetic builds

Configuration can access the network in three ways:

- `cmake/CPM.cmake` downloads the hash-pinned CPM release that it bootstraps.
- CPM clones tree-sitter.
- CPM clones each grammar.

Only configuration accesses the network. A build tree that has already been configured will compile and test without network access.

Set `CPM_SOURCE_CACHE` to store downloads outside the build tree. Set it as an environment variable or with `-DCPM_SOURCE_CACHE=`, then populate it once on a machine with network access:

```sh
export CPM_SOURCE_CACHE=$HOME/.cache/CPM
cmake -S . -B build          # clones into the cache
```

Later configurations use the cache without network requests, including on a host with no network route. In CI, persist this directory as the build cache. Every job after the first can configure offline.

The cache is keyed by the fetched revision and can only satisfy pins it has already seen. Changing a grammar's `VERSION` or `GIT_TAG` requires another configuration with network access.

If a host cannot download CPM but can access a Git mirror, set `CPM_PATH` to an existing local `CPM.cmake` to skip the bootstrap download.


## Installing the library

Build and install `cpp-tree-sitter` once if it should not be fetched through CPM by every project:

```sh
cmake -S . -B build
cmake --build build -j
cmake --install build --prefix /path/to/prefix
```

Downstream projects can then use `find_package`:

```cmake
find_package(cpp-tree-sitter CONFIG REQUIRED)

add_grammar_from_path(
  NAME tree-sitter-json
  PATH /path/to/tree-sitter-json
)

add_executable(demo demo.cpp)
target_link_libraries(demo
  PRIVATE
    tree-sitter-json
    cts::cpp-tree-sitter
)
```

The installed package provides `cts::cpp-tree-sitter`, `cts::tree-sitter`, `add_grammar_from_path`, and `add_grammar_from_repo`. If the prefix is not on CMake's default search path, configure the consumer with `-DCMAKE_PREFIX_PATH=/path/to/prefix`.

Installation (optional) includes tree-sitter's headers and library. Its version is pinned in `cmake/TreeSitter.cmake`. Install into a project-local prefix to avoid conflicts with a system tree-sitter. See [Using a system tree-sitter](#using-a-system-tree-sitter) for that configuration.


## Using a system tree-sitter

By default, `cpp-tree-sitter` downloads and builds tree-sitter from source. Configure with `-DCTS_SYSTEM_TREE_SITTER=ON` to use an installed version:

```sh
cmake -S . -B build -DCTS_SYSTEM_TREE_SITTER=ON
```

The `tree-sitter` and `cts::tree-sitter` target names remain available to grammars and consumers. In this mode they forward to the system library. Installation then only includes `cpp-tree-sitter` headers and CMake package files omitting tree-sitter's headers and archive.

Projects that use CPM to manage `cpp-tree-sitter` can set [`CPM_USE_LOCAL_PACKAGES`](https://github.com/cpm-cmake/CPM.cmake#cpm_use_local_packages) to prefer locally installed packages for every dependency instead of configuring this project individually.


## Providing your own tree-sitter target

You can also manage `tree-sitter` as part of the project yourself by defining a target named `tree-sitter` before adding `cpp-tree-sitter`. It is then used unchanged. The target should provide tree-sitter 0.26 or later and expose its public headers through its interface.


## Static libraries and position-independent code

To use a shared tree-sitter runtime, use [a system tree-sitter](#using-a-system-tree-sitter).
The `tree-sitter` target and grammar libraries created by `add_grammar_from_path` and `add_grammar_from_repo` always build as `STATIC` and ignore `BUILD_SHARED_LIBS`.

For *grammars* that are loaded rather than linked, use `add_grammar_module`. It creates a `MODULE` library named after the grammar, such as `json.so`:

```cmake
add_grammar_module(
  NAME tree-sitter-json
  REPO https://github.com/tree-sitter/tree-sitter-json.git
  VERSION 0.21.0                 # or GIT_TAG, as with add_grammar_from_repo
)
```

See [loading grammars at run time](dynamic-grammars.md) for details, including loading parsers already installed by an editor.

Static libraries use position-independent code by default unless the host sets `CMAKE_POSITION_INDEPENDENT_CODE`. A `MODULE` library is loaded at run time, so `add_grammar_module` enables position-independent code unconditionally.
