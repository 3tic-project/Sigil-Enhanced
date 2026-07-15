#include <cstdlib>
#include <iostream>
#include <memory>

#include <QFile>

#include <hb.h>
#include <hb-subset.h>

namespace
{

template<typename T, void (*Destroy)(T*)>
using HbPtr = std::unique_ptr<T, decltype(Destroy)>;

template<typename T, void (*Destroy)(T*)>
HbPtr<T, Destroy> Take(T* pointer)
{
    return HbPtr<T, Destroy>(pointer, Destroy);
}

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

}

int main()
{
    QFile fixture(QStringLiteral(SIGIL_FONT_TEST_FIXTURE));
    Require(fixture.open(QIODevice::ReadOnly), "could not open the font fixture");
    const QByteArray inputBytes = fixture.readAll();
    Require(!inputBytes.isEmpty(), "font fixture is empty");

    auto blob = Take<hb_blob_t, hb_blob_destroy>(
        hb_blob_create(inputBytes.constData(), inputBytes.size(),
                       HB_MEMORY_MODE_DUPLICATE, nullptr, nullptr));
    Require(blob && hb_blob_get_length(blob.get()) == unsigned(inputBytes.size()),
            "could not create the HarfBuzz input blob");

    auto face = Take<hb_face_t, hb_face_destroy>(hb_face_create(blob.get(), 0));
    Require(face && hb_face_get_glyph_count(face.get()) > 0,
            "could not create the HarfBuzz face");

    auto subsetInput = Take<hb_subset_input_t, hb_subset_input_destroy>(
        hb_subset_input_create_or_fail());
    Require(bool(subsetInput), "could not create subset input");
    hb_set_t* unicodes = hb_subset_input_unicode_set(subsetInput.get());
    hb_set_add(unicodes, 0x0041); // A
    hb_set_add(unicodes, 0x0062); // b
    hb_subset_input_set_flags(
        subsetInput.get(),
        hb_subset_flags_t(HB_SUBSET_FLAGS_DEFAULT | HB_SUBSET_FLAGS_NOTDEF_OUTLINE));

    auto plan = Take<hb_subset_plan_t, hb_subset_plan_destroy>(
        hb_subset_plan_create_or_fail(face.get(), subsetInput.get()));
    Require(bool(plan), "could not create subset plan");
    Require(hb_map_get_population(
                hb_subset_plan_old_to_new_glyph_mapping(plan.get())) >= 3,
            "subset plan did not contain expected glyph mappings");

    auto outputFace = Take<hb_face_t, hb_face_destroy>(
        hb_subset_plan_execute_or_fail(plan.get()));
    Require(outputFace && hb_face_get_glyph_count(outputFace.get()) >= 3,
            "could not execute subset plan");

    auto outputBlob = Take<hb_blob_t, hb_blob_destroy>(
        hb_face_reference_blob(outputFace.get()));
    unsigned outputLength = 0;
    const char* outputData = hb_blob_get_data(outputBlob.get(), &outputLength);
    Require(outputData && outputLength > 0,
            "could not serialize subset output");
    Require(outputLength < unsigned(inputBytes.size()),
            "subset output was not smaller than the fixture");

    auto outputFont = Take<hb_font_t, hb_font_destroy>(
        hb_font_create(outputFace.get()));
    hb_codepoint_t glyph = 0;
    Require(hb_font_get_nominal_glyph(outputFont.get(), 0x0041, &glyph),
            "subset output does not cover A");
    Require(hb_font_get_nominal_glyph(outputFont.get(), 0x0062, &glyph),
            "subset output does not cover b");
    Require(!hb_font_get_nominal_glyph(outputFont.get(), 0x005A, &glyph),
            "subset output unexpectedly covers Z");

    std::cout << "HarfBuzz " << hb_version_string()
              << ": " << inputBytes.size() << " -> " << outputLength
              << " bytes" << std::endl;
    return EXIT_SUCCESS;
}
