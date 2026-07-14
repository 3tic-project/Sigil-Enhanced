find_package(Qt6 6.4 COMPONENTS Core REQUIRED)

add_executable(safe_archive_extractor_test
    safe_archive_extractor_test.cpp
    ${CMAKE_SOURCE_DIR}/src/Misc/SafeArchiveExtractor.cpp
)

target_include_directories(safe_archive_extractor_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${MINIZIP_INCLUDE_DIRS}
    ${ZLIB_INCLUDE_DIRS}
)

target_link_libraries(safe_archive_extractor_test PRIVATE
    Qt6::Core
    ${MINIZIP_LIBRARIES}
)

target_compile_definitions(safe_archive_extractor_test PRIVATE
    SIGIL_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_test(NAME safe_archive_extractor COMMAND safe_archive_extractor_test)
set_tests_properties(safe_archive_extractor PROPERTIES TIMEOUT 30)
