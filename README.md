# cpp-tree-sitter

... is a simple C++ and CMake wrapper around tree-sitter. This project provides
CMake definitions and a C++ wrapper that help with
* managing tree-sitter and tree-sitter grammars as dependencies
* accessing basic tree-sitter APIs for parse tree inspection

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
  GIT_TAG v0.0.1
)

# Downloads a tree-sitter grammar from github and makes it available as a
# cmake library target.
add_grammar_from_repo(tree-sitter-json                 # Defines the library name for a grammar
  https://github.com/tree-sitter/tree-sitter-json.git  # Repository URL of a tree-sitter grammar
  0.19.0                                               # Version tag for the grammar
)

# Use the library in a demo program.
add_executable(demo)
target_sources(demo
  PRIVATE
    demo.cpp
)
target_link_libraries(demo
  tree-sitter-json
  cpp-tree-sitter
)
```

Translating the parsing and tree inspection operations from the example to
use the C++ wrappers then yields a `demo.cpp` like:

```cpp
#include <cassert>
#include <cstdio>
#include <memory>
#include <string>

#include <cpp-tree-sitter.h>


extern "C" {
TSLanguage* tree_sitter_json();
}


int main() {
  // Create a language and parser.
  ts::Language language = tree_sitter_json();
  ts::Parser parser{language};

  // Parse the provided string into a syntax tree.
  std::string sourcecode = "[1, null]";
  ts::Tree tree = parser.parseString(sourcecode);

  // Get the root node of the syntax tree. 
  ts::Node root = tree.getRootNode();

  // Get some child nodes.
  ts::Node array = root.getNamedChild(0);
  ts::Node number = array.getNamedChild(0);

  // Check that the nodes have the expected types.
  assert(root.getType() == "document");
  assert(array.getType() == "array");
  assert(number.getType() == "number");

  // Check that the nodes have the expected child counts.
  assert(root.getNumChildren() == 1);
  assert(array.getNumChildren() == 5);
  assert(array.getNumNamedChildren() == 2);
  assert(number.getNumChildren() == 0);

  // Print the syntax tree as an S-expression.
  auto treestring = root.getSExpr();
  printf("Syntax tree: %s\n", treestring.get());

  return 0;
}
```

In particular, some of the underlying APIs now use method calls for
easier discoverability, and resource cleaning is automatic.
