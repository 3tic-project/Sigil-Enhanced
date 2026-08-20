/************************************************************************
**
**  Copyright (C) 2026 Sigil-Ebook Contributors
**
**  This file is part of Sigil.
**
*************************************************************************/

#include "ViewEditors/PreviewMetricsProbe.h"

#include "ViewEditors/ViewPreview.h"

#include <QPointer>
#include <QVariantMap>
#include <QWebEnginePage>
#include <QWebEngineScript>

namespace
{

const QString METRICS_SCRIPT = QStringLiteral(R"JS(
(() => {
    const initial = %1;
    if (!initial) return null;
    let element = initial.nodeType === Node.ELEMENT_NODE ? initial : initial.parentElement;
    if (!element) return null;
    const relevant = 'p,div,section,article,blockquote,li,h1,h2,h3,h4,h5,h6,figure,pre,table';
    element = element.closest(relevant) || element;
    const style = getComputedStyle(element);
    const rect = element.getBoundingClientRect();
    const px = value => {
        const match = /^(-?(?:\\d+|\\d*\\.\\d+))px$/.exec(String(value).trim());
        return match ? Number(match[1]) : null;
    };
    const path = node => {
        const parts = [];
        for (let current = node; current && current.nodeType === Node.ELEMENT_NODE;
             current = current.parentElement) {
            let part = current.tagName.toLowerCase();
            if (current.id) {
                part += '#' + CSS.escape(current.id);
                parts.unshift(part);
                break;
            }
            if (current.parentElement) {
                const siblings = Array.from(current.parentElement.children)
                    .filter(sibling => sibling.tagName === current.tagName);
                if (siblings.length > 1) part += `:nth-of-type(${siblings.indexOf(current) + 1})`;
            }
            parts.unshift(part);
        }
        return parts.join('>');
    };
    const lineHeight = String(style.lineHeight).trim();
    return {
        tag: element.tagName.toLowerCase(),
        path: path(element),
        style: {
            fontSizePx: px(style.fontSize),
            lineHeightPx: px(lineHeight),
            lineHeightNormal: lineHeight === 'normal',
            marginBlockStartPx: px(style.marginBlockStart),
            marginBlockEndPx: px(style.marginBlockEnd),
            paddingBlockStartPx: px(style.paddingBlockStart),
            paddingBlockEndPx: px(style.paddingBlockEnd),
            display: style.display,
            writingMode: style.writingMode
        },
        rect: { x: rect.x, y: rect.y, width: rect.width, height: rect.height }
    };
})()
)JS");

const QString BODY_ORIGIN_SCRIPT = QStringLiteral(R"JS(
(() => {
    if (!document.body) return null;
    const style = getComputedStyle(document.body);
    const rect = document.body.getBoundingClientRect();
    const number = value => Number.parseFloat(value) || 0;
    return {
        origin: rect.top + window.scrollY + number(style.borderTopWidth) + number(style.paddingTop),
        writingMode: style.writingMode
    };
})()
)JS");

}

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
    return METRICS_SCRIPT.arg(selector);
}

void PreviewMetricsProbe::requestBodyContentOrigin()
{
    if (!m_preview || !m_preview->IsLoadingFinished()) {
        return;
    }

    const quint64 generation = ++m_originGeneration;
    const QPointer<PreviewMetricsProbe> guard(this);
    m_preview->page()->runJavaScript(BODY_ORIGIN_SCRIPT,
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
