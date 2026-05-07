# Change this value to test with different c++ versions
set(CPP_TS_TEST_CXX_STANDARD 20 CACHE STRING "C++ standard for tests" FORCE)

set(PCH_FILE ${CMAKE_CURRENT_SOURCE_DIR}/pch.hpp)
set(TARGET_NAME ${PROJECT_NAME}-tests)

file(GLOB_RECURSE TESTS_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

if(NOT CPP_TS_FEATURE_WASM)
    list(REMOVE_ITEM TESTS_FILES "${CMAKE_CURRENT_SOURCE_DIR}/wasm-tests.cpp")
endif()

enable_testing()

add_executable(${TARGET_NAME} ${TESTS_FILES})

target_precompile_headers(${TARGET_NAME} PUBLIC ${PCH_FILE})

set_target_properties(${TARGET_NAME} PROPERTIES 
    LINKER_LANGUAGE CXX
    CXX_STANDARD_REQUIRED ON
    CXX_STANDARD ${CPP_TS_TEST_CXX_STANDARD}
)

target_include_directories(${TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

get_target_property(ACTUAL_STANDARD ${TARGET_NAME} CXX_STANDARD)
message(STATUS "Actual standard for ${TARGET_NAME}: ${ACTUAL_STANDARD}")

if(WIN32)
    target_compile_definitions(${TARGET_NAME} PRIVATE _WIN32)
elseif(APPLE)
    target_compile_definitions(${TARGET_NAME} PRIVATE __APPLE__)
elseif(UNIX)
    target_compile_definitions(${TARGET_NAME} PRIVATE __linux__)
endif()

set(JSONGrammarArgs
    NAME tree-sitter-json
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-json.git
    VERSION 0.24.8
)

if(CPP_TS_FEATURE_WASM)
    CPPTSCopyWasmtime(TARGET ${TARGET_NAME})
    list(APPEND JSONGrammarArgs FIND_ALSO_WASM_FILE)
endif()

CPPTSAddGrammar(${JSONGrammarArgs})

CPMAddPackage(
    URI "gh:catchorg/Catch2@3.14.0"
	OPTIONS "CATCH_INSTALL_DOCS OFF"
			"BUILD_TESTING OFF"
)

if(Catch2_ADDED)
    set(Catch2_Extras ${Catch2_SOURCE_DIR}/extras CACHE INTERNAL "")

    find_and_copy_lib(
        TARGET ${TARGET_NAME}
        NAMES "$<$<CONFIG:Debug>:Catch2d.dll>$<$<NOT:$<CONFIG:Debug>>:Catch2.dll>"
        PATHS ${Catch2_BINARY_DIR}/src
        POST_BUILD
    )

    find_and_copy_lib(
        TARGET ${TARGET_NAME}
        NAMES "$<$<CONFIG:Debug>:Catch2Maind.dll>$<$<NOT:$<CONFIG:Debug>>:Catch2Main.dll>"
        PATHS ${Catch2_BINARY_DIR}/src
        POST_BUILD
    )
	
	if(WIN32)
		set_target_properties(Catch2WithMain Catch2 PROPERTIES 
			MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>$<$<NOT:$<BOOL:${CPP_TS_MSVC_STATIC_RUNTIME}>>:DLL>"
		)
	endif()
	
	if(MSVC)
		target_compile_definitions(Catch2 PRIVATE NOMINMAX)
		target_compile_definitions(Catch2WithMain PRIVATE NOMINMAX)
	endif()
endif()

target_compile_options(${TARGET_NAME} PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:MSVC>,$<AND:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_FRONTEND_VARIANT:MSVC>>>:
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/GL>
        $<$<CONFIG:Release>:/fp:fast>
        $<$<CONFIG:Debug>:/RTC1>
        /Zc:preprocessor
        /Zc:__cplusplus
    >
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:AppleClang>,$<AND:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_FRONTEND_VARIANT:GNU>>>:
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Release>:-flto=auto>
    >
)
	
target_link_options(${TARGET_NAME} PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:MSVC>,$<AND:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_FRONTEND_VARIANT:MSVC>>>:
        $<$<CONFIG:Release>:/LTCG>
        $<$<CONFIG:Release>:/O2>
    >
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:AppleClang>,$<AND:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_FRONTEND_VARIANT:GNU>>>:
        $<$<CONFIG:Release>:-flto=auto>
        $<$<CONFIG:Release>:-O3>
        $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:Clang>>:-fuse-ld=lld>
        -static
    >
)

if(WIN32)
	set_target_properties(${TARGET_NAME} PROPERTIES 
		MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>$<$<NOT:$<BOOL:${CPP_TS_MSVC_STATIC_RUNTIME}>>:DLL>"
	)
endif()

if(MSVC)
    target_compile_definitions(${TARGET_NAME} PRIVATE NOMINMAX)
endif()

if(CPP_TS_FEATURE_WASM)
    target_compile_definitions(${TARGET_NAME} PRIVATE CPP_TS_TEST_FEATURE_WASM)
    target_compile_definitions(${TARGET_NAME} PRIVATE CPP_TS_TEST_WASM_FILES_DIR="${CPP_TS_WASM_DIR}")
endif()

target_link_libraries(${TARGET_NAME} PRIVATE 
	${PROJECT_NAME}
    tree-sitter-json
	Catch2::Catch2WithMain
)

list(APPEND CMAKE_MODULE_PATH ${Catch2_Extras})
include(CTest)
include(Catch)
catch_discover_tests(
	${TARGET_NAME}
	DISCOVERY_MODE PRE_TEST
)

if(MSVC AND CMAKE_GENERATOR MATCHES "Visual Studio")
    configure_file(
        ${CMAKE_SOURCE_DIR}/.runsettings
        ${CMAKE_BINARY_DIR}/.runsettings
        COPYONLY
    )
endif()