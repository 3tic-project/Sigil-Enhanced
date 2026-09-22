/************************************************************************
**
**  This file is part of Sigil.
**
*************************************************************************/

#pragma once
#ifndef FONTPREVIEWLANGUAGE_H
#define FONTPREVIEWLANGUAGE_H

#include <QFontDatabase>
#include <QString>
#include <QStringList>

enum class FontPreviewProfile {
    SimplifiedChinese,
    TraditionalChinese,
    ChineseMixed,
    Japanese,
    English,
    Generic
};

enum class FontPreviewChoice {
    Auto,
    SimplifiedChinese,
    TraditionalChinese,
    ChineseMixed,
    Japanese,
    English
};

FontPreviewProfile FontPreviewProfileForChoice(FontPreviewChoice choice);

FontPreviewProfile ResolveFontPreviewProfile(
    const QStringList &bookLanguages,
    FontPreviewChoice choice,
    const QList<QFontDatabase::WritingSystem> &fontSystems);

QString FontPreviewProfileId(FontPreviewProfile profile);

#endif
