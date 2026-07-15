#include "BookManipulation/FontSubset/HarfBuzzSubsetEngine.h"

#include <algorithm>

#include <QObject>
#include <hb.h>
#include <hb-subset.h>

#include "BookManipulation/FontSubset/HarfBuzzRAII.h"

namespace FontSubset
{
namespace
{

bool Covers(hb_font_t* font, quint32 codepoint, hb_codepoint_t* glyph = nullptr)
{
    hb_codepoint_t localGlyph = 0;
    return hb_font_get_nominal_glyph(font, codepoint,
                                     glyph ? glyph : &localGlyph);
}

QByteArray Serialize(hb_face_t* face)
{
    auto blob = TakeHb<hb_blob_t, hb_blob_destroy>(hb_face_reference_blob(face));
    unsigned length = 0;
    const char* data = hb_blob_get_data(blob.get(), &length);
    return data && length > 0 ? QByteArray(data, int(length)) : QByteArray();
}

}

Result HarfBuzzSubsetEngine::Subset(const QByteArray& fontBytes,
                                    const QSet<quint32>& codepoints,
                                    const Options& options,
                                    unsigned faceIndex) const
{
    Result result;
    result.oldSize = fontBytes.size();
    result.inputCodepoints = codepoints;
    result.harfbuzzVersion = QString::fromLatin1(hb_version_string());
    result.inspection = m_Inspector.Inspect(fontBytes, faceIndex);
    result.oldGlyphCount = result.inspection.glyphCount;
    result.warnings = result.inspection.warnings;
    if (!result.inspection.canSubset) {
        result.error = result.inspection.blockingReason;
        return result;
    }
    if (codepoints.isEmpty()) {
        result.error = QObject::tr("No Unicode codepoints were requested.");
        return result;
    }

    auto inputBlob = TakeHb<hb_blob_t, hb_blob_destroy>(
        hb_blob_create(fontBytes.constData(), fontBytes.size(),
                       HB_MEMORY_MODE_DUPLICATE, nullptr, nullptr));
    auto inputFace = TakeHb<hb_face_t, hb_face_destroy>(
        hb_face_create(inputBlob.get(), faceIndex));
    auto inputFont = TakeHb<hb_font_t, hb_font_destroy>(hb_font_create(inputFace.get()));
    for (quint32 codepoint : codepoints) {
        if (Covers(inputFont.get(), codepoint)) {
            result.requestedCodepoints.insert(codepoint);
        } else {
            result.unavailableCodepoints.insert(codepoint);
        }
    }
    if (result.requestedCodepoints.isEmpty()) {
        result.error = QObject::tr(
            "The source font does not cover any requested codepoints.");
        return result;
    }
    if (!result.unavailableCodepoints.isEmpty()) {
        result.warnings.append(QObject::tr(
            "Some book codepoints were not present in the source font and were ignored."));
    }

    auto subsetInput = TakeHb<hb_subset_input_t, hb_subset_input_destroy>(
        hb_subset_input_create_or_fail());
    if (!subsetInput) {
        result.error = QObject::tr("Could not create HarfBuzz subset input.");
        return result;
    }
    hb_set_t* unicodeSet = hb_subset_input_unicode_set(subsetInput.get());
    QList<quint32> sortedCodepoints(result.requestedCodepoints.begin(),
                                    result.requestedCodepoints.end());
    std::sort(sortedCodepoints.begin(), sortedCodepoints.end());
    for (quint32 codepoint : sortedCodepoints) {
        hb_set_add(unicodeSet, codepoint);
    }

    unsigned flags = HB_SUBSET_FLAGS_DEFAULT;
    if (options.dropHinting) {
        flags |= HB_SUBSET_FLAGS_NO_HINTING;
    }
    if (options.keepNotdefOutline) {
        flags |= HB_SUBSET_FLAGS_NOTDEF_OUTLINE;
    }
#if HB_VERSION_ATLEAST(8, 5, 0)
    if (result.inspection.risks.contains(Risk::VariableFont)) {
        flags |= HB_SUBSET_FLAGS_OPTIMIZE_IUP_DELTAS;
    }
#endif
    hb_subset_input_set_flags(subsetInput.get(), hb_subset_flags_t(flags));

    auto plan = TakeHb<hb_subset_plan_t, hb_subset_plan_destroy>(
        hb_subset_plan_create_or_fail(inputFace.get(), subsetInput.get()));
    if (!plan) {
        result.error = QObject::tr("Could not create HarfBuzz subset plan.");
        return result;
    }
    const hb_map_t* oldToNew =
        hb_subset_plan_old_to_new_glyph_mapping(plan.get());
    result.mappedGlyphCount = hb_map_get_population(oldToNew);

    auto outputFace = TakeHb<hb_face_t, hb_face_destroy>(
        hb_subset_plan_execute_or_fail(plan.get()));
    if (!outputFace) {
        result.error = QObject::tr("HarfBuzz could not execute the subset plan.");
        return result;
    }
    result.outputBytes = Serialize(outputFace.get());
    result.newSize = result.outputBytes.size();
    result.newGlyphCount = hb_face_get_glyph_count(outputFace.get());
    if (result.outputBytes.isEmpty() || result.newGlyphCount == 0) {
        result.error = QObject::tr("HarfBuzz produced an empty or invalid font.");
        result.outputBytes.clear();
        return result;
    }

    const Inspection outputInspection = m_Inspector.Inspect(result.outputBytes, 0);
    if (!outputInspection.canSubset ||
        outputInspection.format != result.inspection.format) {
        result.error = QObject::tr(
            "The subset output could not be reparsed as the original font format.");
        result.outputBytes.clear();
        return result;
    }

    auto outputFont = TakeHb<hb_font_t, hb_font_destroy>(hb_font_create(outputFace.get()));
    for (quint32 codepoint : result.requestedCodepoints) {
        if (!Covers(outputFont.get(), codepoint)) {
            result.missingCodepoints.insert(codepoint);
        }
    }
    if (!result.missingCodepoints.isEmpty()) {
        result.error = QObject::tr(
            "The subset output does not cover every requested codepoint.");
        result.outputBytes.clear();
        return result;
    }

    if (options.validateShaping && !options.shapingSamples.isEmpty()) {
        QStringList applicableSamples;
        for (const QString& sample : options.shapingSamples) {
            const QList<uint> sampleCodepoints = sample.toUcs4();
            const bool covered = std::all_of(
                sampleCodepoints.begin(), sampleCodepoints.end(),
                [&result](uint codepoint) {
                    return codepoint == '\n' || codepoint == '\r' ||
                           codepoint == '\t' ||
                           result.requestedCodepoints.contains(codepoint);
                });
            if (covered) {
                applicableSamples.append(sample);
            }
        }
        if (!applicableSamples.isEmpty() &&
            !ValidateShaping(inputFace.get(), outputFace.get(), oldToNew,
                             applicableSamples, &result.error)) {
            result.outputBytes.clear();
            return result;
        }
    }
    if (result.newSize >= result.oldSize) {
        result.warnings.append(QObject::tr(
            "The subset output is not smaller than the source font."));
    }
    result.success = true;
    return result;
}

bool HarfBuzzSubsetEngine::ValidateShaping(hb_face_t* inputFace,
                                           hb_face_t* outputFace,
                                           const hb_map_t* oldToNewGlyphs,
                                           const QStringList& samples,
                                           QString* error) const
{
    auto inputFont = TakeHb<hb_font_t, hb_font_destroy>(hb_font_create(inputFace));
    auto outputFont = TakeHb<hb_font_t, hb_font_destroy>(hb_font_create(outputFace));
    const int inputUpem = int(hb_face_get_upem(inputFace));
    const int outputUpem = int(hb_face_get_upem(outputFace));
    hb_font_set_scale(inputFont.get(), inputUpem, inputUpem);
    hb_font_set_scale(outputFont.get(), outputUpem, outputUpem);

    for (const QString& sample : samples) {
        const QByteArray utf8 = sample.toUtf8();
        auto inputBuffer = TakeHb<hb_buffer_t, hb_buffer_destroy>(hb_buffer_create());
        auto outputBuffer = TakeHb<hb_buffer_t, hb_buffer_destroy>(hb_buffer_create());
        hb_buffer_add_utf8(inputBuffer.get(), utf8.constData(), utf8.size(), 0, -1);
        hb_buffer_add_utf8(outputBuffer.get(), utf8.constData(), utf8.size(), 0, -1);
        hb_buffer_guess_segment_properties(inputBuffer.get());
        hb_buffer_guess_segment_properties(outputBuffer.get());
        hb_shape(inputFont.get(), inputBuffer.get(), nullptr, 0);
        hb_shape(outputFont.get(), outputBuffer.get(), nullptr, 0);

        unsigned inputLength = 0;
        unsigned outputLength = 0;
        const hb_glyph_info_t* inputInfo =
            hb_buffer_get_glyph_infos(inputBuffer.get(), &inputLength);
        const hb_glyph_info_t* outputInfo =
            hb_buffer_get_glyph_infos(outputBuffer.get(), &outputLength);
        const hb_glyph_position_t* inputPositions =
            hb_buffer_get_glyph_positions(inputBuffer.get(), nullptr);
        const hb_glyph_position_t* outputPositions =
            hb_buffer_get_glyph_positions(outputBuffer.get(), nullptr);
        if (inputLength != outputLength) {
            *error = QObject::tr("Shaping validation changed the glyph count.");
            return false;
        }
        for (unsigned i = 0; i < inputLength; ++i) {
            const hb_codepoint_t mappedGlyph =
                hb_map_get(oldToNewGlyphs, inputInfo[i].codepoint);
            if (mappedGlyph == HB_MAP_VALUE_INVALID ||
                mappedGlyph != outputInfo[i].codepoint ||
                inputInfo[i].cluster != outputInfo[i].cluster ||
                inputPositions[i].x_advance != outputPositions[i].x_advance ||
                inputPositions[i].y_advance != outputPositions[i].y_advance ||
                inputPositions[i].x_offset != outputPositions[i].x_offset ||
                inputPositions[i].y_offset != outputPositions[i].y_offset) {
                *error = QObject::tr(
                    "Shaping validation changed glyph mapping or positioning.");
                return false;
            }
        }
    }
    return true;
}

}
