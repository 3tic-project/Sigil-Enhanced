/************************************************************************
**
**  Copyright (C) 2026 Sigil-Ebook Contributors
**
**  This file is part of Sigil.
**
*************************************************************************/

#pragma once
#ifndef BASELINEGRIDOVERLAY_H
#define BASELINEGRIDOVERLAY_H

#include "ViewEditors/BaselineGridModel.h"
#include "ViewEditors/Overlay.h"

class BaselineGridOverlay : public OverlayWidget
{
    Q_OBJECT

public:
    explicit BaselineGridOverlay(QWidget *parent = nullptr);

    void setGridSettings(const BaselineGridSettings &settings);
    void setScrollPositionCssPx(qreal scrollCssPx);
    void setZoomFactor(qreal zoomFactor);
    void setOriginPositionCssPx(qreal originCssPx);
    void setCleanPreviewActive(bool active);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void updateVisibility();

    BaselineGridSettings m_settings;
    qreal m_scrollCssPx = 0.0;
    qreal m_zoomFactor = 1.0;
    qreal m_originCssPx = 0.0;
    bool m_cleanPreviewActive = false;
};

#endif // BASELINEGRIDOVERLAY_H
