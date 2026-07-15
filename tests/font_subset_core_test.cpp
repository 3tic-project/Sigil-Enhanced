#include <cstdlib>
#include <iostream>

#include <QFile>

#include "BookManipulation/FontSubset/FontInspector.h"
#include "BookManipulation/FontSubset/HarfBuzzSubsetEngine.h"

namespace
{

using namespace FontSubset;

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

QByteArray Fixture()
{
    QFile file(QStringLiteral(SIGIL_FONT_TEST_FIXTURE));
    Require(file.open(QIODevice::ReadOnly), "could not open font fixture");
    return file.readAll();
}

quint16 ReadU16(const QByteArray& bytes, int offset)
{
    const auto* data = reinterpret_cast<const unsigned char*>(bytes.constData());
    return quint16((quint16(data[offset]) << 8) | data[offset + 1]);
}

quint32 ReadU32(const QByteArray& bytes, int offset)
{
    const auto* data = reinterpret_cast<const unsigned char*>(bytes.constData());
    return (quint32(data[offset]) << 24) | (quint32(data[offset + 1]) << 16) |
           (quint32(data[offset + 2]) << 8) | quint32(data[offset + 3]);
}

QByteArray WithFsType(const QByteArray& source, quint16 fsType)
{
    QByteArray bytes = source;
    const int tableCount = ReadU16(bytes, 4);
    for (int i = 0; i < tableCount; ++i) {
        const int record = 12 + i * 16;
        if (ReadU32(bytes, record) != 0x4f532f32u) { // OS/2
            continue;
        }
        const int tableOffset = int(ReadU32(bytes, record + 8));
        Require(tableOffset >= 0 && tableOffset + 10 <= bytes.size(),
                "invalid OS/2 table in fixture");
        bytes[tableOffset + 8] = char((fsType >> 8) & 0xff);
        bytes[tableOffset + 9] = char(fsType & 0xff);
        return bytes;
    }
    Require(false, "fixture has no OS/2 table");
    return {};
}

QByteArray WithTableTag(const QByteArray& source, quint32 oldTag, quint32 newTag)
{
    QByteArray bytes = source;
    const int tableCount = ReadU16(bytes, 4);
    for (int i = 0; i < tableCount; ++i) {
        const int record = 12 + i * 16;
        if (ReadU32(bytes, record) != oldTag) {
            continue;
        }
        bytes[record] = char((newTag >> 24) & 0xff);
        bytes[record + 1] = char((newTag >> 16) & 0xff);
        bytes[record + 2] = char((newTag >> 8) & 0xff);
        bytes[record + 3] = char(newTag & 0xff);
        return bytes;
    }
    Require(false, "fixture does not contain the requested table");
    return {};
}

void TestInspection()
{
    FontInspector inspector;
    const Inspection inspection = inspector.Inspect(Fixture());
    Require(inspection.valid, "fixture was not recognized as a font");
    Require(inspection.canSubset, "fixture was unexpectedly blocked");
    Require(inspection.format == ContainerFormat::SfntTrueType,
            "fixture format was not TrueType sfnt");
    Require(inspection.faceCount == 1 && inspection.glyphCount > 50,
            "fixture face metadata is incorrect");
    Require(inspection.license == LicenseStatus::Editable,
            "fixture license was not editable");

    Require(FontInspector::DetectContainer(QByteArray("wOFFxxxx", 8)) ==
                ContainerFormat::Woff,
            "WOFF signature was not detected");
    Require(FontInspector::DetectContainer(QByteArray("wOF2xxxx", 8)) ==
                ContainerFormat::Woff2,
            "WOFF2 signature was not detected");
    Require(FontInspector::DetectContainer(QByteArray("ttcfxxxx", 8)) ==
                ContainerFormat::Collection,
            "collection signature was not detected");
    Require(!inspector.Inspect(QByteArray("wOFFxxxx", 8)).canSubset,
            "unsupported container was accepted");

    const Inspection svg = inspector.Inspect(
        WithTableTag(Fixture(), 0x67617370u, 0x53564720u)); // gasp -> SVG
    Require(!svg.canSubset && svg.risks.contains(Risk::SvgTable),
            "unsupported SVG glyph table was accepted");
}

void TestLicensePolicy()
{
    FontInspector inspector;
    Require(inspector.Inspect(WithFsType(Fixture(), 0x0008)).canSubset,
            "editable embedding was blocked");
    Require(inspector.Inspect(WithFsType(Fixture(), 0x0002)).license ==
                LicenseStatus::Restricted,
            "restricted embedding was not detected");
    Require(!inspector.Inspect(WithFsType(Fixture(), 0x0004)).canSubset,
            "preview-and-print embedding was accepted");
    Require(inspector.Inspect(WithFsType(Fixture(), 0x0100)).license ==
                LicenseStatus::NoSubsetting,
            "no-subsetting bit was not detected");
    Require(inspector.Inspect(WithFsType(Fixture(), 0x0200)).license ==
                LicenseStatus::BitmapOnly,
            "bitmap-only bit was not detected");
    Require(inspector.Inspect(WithFsType(Fixture(), 0x0006)).license ==
                LicenseStatus::InvalidOrMissing,
            "conflicting embedding levels were accepted");
}

void TestSubsetAndValidation()
{
    HarfBuzzSubsetEngine engine;
    Options options;
    options.shapingSamples = {QStringLiteral("Abba BAB")};
    const QSet<quint32> requested = {0x0020, 0x0041, 0x0042, 0x0061, 0x0062};
    const Result result = engine.Subset(Fixture(), requested, options);
    Require(result.success, result.error.toUtf8().constData());
    Require(result.outputBytes.size() > 0 && result.newSize < result.oldSize,
            "subset did not reduce the font size");
    Require(result.newGlyphCount < result.oldGlyphCount,
            "subset did not reduce glyph count");
    Require(result.mappedGlyphCount >= requested.size() + 1,
            "subset mapping omitted requested glyphs");
    Require(result.missingCodepoints.isEmpty(),
            "subset output lost a requested codepoint");
    Require(result.harfbuzzVersion == QString::fromLatin1(hb_version_string()),
            "result did not record the HarfBuzz version");

    const Result mixed = engine.Subset(Fixture(), {0x0041, 0x4e2d}, options);
    Require(mixed.success && mixed.requestedCodepoints.contains(0x0041) &&
                mixed.unavailableCodepoints.contains(0x4e2d),
            "global usage was not intersected with source coverage");
    const Result missing = engine.Subset(Fixture(), {0x4e2d}, options);
    Require(!missing.success && missing.unavailableCodepoints.contains(0x4e2d),
            "empty source coverage intersection was not rejected");
    const Result blocked = engine.Subset(WithFsType(Fixture(), 0x0100),
                                         requested, options);
    Require(!blocked.success &&
                blocked.inspection.license == LicenseStatus::NoSubsetting,
            "license-blocked font was subsetted");
}

}

int main()
{
    TestInspection();
    TestLicensePolicy();
    TestSubsetAndValidation();
    return EXIT_SUCCESS;
}
