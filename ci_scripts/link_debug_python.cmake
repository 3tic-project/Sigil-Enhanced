# Expose the interpreter selected by CMake at a dedicated path in Debug apps.
# This is intentionally a development-machine symlink, not a distributable
# replacement for the complete Python.framework copied by package builds.

if ( NOT SIGIL_CONFIG STREQUAL "Debug" )
    return()
endif()

if ( NOT EXISTS "${SIGIL_SOURCE_PYTHON}" )
    message(FATAL_ERROR "Debug Python interpreter does not exist: ${SIGIL_SOURCE_PYTHON}")
endif()

get_filename_component(destination_dir "${SIGIL_DESTINATION_PYTHON}" DIRECTORY)
file(MAKE_DIRECTORY "${destination_dir}")
if ( EXISTS "${SIGIL_DESTINATION_PYTHON}" OR IS_SYMLINK "${SIGIL_DESTINATION_PYTHON}" )
    file(REMOVE "${SIGIL_DESTINATION_PYTHON}")
endif()
file(CREATE_LINK
    "${SIGIL_SOURCE_PYTHON}"
    "${SIGIL_DESTINATION_PYTHON}"
    SYMBOLIC
    RESULT link_result
)
if ( NOT link_result STREQUAL "0" )
    message(FATAL_ERROR "Cannot create Debug Python runtime entry: ${link_result}")
endif()
