cmake_minimum_required(VERSION 3.30)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

include(get_cpm)
include(utils)
include(grammar)

project(
    cpp-tree-sitter
    VERSION 0.26.8
    LANGUAGES C CXX
    DESCRIPTION "Simple C++ and CMake wrapper around tree-sitter."
)

# OPTIONS
set(CPP_TS_BUILD_TESTS_DEFAULT OFF)
if(PROJECT_IS_TOP_LEVEL)
    set(CPP_TS_BUILD_TESTS_DEFAULT ON)
endif()

option(CPP_TS_BUILD_TESTS "Build test for cpp-tree-sitter" ${CPP_TS_BUILD_TESTS_DEFAULT})
option(CPP_TS_FEATURE_WASM "Enable the Wasm feature" OFF)
option(CPP_TS_AMALGAMATED "Build using an amalgamated source" ON)
option(CPP_TS_MSVC_STATIC_RUNTIME "Use static MSVC runtime (/MT or /MTd)" OFF)

if(DEFINED CPP_TS_WASMTIME_PATH AND NOT CACHE{CPP_TS_WASMTIME_PATH})
	set(CPP_TS_WASMTIME_PATH "${CPP_TS_WASMTIME_PATH}" CACHE PATH "Path to locally installed wasmtime" FORCE)
endif()

if(NOT CPP_TS_WASMTIME_PATH)
	set(CPP_TS_WASMTIME_PATH "" CACHE PATH "Path to locally installed wasmtime")
endif()

message(STATUS "========== cpp-tree-sitter configuration ==========")
print_config_option(CPP_TS_BUILD_TESTS)
print_config_option(CPP_TS_FEATURE_WASM)
print_config_option(CPP_TS_AMALGAMATED)
print_config_option(CPP_TS_MSVC_STATIC_RUNTIME)
print_config_option(CPP_TS_WASMTIME_PATH)
message(STATUS "===================================================")

# We want to automatically download and provide tree-sitter to users of
# the package, so pull it in and retrofit cmake dependencies on top of it.
CPMAddPackage(
    NAME tree-sitter
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter.git
    VERSION 0.26.8
    DOWNLOAD_ONLY YES
)

if (tree-sitter_ADDED)
	if(CPP_TS_AMALGAMATED)
		set(TREE_SITTER_SOURCE_FILES "${tree-sitter_SOURCE_DIR}/lib/src/lib.c")
	else()
		file(GLOB TREE_SITTER_SOURCE_FILES CONFIGURE_DEPENDS "${tree-sitter_SOURCE_DIR}/lib/src/*.c")
		list(REMOVE_ITEM TREE_SITTER_SOURCE_FILES "${tree-sitter_SOURCE_DIR}/lib/src/lib.c")
	endif()

    add_library(tree-sitter STATIC ${TREE_SITTER_SOURCE_FILES})
    target_include_directories(tree-sitter PRIVATE $<BUILD_INTERFACE:${tree-sitter_SOURCE_DIR}/lib/src> $<BUILD_INTERFACE:${tree-sitter_SOURCE_DIR}/lib/src/wasm>)
    target_include_directories(tree-sitter PUBLIC  $<INSTALL_INTERFACE:include> $<BUILD_INTERFACE:${tree-sitter_SOURCE_DIR}/lib/include>)
    target_compile_options(tree-sitter PRIVATE ${CPP_TS_COMPILE_OPTIONS})

	if(WIN32)
		set_target_properties(tree-sitter PROPERTIES 
			MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>$<$<NOT:$<BOOL:${CPP_TS_MSVC_STATIC_RUNTIME}>>:DLL>"
		)
	endif()
	
	if(MSVC)
		target_compile_definitions(tree-sitter PRIVATE NOMINMAX)
	endif()
endif()

