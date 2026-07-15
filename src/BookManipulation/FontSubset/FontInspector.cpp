#include "BookManipulation/FontSubset/FontInspector.h"

#include <algorithm>

#include <QObject>
#include <hb.h>

#include "BookManipulation/FontSubset/HarfBuzzRAII.h"

namespace FontSubset
{
namespace
{

constexpr quint32 Tag(char a, char b, char c, char d)
{
    return HB_TAG(a, b, c, d);
}

bool HasAny(const QSet<quint32>& tags, std::initializer_list<quint32> candidates)
{
    return std::any_of(candidates.begin(), candidates.end(),
                       [&tags](quint32 tag) { return tags.contains(tag); });
}

void AddRisk(Inspection& inspection, Risk risk, const QString& warning,
             bool blocking = false)
{
    if (!inspection.risks.contains(risk)) {
        inspection.risks.append(risk);
    }
    if (!warning.isEmpty()) {
        inspection.warnings.append(warning);
    }
    if (blocking && inspection.blockingReason.isEmpty()) {
        inspection.blockingReason = warning;
    }
}

std::optional<quint16> ReadFsType(hb_face_t* face)
{
    auto table = TakeHb<hb_blob_t, hb_blob_destroy>(
        hb_face_reference_table(face, Tag('O', 'S', '/', '2')));
    unsigned length = 0;
    const auto* bytes = reinterpret_cast<const unsigned char*>(
        hb_blob_get_data(table.get(), &length));
    if (!bytes || length < 10) {
        return std::nullopt;
    }
    return quint16((quint16(bytes[8]) << 8) | quint16(bytes[9]));
}

QSet<quint32> ReadTableTags(hb_face_t* face)
{
    unsigned count = hb_face_get_table_tags(face, 0, nullptr, nullptr);
    QList<hb_tag_t> tags;
    tags.resize(int(count));
    if (count > 0) {
        unsigned written = count;
        hb_face_get_table_tags(face, 0, &written, tags.data());
        tags.resize(int(written));
    }
    QSet<quint32> result;
    for (hb_tag_t tag : tags) {
        result.insert(tag);
    }
    return result;
}

}

ContainerFormat FontInspector::DetectContainer(const QByteArray& fontBytes)
{
    if (fontBytes.size() < 4) {
        return ContainerFormat::Unknown;
    }
    const auto* bytes = reinterpret_cast<const unsigned char*>(fontBytes.constData());
    const quint32 signature = (quint32(bytes[0]) << 24) |
                              (quint32(bytes[1]) << 16) |
                              (quint32(bytes[2]) << 8) |
                              quint32(bytes[3]);
    if (signature == 0x00010000u || signature == Tag('t', 'r', 'u', 'e') ||
        signature == Tag('t', 'y', 'p', '1')) {
        return ContainerFormat::SfntTrueType;
    }
    if (signature == Tag('O', 'T', 'T', 'O')) {
        return ContainerFormat::SfntCff;
    }
    if (signature == Tag('t', 't', 'c', 'f')) {
        return ContainerFormat::Collection;
    }
    if (signature == Tag('w', 'O', 'F', 'F')) {
        return ContainerFormat::Woff;
    }
    if (signature == Tag('w', 'O', 'F', '2')) {
        return ContainerFormat::Woff2;
    }
    return ContainerFormat::Unknown;
}

LicenseStatus FontInspector::ClassifyLicense(std::optional<quint16> fsType)
{
    if (!fsType.has_value()) {
        return LicenseStatus::InvalidOrMissing;
    }
    constexpr quint16 knownMask = 0x030e;
    constexpr quint16 embeddingMask = 0x000e;
    const quint16 value = *fsType;
    const quint16 embedding = value & embeddingMask;
    if ((value & ~knownMask) != 0 ||
        (embedding != 0 && embedding != 0x0002 && embedding != 0x0004 &&
         embedding != 0x0008)) {
        return LicenseStatus::InvalidOrMissing;
    }
    if ((value & 0x0100) != 0) {
        return LicenseStatus::NoSubsetting;
    }
    if ((value & 0x0200) != 0) {
        return LicenseStatus::BitmapOnly;
    }
    if (embedding == 0x0002) {
        return LicenseStatus::Restricted;
    }
    if (embedding == 0x0004) {
        return LicenseStatus::PreviewAndPrint;
    }
    if (embedding == 0x0008) {
        return LicenseStatus::Editable;
    }
    return LicenseStatus::Installable;
}

Inspection FontInspector::Inspect(const QByteArray& fontBytes, unsigned faceIndex) const
{
    Inspection inspection;
    inspection.format = DetectContainer(fontBytes);
    inspection.faceIndex = faceIndex;
    if (inspection.format == ContainerFormat::Collection) {
        AddRisk(inspection, Risk::Collection,
                QObject::tr("Font collections are not supported."), true);
        return inspection;
    }
    if (inspection.format != ContainerFormat::SfntTrueType &&
        inspection.format != ContainerFormat::SfntCff) {
        AddRisk(inspection, Risk::UnsupportedContainer,
                QObject::tr("Only sfnt TTF and OTF fonts are supported."), true);
        return inspection;
    }

    auto blob = TakeHb<hb_blob_t, hb_blob_destroy>(
        hb_blob_create(fontBytes.constData(), fontBytes.size(),
                       HB_MEMORY_MODE_DUPLICATE, nullptr, nullptr));
    inspection.faceCount = hb_face_count(blob.get());
    if (inspection.faceCount != 1 || faceIndex >= inspection.faceCount) {
        AddRisk(inspection, Risk::InvalidFont,
                QObject::tr("The font does not contain one accessible face."), true);
        return inspection;
    }

    auto face = TakeHb<hb_face_t, hb_face_destroy>(hb_face_create(blob.get(), faceIndex));
    inspection.glyphCount = hb_face_get_glyph_count(face.get());
    inspection.tableTags = ReadTableTags(face.get());
    if (inspection.glyphCount == 0 || inspection.tableTags.isEmpty()) {
        AddRisk(inspection, Risk::InvalidFont,
                QObject::tr("The font has no readable glyphs or tables."), true);
        return inspection;
    }

    inspection.valid = true;
    inspection.fsType = ReadFsType(face.get());
    inspection.license = ClassifyLicense(inspection.fsType);
    switch (inspection.license) {
    case LicenseStatus::Installable:
    case LicenseStatus::Editable:
        break;
    case LicenseStatus::PreviewAndPrint:
        inspection.blockingReason = QObject::tr(
            "Preview-and-print embedding does not allow editing the font.");
        break;
    case LicenseStatus::Restricted:
        inspection.blockingReason = QObject::tr(
            "The font has a restricted embedding license.");
        break;
    case LicenseStatus::NoSubsetting:
        inspection.blockingReason = QObject::tr(
            "The font license explicitly forbids subsetting.");
        break;
    case LicenseStatus::BitmapOnly:
        inspection.blockingReason = QObject::tr(
            "The font license permits bitmap embedding only.");
        break;
    case LicenseStatus::InvalidOrMissing:
        inspection.blockingReason = QObject::tr(
            "The font has missing or invalid embedding permissions.");
        break;
    }

    const bool hasOutline = HasAny(inspection.tableTags,
                                   {Tag('g', 'l', 'y', 'f'),
                                    Tag('C', 'F', 'F', ' '),
                                    Tag('C', 'F', 'F', '2')});
    if (!hasOutline) {
        AddRisk(inspection, Risk::MissingOutline,
                QObject::tr("The font has no supported outline table."), true);
    }
    if (inspection.tableTags.contains(Tag('S', 'V', 'G', ' '))) {
        AddRisk(inspection, Risk::SvgTable,
                QObject::tr("Fonts with SVG glyph tables are not supported."), true);
    }
    if (HasAny(inspection.tableTags,
               {Tag('E', 'B', 'D', 'T'), Tag('E', 'B', 'L', 'C')})) {
        AddRisk(inspection, Risk::EbdtEblc,
                QObject::tr("EBDT/EBLC bitmap fonts are not supported."), true);
    }
    if (HasAny(inspection.tableTags,
               {Tag('S', 'i', 'l', 'f'), Tag('G', 'l', 'o', 'c'),
                Tag('G', 'l', 'a', 't'), Tag('F', 'e', 'a', 't'),
                Tag('S', 'i', 'l', 'l')})) {
        AddRisk(inspection, Risk::GraphiteLayout,
                QObject::tr("Graphite layout tables require conservative handling."), true);
    }
    if (HasAny(inspection.tableTags,
               {Tag('m', 'o', 'r', 'x'), Tag('k', 'e', 'r', 'x'),
                Tag('a', 'n', 'k', 'r'), Tag('t', 'r', 'a', 'k')})) {
        AddRisk(inspection, Risk::AatLayout,
                QObject::tr("AAT layout tables require conservative handling."), true);
    }
    if (HasAny(inspection.tableTags,
               {Tag('C', 'O', 'L', 'R'), Tag('C', 'B', 'D', 'T'),
                Tag('C', 'B', 'L', 'C'), Tag('s', 'b', 'i', 'x')})) {
        AddRisk(inspection, Risk::ColorBitmap,
                QObject::tr("Color or bitmap glyph tables require extra validation."));
    }
    if (HasAny(inspection.tableTags,
               {Tag('f', 'v', 'a', 'r'), Tag('g', 'v', 'a', 'r'),
                Tag('a', 'v', 'a', 'r')})) {
        AddRisk(inspection, Risk::VariableFont,
                QObject::tr("Variable font axes will be preserved."));
    }

    inspection.canSubset = inspection.blockingReason.isEmpty();
    return inspection;
}

}
