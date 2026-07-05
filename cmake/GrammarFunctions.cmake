# Records where a grammar target's highlight/locals/tags queries live for
# consumers that want to wire them into their own tooling.
define_property(TARGET PROPERTY TS_QUERIES_DIR
  BRIEF_DOCS "Directory containing the grammar's highlight/locals/tags queries"
  FULL_DOCS "Absolute path into the grammar's source checkout. Valid during the \
build only; do not export or install this path."
)


# Rejects argument mistakes that cmake_parse_arguments reports, then rejects
# any of the keywords named in ARGN that the caller left out. Defined as a
# macro to see the ARG_* variables cmake_parse_arguments set in the calling
# function.
macro(_cts_check_arguments CALLER)
  if(ARG_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "${CALLER}: unknown arguments: ${ARG_UNPARSED_ARGUMENTS}")
  endif()
  if(ARG_KEYWORDS_MISSING_VALUES)
    message(FATAL_ERROR "${CALLER}: missing values for: ${ARG_KEYWORDS_MISSING_VALUES}")
  endif()
  foreach(_cts_required ${ARGN})
    if(NOT DEFINED ARG_${_cts_required})
      message(FATAL_ERROR "${CALLER}: ${_cts_required} is required")
    endif()
  endforeach()
endmacro()


# Explain the expected source locations and structure for users on failure.
function(_cts_require_grammar_sources GRAMMAR_SRC)
  if(EXISTS "${GRAMMAR_SRC}/parser.c")
    return()
  endif()
  message(FATAL_ERROR
    "add_grammar: '${GRAMMAR_SRC}' does not contain src/parser.c.\n"
    "\n"
    "Expected a tree-sitter grammar source directory.\n"
    "\n"
    "Common causes:\n"
    "  * You passed a directory containing compiled parser libraries (.so, "
    ".dll, .dylib) instead of grammar sources.\n"
    "  * You passed the root of a multi-grammar repository. Use "
    "SUBDIRECTORY (for example, typescript or grammars/ocaml).\n"
    "  * For nvim-treesitter, use the grammar sources under "
    "~/.cache/nvim/tree-sitter-<lang>, not the installed parser directory.\n"
    "\n"
    "If you already have compiled parsers, load them at run time instead. "
    "See docs/dynamic-grammars.md.")
endfunction()


