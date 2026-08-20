/************************************************************************
**
**  Copyright (C) 2026 Sigil-Ebook Contributors
**
**  This file is part of Sigil.
**
*************************************************************************/

#pragma once
#ifndef VISUALTYPESETTINGCONTROLLER_H
#define VISUALTYPESETTINGCONTROLLER_H

#include "ViewEditors/BaselineGridModel.h"
#include "ViewEditors/PreviewLayoutMetrics.h"
#include "ViewEditors/Viewer.h"

#include <QObject>
#include <QPoint>

class QAction;
class BaselineGridOverlay;
class OverlayHelperWidget;
class PreviewMetricsProbe;
class QMenu;
class QTimer;
class ViewPreview;
class QWidget;

class VisualTypesettingController : public QObject
{
    Q_OBJECT

public:
    VisualTypesettingController(ViewPreview *preview,
                                OverlayHelperWidget *overlayParent,
                                QWidget *dialogParent,
                                QObject *parent = nullptr);

    QMenu *menu() const;
    QAction *showGridAction() const;
    QAction *showMetricsAction() const;
    QAction *useCurrentElementAction() const;
    QAction *settingsAction() const;
    QAction *cleanPreviewAction() const;

    void setCurrentElement(const QList<ElementIndex> &hierarchy);
    void useElementAtPreviewPosition(const QPoint &viewportPosition);
    void refreshThemeDefaults();

signals:
    void metricsTextChanged(const QString &text);
    void notificationRequested(const QString &message);

private slots:
    void setGridEnabled(bool enabled);
    void setMetricsEnabled(bool enabled);
    void useCurrentElementAsReference();
    void showSettings();
    void setCleanPreviewActive(bool active);
    void requestCurrentMetrics();
    void documentLoaded();
    void metricsReady(const PreviewLayoutMetrics &metrics);
    void metricsUnavailable();
    void bodyContentOriginReady(qreal originCssPx, const QString &writingMode);

private:
    void applySettings(const BaselineGridSettings &settings, bool persist);
    void persistSettings();
    void updateActions();
    void updateOverlayOrigin();
    void updateMetricsText();
    QString gridSummary() const;
    QString metricsSummary(const PreviewLayoutMetrics &metrics) const;

    ViewPreview *m_preview;
    QWidget *m_dialogParent;
    BaselineGridOverlay *m_overlay;
    PreviewMetricsProbe *m_probe;
    QTimer *m_metricsTimer;
    QMenu *m_menu;
    QAction *m_showGridAction;
    QAction *m_showMetricsAction;
    QAction *m_useCurrentElementAction;
    QAction *m_settingsAction;
    QAction *m_cleanPreviewAction;
    BaselineGridSettings m_settings;
    QList<ElementIndex> m_currentHierarchy;
    PreviewLayoutMetrics m_lastMetrics;
    qreal m_bodyOriginCssPx = 0.0;
    QString m_documentWritingMode;
    bool m_cleanPreviewActive = false;
    bool m_calibrationPending = false;
};

#endif // VISUALTYPESETTINGCONTROLLER_H
