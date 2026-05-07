if(DEFINED CPP_TS_GRAMMAR_PATH AND NOT CACHE{CPP_TS_GRAMMAR_PATH})
    set(CPP_TS_GRAMMAR_PATH "${CPP_TS_GRAMMAR_PATH}" CACHE PATH "Path to locally installed tree-sitter grammars" FORCE)
endif()

if(NOT CPP_TS_GRAMMAR_PATH)
    set(CPP_TS_GRAMMAR_PATH "" CACHE PATH "Path to locally installed tree-sitter grammars")
endif()

if(DEFINED CPP_TS_WASM_DIR AND NOT CACHE{CPP_TS_WASM_DIR})
    set(CPP_TS_WASM_DIR "${CPP_TS_WASM_DIR}" CACHE PATH "Path where to store downloaded Tree-sitter wasm grammar files" FORCE)
endif()

if(NOT CPP_TS_WASM_DIR)
    set(CPP_TS_WASM_DIR "${CMAKE_BINARY_DIR}/wasm_files" CACHE PATH "Path where to store downloaded Tree-sitter wasm grammar files")
endif()

macro(cpp_ts_export_variables name)
    set(${name}_WASM_FILE
        "${${name}_WASM_FILE}"
        PARENT_SCOPE
    )
    set(${name}_ADDED
        "${${name}_ADDED}"
        PARENT_SCOPE
    )
    set(CPP_TS_LAST_GRAMMAR_NAME
        "${name}"
        PARENT_SCOPE
    )
endmacro()

function(cpp_ts_get_source_folder_name FULL_PATH RESULT)
    cmake_path(GET FULL_PATH 
        PARENT_PATH 
        SRC_FOLDER_PATH
    )
    cmake_path(GET SRC_FOLDER_PATH 
        PARENT_PATH
        PROJECT_ROOT_PATH
    )
    cmake_path(GET PROJECT_ROOT_PATH
        FILENAME
        PROJECT_FOLDER_NAME
    )

    set(${RESULT}
        ${PROJECT_FOLDER_NAME}
        PARENT_SCOPE
    )
endfunction()

function(cpp_ts_url_without_git REPO_URL RESULT)
    string(REGEX REPLACE "\\.git$" "" CLEAN_URL "${REPO_URL}")

    set(${RESULT}
        ${CLEAN_URL}
        PARENT_SCOPE
    )
endfunction()

