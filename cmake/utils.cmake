set(CPP_TS_COMPILE_OPTIONS
	$<$<OR:$<CXX_COMPILER_ID:MSVC>,$<AND:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_FRONTEND_VARIANT:MSVC>>>:
		/W4
		/Zc:__cplusplus
	>
	$<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:AppleClang>,$<AND:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_FRONTEND_VARIANT:GNU>>>:
		-Wno-unused-but-set-variable
		-Wno-conversion
		-Wall
		-Wextra
	>
)

function(print_config_option NAME)
	message(STATUS "${NAME} = ${${NAME}}")
endfunction()

function(find_and_copy_lib)
	set(options POST_BUILD)
	set(oneValueArgs TARGET)
	set(multiValueArgs NAMES PATHS)

	cmake_parse_arguments(PARSE_ARGV 0 arg
		"${options}" "${oneValueArgs}" "${multiValueArgs}"
	)

	if(NOT arg_TARGET)
		message(FATAL_ERROR "find_and_copy_lib requires TARGET")
	endif()

	if(NOT arg_NAMES)
		message(FATAL_ERROR "find_and_copy_lib requires NAMES")
	endif()

	if(NOT arg_PATHS)
		message(FATAL_ERROR "find_and_copy_lib requires PATHS")
	endif()

	if(arg_NAMES)
        list(GET arg_NAMES 0 FIRST_NAME)
    else()
        set(FIRST_NAME "unknown")
    endif()

	if(BUILD_SHARED_LIBS AND WIN32)
		if(arg_POST_BUILD)
			message(STATUS "Postponing search for ${FIRST_NAME} until build time")

            add_custom_command(TARGET ${arg_TARGET} POST_BUILD
                COMMAND ${CMAKE_COMMAND} 
                    -D NAMES="${arg_NAMES}" 
                    -D PATHS="${arg_PATHS}" 
                    -D DEST="$<TARGET_FILE_DIR:${arg_TARGET}>"
                    -P "${CMAKE_SOURCE_DIR}/cmake/copy_helper.cmake"
                COMMENT "Searching and copying ${FIRST_NAME} at build time"
            )
		else()
			find_file(DLL_PATH_FOR_${FIRST_NAME}
				NAMES ${arg_NAMES}
				PATHS ${arg_PATHS}
				NO_DEFAULT_PATH
			)

			if(DLL_PATH_FOR_${FIRST_NAME})
				set(CURRENT_DLL ${DLL_PATH_FOR_${FIRST_NAME}})
				message(STATUS "Found ${FIRST_NAME} file: ${CURRENT_DLL}")
        
				add_custom_command(TARGET ${arg_TARGET} POST_BUILD
					COMMAND ${CMAKE_COMMAND} -E copy_if_different
						"${CURRENT_DLL}"
						"$<TARGET_FILE_DIR:${arg_TARGET}>"
					COMMENT "Copying ${FIRST_NAME} to target directory"
				)
			else()
				message(WARNING "Couldn't find ${FIRST_NAME} file")
			endif()
		endif()
    endif()
endfunction()