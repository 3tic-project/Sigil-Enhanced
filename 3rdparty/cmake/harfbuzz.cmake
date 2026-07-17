set(SIGIL_BUNDLED_HARFBUZZ_VERSION "14.2.1")

function(sigil_add_bundled_harfbuzz)
    # The upstream subset amalgam contains the core and subset APIs in one
    # translation unit. This is the supported simplified static build and
    # avoids duplicate core objects across split archives on MSVC.
    add_library(sigil_bundled_harfbuzz STATIC
        "${CMAKE_CURRENT_SOURCE_DIR}/harfbuzz/src/harfbuzz-subset.cc"
    )
    target_compile_features(sigil_bundled_harfbuzz PRIVATE cxx_std_11)
    target_include_directories(sigil_bundled_harfbuzz PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/harfbuzz/src"
    )
    set_target_properties(sigil_bundled_harfbuzz PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        VISIBILITY_INLINES_HIDDEN TRUE
    )

    if(MSVC)
        target_compile_options(sigil_bundled_harfbuzz PRIVATE
            /bigobj /utf-8 /wd4244 /wd4267
        )
        target_compile_definitions(sigil_bundled_harfbuzz PRIVATE
            _CRT_SECURE_NO_WARNINGS
            _CRT_NONSTDC_NO_WARNINGS
        )
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR
           CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(sigil_bundled_harfbuzz PRIVATE
            -fno-exceptions
            -fno-rtti
            -fno-threadsafe-statics
        )
    endif()

    if(NOT MSVC)
        set(THREADS_PREFER_PTHREAD_FLAG ON)
        find_package(Threads REQUIRED)
        if(CMAKE_USE_PTHREADS_INIT)
            target_compile_definitions(sigil_bundled_harfbuzz PRIVATE HAVE_PTHREAD)
            target_link_libraries(sigil_bundled_harfbuzz PRIVATE Threads::Threads)
        endif()
    endif()
    if(UNIX)
        target_link_libraries(sigil_bundled_harfbuzz PRIVATE m)
    endif()

    target_compile_definitions(sigil_bundled_harfbuzz INTERFACE
        SIGIL_BUNDLED_HARFBUZZ_VERSION="${SIGIL_BUNDLED_HARFBUZZ_VERSION}"
    )
    add_library(Sigil::HarfBuzzSubset ALIAS sigil_bundled_harfbuzz)
endfunction()

message(STATUS
    "Using bundled HarfBuzz ${SIGIL_BUNDLED_HARFBUZZ_VERSION} for font subsetting")
sigil_add_bundled_harfbuzz()