function(CPPTSAddGrammar)
    set(options NOT_IN_GLOBAL_DIR FIND_ALSO_WASM_FILE FIND_ONLY_WASM_FILE)  # Bool Options
    set(oneValueArgs NAME GIT_REPOSITORY VERSION GIT_TAG SOURCE_DIR)        # One element arguments
    set(multiValueArgs)                             	  			        # No multi-value arguments

    cmake_parse_arguments(PARSE_ARGV 0 arg
        "${options}" "${oneValueArgs}" "${multiValueArgs}"
    )

	set(GRAMMAR_SOURCE_DIR "")
	
	if(arg_SOURCE_DIR AND EXISTS "${arg_SOURCE_DIR}/src/parser.c")
        set(GRAMMAR_SOURCE_DIR "${arg_SOURCE_DIR}")
        cpp_ts_get_source_folder_name("${arg_SOURCE_DIR}/src/parser.c" NAME)
        message(STATUS "Tree-sitter: Using ${NAME} from explicitly provided path: ${GRAMMAR_SOURCE_DIR}")
	elseif(CPP_TS_GRAMMAR_PATH AND NOT arg_NOT_IN_GLOBAL_DIR)
        if(NOT arg_NAME)
			message(FATAL_ERROR "cpp_ts_add_grammar requires NAME for GRAMMAR_SOURCE_DIR")
		endif()

		set(NAME ${arg_NAME})

        if(NOT arg_FIND_ONLY_WASM_FILE)
            if(EXISTS "${CPP_TS_GRAMMAR_PATH}/${NAME}/src/parser.c")
                set(GRAMMAR_SOURCE_DIR "${CPP_TS_GRAMMAR_PATH}/${NAME}")
                message(STATUS "Tree-sitter: Found ${NAME} in CPP_TS_GRAMMAR_PATH: ${GRAMMAR_SOURCE_DIR}")
            endif()
        else()
            if(EXISTS "${CPP_TS_GRAMMAR_PATH}/${NAME}/${NAME}.wasm")
                set(GRAMMAR_SOURCE_DIR "${CPP_TS_GRAMMAR_PATH}/${NAME}")
                message(STATUS "Tree-sitter: Found ${NAME} in CPP_TS_GRAMMAR_PATH: ${GRAMMAR_SOURCE_DIR}")
            endif()
        endif()
    endif()

	if(NOT GRAMMAR_SOURCE_DIR)		
		if(NOT arg_GIT_REPOSITORY)
            message(FATAL_ERROR "cpp_ts_add_grammar requires GIT_REPOSITORY OR SOURCE_DIR to fetch grammar")
        endif()

        message(STATUS "Tree-sitter: Grammar not found locally, fetching from ${arg_GIT_REPOSITORY}")
		
        set(CPM_NAME ${arg_NAME})
        if(NOT CPM_NAME)
            set(CPM_NAME ${NAME})
        endif()

        set(CPM_ARGS
            GIT_REPOSITORY ${arg_GIT_REPOSITORY}
            DOWNLOAD_ONLY YES
        )

        if(CPM_NAME)
            list(APPEND CPM_ARGS NAME ${CPM_NAME})
        endif()
		
		if(arg_VERSION)
            list(APPEND CPM_ARGS VERSION ${arg_VERSION})
        elseif(arg_GIT_TAG)
            list(APPEND CPM_ARGS GIT_TAG ${arg_GIT_TAG})
        else()
            message(STATUS "Tree-sitter: No VERSION or GIT_TAG for ${NAME}, defaulting to master/main branch")
        endif()
		
		CPMAddPackage(${CPM_ARGS})
        set(NAME ${CPM_LAST_PACKAGE_NAME})
		
		if (${NAME}_ADDED)
			set(GRAMMAR_SOURCE_DIR "${${NAME}_SOURCE_DIR}")
		endif()
	endif()

    if(GRAMMAR_SOURCE_DIR)
        if(arg_FIND_ALSO_WASM_FILE OR arg_FIND_ONLY_WASM_FILE)
            file(GLOB files "${GRAMMAR_SOURCE_DIR}/${NAME}.wasm")
            list(LENGTH files num_files)

            if(num_files GREATER_EQUAL 1)
                list(GET files 0 WASM_FILE)
            elseif(arg_GIT_REPOSITORY)
                cpp_ts_url_without_git(${arg_GIT_REPOSITORY} REPO)

                set(RELEASE_VERSION "latest")
                if(arg_VERSION)
                    set(RELEASE_VERSION "v${arg_VERSION}")
                elseif(arg_GIT_TAG)
                    set(RELEASE_VERSION "${arg_GIT_TAG}")
                endif()

                set(WASM_FILE_URL "${REPO}/releases/download/${RELEASE_VERSION}/${NAME}.wasm")
                set(WASM_FILE_DESTINATION "${CPP_TS_WASM_DIR}/${NAME}.wasm")

                if(NOT EXISTS "${WASM_FILE_DESTINATION}")
                    if(NOT EXISTS "${CPP_TS_WASM_DIR}")
                        file(MAKE_DIRECTORY "${CPP_TS_WASM_DIR}")
                    endif()
                    file(DOWNLOAD 
                        "${WASM_FILE_URL}" 
                        "${WASM_FILE_DESTINATION}"
                        SHOW_PROGRESS
                        STATUS download_status
                    )

                    list(GET download_status 0 status_code)
                    if(status_code EQUAL 0)
                        set(WASM_FILE "${WASM_FILE_DESTINATION}")
                    else()
                        set(WASM_FILE "")
                    endif()
                else()
                    set(WASM_FILE "${WASM_FILE_DESTINATION}")
                endif()
            else()
                set(WASM_FILE "")
            endif()

            set(${NAME}_WASM_FILE "${WASM_FILE}")
        else()
            set(${NAME}_WASM_FILE "")
        endif()

        if(NOT arg_FIND_ONLY_WASM_FILE)
            file(GLOB maybe_scanner 
			    "${GRAMMAR_SOURCE_DIR}/src/scanner.c"
			    "${GRAMMAR_SOURCE_DIR}/src/scanner.cc"
			    "${GRAMMAR_SOURCE_DIR}/src/scanner.cpp"
		    )

            add_library(${NAME} STATIC "${GRAMMAR_SOURCE_DIR}/src/parser.c" ${maybe_scanner})

            # parser.h is stored within the src directory, so we need to include
            # src in the search paths
            target_include_directories(${NAME} PRIVATE $<BUILD_INTERFACE:${GRAMMAR_SOURCE_DIR}/src>)
            target_include_directories(${NAME} PUBLIC  $<INSTALL_INTERFACE:include>)

            target_link_libraries(${NAME} PRIVATE tree-sitter)
		
		    foreach(scanner_file ${maybe_scanner})
                if(scanner_file MATCHES ".*\\.cc$" OR scanner_file MATCHES ".*\\.cpp$")
                    set_target_properties(${NAME} PROPERTIES LINKER_LANGUAGE CXX)
                    break()
                endif()
            endforeach()
		
            target_compile_options(${NAME} PRIVATE ${CPP_TS_COMPILE_OPTIONS})

            if(MSVC)
			    target_compile_definitions(${NAME} PRIVATE NOMINMAX)
                set_target_properties(${NAME} PROPERTIES 
		            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>$<$<NOT:$<BOOL:${CPP_TS_MSVC_STATIC_RUNTIME}>>:DLL>"
	            )
		    endif()
        endif()

        set(${NAME}_ADDED YES)
        cpp_ts_export_variables(${NAME})
    endif()
endfunction()