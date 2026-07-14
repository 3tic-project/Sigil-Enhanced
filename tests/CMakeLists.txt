find_package(Qt6 6.4 COMPONENTS Concurrent Core Gui Network Svg REQUIRED)

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

add_executable(plugin_metadata_test
    plugin_metadata_test.cpp
    ${CMAKE_SOURCE_DIR}/src/Misc/Plugin.cpp
    ${CMAKE_SOURCE_DIR}/src/Misc/Plugin.h
)

target_include_directories(plugin_metadata_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(plugin_metadata_test PRIVATE
    Qt6::Core
)

add_test(NAME plugin_metadata COMMAND plugin_metadata_test)
set_tests_properties(plugin_metadata PROPERTIES TIMEOUT 30)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME python_live_sdk
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/python_live_sdk_test.py
)
set_tests_properties(python_live_sdk PROPERTIES TIMEOUT 30)

add_test(NAME plugin_openrpc_contract
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/plugin_openrpc_contract_test.py
)
set_tests_properties(plugin_openrpc_contract PROPERTIES TIMEOUT 30)

add_test(NAME legacy_plugin_api_surface
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/legacy_plugin_api_surface_test.py
)
set_tests_properties(legacy_plugin_api_surface PROPERTIES TIMEOUT 30)

add_test(NAME live_legacy_wrapper
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/live_legacy_wrapper_test.py
)
set_tests_properties(live_legacy_wrapper PROPERTIES TIMEOUT 30)

add_test(NAME live_launcher_compat
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/live_launcher_compat_test.py
)
set_tests_properties(live_launcher_compat PROPERTIES TIMEOUT 30)

add_test(NAME live_plugin_examples
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/live_plugin_examples_test.py
)
set_tests_properties(live_plugin_examples PROPERTIES TIMEOUT 30)

add_executable(plugin_transport_integration_test
    plugin_transport_integration_test.cpp
    ${CMAKE_SOURCE_DIR}/src/PluginAPI/PluginProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/PluginAPI/PluginProtocol.h
)

target_include_directories(plugin_transport_integration_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_definitions(plugin_transport_integration_test PRIVATE
    SIGIL_TEST_PYTHON="${Python3_EXECUTABLE}"
    SIGIL_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(plugin_transport_integration_test PRIVATE
    Qt6::Core
    Qt6::Network
)

add_test(NAME plugin_transport_integration COMMAND plugin_transport_integration_test)
set_tests_properties(plugin_transport_integration PROPERTIES TIMEOUT 30)

add_executable(plugin_launcher_integration_test
    plugin_launcher_integration_test.cpp
    ${CMAKE_SOURCE_DIR}/src/PluginAPI/PluginProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/PluginAPI/PluginProtocol.h
)

target_include_directories(plugin_launcher_integration_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_definitions(plugin_launcher_integration_test PRIVATE
    SIGIL_TEST_PYTHON="${Python3_EXECUTABLE}"
    SIGIL_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(plugin_launcher_integration_test PRIVATE
    Qt6::Core
    Qt6::Network
)

add_test(NAME plugin_launcher_integration COMMAND plugin_launcher_integration_test)
set_tests_properties(plugin_launcher_integration PROPERTIES TIMEOUT 30)

add_executable(plugin_text_edit_test
    plugin_text_edit_test.cpp
    ${CMAKE_SOURCE_DIR}/src/PluginAPI/PluginTextEdit.cpp
    ${CMAKE_SOURCE_DIR}/src/PluginAPI/PluginTextEdit.h
)

target_include_directories(plugin_text_edit_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(plugin_text_edit_test PRIVATE
    Qt6::Core
    Qt6::Gui
)

add_test(NAME plugin_text_edit COMMAND plugin_text_edit_test)
set_tests_properties(plugin_text_edit PROPERTIES TIMEOUT 30)

add_executable(plugin_text_transaction_test
    plugin_text_transaction_test.cpp
    ${CMAKE_SOURCE_DIR}/src/PluginAPI/PluginTextEdit.cpp
    ${CMAKE_SOURCE_DIR}/src/PluginAPI/PluginTextEdit.h
    ${CMAKE_SOURCE_DIR}/src/PluginAPI/PluginTextTransaction.cpp
    ${CMAKE_SOURCE_DIR}/src/PluginAPI/PluginTextTransaction.h
)

target_include_directories(plugin_text_transaction_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(plugin_text_transaction_test PRIVATE
    Qt6::Core
    Qt6::Gui
)

add_test(NAME plugin_text_transaction COMMAND plugin_text_transaction_test)
set_tests_properties(plugin_text_transaction PROPERTIES TIMEOUT 30)
