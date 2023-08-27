cmake_minimum_required(VERSION 3.19)
project(cpp-tree-sitter)

set(PACKAGE_NAME cpp-tree-sitter)
set(PACKAGE_VERSION 0.0.1)
set(PACKAGE_STRING "${PACKAGE_NAME} ${PACKAGE_VERSION}")
set(PACKAGE_BUGREPORT "wsumner@sfu.ca")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_compile_options(
  "$<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wconversion>"
  "$<$<AND:$<CXX_COMPILER_ID:GNU,Clang,AppleClang>,$<CONFIG:DEBUG>>:-fno-omit-frame-pointer;-O0;-g;-fsanitize=address,undefined>"
)
add_link_options(
  "$<$<AND:$<CXX_COMPILER_ID:GNU,Clang,AppleClang>,$<CONFIG:DEBUG>>:-fsanitize=address,undefined>"
)

include(cmake/CPM.cmake)

# We want to automatically download and provide tree-sitter to users of
# the package, so pull it in and retrofit cmake dependencies on top of it.
CPMAddPackage(
  NAME tree-sitter
  GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter.git
  VERSION 0.20.8
  DOWNLOAD_ONLY YES
)

if (tree-sitter_ADDED)
  add_library(tree-sitter)
  target_sources(tree-sitter
    PRIVATE
      "${tree-sitter_SOURCE_DIR}/lib/src/lib.c"
  )
  target_include_directories(tree-sitter
    PRIVATE
      $<BUILD_INTERFACE:${tree-sitter_SOURCE_DIR}/lib/src>
    PUBLIC
      $<INSTALL_INTERFACE:include>
      $<BUILD_INTERFACE:${tree-sitter_SOURCE_DIR}/lib/include>
  )
  target_compile_options(tree-sitter
    PRIVATE
      "$<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-conversion>"
  )
endif()


function(add_grammar_from_repo NAME REPO VERSION)
  CPMAddPackage(
    NAME ${NAME}
    GIT_REPOSITORY ${REPO}
    VERSION ${VERSION}
    DOWNLOAD_ONLY YES
  )

  if ("${${NAME}_ADDED}")
    add_library(${NAME})

    file(GLOB maybe_scanner "${${NAME}_SOURCE_DIR}/src/scanner.c")
    target_sources(${NAME}
      PRIVATE
        "${${NAME}_SOURCE_DIR}/src/parser.c"
        ${maybe_scanner}
    )
    target_include_directories(${NAME}
      PRIVATE
        # parser.h is stored within the src directory, so we need to include
        # src in the search paths
        $<BUILD_INTERFACE:${${NAME}_SOURCE_DIR}/src>
      PUBLIC
        $<INSTALL_INTERFACE:include>
    )

    target_link_libraries(${NAME}
      INTERFACE
        tree-sitter
    )
    target_compile_options(${NAME}
      PRIVATE
        "$<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-unused-but-set-variable>"
    )
  endif()
endfunction(add_grammar_from_repo)


add_library(cpp-tree-sitter INTERFACE)
target_include_directories(cpp-tree-sitter
  INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(cpp-tree-sitter
  INTERFACE
    tree-sitter
)
