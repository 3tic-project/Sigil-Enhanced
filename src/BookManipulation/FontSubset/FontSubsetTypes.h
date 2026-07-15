#pragma once

#include <optional>

#include <QByteArray>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

namespace FontSubset
{

enum class ContainerFormat {
    SfntTrueType,
    SfntCff,
    Collection,
    Woff,
    Woff2,
    Unknown
};

enum class LicenseStatus {
    Installable,
    Editable,
    PreviewAndPrint,
    Restricted,
    NoSubsetting,
    BitmapOnly,
    InvalidOrMissing
};

enum class Risk {
    Collection,
    UnsupportedContainer,
    MissingOutline,
    SvgTable,
    EbdtEblc,
    GraphiteLayout,
    AatLayout,
    ColorBitmap,
    VariableFont,
    InvalidFont
};

struct Options {
    bool dropHinting = false;
    bool keepNotdefOutline = true;
    bool validateShaping = true;
    QStringList shapingSamples;
};

struct Inspection {
    bool valid = false;
    bool canSubset = false;
    ContainerFormat format = ContainerFormat::Unknown;
    LicenseStatus license = LicenseStatus::InvalidOrMissing;
    std::optional<quint16> fsType;
    unsigned faceCount = 0;
    unsigned faceIndex = 0;
    unsigned glyphCount = 0;
    QSet<quint32> tableTags;
    QList<Risk> risks;
    QStringList warnings;
    QString blockingReason;
};

struct Result {
    bool success = false;
    QString error;
    Inspection inspection;
    QByteArray outputBytes;
    qsizetype oldSize = 0;
    qsizetype newSize = 0;
    unsigned oldGlyphCount = 0;
    unsigned newGlyphCount = 0;
    unsigned mappedGlyphCount = 0;
    QSet<quint32> inputCodepoints;
    QSet<quint32> requestedCodepoints;
    QSet<quint32> unavailableCodepoints;
    QSet<quint32> missingCodepoints;
    QStringList warnings;
    QString harfbuzzVersion;
};

}
