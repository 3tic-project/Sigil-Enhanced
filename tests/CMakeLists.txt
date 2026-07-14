find_package(Qt6 6.4 COMPONENTS Concurrent Core Gui Svg REQUIRED)

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

add_executable(image_preview_service_test
    image_preview_service_test.cpp
    ${CMAKE_SOURCE_DIR}/src/Misc/ImagePreviewService.cpp
    ${CMAKE_SOURCE_DIR}/src/Misc/ImagePreviewService.h
)

set_target_properties(image_preview_service_test PROPERTIES AUTOMOC ON)

target_include_directories(image_preview_service_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(image_preview_service_test PRIVATE
    Qt6::Concurrent
    Qt6::Core
    Qt6::Gui
    Qt6::Svg
)

add_test(NAME image_preview_service COMMAND image_preview_service_test)
set_tests_properties(image_preview_service PROPERTIES TIMEOUT 30)

add_executable(opf_model_clear_benchmark
    opf_model_clear_benchmark.cpp
)

target_link_libraries(opf_model_clear_benchmark PRIVATE
    Qt6::Core
    Qt6::Gui
)

add_test(NAME opf_model_clear_benchmark COMMAND opf_model_clear_benchmark)
set_tests_properties(opf_model_clear_benchmark PROPERTIES TIMEOUT 30)

add_executable(plugin_protocol_test
    plugin_protocol_test.cpp
    ${CMAKE_SOURCE_DIR}/src/PluginAPI/PluginProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/PluginAPI/PluginProtocol.h
)

target_include_directories(plugin_protocol_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(plugin_protocol_test PRIVATE
    Qt6::Core
)

add_test(NAME plugin_protocol COMMAND plugin_protocol_test)
set_tests_properties(plugin_protocol PROPERTIES TIMEOUT 30)
