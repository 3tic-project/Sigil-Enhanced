/************************************************************************
**
**  Copyright (C) 2026 Sigil-Ebook Contributors
**
**  This file is part of Sigil.
**
**  Sigil is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  (at your option) any later version.
**
*************************************************************************/

#include "ViewEditors/BaselineGridModel.h"

#include <QtMath>

namespace
{

constexpr int MAX_VISIBLE_GRID_LINES = 20000;

int positiveModulo(qint64 value, int divisor)
{
    const int remainder = static_cast<int>(value % divisor);
    return remainder < 0 ? remainder + divisor : remainder;
}

}

qreal BaselineGridSettings::resolvedStepCssPx() const
{
    return unit == BaselineGridUnit::Em ? step * referenceFontPx : step;
}

qreal BaselineGridSettings::resolvedVerticalStepCssPx() const
{
    return unit == BaselineGridUnit::Em ? verticalStep * referenceFontPx : verticalStep;
}

bool BaselineGridSettings::isValid() const
{
    const qreal resolved = resolvedStepCssPx();
    const qreal verticalResolved = resolvedVerticalStepCssPx();
    return qIsFinite(step)
        && qIsFinite(verticalStep)
        && qIsFinite(referenceFontPx)
        && qIsFinite(offsetCssPx)
        && qIsFinite(resolved)
        && qIsFinite(verticalResolved)
        && step > 0.0
        && verticalStep > 0.0
        && referenceFontPx >= 0.25
        && referenceFontPx <= 1000.0
        && resolved >= 0.25
        && resolved <= 1000.0
        && verticalResolved >= 0.25
        && verticalResolved <= 1000.0
        && majorEvery >= 1
        && minorOpacity >= 0.0
        && minorOpacity <= 1.0
        && majorOpacity >= 0.0
        && majorOpacity <= 1.0
        && minimumZoomPercent >= 10
        && minimumZoomPercent <= 400
        && minorColor.isValid()
        && majorColor.isValid();
}

BaselineGridSettings BaselineGridSettings::defaults(bool darkTheme)
{
    BaselineGridSettings settings;
    if (darkTheme) {
        settings.minorColor = QColor(QStringLiteral("#74c0fc"));
        settings.majorColor = QColor(QStringLiteral("#a5d8ff"));
    }
    return settings;
}

QVector<BaselineGridLine> BaselineGridModel::linesForViewport(
    qreal viewportExtent,
    qreal scrollCssPx,
    qreal zoomFactor,
    qreal originCssPx,
    const BaselineGridSettings &settings,
    BaselineGridAxis axis)
{
    QVector<BaselineGridLine> lines;
    const bool axisEnabled = axis == BaselineGridAxis::Horizontal
        ? settings.horizontalEnabled : settings.verticalEnabled;
    if (!settings.enabled || !axisEnabled || !settings.isValid() || viewportExtent <= 0.0
            || !qIsFinite(viewportExtent) || zoomFactor <= 0.0
            || !qIsFinite(zoomFactor) || !qIsFinite(scrollCssPx)
            || !qIsFinite(originCssPx)) {
        return lines;
    }

    const qreal resolvedStep = axis == BaselineGridAxis::Horizontal
        ? settings.resolvedStepCssPx() : settings.resolvedVerticalStepCssPx();
    const qreal stepPaint = resolvedStep * zoomFactor;
    if (stepPaint <= 0.0 || !qIsFinite(stepPaint)) {
        return lines;
    }

    const bool showMinor = zoomFactor * 100.0 >= settings.minimumZoomPercent;
    const qreal visibleStep = showMinor ? stepPaint : stepPaint * settings.majorEvery;
    // Avoid turning sub-pixel grid density into a high-frequency texture.
    if (visibleStep < 1.0) {
        return lines;
    }

    const qreal axisOffset = axis == BaselineGridAxis::Horizontal ? settings.offsetCssPx : 0.0;
    const qreal basePaint = (originCssPx + axisOffset - scrollCssPx) * zoomFactor;
    qint64 firstIndex = static_cast<qint64>(qCeil(-basePaint / stepPaint));
    if (!showMinor) {
        const int remainder = positiveModulo(firstIndex, settings.majorEvery);
        if (remainder != 0) {
            firstIndex += settings.majorEvery - remainder;
        }
    }

    const qint64 increment = showMinor ? 1 : settings.majorEvery;
    const qreal epsilon = 0.001;
    for (qint64 index = firstIndex; lines.size() < MAX_VISIBLE_GRID_LINES; index += increment) {
        const qreal position = basePaint + static_cast<qreal>(index) * stepPaint;
        if (position > viewportExtent + epsilon) {
            break;
        }
        if (position >= -epsilon) {
            BaselineGridLine line;
            line.position = qBound<qreal>(0.0, position, viewportExtent);
            line.major = positiveModulo(index, settings.majorEvery) == 0;
            lines.append(line);
        }
    }
    return lines;
}

QString BaselineGridUnitName(BaselineGridUnit unit)
{
    return unit == BaselineGridUnit::Em ? QStringLiteral("em") : QStringLiteral("px");
}

QString BaselineGridOriginName(BaselineGridOrigin origin)
{
    return origin == BaselineGridOrigin::BodyContentTop
        ? QStringLiteral("body-content-top")
        : QStringLiteral("document-top");
}
