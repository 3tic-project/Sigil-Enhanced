find_package(Qt6 6.4 COMPONENTS Concurrent Core Gui Network Svg Widgets Xml REQUIRED)

add_executable(regex_match_enumerator_test
    regex_match_enumerator_test.cpp
    ${CMAKE_SOURCE_DIR}/src/Misc/RegexMatchEnumerator.cpp
    ${CMAKE_SOURCE_DIR}/src/Misc/RegexMatchEnumerator.h
)

target_include_directories(regex_match_enumerator_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${PCRE2_INCLUDE_DIRS}
)

target_link_libraries(regex_match_enumerator_test PRIVATE
    Qt6::Core
    ${PCRE2_LIBRARIES}
)
target_compile_features(regex_match_enumerator_test PRIVATE cxx_std_17)

add_test(NAME regex_match_enumerator COMMAND regex_match_enumerator_test)
set_tests_properties(regex_match_enumerator PROPERTIES TIMEOUT 30)

add_executable(presearch_progress_test
    presearch_progress_test.cpp
    ${CMAKE_SOURCE_DIR}/src/Misc/PreSearchMatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/Misc/PreSearchMatcher.h
    ${CMAKE_SOURCE_DIR}/src/Misc/RegexMatchEnumerator.cpp
    ${CMAKE_SOURCE_DIR}/src/Misc/RegexMatchEnumerator.h
)

target_include_directories(presearch_progress_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${PCRE2_INCLUDE_DIRS}
)

target_link_libraries(presearch_progress_test PRIVATE
    Qt6::Core
    ${PCRE2_LIBRARIES}
)
target_compile_features(presearch_progress_test PRIVATE cxx_std_17)

add_test(NAME presearch_progress COMMAND presearch_progress_test)
set_tests_properties(presearch_progress PROPERTIES TIMEOUT 30)

add_executable(regex_secondary_match_test
    regex_secondary_match_test.cpp
    ${CMAKE_SOURCE_DIR}/src/BuiltinPlugins/RegexWorkbench/SecondaryRegexMatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/BuiltinPlugins/RegexWorkbench/SecondaryRegexMatcher.h
    ${CMAKE_SOURCE_DIR}/src/BuiltinPlugins/RegexWorkbench/RegexWorkbenchTypes.h
    ${CMAKE_SOURCE_DIR}/src/Misc/PreSearchMatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/Misc/PreSearchMatcher.h
    ${CMAKE_SOURCE_DIR}/src/Misc/RegexMatchEnumerator.cpp
    ${CMAKE_SOURCE_DIR}/src/Misc/RegexMatchEnumerator.h
)

target_include_directories(regex_secondary_match_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${PCRE2_INCLUDE_DIRS}
)

target_link_libraries(regex_secondary_match_test PRIVATE
    Qt6::Core
    ${PCRE2_LIBRARIES}
)
target_compile_features(regex_secondary_match_test PRIVATE cxx_std_17)

add_test(NAME regex_secondary_match COMMAND regex_secondary_match_test)
set_tests_properties(regex_secondary_match PROPERTIES TIMEOUT 30)

add_executable(regex_apply_shared_test
    regex_apply_shared_test.cpp
    ${CMAKE_SOURCE_DIR}/src/Misc/SearchOperationsApply.cpp
    ${CMAKE_SOURCE_DIR}/src/Misc/SearchOperations.h
)

