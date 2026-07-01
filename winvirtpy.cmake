find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)

set(SIGIL_WIN_PYTHON_CACHE_DIR "${SIGIL_PYTHON_CACHE_DIR}/windows/py${Python3_VERSION_MAJOR}${Python3_VERSION_MINOR}/qt${QTVER}")
set(SIGIL_WIN_PYTHON_VENV_DIR "${SIGIL_WIN_PYTHON_CACHE_DIR}/sigilpy")
file(MAKE_DIRECTORY "${SIGIL_WIN_PYTHON_CACHE_DIR}")

set(REQUIREMENTS "${PROJECT_SOURCE_DIR}/src/Resource_Files/python_pkg/winreqs.txt")
set(SIGIL_WIN_REQUIREMENTS "${SIGIL_WIN_PYTHON_CACHE_DIR}/requirements.txt")
configure_file("${REQUIREMENTS}" "${SIGIL_WIN_REQUIREMENTS}")
file(SHA256 "${SIGIL_WIN_REQUIREMENTS}" SIGIL_WIN_REQUIREMENTS_HASH)
set(SIGIL_WIN_REQUIREMENTS_STAMP "${SIGIL_WIN_PYTHON_VENV_DIR}/.sigil-requirements-${SIGIL_WIN_REQUIREMENTS_HASH}.stamp")

if(NOT EXISTS "${SIGIL_WIN_PYTHON_VENV_DIR}/Scripts/python.exe")
    # Create venv with system python
    message(STATUS "Creating virtual python environment in ${SIGIL_WIN_PYTHON_VENV_DIR}")
    execute_process(COMMAND "${Python3_EXECUTABLE}" -m venv "${SIGIL_WIN_PYTHON_VENV_DIR}"
                    RESULT_VARIABLE SIGIL_WIN_VENV_RESULT)
    if(NOT SIGIL_WIN_VENV_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to create virtual python environment in ${SIGIL_WIN_PYTHON_VENV_DIR}")
    endif()
endif()

if(NOT EXISTS "${SIGIL_WIN_REQUIREMENTS_STAMP}")
    # Update venv pip
    execute_process(COMMAND "${SIGIL_WIN_PYTHON_VENV_DIR}/Scripts/python.exe" -m pip install -U pip
                    RESULT_VARIABLE SIGIL_WIN_PIP_UPDATE_RESULT)
    if(NOT SIGIL_WIN_PIP_UPDATE_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to update pip in ${SIGIL_WIN_PYTHON_VENV_DIR}")
    endif()
    # Pip install -r requirements.txt into venv
    message(STATUS "Installing required python modules into cached venv using ${SIGIL_WIN_REQUIREMENTS}")
    execute_process(COMMAND "${SIGIL_WIN_PYTHON_VENV_DIR}/Scripts/python.exe" -m pip install -r "${SIGIL_WIN_REQUIREMENTS}"
                    RESULT_VARIABLE SIGIL_WIN_PIP_INSTALL_RESULT)
    if(NOT SIGIL_WIN_PIP_INSTALL_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to install Python requirements into ${SIGIL_WIN_PYTHON_VENV_DIR}")
    endif()
    file(GLOB SIGIL_WIN_OLD_STAMPS "${SIGIL_WIN_PYTHON_VENV_DIR}/.sigil-requirements-*.stamp")
    if(SIGIL_WIN_OLD_STAMPS)
        file(REMOVE ${SIGIL_WIN_OLD_STAMPS})
    endif()
    file(WRITE "${SIGIL_WIN_REQUIREMENTS_STAMP}" "${SIGIL_WIN_REQUIREMENTS_HASH}\n")
else()
    message(STATUS "Reusing cached virtual python environment in ${SIGIL_WIN_PYTHON_VENV_DIR}")
endif()

set(Python3_EXECUTABLE "${SIGIL_WIN_PYTHON_VENV_DIR}/Scripts/python.exe")
