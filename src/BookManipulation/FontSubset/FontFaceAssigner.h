#pragma once

#include "BookManipulation/FontSubset/FontSubsetController.h"

namespace FontSubset
{

struct FontTextAssignment {
    QSet<quint32> codepoints;
    QSet<quint32> uncoveredCodepoints;
    QStringList shapingSamples;
};

struct FontFaceUsage {
    QHash<QString, FontTextAssignment> fonts;
    QSet<quint32> codepoints;
    QStringList shapingSamples;
    QStringList warnings;
};

// Assigns characters the way SigilFontSubset does: each embedded face keeps the
// text whose computed font-family stack resolves to that face, not the whole book.
class FontFaceAssigner
{
public:
    FontFaceUsage Assign(const QList<UsageSource>& sources,
                         const QList<FontSnapshot>& fonts) const;
};

}
