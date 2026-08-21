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
    ++m_pageGeneration;
    ++m_originGeneration;
}

quint64 PreviewMetricsProbe::requestMetrics(const QList<ElementIndex> &hierarchy)
{
    if (!m_preview || hierarchy.isEmpty()) {
        return 0;
    }
    return requestMetricsForSelector(m_preview->ElementSelectingJavascript(hierarchy));
}

quint64 PreviewMetricsProbe::requestMetricsAtViewportPoint(const QPointF &cssPoint)
{
    const QString selector = QStringLiteral("document.elementFromPoint(%1, %2)")
        .arg(cssPoint.x(), 0, 'f', 3)
        .arg(cssPoint.y(), 0, 'f', 3);
    return requestMetricsForSelector(selector);
}

quint64 PreviewMetricsProbe::requestMetricsForSelector(const QString &selector)
{
    if (!m_preview || !m_preview->IsLoadingFinished()) {
        return 0;
    }

    if (++m_nextRequestId == 0) {
        ++m_nextRequestId;
    }
    const quint64 requestId = m_nextRequestId;
    const quint64 pageGeneration = m_pageGeneration;
    const QPointer<PreviewMetricsProbe> guard(this);
    m_preview->page()->runJavaScript(metricsJavascript(selector),
        QWebEngineScript::ApplicationWorld,
        [guard, requestId, pageGeneration](const QVariant &value) {
            if (!guard || pageGeneration != guard->m_pageGeneration) {
                return;
            }
            const PreviewLayoutMetrics metrics = PreviewLayoutMetrics::fromVariant(value);
            if (metrics.valid) {
                emit guard->metricsReady(requestId, metrics);
            } else {
                emit guard->metricsUnavailable(requestId);
            }
        });
    return requestId;
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
