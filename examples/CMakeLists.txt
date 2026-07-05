# The test suite defines the same grammar target. It is reused when both tests
# and examples are enabled.
if(NOT TARGET tree-sitter-json)
  # This macro will fetch tree-sitter-json from github and make it
  # available as a library target that you can link against.
  add_grammar_from_repo(
    NAME tree-sitter-json
    REPO https://github.com/tree-sitter/tree-sitter-json.git
    VERSION 0.21.0
  )
endif()

foreach(demo
    parse_demo
    query_demo
    query_advanced_demo
    visitor_demo
    fold_demo
    typed_demo
    edit_demo)
  add_executable(${demo} ${demo}.cpp)
  target_link_libraries(${demo} PRIVATE
    # This cpp-tree-sitter library makes the wrapper library usable.
    cts::cpp-tree-sitter

    # And the library we fetched with add_grammar_from_repo can be
    # directly linked in.
    tree-sitter-json

    # This last dependency is just for extra warnings during development.
    # Users should omit it in their own build configuration.
    cts-dev-warnings
  )
endforeach()


# This demo loads a grammar at run time instead of linking one. It links
# against an *alternative* library (cpp-tree-sitter-dynamic) to drive the
# process.
add_executable(load_demo load_demo.cpp)
target_link_libraries(load_demo PRIVATE
  # This cpp-tree-sitter library pulls in the base wrappers but also enables
  # the loader.h header and dynamic plugin loading.
  cts::cpp-tree-sitter-dynamic

  # This last dependency is just for extra warnings during development.
  # Users should omit it in their own build configuration.
  cts-dev-warnings
)