add_library(${PROJECT_NAME} INTERFACE)
target_include_directories(${PROJECT_NAME} INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_link_libraries(${PROJECT_NAME} INTERFACE tree-sitter)
target_compile_options(${PROJECT_NAME} INTERFACE ${CPP_TS_COMPILE_OPTIONS})

if(WIN32)
	set_target_properties(${PROJECT_NAME} PROPERTIES 
		MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>$<$<NOT:$<BOOL:${CPP_TS_MSVC_STATIC_RUNTIME}>>:DLL>"
	)
endif()

if(MSVC)
	target_compile_definitions(${PROJECT_NAME} INTERFACE NOMINMAX)
endif()

if(CPP_TS_FEATURE_WASM)
	set(WASMTIME_INCLUDE_DIR "")
	set(WASMTIME_LIB_DIR "")
	if(NOT CPP_TS_WASMTIME_PATH)
		set(WASM_VERSION 43.0.1)
		if(WIN32)
			set(WASMTIME_URL "https://github.com/bytecodealliance/wasmtime/releases/download/v${WASM_VERSION}/wasmtime-v${WASM_VERSION}-x86_64-windows-c-api.zip")
		elseif(APPLE)
			set(WASMTIME_URL "https://github.com/bytecodealliance/wasmtime/releases/download/v${WASM_VERSION}/wasmtime-v${WASM_VERSION}-x86_64-macos-c-api.tar.xz")
		elseif(UNIX)
			set(WASMTIME_URL "https://github.com/bytecodealliance/wasmtime/releases/download/v${WASM_VERSION}/wasmtime-v${WASM_VERSION}-x86_64-linux-c-api.tar.xz")
		endif()

		CPMAddPackage(
			NAME wasmtime
			URL ${WASMTIME_URL}
			DOWNLOAD_ONLY YES
		)

		if(wasmtime_ADDED)
			set(WASMTIME_INCLUDE_DIR ${wasmtime_SOURCE_DIR}/include)
			set(WASMTIME_LIB_DIR ${wasmtime_SOURCE_DIR}/lib)
		endif()
	else()
		if(EXISTS "${CPP_TS_WASMTIME_PATH}")
			set(WASMTIME_INCLUDE_DIR ${CPP_TS_WASMTIME_PATH}/include)
			set(WASMTIME_LIB_DIR ${CPP_TS_WASMTIME_PATH}/lib)
		endif()
	endif()

	if(WASMTIME_INCLUDE_DIR AND WASMTIME_LIB_DIR)
		set(WASMTIME_LIB_NAME wasmtime)
		if(NOT BUILD_SHARED_LIBS)
			if(WIN32)
				set(WASMTIME_LIB_NAME wasmtime.lib)

				# Project
				target_compile_definitions(${PROJECT_NAME} INTERFACE WASM_API_EXTERN= WASI_API_EXTERN=)
				target_link_libraries(${PROJECT_NAME} INTERFACE ws2_32 advapi32 userenv ntdll shell32 ole32 bcrypt)

				# Tree-Sitter
				target_compile_definitions(tree-sitter PRIVATE WASM_API_EXTERN= WASI_API_EXTERN=)
				target_link_libraries(tree-sitter INTERFACE ws2_32 advapi32 userenv ntdll shell32 ole32 bcrypt)
			elseif(NOT APPLE)
				# Project
				target_link_libraries(${PROJECT_NAME} INTERFACE pthread dl m)

				# Tree-Sitter
				target_link_libraries(tree-sitter INTERFACE pthread dl m)
			endif()
		endif()

		unset(WASMTIME_LIBRARY CACHE)
		find_library(
			WASMTIME_LIBRARY 
			NAMES ${WASMTIME_LIB_NAME}
			PATHS ${WASMTIME_LIB_DIR}
			NO_DEFAULT_PATH
		)

		# Project
		target_include_directories(${PROJECT_NAME} INTERFACE ${WASMTIME_INCLUDE_DIR})
		target_compile_definitions(${PROJECT_NAME} INTERFACE CPP_TREE_SITTER_FEATURE_WASM)
		target_link_libraries(${PROJECT_NAME} INTERFACE ${WASMTIME_LIBRARY})

		# Tree-Sitter
		target_include_directories(tree-sitter PRIVATE ${WASMTIME_INCLUDE_DIR})
		target_compile_definitions(tree-sitter PUBLIC TREE_SITTER_FEATURE_WASM)
		target_link_libraries(tree-sitter PUBLIC ${WASMTIME_LIBRARY})
	endif()

	function(CPPTSCopyWasmtime)
		set(options)
		set(oneValueArgs TARGET)
		set(multiValueArgs)

		cmake_parse_arguments(PARSE_ARGV 0 arg
			"${options}" "${oneValueArgs}" "${multiValueArgs}"
		)

		if(NOT arg_TARGET)
			message(FATAL_ERROR "cpp_ts_copy_wasmtime requires TARGET")
		endif()

		if(EXISTS "${WASMTIME_LIB_DIR}")
			find_and_copy_lib(
				TARGET ${arg_TARGET}
				NAMES wasmtime.dll
				PATHS ${WASMTIME_LIB_DIR}
			)
		endif()
	endfunction()
endif()

if(CPP_TS_BUILD_TESTS)
	add_subdirectory(tests)
endif()