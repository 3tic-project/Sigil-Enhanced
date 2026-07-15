set(SIGIL_BUNDLED_HARFBUZZ_VERSION "14.2.1")

function(sigil_add_bundled_harfbuzz)
    # Keep HarfBuzz hermetic: Sigil needs only its core and subset libraries.
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_FRAMEWORK OFF)
    set(HB_BUILD_SUBSET ON)
    set(HB_BUILD_RASTER OFF)
    set(HB_BUILD_VECTOR OFF)
    set(HB_BUILD_GPU OFF)
    set(HB_BUILD_UTILS OFF)
    set(HB_HAVE_CAIRO OFF)
    set(HB_HAVE_CORETEXT OFF)
    set(HB_HAVE_DIRECTWRITE OFF)
    set(HB_HAVE_FREETYPE OFF)
    set(HB_HAVE_GDI OFF)
    set(HB_HAVE_GLIB OFF)
    set(HB_HAVE_GOBJECT OFF)
    set(HB_HAVE_GRAPHITE2 OFF)
    set(HB_HAVE_ICU OFF)
    set(HB_HAVE_INTROSPECTION OFF)
    set(HB_HAVE_UNISCRIBE OFF)

    cmake_policy(PUSH)
    cmake_policy(SET CMP0077 NEW)
    add_subdirectory(
        "${CMAKE_CURRENT_SOURCE_DIR}/harfbuzz"
        "${CMAKE_CURRENT_BINARY_DIR}/harfbuzz"
        EXCLUDE_FROM_ALL
    )
    cmake_policy(POP)

    foreach(hb_target harfbuzz harfbuzz-subset)
        get_target_property(hb_target_type ${hb_target} TYPE)
        if(NOT hb_target_type STREQUAL "STATIC_LIBRARY")
            message(FATAL_ERROR
                "Bundled ${hb_target} must be a static library, got ${hb_target_type}")
        endif()
        set_target_properties(${hb_target} PROPERTIES
            POSITION_INDEPENDENT_CODE ON
        )
    endforeach()

    add_library(sigil_bundled_harfbuzz INTERFACE)
    target_include_directories(sigil_bundled_harfbuzz INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/harfbuzz/src"
    )
    target_link_libraries(sigil_bundled_harfbuzz INTERFACE
        harfbuzz-subset
        harfbuzz
    )
    target_compile_definitions(sigil_bundled_harfbuzz INTERFACE
        SIGIL_BUNDLED_HARFBUZZ_VERSION="${SIGIL_BUNDLED_HARFBUZZ_VERSION}"
    )
    add_library(Sigil::HarfBuzzSubset ALIAS sigil_bundled_harfbuzz)
endfunction()

message(STATUS
    "Using bundled HarfBuzz ${SIGIL_BUNDLED_HARFBUZZ_VERSION} for font subsetting")
sigil_add_bundled_harfbuzz()
