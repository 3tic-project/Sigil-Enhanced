/************************************************************************
**
**  Copyright (C) 2026 Sigil-Ebook Contributors
**
**  This file is part of Sigil.
**
*************************************************************************/

#include "ViewEditors/BaselineGridOverlay.h"

#include <QPainter>
#include <QPaintEvent>

BaselineGridOverlay::BaselineGridOverlay(QWidget *parent)
    : OverlayWidget(parent)
{
    setObjectName(QStringLiteral("baselineGridOverlay"));
    setFocusPolicy(Qt::NoFocus);
    updateVisibility();
}

void BaselineGridOverlay::setGridSettings(const BaselineGridSettings &settings)
{
    m_settings = settings;
    updateVisibility();
    update();
}

void BaselineGridOverlay::setScrollPositionCssPx(qreal scrollCssPx)
{
    if (qFuzzyCompare(m_scrollCssPx, scrollCssPx)) {
        return;
    }
    m_scrollCssPx = scrollCssPx;
    update();
}

void BaselineGridOverlay::setZoomFactor(qreal zoomFactor)
{
    if (qFuzzyCompare(m_zoomFactor, zoomFactor)) {
        return;
    }
    m_zoomFactor = zoomFactor;
    update();
}

void BaselineGridOverlay::setOriginPositionCssPx(qreal originCssPx)
{
    if (qFuzzyCompare(m_originCssPx, originCssPx)) {
        return;
    }
    m_originCssPx = originCssPx;
    update();
}

void BaselineGridOverlay::setCleanPreviewActive(bool active)
{
    if (m_cleanPreviewActive == active) {
        return;
    }
    m_cleanPreviewActive = active;
    updateVisibility();
    update();
}

void BaselineGridOverlay::updateVisibility()
{
    setVisible(m_settings.enabled && !m_cleanPreviewActive);
    if (isVisible()) {
        raise();
    }
}

void BaselineGridOverlay::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    if (!m_settings.enabled || m_cleanPreviewActive) {
        return;
    }

    const QVector<BaselineGridLine> lines = BaselineGridModel::linesForViewport(
        height(), m_scrollCssPx, m_zoomFactor, m_originCssPx, m_settings);
    if (lines.isEmpty()) {
        return;
    }

    QColor minor = m_settings.minorColor;
    minor.setAlphaF(m_settings.minorOpacity);
    QColor major = m_settings.majorColor;
    major.setAlphaF(m_settings.majorOpacity);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    for (const BaselineGridLine &line : lines) {
        QPen pen(line.major ? major : minor);
        pen.setWidthF(1.0);
        pen.setCosmetic(true);
        painter.setPen(pen);
        painter.drawLine(QPointF(0.0, line.position), QPointF(width(), line.position));
    }
}
