/************************************************************************
**
**  Copyright (C) 2026 Sigil-Ebook Contributors
**
**  This file is part of Sigil.
**
*************************************************************************/

#include "ViewEditors/PreviewLayoutMetrics.h"

#include <QVariantMap>
#include <QtMath>

namespace
{

bool finiteNumber(const QVariantMap &map, const QString &key, qreal *target)
{
    bool okay = false;
    const qreal value = map.value(key).toDouble(&okay);
    if (!okay || !qIsFinite(value)) {
        return false;
    }
    *target = value;
    return true;
}

}

PreviewLayoutMetrics PreviewLayoutMetrics::fromVariant(const QVariant &value)
{
    PreviewLayoutMetrics metrics;
    const QVariantMap root = value.toMap();
    if (root.isEmpty()) {
        return metrics;
    }

    const QVariantMap style = root.value(QStringLiteral("style")).toMap();
    const QVariantMap rect = root.value(QStringLiteral("rect")).toMap();
    metrics.tagName = root.value(QStringLiteral("tag")).toString().toLower();
    metrics.domPath = root.value(QStringLiteral("path")).toString();
    metrics.display = style.value(QStringLiteral("display")).toString();
    metrics.writingMode = style.value(QStringLiteral("writingMode")).toString();
    metrics.lineHeightNormal = style.value(QStringLiteral("lineHeightNormal")).toBool();
    metrics.hasLineHeightPx = finiteNumber(style, QStringLiteral("lineHeightPx"), &metrics.lineHeightPx);

    qreal x = 0.0;
    qreal y = 0.0;
    qreal width = 0.0;
    qreal height = 0.0;
    const bool required = !metrics.tagName.isEmpty()
        && finiteNumber(style, QStringLiteral("fontSizePx"), &metrics.fontSizePx)
        && finiteNumber(style, QStringLiteral("marginBlockStartPx"), &metrics.marginBlockStartPx)
        && finiteNumber(style, QStringLiteral("marginBlockEndPx"), &metrics.marginBlockEndPx)
        && finiteNumber(style, QStringLiteral("paddingBlockStartPx"), &metrics.paddingBlockStartPx)
        && finiteNumber(style, QStringLiteral("paddingBlockEndPx"), &metrics.paddingBlockEndPx)
        && finiteNumber(rect, QStringLiteral("x"), &x)
        && finiteNumber(rect, QStringLiteral("y"), &y)
        && finiteNumber(rect, QStringLiteral("width"), &width)
        && finiteNumber(rect, QStringLiteral("height"), &height);
    if (!required || (!metrics.hasLineHeightPx && !metrics.lineHeightNormal)) {
        return PreviewLayoutMetrics();
    }

    metrics.viewportRect = QRectF(x, y, width, height);
    metrics.valid = true;
    return metrics;
}
