cmake_minimum_required(VERSION 3.16)

foreach(_var APP_DIR APP_EXE QT_BIN_DIR)
    if(NOT DEFINED ${_var} OR "${${_var}}" STREQUAL "")
        message(FATAL_ERROR "${_var} is required")
    endif()
endforeach()

file(TO_CMAKE_PATH "${APP_DIR}" APP_DIR)
file(TO_CMAKE_PATH "${APP_EXE}" APP_EXE)
file(TO_CMAKE_PATH "${QT_BIN_DIR}" QT_BIN_DIR)

if(NOT EXISTS "${APP_EXE}")
    message(FATAL_ERROR "Application executable was not found: ${APP_EXE}")
endif()

if(NOT IS_DIRECTORY "${QT_BIN_DIR}")
    message(FATAL_ERROR "Qt bin directory was not found: ${QT_BIN_DIR}")
endif()

file(MAKE_DIRECTORY "${APP_DIR}")

# Qt 6.8+ official Windows packages use a combined icu.dll, while some older
# packages still use icudt/icuin/icuuc DLLs. windeployqt can miss these in
# WebEngine builds, so copy every Qt ICU runtime variant explicitly.
file(GLOB _qt_icu_dlls
    "${QT_BIN_DIR}/icu*.dll"
    "${QT_BIN_DIR}/icu*.DLL"
    "${QT_BIN_DIR}/ICU*.dll"
    "${QT_BIN_DIR}/ICU*.DLL"
)
list(REMOVE_DUPLICATES _qt_icu_dlls)
foreach(_icu IN LISTS _qt_icu_dlls)
    get_filename_component(_icu_name "${_icu}" NAME)
    file(COPY "${_icu}" DESTINATION "${APP_DIR}")
    message(STATUS "Bundled Qt ICU runtime: ${_icu_name}")
endforeach()

set(_executables "${APP_EXE}")
file(GLOB_RECURSE _webengine_processes
    "${APP_DIR}/QtWebEngineProcess.exe"
    "${APP_DIR}/*/QtWebEngineProcess.exe"
)
list(APPEND _executables ${_webengine_processes})
list(REMOVE_DUPLICATES _executables)

foreach(_webengine_process IN LISTS _webengine_processes)
    get_filename_component(_webengine_dir "${_webengine_process}" DIRECTORY)
    foreach(_icu IN LISTS _qt_icu_dlls)
        file(COPY "${_icu}" DESTINATION "${_webengine_dir}")
    endforeach()
endforeach()

set(_dependency_dirs "${APP_DIR}" "${QT_BIN_DIR}")
if(DEFINED PYTHON_DIR AND NOT "${PYTHON_DIR}" STREQUAL "")
    file(TO_CMAKE_PATH "${PYTHON_DIR}" PYTHON_DIR)
    if(IS_DIRECTORY "${PYTHON_DIR}")
        list(APPEND _dependency_dirs "${PYTHON_DIR}" "${PYTHON_DIR}/DLLs")
    endif()
endif()

file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES ${_executables}
    DIRECTORIES ${_dependency_dirs}
    PRE_EXCLUDE_REGEXES
        "^[Aa][Pp][Ii]-[Mm][Ss]-.*"
        "^[Ee][Xx][Tt]-[Mm][Ss]-.*"
    POST_EXCLUDE_REGEXES
        ".*[/\\\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\][Ss][Yy][Ss][Tt][Ee][Mm]32[/\\\\].*"
        ".*[/\\\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\][Ss][Yy][Ss][Ww][Oo][Ww]64[/\\\\].*"
    RESOLVED_DEPENDENCIES_VAR _resolved_deps
    UNRESOLVED_DEPENDENCIES_VAR _unresolved_deps
)

foreach(_dep IN LISTS _resolved_deps)
    get_filename_component(_dep_name "${_dep}" NAME)
    string(TOLOWER "${_dep_name}" _dep_name_lower)
    if(_dep_name_lower MATCHES "^(qt6|icu).*[.]dll$")
        if(NOT EXISTS "${APP_DIR}/${_dep_name}")
            file(COPY "${_dep}" DESTINATION "${APP_DIR}")
            message(STATUS "Bundled resolved Qt runtime dependency: ${_dep_name}")
        endif()
    endif()
endforeach()

if(_unresolved_deps)
    list(SORT _unresolved_deps)
    string(REPLACE ";" "\n  " _unresolved_text "${_unresolved_deps}")
    message(FATAL_ERROR "Unresolved Windows runtime dependencies:\n  ${_unresolved_text}")
endif()

file(GLOB _packaged_icu_dlls
    "${APP_DIR}/icu*.dll"
    "${APP_DIR}/icu*.DLL"
    "${APP_DIR}/ICU*.dll"
    "${APP_DIR}/ICU*.DLL"
)
if(NOT _packaged_icu_dlls)
    message(FATAL_ERROR "No ICU runtime DLL was packaged. The Windows build would fail with a missing icu.dll error.")
endif()

if(NOT _webengine_processes)
    message(FATAL_ERROR "QtWebEngineProcess.exe was not packaged by windeployqt.")
endif()

set(_webengine_resource_candidates
    "${APP_DIR}/resources/icudtl.dat"
    "${APP_DIR}/icudtl.dat"
)
set(_has_webengine_icu_data OFF)
foreach(_candidate IN LISTS _webengine_resource_candidates)
    if(EXISTS "${_candidate}")
        set(_has_webengine_icu_data ON)
    endif()
endforeach()
if(NOT _has_webengine_icu_data)
    message(FATAL_ERROR "Qt WebEngine ICU data file icudtl.dat was not packaged.")
endif()

message(STATUS "Windows Qt/WebEngine runtime deployment verified.")
