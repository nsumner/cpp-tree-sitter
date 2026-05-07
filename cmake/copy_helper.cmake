find_file(FOUND_FILE
    NAMES ${NAMES}
    PATHS ${PATHS}
    NO_DEFAULT_PATH
)

if(FOUND_FILE)
    message(STATUS "Build-time search found: ${FOUND_FILE}")
    execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FOUND_FILE}" "${DEST}")
else()
    message(FATAL_ERROR "Build-time search FAILED: Could not find any of [${NAMES}] in [${PATHS}]")
endif()