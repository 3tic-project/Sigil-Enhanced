/************************************************************************
**
**  Copyright (C) 2026 Sigil-Ebook Contributors
**
**  This file is part of Sigil.
**
*************************************************************************/

#pragma once
#ifndef PREVIEWLAYOUTMETRICS_H
#define PREVIEWLAYOUTMETRICS_H

#include <QMetaType>
#include <QRectF>
#include <QString>
#include <QVariant>

struct PreviewLayoutMetrics
{
    bool valid = false;
    QString tagName;
    QString domPath;
    qreal fontSizePx = 0.0;
    qreal lineHeightPx = 0.0;
    bool hasLineHeightPx = false;
    bool lineHeightNormal = false;
    qreal marginBlockStartPx = 0.0;
    qreal marginBlockEndPx = 0.0;
    qreal paddingBlockStartPx = 0.0;
    qreal paddingBlockEndPx = 0.0;
    QString display;
    QString writingMode;
    QRectF viewportRect;

    static PreviewLayoutMetrics fromVariant(const QVariant &value);
};

Q_DECLARE_METATYPE(PreviewLayoutMetrics)

#endif // PREVIEWLAYOUTMETRICS_H
