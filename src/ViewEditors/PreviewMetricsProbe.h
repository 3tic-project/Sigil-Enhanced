/************************************************************************
**
**  Copyright (C) 2026 Sigil-Ebook Contributors
**
**  This file is part of Sigil.
**
*************************************************************************/

#pragma once
#ifndef PREVIEWMETRICSPROBE_H
#define PREVIEWMETRICSPROBE_H

#include "ViewEditors/PreviewLayoutMetrics.h"
#include "ViewEditors/Viewer.h"

#include <QObject>
#include <QPointF>
#include <QPointer>

class ViewPreview;

class PreviewMetricsProbe : public QObject
{
    Q_OBJECT

public:
    explicit PreviewMetricsProbe(ViewPreview *preview, QObject *parent = nullptr);

    void requestMetrics(const QList<ElementIndex> &hierarchy);
    void requestMetricsAtViewportPoint(const QPointF &cssPoint);
    void requestBodyContentOrigin();
    void invalidatePendingRequests();

signals:
    void metricsReady(const PreviewLayoutMetrics &metrics);
    void metricsUnavailable();
    void bodyContentOriginReady(qreal originCssPx, const QString &writingMode);

private:
    void requestMetricsForSelector(const QString &selector);
    QString metricsJavascript(const QString &selector) const;

    QPointer<ViewPreview> m_preview;
    quint64 m_metricsGeneration = 0;
    quint64 m_originGeneration = 0;
};

#endif // PREVIEWMETRICSPROBE_H
