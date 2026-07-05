# Use the JSON grammar from the README as the main test grammar. It pins
# expectations consistently and is small, stable, and quick to fetch.
# v0.21.0 is deliberately older. It generates an ABI-14 parser, so the
# tests exercise a grammar without ABI-15 features.
add_grammar_from_repo(
  NAME tree-sitter-json
  REPO https://github.com/tree-sitter/tree-sitter-json.git
  VERSION 0.21.0
)

# Use an in-repo vendored grammar to control testing of newer ABIs and grammar
# features as/if they are handled by the library. For example, we can check
# supertype metadata and alias symbols.
add_grammar_from_path(
  NAME tree-sitter-ctstest
  PATH ${CMAKE_CURRENT_SOURCE_DIR}/grammars/ctstest
)

# The same grammar again as a loadable module to exercise runtime loading.
add_grammar_module(
  NAME ctstest-module
  PATH ${CMAKE_CURRENT_SOURCE_DIR}/grammars/ctstest
  OUTPUT_NAME ctstest
)

# Fixture grammars for the loader's failure paths.
add_grammar_module(
  NAME nulllang-module
  PATH ${CMAKE_CURRENT_SOURCE_DIR}/grammars/nulllang
  OUTPUT_NAME nulllang
)
add_grammar_module(
  NAME oldabi-module
  PATH ${CMAKE_CURRENT_SOURCE_DIR}/grammars/oldabi
  OUTPUT_NAME oldabi
)


add_executable(cts-tests)
target_sources(cts-tests
  PRIVATE
    main.cpp
    include_hygiene_tests.cpp
    parse_tests.cpp
    navigation_tests.cpp
    field_tests.cpp
    lifetime_tests.cpp
    error_tests.cpp
    language_tests.cpp
    edit_tests.cpp
    query_tests.cpp
    predicate_tests.cpp
    debug_tests.cpp
    visitor_tests.cpp
    typed_tests.cpp
    types_tests.cpp
    format_tests.cpp
    mapped_range_tests.cpp
    loader_tests.cpp
    loader_error_tests.cpp
    search_path_tests.cpp
    layout_tests.cpp
)
target_include_directories(cts-tests
  SYSTEM PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(cts-tests
  PRIVATE
    cpp-tree-sitter
    cpp-tree-sitter-dynamic
    tree-sitter-json
    tree-sitter-ctstest
    cts-dev-warnings
)
add_dependencies(cts-tests
  ctstest-module
  nulllang-module
  oldabi-module
)

# Tests that need to build parser directories or write junk files do it
# under the build tree, so no fixed shared names collide.
target_compile_definitions(cts-tests
  PRIVATE
    CTS_TEST_CTSTEST_MODULE="$<TARGET_FILE:ctstest-module>"
    CTS_TEST_NULLLANG_MODULE="$<TARGET_FILE:nulllang-module>"
    CTS_TEST_OLDABI_MODULE="$<TARGET_FILE:oldabi-module>"
    CTS_TEST_SCRATCH_DIR="${CMAKE_CURRENT_BINARY_DIR}/scratch"
)

add_test(NAME cts-tests COMMAND cts-tests)

# tree-sitter itself has a possible leak in one ts_query_new error path, so
# suppress just that leak when running under LeakSanitizer.
set_tests_properties(cts-tests PROPERTIES
  ENVIRONMENT "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/tree-sitter.lsan.supp:print_suppressions=0"
)

# Configure, build, and run test/consumer as a standalone project for
# end-to-end CPM testing.
add_test(NAME consumer-build
  COMMAND ${CMAKE_CTEST_COMMAND}
    --build-and-test ${CMAKE_CURRENT_SOURCE_DIR}/consumer
                     ${CMAKE_CURRENT_BINARY_DIR}/consumer-build
    --build-generator ${CMAKE_GENERATOR}
    --test-command ${CMAKE_CURRENT_BINARY_DIR}/consumer-build/demo
)
set_tests_properties(consumer-build PROPERTIES
  LABELS slow
  TIMEOUT 600
)
