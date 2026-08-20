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

#pragma once
#ifndef BASELINEGRIDMODEL_H
#define BASELINEGRIDMODEL_H

#include <QColor>
#include <QString>
#include <QVector>

enum class BaselineGridUnit {
    Pixels,
    Em
};

enum class BaselineGridOrigin {
    DocumentTop,
    BodyContentTop
};

struct BaselineGridSettings
{
    bool enabled = false;
    bool metricsEnabled = false;
    BaselineGridUnit unit = BaselineGridUnit::Pixels;
    qreal step = 8.0;
    qreal referenceFontPx = 16.0;
    BaselineGridOrigin origin = BaselineGridOrigin::DocumentTop;
    qreal offsetCssPx = 0.0;
    int majorEvery = 5;
    QColor minorColor = QColor(QStringLiteral("#4263eb"));
    qreal minorOpacity = 0.18;
    QColor majorColor = QColor(QStringLiteral("#364fc7"));
    qreal majorOpacity = 0.34;
    bool colorsCustomized = false;
    int minimumZoomPercent = 60;

    qreal resolvedStepCssPx() const;
    bool isValid() const;

    static BaselineGridSettings defaults(bool darkTheme);
};

struct BaselineGridLine
{
    qreal position = 0.0;
    bool major = false;
};

class BaselineGridModel
{
public:
    static QVector<BaselineGridLine> linesForViewport(
        qreal viewportExtent,
        qreal scrollCssPx,
        qreal zoomFactor,
        qreal originCssPx,
        const BaselineGridSettings &settings);
};

QString BaselineGridUnitName(BaselineGridUnit unit);
QString BaselineGridOriginName(BaselineGridOrigin origin);

#endif // BASELINEGRIDMODEL_H
