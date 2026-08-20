/************************************************************************
**
**  Copyright (C) 2026 Sigil-Ebook Contributors
**
**  This file is part of Sigil.
**
*************************************************************************/

#include "ViewEditors/PreviewMetricsProbe.h"

#include "ViewEditors/PreviewMetricsJavascript.h"
#include "ViewEditors/ViewPreview.h"

#include <QPointer>
#include <QVariantMap>
#include <QWebEnginePage>
#include <QWebEngineScript>

PreviewMetricsProbe::PreviewMetricsProbe(ViewPreview *preview, QObject *parent)
    : QObject(parent),
      m_preview(preview)
{
}

void PreviewMetricsProbe::invalidatePendingRequests()
{
    ++m_metricsGeneration;
    ++m_originGeneration;
}

void PreviewMetricsProbe::requestMetrics(const QList<ElementIndex> &hierarchy)
{
    if (!m_preview || hierarchy.isEmpty()) {
        emit metricsUnavailable();
        return;
    }
    requestMetricsForSelector(m_preview->ElementSelectingJavascript(hierarchy));
}

void PreviewMetricsProbe::requestMetricsAtViewportPoint(const QPointF &cssPoint)
{
    const QString selector = QStringLiteral("document.elementFromPoint(%1, %2)")
        .arg(cssPoint.x(), 0, 'f', 3)
        .arg(cssPoint.y(), 0, 'f', 3);
    requestMetricsForSelector(selector);
}

void PreviewMetricsProbe::requestMetricsForSelector(const QString &selector)
{
    if (!m_preview || !m_preview->IsLoadingFinished()) {
        emit metricsUnavailable();
        return;
    }

    const quint64 generation = ++m_metricsGeneration;
    const QPointer<PreviewMetricsProbe> guard(this);
    m_preview->page()->runJavaScript(metricsJavascript(selector),
        QWebEngineScript::ApplicationWorld,
        [guard, generation](const QVariant &value) {
            if (!guard || generation != guard->m_metricsGeneration) {
                return;
            }
            const PreviewLayoutMetrics metrics = PreviewLayoutMetrics::fromVariant(value);
            if (metrics.valid) {
                emit guard->metricsReady(metrics);
            } else {
                emit guard->metricsUnavailable();
            }
        });
}

QString PreviewMetricsProbe::metricsJavascript(const QString &selector) const
{
    return PreviewMetricsJavascript::metricsForSelector(selector);
}

void PreviewMetricsProbe::requestBodyContentOrigin()
{
    if (!m_preview || !m_preview->IsLoadingFinished()) {
        return;
    }

    const quint64 generation = ++m_originGeneration;
    const QPointer<PreviewMetricsProbe> guard(this);
    m_preview->page()->runJavaScript(PreviewMetricsJavascript::bodyContentOrigin(),
        QWebEngineScript::ApplicationWorld,
        [guard, generation](const QVariant &value) {
            if (!guard || generation != guard->m_originGeneration) {
                return;
            }
            const QVariantMap result = value.toMap();
            bool okay = false;
            const qreal origin = result.value(QStringLiteral("origin")).toDouble(&okay);
            if (okay) {
                emit guard->bodyContentOriginReady(
                    origin, result.value(QStringLiteral("writingMode")).toString());
            }
        });
}
