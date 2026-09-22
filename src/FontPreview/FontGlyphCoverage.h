/************************************************************************
**
**  This file is part of Sigil.
**
*************************************************************************/

#pragma once
#ifndef FONTGLYPHCOVERAGE_H
#define FONTGLYPHCOVERAGE_H

#include <QList>
#include <QRawFont>
#include <QString>

struct GlyphCoverageResult {
    int supported = 0;
    int total = 0;
    QList<uint> missing;
};

GlyphCoverageResult AnalyzeGlyphCoverage(const QRawFont &font, const QString &text);

// A cmap hit whose glyph has no outline still "supports" the character.
// Those blanks are what make a title font look like scattered text.
bool GlyphHasInk(const QRawFont &font, uint codepoint);

// Prefer the Unicode name table over a broken Mac Roman family name.
QString PreferredFontFamilyName(const QRawFont &font);

#endif
