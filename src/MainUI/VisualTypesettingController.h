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
#include "ViewEditors/PreviewMetricsRequestTracker.h"
#include "ViewEditors/Viewer.h"

#include <QObject>
#include <QPoint>

class QAction;
class BaselineGridSettingsDialog;
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
    ~VisualTypesettingController() override;

    QMenu *menu() const;
    QAction *showGridAction() const;
    QAction *showMetricsAction() const;
    QAction *useCurrentElementAction() const;
    QAction *settingsAction() const;
    QAction *cleanPreviewAction() const;

    void setCurrentElement(const QList<ElementIndex> &hierarchy);
    void inspectElementAtPreviewPosition(const QPoint &viewportPosition);
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
    void documentLoading();
    void documentLoaded();
    void metricsReady(quint64 requestId, const PreviewLayoutMetrics &metrics);
    void metricsUnavailable(quint64 requestId);
    void bodyContentOriginReady(qreal originCssPx, const QString &writingMode);

private:
    void applySettings(const BaselineGridSettings &settings, bool persist);
    bool beginCurrentElementRequest(PreviewMetricsRequestPurpose purpose);
    bool beginPreviewPointRequest(const QPoint &viewportPosition,
                                  PreviewMetricsRequestPurpose purpose);
    void handleUnavailableRequest(const PreviewMetricsRequestToken &token);
    bool hasFreshCurrentMetrics() const;
    bool tryApplyReferenceFont(const PreviewLayoutMetrics &metrics);
    bool usesDarkPreviewTheme() const;
    void persistSettings();
    void updateActions();
    void updateOverlayOrigin();
    void updateMetricsText();
    QString gridSummary() const;
    QString metricsSummary(const PreviewLayoutMetrics &metrics) const;
    static QString hierarchyKey(const QList<ElementIndex> &hierarchy);

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
    PreviewMetricsRequestTracker m_requests;
    BaselineGridSettings m_settings;
    QList<ElementIndex> m_currentHierarchy;
    QString m_currentHierarchyKey;
    PreviewLayoutMetrics m_lastMetrics;
    QString m_lastMetricsKey;
    BaselineGridSettingsDialog *m_activeSettingsDialog = nullptr;
    qreal m_bodyOriginCssPx = 0.0;
    QString m_documentWritingMode;
    bool m_cleanPreviewActive = false;
    bool m_transientInspectionActive = false;
};

#endif // VISUALTYPESETTINGCONTROLLER_H