target_include_directories(regex_apply_shared_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(regex_apply_shared_test PRIVATE
    Qt6::Core
)
target_compile_features(regex_apply_shared_test PRIVATE cxx_std_17)

add_test(NAME regex_apply_shared COMMAND regex_apply_shared_test)
set_tests_properties(regex_apply_shared PROPERTIES TIMEOUT 30)

add_executable(regex_recursive_replace_test
    regex_recursive_replace_test.cpp
    ${CMAKE_SOURCE_DIR}/src/BuiltinPlugins/RegexWorkbench/RecursiveReplaceGuard.cpp
    ${CMAKE_SOURCE_DIR}/src/BuiltinPlugins/RegexWorkbench/RecursiveReplaceGuard.h
    ${CMAKE_SOURCE_DIR}/src/BuiltinPlugins/RegexWorkbench/RegexWorkbenchEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/BuiltinPlugins/RegexWorkbench/RegexWorkbenchEngine.h
    ${CMAKE_SOURCE_DIR}/src/BuiltinPlugins/RegexWorkbench/RegexWorkbenchTypes.h
    ${CMAKE_SOURCE_DIR}/src/BuiltinPlugins/RegexWorkbench/SecondaryRegexMatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/BuiltinPlugins/RegexWorkbench/SecondaryRegexMatcher.h
    ${CMAKE_SOURCE_DIR}/src/Misc/PreSearchMatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/Misc/PreSearchMatcher.h
    ${CMAKE_SOURCE_DIR}/src/Misc/RegexMatchEnumerator.cpp
    ${CMAKE_SOURCE_DIR}/src/Misc/RegexMatchEnumerator.h
    ${CMAKE_SOURCE_DIR}/src/Misc/SearchOperationsApply.cpp
    ${CMAKE_SOURCE_DIR}/src/Misc/SearchOperations.h
)

target_include_directories(regex_recursive_replace_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${PCRE2_INCLUDE_DIRS}
)

target_link_libraries(regex_recursive_replace_test PRIVATE
    Qt6::Core
    ${PCRE2_LIBRARIES}
)
target_compile_features(regex_recursive_replace_test PRIVATE cxx_std_17)

add_test(NAME regex_recursive_replace COMMAND regex_recursive_replace_test)
set_tests_properties(regex_recursive_replace PROPERTIES TIMEOUT 30)

add_executable(regex_capture_names_test
    regex_capture_names_test.cpp
    ${CMAKE_SOURCE_DIR}/src/PCRE2/CaptureNameTable.cpp
    ${CMAKE_SOURCE_DIR}/src/PCRE2/CaptureNameTable.h
)

target_include_directories(regex_capture_names_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${PCRE2_INCLUDE_DIRS}
)

target_link_libraries(regex_capture_names_test PRIVATE
    Qt6::Core
    ${PCRE2_LIBRARIES}
)
target_compile_features(regex_capture_names_test PRIVATE cxx_std_17)

add_test(NAME regex_capture_names COMMAND regex_capture_names_test)
set_tests_properties(regex_capture_names PROPERTIES TIMEOUT 30)

add_executable(regex_variable_store_test
    regex_variable_store_test.cpp
    ${CMAKE_SOURCE_DIR}/src/BuiltinPlugins/RegexWorkbench/SearchVariableStore.cpp
    ${CMAKE_SOURCE_DIR}/src/BuiltinPlugins/RegexWorkbench/SearchVariableStore.h
)

target_include_directories(regex_variable_store_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(regex_variable_store_test PRIVATE
    Qt6::Core
)
target_compile_features(regex_variable_store_test PRIVATE cxx_std_17)

add_test(NAME regex_variable_store COMMAND regex_variable_store_test)
set_tests_properties(regex_variable_store PROPERTIES TIMEOUT 30)

add_executable(regex_replacement_resolver_test
    regex_replacement_resolver_test.cpp
    ${CMAKE_SOURCE_DIR}/src/PCRE2/PCREReplaceTextBuilder.cpp
    ${CMAKE_SOURCE_DIR}/src/PCRE2/PCREReplaceTextBuilder.h
    ${CMAKE_SOURCE_DIR}/src/PCRE2/ReplacementVariableResolver.h
)

target_include_directories(regex_replacement_resolver_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${PCRE2_INCLUDE_DIRS}
)

target_link_libraries(regex_replacement_resolver_test PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)
target_compile_features(regex_replacement_resolver_test PRIVATE cxx_std_17)

add_test(NAME regex_replacement_resolver COMMAND regex_replacement_resolver_test)
set_tests_properties(regex_replacement_resolver PROPERTIES TIMEOUT 30)

add_executable(regex_filter_ingest_order_test
    regex_filter_ingest_order_test.cpp
    ${CMAKE_SOURCE_DIR}/src/BuiltinPlugins/RegexWorkbench/RecursiveReplaceGuard.cpp
    ${CMAKE_SOURCE_DIR}/src/BuiltinPlugins/RegexWorkbench/RecursiveReplaceGuard.h
    ${CMAKE_SOURCE_DIR}/src/BuiltinPlugins/RegexWorkbench/RegexWorkbenchEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/BuiltinPlugins/RegexWorkbench/RegexWorkbenchEngine.h
    ${CMAKE_SOURCE_DIR}/src/BuiltinPlugins/RegexWorkbench/RegexWorkbenchTypes.h
    ${CMAKE_SOURCE_DIR}/src/BuiltinPlugins/RegexWorkbench/SearchVariableStore.cpp
    ${CMAKE_SOURCE_DIR}/src/BuiltinPlugins/RegexWorkbench/SearchVariableStore.h
    ${CMAKE_SOURCE_DIR}/src/BuiltinPlugins/RegexWorkbench/SecondaryRegexMatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/BuiltinPlugins/RegexWorkbench/SecondaryRegexMatcher.h
    ${CMAKE_SOURCE_DIR}/src/Misc/PreSearchMatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/Misc/PreSearchMatcher.h
    ${CMAKE_SOURCE_DIR}/src/Misc/RegexMatchEnumerator.cpp
    ${CMAKE_SOURCE_DIR}/src/Misc/RegexMatchEnumerator.h
    ${CMAKE_SOURCE_DIR}/src/Misc/SearchOperationsApply.cpp
    ${CMAKE_SOURCE_DIR}/src/Misc/SearchOperations.h
)

target_include_directories(regex_filter_ingest_order_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${PCRE2_INCLUDE_DIRS}
)

target_link_libraries(regex_filter_ingest_order_test PRIVATE
    Qt6::Core
    ${PCRE2_LIBRARIES}
)
target_compile_features(regex_filter_ingest_order_test PRIVATE cxx_std_17)

add_test(NAME regex_filter_ingest_order COMMAND regex_filter_ingest_order_test)
set_tests_properties(regex_filter_ingest_order PROPERTIES TIMEOUT 30)

add_executable(regex_variable_executor_test
    regex_variable_executor_test.cpp
    ${CMAKE_SOURCE_DIR}/src/BuiltinPlugins/RegexWorkbench/RecursiveReplaceGuard.cpp
    ${CMAKE_SOURCE_DIR}/src/BuiltinPlugins/RegexWorkbench/RecursiveReplaceGuard.h
    ${CMAKE_SOURCE_DIR}/src/BuiltinPlugins/RegexWorkbench/RegexWorkbenchEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/BuiltinPlugins/RegexWorkbench/RegexWorkbenchEngine.h
    ${CMAKE_SOURCE_DIR}/src/BuiltinPlugins/RegexWorkbench/RegexWorkbenchTypes.h
    ${CMAKE_SOURCE_DIR}/src/BuiltinPlugins/RegexWorkbench/RegexWorkbenchVariableExecutor.cpp
    ${CMAKE_SOURCE_DIR}/src/BuiltinPlugins/RegexWorkbench/RegexWorkbenchVariableExecutor.h
    ${CMAKE_SOURCE_DIR}/src/BuiltinPlugins/RegexWorkbench/SearchVariableStore.cpp
    ${CMAKE_SOURCE_DIR}/src/BuiltinPlugins/RegexWorkbench/SearchVariableStore.h
    ${CMAKE_SOURCE_DIR}/src/BuiltinPlugins/RegexWorkbench/SearchVariableStoreSPCRE.cpp
    ${CMAKE_SOURCE_DIR}/src/BuiltinPlugins/RegexWorkbench/SecondaryRegexMatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/BuiltinPlugins/RegexWorkbench/SecondaryRegexMatcher.h
    ${CMAKE_SOURCE_DIR}/src/Misc/PreSearchMatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/Misc/PreSearchMatcher.h
    ${CMAKE_SOURCE_DIR}/src/Misc/RegexMatchEnumerator.cpp
    ${CMAKE_SOURCE_DIR}/src/Misc/RegexMatchEnumerator.h
    ${CMAKE_SOURCE_DIR}/src/Misc/SearchOperationsApply.cpp
    ${CMAKE_SOURCE_DIR}/src/Misc/SearchOperations.h
)

target_include_directories(regex_variable_executor_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${PCRE2_INCLUDE_DIRS}
)

target_link_libraries(regex_variable_executor_test PRIVATE
    Qt6::Core
    ${PCRE2_LIBRARIES}
)
target_compile_features(regex_variable_executor_test PRIVATE cxx_std_17)

add_test(NAME regex_variable_executor COMMAND regex_variable_executor_test)
set_tests_properties(regex_variable_executor PROPERTIES TIMEOUT 30)

add_executable(regex_recipe_store_test
    regex_recipe_store_test.cpp
    ${CMAKE_SOURCE_DIR}/src/BuiltinPlugins/RegexWorkbench/RegexRecipeSearchEditorAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/BuiltinPlugins/RegexWorkbench/RegexRecipeSearchEditorAdapter.h
    ${CMAKE_SOURCE_DIR}/src/BuiltinPlugins/RegexWorkbench/RegexRecipeStore.cpp
    ${CMAKE_SOURCE_DIR}/src/BuiltinPlugins/RegexWorkbench/RegexRecipeStore.h
    ${CMAKE_SOURCE_DIR}/src/BuiltinPlugins/RegexWorkbench/RegexWorkbenchTypes.h
    ${CMAKE_SOURCE_DIR}/src/BuiltinPlugins/RegexWorkbench/SearchVariableStore.cpp
    ${CMAKE_SOURCE_DIR}/src/BuiltinPlugins/RegexWorkbench/SearchVariableStore.h
)

target_include_directories(regex_recipe_store_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(regex_recipe_store_test PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)
target_compile_features(regex_recipe_store_test PRIVATE cxx_std_17)

add_test(NAME regex_recipe_store COMMAND regex_recipe_store_test)
set_tests_properties(regex_recipe_store PROPERTIES TIMEOUT 30)

add_executable(search_batch_runner_test
    search_batch_runner_test.cpp
    ${CMAKE_SOURCE_DIR}/src/Misc/SearchBatchRunner.cpp
    ${CMAKE_SOURCE_DIR}/src/Misc/SearchBatchRunner.h
)

target_include_directories(search_batch_runner_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(search_batch_runner_test PRIVATE
    Qt6::Core
)

add_test(NAME search_batch_runner COMMAND search_batch_runner_test)
set_tests_properties(search_batch_runner PROPERTIES TIMEOUT 30)

add_executable(search_batch_benchmark
    search_batch_benchmark.cpp
    ${CMAKE_SOURCE_DIR}/src/Misc/SearchBatchRunner.cpp
    ${CMAKE_SOURCE_DIR}/src/Misc/SearchBatchRunner.h
)

target_include_directories(search_batch_benchmark PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(search_batch_benchmark PRIVATE
    Qt6::Core
)

add_test(NAME search_batch_benchmark COMMAND search_batch_benchmark)
set_tests_properties(search_batch_benchmark PROPERTIES TIMEOUT 60)

add_executable(staged_text_validator_test
    staged_text_validator_test.cpp
    ${CMAKE_SOURCE_DIR}/src/Misc/StagedTextValidator.cpp
    ${CMAKE_SOURCE_DIR}/src/Misc/StagedTextValidator.h
)

target_include_directories(staged_text_validator_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(staged_text_validator_test PRIVATE
    Qt6::Core
)
target_compile_features(staged_text_validator_test PRIVATE cxx_std_17)

add_test(NAME staged_text_validator COMMAND staged_text_validator_test)
set_tests_properties(staged_text_validator PROPERTIES TIMEOUT 30)

add_test(NAME search_operations_reentrancy
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/search_operations_reentrancy_test.py
            ${CMAKE_SOURCE_DIR}
)
set_tests_properties(search_operations_reentrancy PROPERTIES TIMEOUT 30)

add_executable(text_document_undo_test
    text_document_undo_test.cpp
    ${CMAKE_SOURCE_DIR}/src/Widgets/TextDocument.cpp
    ${CMAKE_SOURCE_DIR}/src/Widgets/TextDocument.h
)

target_include_directories(text_document_undo_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(text_document_undo_test PRIVATE
    Qt6::Core
    Qt6::Gui
)
set_target_properties(text_document_undo_test PROPERTIES AUTOMOC ON)

add_test(NAME text_document_undo COMMAND text_document_undo_test)
set_tests_properties(text_document_undo PROPERTIES
    TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_test(NAME search_undo_contract
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/search_undo_contract_test.py
            ${CMAKE_SOURCE_DIR}
)
set_tests_properties(search_undo_contract PROPERTIES TIMEOUT 30)

add_executable(booklive_paragraph_normalizer_test
    booklive_paragraph_normalizer_test.cpp
    ${CMAKE_SOURCE_DIR}/src/BuiltinPlugins/BookLiveParagraphNormalizer.cpp
    ${CMAKE_SOURCE_DIR}/src/BuiltinPlugins/BookLiveParagraphNormalizer.h
)

target_include_directories(booklive_paragraph_normalizer_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(booklive_paragraph_normalizer_test PRIVATE
    Qt6::Core
    Qt6::Xml
)

add_test(NAME booklive_paragraph_normalizer COMMAND booklive_paragraph_normalizer_test)
set_tests_properties(booklive_paragraph_normalizer PROPERTIES TIMEOUT 30)

    add_executable(font_subset_smoke_test
        font_subset_smoke_test.cpp
    )
    target_link_libraries(font_subset_smoke_test PRIVATE
        Qt6::Core
        Sigil::HarfBuzzSubset
    )
    target_compile_definitions(font_subset_smoke_test PRIVATE
        SIGIL_FONT_TEST_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/fonts/OpenSans-Test.ttf"
    )
    add_test(NAME font_subset_smoke COMMAND font_subset_smoke_test)
    set_tests_properties(font_subset_smoke PROPERTIES TIMEOUT 30)

    add_executable(font_subset_core_test
        font_subset_core_test.cpp
        ${CMAKE_SOURCE_DIR}/src/BookManipulation/FontSubset/FontInspector.cpp
        ${CMAKE_SOURCE_DIR}/src/BookManipulation/FontSubset/FontInspector.h
        ${CMAKE_SOURCE_DIR}/src/BookManipulation/FontSubset/FontSubsetTypes.h
        ${CMAKE_SOURCE_DIR}/src/BookManipulation/FontSubset/HarfBuzzRAII.h
        ${CMAKE_SOURCE_DIR}/src/BookManipulation/FontSubset/HarfBuzzSubsetEngine.cpp
        ${CMAKE_SOURCE_DIR}/src/BookManipulation/FontSubset/HarfBuzzSubsetEngine.h
    )
    target_include_directories(font_subset_core_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )
    target_link_libraries(font_subset_core_test PRIVATE
        Qt6::Core
        Sigil::HarfBuzzSubset
    )
    target_compile_definitions(font_subset_core_test PRIVATE
        SIGIL_FONT_TEST_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/fonts/OpenSans-Test.ttf"
    )
    add_test(NAME font_subset_core COMMAND font_subset_core_test)
    set_tests_properties(font_subset_core PROPERTIES TIMEOUT 30)

    add_executable(font_subset_workflow_test
        font_subset_workflow_test.cpp
        ${CMAKE_SOURCE_DIR}/src/BookManipulation/FontSubset/GlobalFontUsageCollector.cpp
        ${CMAKE_SOURCE_DIR}/src/BookManipulation/FontSubset/GlobalFontUsageCollector.h
        ${CMAKE_SOURCE_DIR}/src/BookManipulation/FontSubset/FontSubsetTransaction.cpp
        ${CMAKE_SOURCE_DIR}/src/BookManipulation/FontSubset/FontSubsetTransaction.h
        ${CMAKE_SOURCE_DIR}/src/Misc/XMLEntities.cpp
        ${CMAKE_SOURCE_DIR}/src/Misc/XMLEntities.h
    )
    target_include_directories(font_subset_workflow_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )
    target_link_libraries(font_subset_workflow_test PRIVATE
        Qt6::Core
    )
    add_test(NAME font_subset_workflow COMMAND font_subset_workflow_test)
    set_tests_properties(font_subset_workflow PROPERTIES TIMEOUT 30)

    add_executable(font_subset_batch_test
        font_subset_batch_test.cpp
        ${CMAKE_SOURCE_DIR}/src/BookManipulation/FontSubset/FontSubsetBatchAnalysis.cpp
        ${CMAKE_SOURCE_DIR}/src/BookManipulation/FontSubset/FontSubsetController.h
        ${CMAKE_SOURCE_DIR}/src/BookManipulation/FontSubset/FontSubsetTypes.h
        ${CMAKE_SOURCE_DIR}/src/BookManipulation/FontSubset/FontInspector.cpp
        ${CMAKE_SOURCE_DIR}/src/BookManipulation/FontSubset/FontInspector.h
        ${CMAKE_SOURCE_DIR}/src/BookManipulation/FontSubset/GlobalFontUsageCollector.cpp
        ${CMAKE_SOURCE_DIR}/src/BookManipulation/FontSubset/GlobalFontUsageCollector.h
        ${CMAKE_SOURCE_DIR}/src/BookManipulation/FontSubset/HarfBuzzRAII.h
        ${CMAKE_SOURCE_DIR}/src/BookManipulation/FontSubset/HarfBuzzSubsetEngine.cpp
        ${CMAKE_SOURCE_DIR}/src/BookManipulation/FontSubset/HarfBuzzSubsetEngine.h
        ${CMAKE_SOURCE_DIR}/src/Misc/XMLEntities.cpp
        ${CMAKE_SOURCE_DIR}/src/Misc/XMLEntities.h
    )
    target_include_directories(font_subset_batch_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )
    target_link_libraries(font_subset_batch_test PRIVATE
        Qt6::Core
        Qt6::Xml
        Sigil::HarfBuzzSubset
    )
    target_compile_definitions(font_subset_batch_test PRIVATE
        SIGIL_FONT_TEST_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/fonts/OpenSans-Test.ttf"
    )
    add_test(NAME font_subset_batch COMMAND font_subset_batch_test)
    set_tests_properties(font_subset_batch PROPERTIES TIMEOUT 30)
add_executable(safe_archive_extractor_test
    safe_archive_extractor_test.cpp
    ${CMAKE_SOURCE_DIR}/src/Misc/SafeArchiveExtractor.cpp
    ${CMAKE_SOURCE_DIR}/src/PluginAPI/PluginInputValidator.cpp
    ${CMAKE_SOURCE_DIR}/src/PluginAPI/PluginInputValidator.h
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

add_executable(chinese_conversion_core_test
    chinese_conversion_core_test.cpp
    ${CMAKE_SOURCE_DIR}/src/ChineseConversion/ChineseConversionProfile.cpp
    ${CMAKE_SOURCE_DIR}/src/ChineseConversion/ChineseConversionProfile.h
    ${CMAKE_SOURCE_DIR}/src/ChineseConversion/ChineseConversionData.cpp
    ${CMAKE_SOURCE_DIR}/src/ChineseConversion/ChineseConversionData.h
    ${CMAKE_SOURCE_DIR}/src/ChineseConversion/ChineseConversionSettings.cpp
    ${CMAKE_SOURCE_DIR}/src/ChineseConversion/ChineseConversionSettings.h
    ${CMAKE_SOURCE_DIR}/src/ChineseConversion/ChineseConversionTypes.h
    ${CMAKE_SOURCE_DIR}/src/ChineseConversion/OpenCCConverter.cpp
    ${CMAKE_SOURCE_DIR}/src/ChineseConversion/OpenCCConverter.h
)

target_include_directories(chinese_conversion_core_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(chinese_conversion_core_test PRIVATE
    Qt6::Core
    OpenCC::OpenCC
)

target_compile_definitions(chinese_conversion_core_test PRIVATE
    SIGIL_OPENCC_TEST_DATA_DIR="${SIGIL_OPENCC_DATA_DIR}"
    SIGIL_OPENCC_BUILD_DATA_DIR="${SIGIL_OPENCC_DATA_DIR}"
)

add_dependencies(chinese_conversion_core_test sigil_opencc_data)
add_test(NAME chinese_conversion_core COMMAND chinese_conversion_core_test)
set_tests_properties(chinese_conversion_core PROPERTIES TIMEOUT 60)

add_executable(chinese_conversion_markup_test
    chinese_conversion_markup_test.cpp
    ${CMAKE_SOURCE_DIR}/src/ChineseConversion/ChineseConversionProfile.cpp
    ${CMAKE_SOURCE_DIR}/src/ChineseConversion/ChineseConversionProfile.h
    ${CMAKE_SOURCE_DIR}/src/ChineseConversion/ChineseConversionTypes.h
    ${CMAKE_SOURCE_DIR}/src/ChineseConversion/ChineseTextConversionPlan.cpp
    ${CMAKE_SOURCE_DIR}/src/ChineseConversion/ChineseTextConversionPlan.h
    ${CMAKE_SOURCE_DIR}/src/ChineseConversion/OpenCCConverter.cpp
    ${CMAKE_SOURCE_DIR}/src/ChineseConversion/OpenCCConverter.h
)

target_include_directories(chinese_conversion_markup_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${GUMBO_INCLUDE_DIRS}
)

target_link_libraries(chinese_conversion_markup_test PRIVATE
    Qt6::Core
    OpenCC::OpenCC
    ${GUMBO_LIBRARIES}
)

target_compile_definitions(chinese_conversion_markup_test PRIVATE
    SIGIL_OPENCC_TEST_DATA_DIR="${SIGIL_OPENCC_DATA_DIR}"
)

add_dependencies(chinese_conversion_markup_test sigil_opencc_data)
add_test(NAME chinese_conversion_markup COMMAND chinese_conversion_markup_test)
set_tests_properties(chinese_conversion_markup PROPERTIES TIMEOUT 60)

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
add_test(NAME vendored_source_manifest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/vendored_source_manifest_test.py
)
set_tests_properties(vendored_source_manifest PROPERTIES TIMEOUT 30)

add_test(NAME ci_packaging
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/ci_packaging_test.py
)
set_tests_properties(ci_packaging PROPERTIES TIMEOUT 30)

add_test(NAME zh_cn_translation_coverage
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/zh_cn_translation_coverage_test.py
        --source-root ${CMAKE_SOURCE_DIR}
        --lupdate $<TARGET_FILE:Qt6::lupdate>
)
set_tests_properties(zh_cn_translation_coverage PROPERTIES TIMEOUT 60)

add_test(NAME zh_tw_translation_coverage
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/zh_cn_translation_coverage_test.py
        --source-root ${CMAKE_SOURCE_DIR}
        --lupdate $<TARGET_FILE:Qt6::lupdate>
        --locale zh_TW
)
set_tests_properties(zh_tw_translation_coverage PROPERTIES TIMEOUT 60)

add_test(NAME ja_translation_coverage
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/zh_cn_translation_coverage_test.py
        --source-root ${CMAKE_SOURCE_DIR}
        --lupdate $<TARGET_FILE:Qt6::lupdate>
        --locale ja
)
set_tests_properties(ja_translation_coverage PROPERTIES TIMEOUT 60)

add_test(NAME python_live_sdk
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/python_live_sdk_test.py
)
set_tests_properties(python_live_sdk PROPERTIES TIMEOUT 30)

add_test(NAME python_package_sync
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/python_package_sync_test.py
)
set_tests_properties(python_package_sync PROPERTIES TIMEOUT 30)

add_test(NAME sigil_mcp_backend
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/sigil_mcp_backend_test.py
)
set_tests_properties(sigil_mcp_backend PROPERTIES TIMEOUT 30)

add_test(NAME sigil_mcp_proxy
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/sigil_mcp_proxy_test.py
)
set_tests_properties(sigil_mcp_proxy PROPERTIES TIMEOUT 30)

add_test(NAME sigil_mcp_docs
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/sigil_mcp_docs_test.py
)
set_tests_properties(sigil_mcp_docs PROPERTIES TIMEOUT 30)

add_test(NAME sigil_epub_layout_skill
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/sigil_epub_layout_skill_test.py
)
set_tests_properties(sigil_epub_layout_skill PROPERTIES TIMEOUT 30)

add_test(NAME sigil_mcp_protocol
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/sigil_mcp_protocol_test.py
)
set_tests_properties(sigil_mcp_protocol PROPERTIES TIMEOUT 30)
if( APPLE )
    set_tests_properties(sigil_mcp_protocol PROPERTIES
        ENVIRONMENT "PYTHONPATH=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Sigil.app/Contents/python3lib"
    )
endif()

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

add_test(NAME live_plugin_docs
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/live_plugin_docs_test.py
)
set_tests_properties(live_plugin_docs PROPERTIES TIMEOUT 30)

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

add_executable(plugin_writer_lock_test
    plugin_writer_lock_test.cpp
    ${CMAKE_SOURCE_DIR}/src/PluginAPI/PluginWriterLock.h
)

target_include_directories(plugin_writer_lock_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(plugin_writer_lock_test PRIVATE
    Qt6::Core
)

add_test(NAME plugin_writer_lock COMMAND plugin_writer_lock_test)
set_tests_properties(plugin_writer_lock PROPERTIES TIMEOUT 30)

add_executable(plugin_package_update_test
    plugin_package_update_test.cpp
    ${CMAKE_SOURCE_DIR}/src/PluginAPI/PluginPackageUpdate.cpp
    ${CMAKE_SOURCE_DIR}/src/PluginAPI/PluginPackageUpdate.h
)

target_include_directories(plugin_package_update_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(plugin_package_update_test PRIVATE
    Qt6::Core
    Qt6::Xml
)

add_test(NAME plugin_package_update COMMAND plugin_package_update_test)
set_tests_properties(plugin_package_update PROPERTIES TIMEOUT 30)