# A grammar to link into a program. It propagates the tree-sitter runtime to
# whatever links it.
function(_cts_add_linked_grammar_library NAME)
  add_library(${NAME} STATIC)
  if(NOT DEFINED CMAKE_POSITION_INDEPENDENT_CODE)
    set_target_properties(${NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON)
  endif()

  # Link against whichever spelling of the tree-sitter target exists.
  # In-tree and CPM consumers have a plain `tree-sitter` target, while
  # find_package consumers only have the exported `cts::tree-sitter`.
  if(TARGET tree-sitter)
    target_link_libraries(${NAME} INTERFACE tree-sitter)
  else()
    target_link_libraries(${NAME} INTERFACE cts::tree-sitter)
  endif()
endfunction()


# A grammar to load at run time. (no link dependency on the runtime)
function(_cts_add_loadable_grammar_library NAME OUTPUT_NAME)
  add_library(${NAME} MODULE)
  # PREFIX/OUTPUT_NAME give <lang>.so rather than lib<target>.so, matching
  # what the ecosystem installs and what we probe for. PIC is mandatory in
  # this context.
  set_target_properties(${NAME} PROPERTIES
    PREFIX ""
    OUTPUT_NAME "${OUTPUT_NAME}"
    POSITION_INDEPENDENT_CODE ON
  )
endfunction()


# The common convention is that `tree_sitter_<LANGUAGE>` is used for both the
# project/directory name and the C entry point for the grammar. Read the
# grammar's metadata and warn when the chosen name does not match.
# When set, OUTPUT_NAME is the actual name (to allow overrides).
function(_cts_check_grammar_name NAME OUTPUT_NAME GRAMMAR_SRC)
  if(NOT EXISTS "${GRAMMAR_SRC}/grammar.json")
    return()
  endif()
  file(READ "${GRAMMAR_SRC}/grammar.json" grammar_json)
  string(JSON grammar_name ERROR_VARIABLE json_error GET "${grammar_json}" name)
  if(json_error)
    return()
  endif()

  set(chosen_name "${NAME}")
  if(OUTPUT_NAME)
    set(chosen_name "${OUTPUT_NAME}")
  endif()
  string(REPLACE "-" "_" normalized "${chosen_name}")
  if(normalized STREQUAL "tree_sitter_${grammar_name}"
     OR normalized STREQUAL "${grammar_name}")
    return()
  endif()

  message(WARNING
    "add_grammar: target '${NAME}' wraps grammar "
    "'${grammar_name}' whose entry point is "
    "tree_sitter_${grammar_name}(). Consider naming the target "
    "tree-sitter-${grammar_name} to avoid confusion.")
endfunction()


# Implementation helpr for add_grammar_* functions. Defines the grammar
# library target NAME from a grammar source checkout.
#
# SOURCE_ROOT is the repository root containing queries/ and common/. For
# multi-grammar repositories, SUBDIRECTORY selects the directory holding
# src/, which is empty for single-grammar layouts. Sources are compiled where
# in situ to preserve relative includes.
#
# LIBRARY_TYPE is STATIC or MODULE. OUTPUT_NAME renames the built file and
# should be empty for STATIC grammars.
function(_cts_add_grammar_target NAME SOURCE_ROOT SUBDIRECTORY LIBRARY_TYPE OUTPUT_NAME)
  file(REAL_PATH "${SOURCE_ROOT}" source_root EXPAND_TILDE)
  set(grammar_dir "${source_root}")
  if(SUBDIRECTORY)
    set(grammar_dir "${source_root}/${SUBDIRECTORY}")
  endif()
  set(grammar_src "${grammar_dir}/src")

  _cts_require_grammar_sources("${grammar_src}")
  if(TARGET ${NAME})
    message(FATAL_ERROR
      "add_grammar: target '${NAME}' already exists. If two subprojects may "
      "both request this grammar, guard the call with if(NOT TARGET ${NAME}).")
  endif()

  if(LIBRARY_TYPE STREQUAL "MODULE")
    _cts_add_loadable_grammar_library(${NAME} "${OUTPUT_NAME}")
  else()
    _cts_add_linked_grammar_library(${NAME})
  endif()

  # A grammar's scanner can be written in C or C++.
  # CONFIGURE_DEPENDS is omitted because fetched sources do not change, and
  # someone adding a scanner to a local grammar re-runs cmake anyway.
  file(GLOB scanner_sources
    "${grammar_src}/scanner.c"
    "${grammar_src}/scanner.cc"
    "${grammar_src}/scanner.cpp"
    "${grammar_src}/scanner.cxx"
  )
  target_sources(${NAME}
    PRIVATE
      "${grammar_src}/parser.c"
      ${scanner_sources}
  )
  target_compile_features(${NAME} PRIVATE c_std_11)
  # Grammars vendor their own tree_sitter/{parser,alloc,array}.h in src/.
  target_include_directories(${NAME}
    PRIVATE
      $<BUILD_INTERFACE:${grammar_src}>
  )

  _cts_check_grammar_name(${NAME} "${OUTPUT_NAME}" "${grammar_src}")

  # Highlight/locals/tags queries live at the repository root, shared by
  # every sub-grammar of a multi-grammar repository.
  if(EXISTS "${source_root}/queries")
    set_target_properties(${NAME} PROPERTIES
      TS_QUERIES_DIR "${source_root}/queries")
  endif()
endfunction()


# Builds a grammar from a local source checkout whether it is a git clone, an
# nvim-treesitter cache directory, a vendored copy, ....
function(add_grammar_from_path)
  cmake_parse_arguments(ARG "" "NAME;PATH;SUBDIRECTORY" "" ${ARGN})
  _cts_check_arguments("add_grammar_from_path" NAME PATH)

  _cts_add_grammar_target(${ARG_NAME}
    "${ARG_PATH}" "${ARG_SUBDIRECTORY}" STATIC "")
endfunction()


# Turns the mutually exclusive pin arguments into CPMAddPackage arguments
# for the pin. Grammars are selected by either VERSION or GIT_TAG.
function(_cts_grammar_pin_args CALLER VERSION GIT_TAG OUT_VAR)
  if(VERSION AND GIT_TAG)
    message(FATAL_ERROR "${CALLER}: give VERSION or GIT_TAG, not both")
  elseif(VERSION)
    set(${OUT_VAR} VERSION "${VERSION}" PARENT_SCOPE)
  elseif(GIT_TAG)
    set(${OUT_VAR} GIT_TAG "${GIT_TAG}" PARENT_SCOPE)
  else()
    message(FATAL_ERROR "${CALLER}: one of VERSION or GIT_TAG is required")
  endif()
endfunction()


# Downloads the grammar repository NAME at the revision PIN_ARGS expresses and
# reports the checkout in OUT_SOURCE_ROOT.
function(_cts_fetch_grammar_sources CALLER NAME REPO PIN_ARGS OUT_SOURCE_ROOT)
  if(NOT COMMAND CPMAddPackage)
    message(FATAL_ERROR
      "${CALLER} requires CPM.cmake to fetch a REPO. Include it first, or "
      "build from a checkout already on disk with add_grammar_from_path "
      "(or add_grammar_module with PATH).")
  endif()

  CPMAddPackage(
    NAME ${NAME}
    GIT_REPOSITORY ${REPO}
    ${PIN_ARGS}
    DOWNLOAD_ONLY YES
  )

  # Grammars are always built from source. CPM can satisfy a request from an
  # installed package instead, so verify that it produced a source checkout.
  if(NOT ${NAME}_SOURCE_DIR)
    message(FATAL_ERROR
      "${CALLER}: CPM did not provide a source checkout for '${NAME}'.\n"
      "\n"
      "This happens when CPM_USE_LOCAL_PACKAGES or "
      "CPM_LOCAL_PACKAGES_ONLY causes find_package(${NAME}) to return an "
      "installed package instead of downloading the grammar sources.\n"
      "\n"
      "Grammars are always built from source. Disable those variables for this "
      "build, or build from an existing checkout with "
      "add_grammar_from_path().")
  endif()

  set(${OUT_SOURCE_ROOT} "${${NAME}_SOURCE_DIR}" PARENT_SCOPE)
  set(${NAME}_ADDED "${${NAME}_ADDED}" PARENT_SCOPE)
endfunction()


# Fetches a grammar repository at a pinned revision and builds it.
function(add_grammar_from_repo)
  cmake_parse_arguments(ARG "" "NAME;REPO;VERSION;GIT_TAG;SUBDIRECTORY" "" ${ARGN})
  _cts_check_arguments("add_grammar_from_repo" NAME REPO)
  _cts_grammar_pin_args("add_grammar_from_repo"
    "${ARG_VERSION}" "${ARG_GIT_TAG}" pin_args)

  _cts_fetch_grammar_sources("add_grammar_from_repo"
    ${ARG_NAME} "${ARG_REPO}" "${pin_args}" source_root)
  # CPM skipped the fetch but the target is already there.... So it was
  # already added. Okay.
  if(NOT ${ARG_NAME}_ADDED AND TARGET ${ARG_NAME})
    return()
  endif()

  _cts_add_grammar_target(${ARG_NAME}
    "${source_root}" "${ARG_SUBDIRECTORY}" STATIC "")
endfunction()


# Builds a grammar as a loadable module for dlopen rather than a static
# library to link. Use this with parsers that editors also consume. Use
# add_grammar_from_path/_repo to link one in.
function(add_grammar_module)
  cmake_parse_arguments(ARG "" "NAME;PATH;REPO;VERSION;GIT_TAG;SUBDIRECTORY;OUTPUT_NAME" "" ${ARGN})
  _cts_check_arguments("add_grammar_module" NAME)
  if(DEFINED ARG_PATH AND DEFINED ARG_REPO)
    message(FATAL_ERROR "add_grammar_module: give PATH or REPO, not both")
  endif()
  if(NOT DEFINED ARG_PATH AND NOT DEFINED ARG_REPO)
    message(FATAL_ERROR "add_grammar_module: one of PATH or REPO is required")
  endif()

  # Again, by convention a module is named after the grammar, e.g.
  # tree-sitter-json yields json.so.
  set(output_name "${ARG_OUTPUT_NAME}")
  if(NOT output_name)
    string(REGEX REPLACE "^tree-sitter-" "" output_name "${ARG_NAME}")
  endif()

  if(DEFINED ARG_PATH)
    set(source_root "${ARG_PATH}")
  else()
    _cts_grammar_pin_args("add_grammar_module with REPO"
      "${ARG_VERSION}" "${ARG_GIT_TAG}" pin_args)

    _cts_fetch_grammar_sources("add_grammar_module"
      ${ARG_NAME} "${ARG_REPO}" "${pin_args}" source_root)
    # CPM skipped the fetch but the target is already there.... So it was
    # already added. Okay.
    if(NOT ${ARG_NAME}_ADDED AND TARGET ${ARG_NAME})
      return()
    endif()
  endif()

  _cts_add_grammar_target(${ARG_NAME}
    "${source_root}" "${ARG_SUBDIRECTORY}" MODULE "${output_name}")
endfunction()
