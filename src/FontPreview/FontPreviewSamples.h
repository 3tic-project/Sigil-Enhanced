/************************************************************************
**
**  This file is part of Sigil.
**
*************************************************************************/

#pragma once
#ifndef FONTPREVIEWSAMPLES_H
#define FONTPREVIEWSAMPLES_H

#include <QString>
#include <QStringList>

#include "FontPreview/FontPreviewLanguage.h"

struct FontPreviewSample {
    FontPreviewProfile profile;
    QString headline;
    QStringList passages;
    QStringList attributions;
    QString characterLine;
    QString punctuationLine;
};

const FontPreviewSample &FontPreviewSampleFor(FontPreviewProfile profile);

QString FontPreviewSpecimenText(const FontPreviewSample &sample);

#endif
