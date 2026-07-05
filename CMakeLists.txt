cmake_minimum_required(VERSION 3.28...4.2)
project(cpp-tree-sitter
  VERSION 0.1.0
  DESCRIPTION "A C++ and CMake wrapper around tree-sitter"
  LANGUAGES C CXX
)

include(GNUInstallDirs)

include(cmake/CPM.cmake)
include(cmake/TreeSitter.cmake)
include(cmake/GrammarFunctions.cmake)


add_library(cpp-tree-sitter INTERFACE)
add_library(cts::cpp-tree-sitter ALIAS cpp-tree-sitter)
set(cts_public_headers
  include/cpp-tree-sitter.h
  include/cts/children.h
  include/cts/common.h
  include/cts/cursor.h
  include/cts/format.h
  include/cts/language.h
  include/cts/node.h
  include/cts/parser.h
  include/cts/query.h
  include/cts/query/format.h
  include/cts/query/ids.h
  include/cts/query/predicates.h
  include/cts/query/query.h
  include/cts/tree.h
  include/cts/typed.h
  include/cts/visitor.h
)
target_sources(cpp-tree-sitter INTERFACE
  FILE_SET HEADERS
  BASE_DIRS include
  FILES ${cts_public_headers}
)
target_compile_features(cpp-tree-sitter INTERFACE cxx_std_23)
target_link_libraries(cpp-tree-sitter INTERFACE tree-sitter)

# Publish the library with dynamic loading separately so that we can
# make sure it manages needed dependencies across systems in an
# opt-in fashion.
add_library(cpp-tree-sitter-dynamic INTERFACE)
add_library(cts::cpp-tree-sitter-dynamic ALIAS cpp-tree-sitter-dynamic)
target_sources(cpp-tree-sitter-dynamic INTERFACE
  FILE_SET HEADERS
  BASE_DIRS include
  FILES include/cts/loader.h include/cts/loader/format.h
)
target_link_libraries(cpp-tree-sitter-dynamic
  INTERFACE
    cpp-tree-sitter
    ${CMAKE_DL_LIBS}
)
target_compile_definitions(cpp-tree-sitter-dynamic INTERFACE CTS_DYNAMIC_GRAMMARS)


option(CTS_BUILD_TESTS "Build cpp-tree-sitter tests" ${PROJECT_IS_TOP_LEVEL})
option(CTS_BUILD_EXAMPLES "Build cpp-tree-sitter examples" ${PROJECT_IS_TOP_LEVEL})

if(CTS_BUILD_TESTS OR CTS_BUILD_EXAMPLES)
  add_library(cts-dev-warnings INTERFACE)
  target_compile_options(cts-dev-warnings INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wconversion>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<CXX_COMPILER_ID:GNU>:-Werror=missing-field-initializers>
    $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Werror=missing-designated-field-initializers>
  )
endif()

if(CTS_BUILD_TESTS)
  set(CMAKE_EXPORT_COMPILE_COMMANDS On)
  enable_testing()
  add_subdirectory(test)
endif()

if(CTS_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()


option(CTS_INSTALL "Generate install rules for cpp-tree-sitter" ${PROJECT_IS_TOP_LEVEL})
if(CTS_INSTALL)

  if(NOT CTS_TREE_SITTER_IS_OURS)
    message(FATAL_ERROR
      "CTS_INSTALL is ON, but the `tree-sitter` target was provided"
      "externally. Disable CTS_INSTALL, or have the parent project"
      "provide install rules for its own `tree-sitter` target."
    )
  endif()

  include(CMakePackageConfigHelpers)

  install(TARGETS cpp-tree-sitter cpp-tree-sitter-dynamic
    EXPORT cpp-tree-sitterTargets
    FILE_SET HEADERS
  )

  # cpp-tree-sitter links against `tree-sitter`, so CMake requires that
  # target to be part of the same export set. Exactly what gets installed
  # with it depends on how tree-sitter was resolved.
  if(CTS_SYSTEM_TREE_SITTER)
    # If we are using the already installed tree-sitter, then  our
    # `tree-sitter` target is just a thin wrapper around the installed one.
    # There is no need to install its headers or library.
    install(TARGETS tree-sitter
      EXPORT cpp-tree-sitterTargets
    )
  else()
    # If we fetched and built tree-sitter, its headers must be installed
    # alongside the library.
    install(TARGETS tree-sitter
      EXPORT cpp-tree-sitterTargets
      FILE_SET HEADERS
    )
  endif()

  install(EXPORT cpp-tree-sitterTargets
    NAMESPACE cts::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cpp-tree-sitter
  )

  # Also write a targets file into the build tree, so consumers can point
  # find_package() at the build directory without installing anything.
  export(EXPORT cpp-tree-sitterTargets
    NAMESPACE cts::
    FILE "${CMAKE_CURRENT_BINARY_DIR}/cpp-tree-sitterTargets.cmake"
  )

  # The package config file loads GrammarFunctions.cmake from its own
  # directory, so a copy must sit next to the config in both the build and
  # the install trees.
  configure_file(cmake/GrammarFunctions.cmake
    "${CMAKE_CURRENT_BINARY_DIR}/GrammarFunctions.cmake" COPYONLY
  )
  install(FILES cmake/GrammarFunctions.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cpp-tree-sitter
  )

  configure_package_config_file(
    cmake/Config.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/cpp-tree-sitterConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cpp-tree-sitter
  )
  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/cpp-tree-sitterConfigVersion.cmake"
    COMPATIBILITY SameMinorVersion
  )
  install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/cpp-tree-sitterConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/cpp-tree-sitterConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cpp-tree-sitter
  )
endif()
