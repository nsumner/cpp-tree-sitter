cmake_minimum_required(VERSION 3.28...4.2)
project(cts-consumer LANGUAGES C CXX)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/CPM.cmake)

# Pull in this repository's checkout like an end user with CPM.
CPMAddPackage(NAME cpp-tree-sitter SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../..)

# Pinned with GIT_TAG rather than the VERSION the in-tree build uses, so both
# ways of naming a revision are exercised without a second grammar to fetch.
add_grammar_from_repo(
  NAME tree-sitter-json
  REPO https://github.com/tree-sitter/tree-sitter-json.git
  GIT_TAG v0.21.0
)

# Build a grammar as a loadable module, so the module helper is exercised
# from a consuming project rather than only in-tree.
#
# This points at the in-repo test grammar rather than fetching one. The
# helper does not care which grammar it builds.
add_grammar_module(
  NAME ctstest-module
  PATH ${CMAKE_CURRENT_LIST_DIR}/../grammars/ctstest
  OUTPUT_NAME ctstest
)

add_executable(demo demo.cpp)
target_compile_features(demo PRIVATE cxx_std_23)
target_link_libraries(demo PRIVATE
  tree-sitter-json
  cpp-tree-sitter
  cts::cpp-tree-sitter-dynamic
)
# A module is not linked by anything, so nothing else orders it before demo.
add_dependencies(demo ctstest-module)
target_compile_definitions(demo PRIVATE
  CTS_CONSUMER_CTSTEST_MODULE="$<TARGET_FILE:ctstest-module>"
)
