# cpp-tree-sitter

`cpp-tree-sitter` is a modern, lightweight **C++11 (and newer)** wrapper around the [tree-sitter](https://github.com/tree-sitter/tree-sitter/) parsing library. 
It provides a clean, RAII-compliant interface and robust CMake integration to simplify working with syntax trees in C++ projects.

## Key Features
* **Modern RAII API:** Automatic memory management for `Parsers`, `Trees`, `Nodes`, and `Cursors` using standard smart pointers.
* **Broad Compatibility:** Supports **C++11, C++14, C++17, C++20 and C++23**. It automatically leverages modern features like `std::string_view`, `std::concepts` and `std::expected` if available, while providing fallbacks for older standards.
* **STL-style Iterators:** Use standard `for` loops to iterate over child nodes.
* **Tree Visitor:** A built-in Depth-First Search (DFS) visitor (`ts::visit`) for easy tree traversal.
* **Wasm Support:** High-level wrappers for WebAssembly-based grammars.
* **Query Engine:** Full support for tree-sitter queries (patterns and captures).
* **CMake Integration:** Seamless dependency management via [CPM.cmake](https://github.com/cpm-cmake/cpm.cmake).

## Requirements
* **Compiler:** C++11 compatible or newer (C++20/23 recommended).
* **Build System:** [CMake](https://cmake.org/) 3.30 or newer.

## Using in a CMake Project
The easiest way to integrate `cpp-tree-sitter` is via CPM. Adding this wrapper automatically makes the core `tree-sitter` library available as well.

```cmake
cmake_minimum_required(VERSION 3.30)

project(MyParser)

set(CMAKE_CXX_STANDARD 17) # Works with 11, 14, 17, 20, 23

include(cmake/CPM.cmake)

# Download the wrapper and tree-sitter core
CPMAddPackage(
    NAME cpp-tree-sitter
    GIT_REPOSITORY https://github.com/nsumner/cpp-tree-sitter.git
    GIT_TAG main
)

# Download a grammar (e.g., JSON) and make it a CMake target
CPPTSAddGrammar(
    NAME tree-sitter-json
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-json.git
    VERSION 0.24.8
)

add_executable(demo main.cpp)
target_link_libraries(demo PRIVATE cpp-tree-sitter tree-sitter-json)
```

## CMake Configuration

`cpp-tree-sitter` provides several CMake options to customize the build process and manage dependencies.

### Build Options

You can set these options using `-DOPTION=VALUE` during the CMake configuration phase.

|             Option            |      Default      |                                  Description                                   |
|:------------------------------|:-----------------:|:-------------------------------------------------------------------------------|
|     `CPP_TS_BUILD_TESTS`      | ON (if top-level) |                       Build unit tests for the library.                        |
|     `CPP_TS_FEATURE_WASM`     |        OFF        |               Enable WebAssembly support (requires `wasmtime`).                |
|     `CPP_TS_AMALGAMATED`      |        ON         | Build `tree-sitter` core using the amalgamated `lib.c` for faster compilation. |
| `CPP_TS_MSVC_STATIC_RUNTIME`  |        OFF        |                    Link static MSVC runtime (/MT or /MTd).                     |

### Path Variables

These variables control where the library looks for external dependencies or grammars.

* `CPP_TS_WASMTIME_PATH`: Path to a local `wasmtime` installation. If not set and WASM is enabled, CMake will automatically download the appropriate binary for your system.

* `CPP_TS_GRAMMAR_PATH`: A global directory where you store your Tree-sitter grammars. If set, `CPPTSAddGrammar` will first look here before attempting to download from Git.

* `CPP_TS_WASM_DIR`: (Default: `${CMAKE_BINARY_DIR}/wasm_files`) Directory where downloaded `.wasm` grammar files are stored. It is only valid if you pass option `FIND_ALSO_WASM_FILE` or `FIND_ONLY_WASM_FILE` to `CPPTSAddGrammar`

## Grammar Management

The `CPPTSAddGrammar` function automates fetching and building grammars.

|       Argument        |                              Description                              |
|:----------------------|:----------------------------------------------------------------------|
|        `NAME`         |          The name of the grammar (e.g., `tree-sitter-json`).          |
|   `GIT_REPOSITORY`    |                    URL to the grammar repository.                     |
|  `VERSION`/`GIT_TAG`  |                 Specific version or tag to download.                  |
|     `SOURCE_DIR`      |       Path to a local grammar source (overrides Git download).        |
| `FIND_ALSO_WASM_FILE` | If set, attempts to download/find the `.wasm` binary for the grammar. |
| `FIND_ONLY_WASM_FILE` | Skip building the static library and only look for the `.wasm` file.  |

### Helper Functions

* `CPPTSCopyWasmtime(TARGET <target>)`: (Windows) Copies `wasmtime.dll` to the target's output directory.

## Quick Start Example

This example demonstrates parsing a JSON string and using the visitor to inspect nodes.

```cpp
#include <iostream>
#include <string>
#include <functional>
#include <cpp-tree-sitter.h>

// Extern declaration for the grammar function
extern "C" TSLanguage* tree_sitter_json();

int main() {
    // Initialize language and parser
    ts::Language language = tree_sitter_json();
    ts::Parser parser{language};

    // Parse source code into a syntax tree
    std::string code = "[1, null, \"example\"]";
    ts::Tree tree = parser.parseString(code);
    ts::Node root = tree.getRootNode();

    // Use the Visitor for easy traversal (New in this fork)
    ts::visit(root, [](ts::Node node) -> bool {
        if (node.isNamed()) {
            std::cout << "Node: " << node.getType() << " at " 
                      << node.getByteRange().start << "\n";
        }
        return false;
    });

    // Or use STL-style iteration
    for (auto child : ts::Children{root}) {
        std::cout << "Child type: " << child.getType() << "\n";
    }

    return 0; // Resources are cleaned up automatically via RAII
}
```

## Technical Improvements

### Memory & Safety
Implementation of a **Full RAII** architecture. It uses specialized `FreeHelper` functors for C-allocated `strings` and `shared_ptr` for `Language` objects to prevent use-after-free errors.

### Compatibility Layer
The library detects the C++ standard version to enable modern features like `std::optional`, C++20 `concepts` or C++23 `std::expected` dynamically, while providing a custom `StringView` fallback for C++11/14 environments.

### Extended API
* **Queries:** Perform pattern matching with `Query` and `QueryCursor` classes.
* **Wasm:** Load grammars in WebAssembly environments via `WasmStore` and `WasmEngine`.
* **Cross-Platform:** Fixed Windows-specific issues regarding file descriptors and DLL management (see `CPPTSCopyWasmtime`).

## License
This project is licensed under the [**MIT License**](LICENSE).