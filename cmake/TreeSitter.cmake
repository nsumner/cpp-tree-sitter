# Resolves tree-sitter and defines the `tree-sitter` C library target. Three
# things can satisfy it, checked in this order:
#
#   1. CTS_SYSTEM_TREE_SITTER=ON: wrap a tree-sitter already installed on the
#      system, discovered through pkg-config.
#   2. A `tree-sitter` target a parent project defined before adding this
#      project. Use it as-is. Defining a target with the same name opts-in.
#   3. Otherwise fetch a known-good tree-sitter with CPM and build it here.


option(CTS_SYSTEM_TREE_SITTER "Use an installed tree-sitter instead of fetching" OFF)

if(CTS_SYSTEM_TREE_SITTER)

  # GLOBAL makes the imported target visible in every directory scope, which
  # the exported package file depends on.
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(system_tree_sitter REQUIRED IMPORTED_TARGET GLOBAL tree-sitter)

  # A thin wrapper, so that the target this project exports is one it owns.
  add_library(tree-sitter INTERFACE)
  add_library(cts::tree-sitter ALIAS tree-sitter)
  target_link_libraries(tree-sitter
    INTERFACE
      PkgConfig::system_tree_sitter
  )
  set(CTS_TREE_SITTER_IS_OURS TRUE)

elseif(NOT TARGET tree-sitter AND NOT TARGET cts::tree-sitter)

  CPMAddPackage(
    NAME tree-sitter
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter.git
    VERSION 0.26.11
    DOWNLOAD_ONLY YES
  )

  # This project requires a tree-sitter source checkout. If CPM satisfies the
  # request from an installed package instead, there is nothing to build.
  if(NOT tree-sitter_SOURCE_DIR)
    message(FATAL_ERROR
      "cpp-tree-sitter: CPM did not provide sources for 'tree-sitter'.\n"
      "\n"
      "This happens when CPM_USE_LOCAL_PACKAGES or "
      "CPM_LOCAL_PACKAGES_ONLY causes find_package(tree-sitter) to satisfy the "
      "request from an installed package.\n"
      "\n"
      "To use an installed tree-sitter library, configure with "
      "-DCTS_SYSTEM_TREE_SITTER=ON, or define a `tree-sitter` target before "
      "adding this project.")
  endif()

  add_library(tree-sitter STATIC)
  add_library(cts::tree-sitter ALIAS tree-sitter)
  target_sources(tree-sitter
    PRIVATE
      "${tree-sitter_SOURCE_DIR}/lib/src/lib.c"
  )
  target_compile_features(tree-sitter PRIVATE c_std_11)
  if(NOT DEFINED CMAKE_POSITION_INDEPENDENT_CODE)
    set_target_properties(tree-sitter PROPERTIES POSITION_INDEPENDENT_CODE ON)
  endif()
  target_include_directories(tree-sitter
    PRIVATE
      $<BUILD_INTERFACE:${tree-sitter_SOURCE_DIR}/lib/src>
  )

  # The public headers travel with the target so that installing it installs
  # them. They are marked SYSTEM so that consumers' warning flags stop at them.
  file(GLOB_RECURSE ts_public_headers "${tree-sitter_SOURCE_DIR}/lib/include/*.h")
  target_sources(tree-sitter PUBLIC
    FILE_SET HEADERS
    BASE_DIRS "${tree-sitter_SOURCE_DIR}/lib/include"
    FILES ${ts_public_headers}
  )
  set_target_properties(tree-sitter PROPERTIES
    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "$<BUILD_INTERFACE:${tree-sitter_SOURCE_DIR}/lib/include>"
  )

  set(CTS_TREE_SITTER_IS_OURS TRUE)

endif()
