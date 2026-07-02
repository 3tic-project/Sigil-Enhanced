if(NOT EXISTS "${CMAKE_CURRENT_BINARY_DIR}/Qt${QTVER}")
    # The custom Qt archives on the win-qtwebkit-5.212 releases page are tagged
    # with the Visual Studio toolset they were built with. Newer Qt versions
    # ship with a newer toolset (e.g. Qt 6.10.x uses VS2026 while 6.9.x uses
    # VS2022). Allow the tag to be overridden, but pick a sensible default.
    if(NOT DEFINED WINQT_VSTAG)
        if(QTVER VERSION_GREATER_EQUAL "6.10.0")
            set(WINQT_VSTAG "VS2026")
        else()
            set(WINQT_VSTAG "VS2022")
        endif()
    endif()
    message(STATUS "Downloading Custom Qt6 from developer's github...")
    set(QTURL "${WINQTURL}/Qt${QTVER}ci_x64_${WINQT_VSTAG}.7z")
    file(DOWNLOAD "${QTURL}" "${CMAKE_CURRENT_BINARY_DIR}/qt6.7z")
    file(ARCHIVE_EXTRACT INPUT "${CMAKE_CURRENT_BINARY_DIR}/qt6.7z" )
else()
    message(STATUS "Custom Qt6 has already been downloaded to ${CMAKE_CURRENT_BINARY_DIR}. Will try to use it.")
endif()
