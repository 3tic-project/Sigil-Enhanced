/************************************************************************
**
**  This file is part of Sigil.
**
*************************************************************************/

#pragma once
#ifndef FONTPREVIEWHTML_H
#define FONTPREVIEWHTML_H

#include <QList>
#include <QString>

#include "FontPreview/FontGlyphCoverage.h"
#include "FontPreview/FontPreviewSamples.h"

struct FontPreviewDocument {
    QString fontUrl;
    QString fontWeight;
    QString fontStyle;
    QString description;
    QString fileName;
    qint64 fileBytes = 0;
    FontPreviewSample sample;
    GlyphCoverageResult coverage;
    QString missingTooltip;
    bool omitFullyMissingLines = false;
};

QString MarkMissingGlyphs(const QString &text, const QList<uint> &missing, const QString &tooltip);

QString BuildFontPreviewHtml(const FontPreviewDocument &document);

#endif
