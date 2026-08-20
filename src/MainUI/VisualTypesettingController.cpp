/************************************************************************
**
**  Copyright (C) 2026 Sigil-Ebook Contributors
**
**  This file is part of Sigil.
**
*************************************************************************/

#include "MainUI/VisualTypesettingController.h"

#include "Dialogs/BaselineGridSettingsDialog.h"
#include "Misc/SettingsStore.h"
#include "Misc/Utility.h"
#include "ViewEditors/BaselineGridOverlay.h"
#include "ViewEditors/BaselineGridSettingsStore.h"
#include "ViewEditors/Overlay.h"
#include "ViewEditors/PreviewMetricsProbe.h"
#include "ViewEditors/ViewPreview.h"

#include <QAction>
#include <QDialog>
#include <QLocale>
#include <QMenu>
#include <QSignalBlocker>
#include <QTimer>
#include <QtMath>

namespace
{

QString number(qreal value, int decimals = 2)
{
    QString formatted = QLocale().toString(value, 'f', decimals);
    const QString decimalPoint = QLocale().decimalPoint();
    while (formatted.contains(decimalPoint) && formatted.endsWith(QChar('0'))) {
        formatted.chop(1);
    }
    if (formatted.endsWith(decimalPoint)) {
        formatted.chop(decimalPoint.size());
    }
    return formatted;
}

QString pxAndRatio(qreal value, qreal gridStep)
{
    return QStringLiteral("%1px / %2×").arg(number(value), number(value / gridStep, 2));
}

}

VisualTypesettingController::VisualTypesettingController(
    ViewPreview *preview,
    OverlayHelperWidget *overlayParent,
    QWidget *dialogParent,
    QObject *parent)
    : QObject(parent),
      m_preview(preview),
      m_dialogParent(dialogParent),
      m_overlay(new BaselineGridOverlay(overlayParent)),
      m_probe(new PreviewMetricsProbe(preview, this)),
      m_metricsTimer(new QTimer(this)),
      m_menu(new QMenu(tr("Visual Typesetting Aids"), dialogParent)),
      m_showGridAction(new QAction(tr("Show Baseline Grid"), this)),
      m_showMetricsAction(new QAction(tr("Show Layout Metrics"), this)),
      m_useCurrentElementAction(new QAction(tr("Use Current Element as Grid Reference"), this)),
      m_settingsAction(new QAction(tr("Baseline Grid Settings…"), this)),
      m_cleanPreviewAction(new QAction(tr("Clean Preview"), this))
{
    SettingsStore preferences;
    m_settings = BaselineGridSettingsStore::load(preferences, Utility::IsDarkMode());

    m_showGridAction->setObjectName(QStringLiteral("actionShowBaselineGrid"));
    m_showGridAction->setCheckable(true);
    m_showGridAction->setToolTip(tr("Show a non-exported rhythm grid anchored to the Preview document."));
    m_showMetricsAction->setObjectName(QStringLiteral("actionShowLayoutMetrics"));
    m_showMetricsAction->setCheckable(true);
    m_showMetricsAction->setToolTip(tr("Show computed typography and spacing for the current element."));
    m_useCurrentElementAction->setObjectName(QStringLiteral("actionUseCurrentElementAsGridReference"));
    m_settingsAction->setObjectName(QStringLiteral("actionBaselineGridSettings"));
    m_cleanPreviewAction->setObjectName(QStringLiteral("actionCleanPreview"));
    m_cleanPreviewAction->setCheckable(true);
    m_cleanPreviewAction->setToolTip(tr("Temporarily hide all visual typesetting aids."));

    m_menu->addAction(m_showGridAction);
    m_menu->addAction(m_showMetricsAction);
    m_menu->addSeparator();
    m_menu->addAction(m_useCurrentElementAction);
    m_menu->addAction(m_settingsAction);
    m_menu->addSeparator();
    m_menu->addAction(m_cleanPreviewAction);

    m_metricsTimer->setSingleShot(true);
    m_metricsTimer->setInterval(75);

    connect(m_showGridAction, &QAction::toggled, this, &VisualTypesettingController::setGridEnabled);
    connect(m_showMetricsAction, &QAction::toggled, this, &VisualTypesettingController::setMetricsEnabled);
    connect(m_useCurrentElementAction, &QAction::triggered,
            this, &VisualTypesettingController::useCurrentElementAsReference);
    connect(m_settingsAction, &QAction::triggered, this, &VisualTypesettingController::showSettings);
    connect(m_cleanPreviewAction, &QAction::toggled,
            this, &VisualTypesettingController::setCleanPreviewActive);
    connect(m_metricsTimer, &QTimer::timeout, this, &VisualTypesettingController::requestCurrentMetrics);
    connect(m_preview, &ViewPreview::DocumentLoaded, this, &VisualTypesettingController::documentLoaded);
    connect(m_preview, &ViewPreview::ZoomFactorChanged, this, [this](float factor) {
        m_overlay->setZoomFactor(factor);
    });
    connect(m_preview->page(), &QWebEnginePage::scrollPositionChanged, this, [this](const QPointF &position) {
        m_overlay->setScrollPositionCssPx(position.y());
    });
    connect(m_probe, &PreviewMetricsProbe::metricsReady,
            this, &VisualTypesettingController::metricsReady);
    connect(m_probe, &PreviewMetricsProbe::metricsUnavailable,
            this, &VisualTypesettingController::metricsUnavailable);
    connect(m_probe, &PreviewMetricsProbe::bodyContentOriginReady,
            this, &VisualTypesettingController::bodyContentOriginReady);

    m_overlay->setZoomFactor(m_preview->GetZoomFactor());
    applySettings(m_settings, false);
}

VisualTypesettingController::~VisualTypesettingController()
{
    delete m_overlay;
    m_overlay = nullptr;
}

QMenu *VisualTypesettingController::menu() const
{
    return m_menu;
}

QAction *VisualTypesettingController::showGridAction() const
{
    return m_showGridAction;
}

QAction *VisualTypesettingController::showMetricsAction() const
{
    return m_showMetricsAction;
}

QAction *VisualTypesettingController::useCurrentElementAction() const
{
    return m_useCurrentElementAction;
}

QAction *VisualTypesettingController::settingsAction() const
{
    return m_settingsAction;
}

QAction *VisualTypesettingController::cleanPreviewAction() const
{
    return m_cleanPreviewAction;
}

void VisualTypesettingController::setCurrentElement(const QList<ElementIndex> &hierarchy)
{
    m_currentHierarchy = hierarchy;
    m_useCurrentElementAction->setEnabled(!hierarchy.isEmpty());
    if (m_settings.metricsEnabled && !m_cleanPreviewActive) {
        m_metricsTimer->start();
    }
}

void VisualTypesettingController::refreshThemeDefaults()
{
    if (m_settings.colorsCustomized) {
        return;
    }
    const BaselineGridSettings defaults = BaselineGridSettings::defaults(Utility::IsDarkMode());
    m_settings.minorColor = defaults.minorColor;
    m_settings.majorColor = defaults.majorColor;
    m_overlay->setGridSettings(m_settings);
}

void VisualTypesettingController::setGridEnabled(bool enabled)
{
    m_settings.enabled = enabled;
    applySettings(m_settings, true);
}

void VisualTypesettingController::setMetricsEnabled(bool enabled)
{
    m_settings.metricsEnabled = enabled;
    applySettings(m_settings, true);
    if (enabled && !m_cleanPreviewActive) {
        m_metricsTimer->start();
    }
}

void VisualTypesettingController::useCurrentElementAsReference()
{
    if (m_currentHierarchy.isEmpty()) {
        emit notificationRequested(tr("No current Preview element is available for grid calibration."));
        return;
    }
    m_calibrationPending = true;
    m_probe->requestMetrics(m_currentHierarchy);
}

void VisualTypesettingController::useElementAtPreviewPosition(const QPoint &viewportPosition)
{
    if (!m_preview || m_preview->GetZoomFactor() <= 0.0) {
        return;
    }
    m_calibrationPending = true;
    const qreal zoom = m_preview->GetZoomFactor();
    m_probe->requestMetricsAtViewportPoint(QPointF(viewportPosition.x() / zoom,
                                                   viewportPosition.y() / zoom));
}

void VisualTypesettingController::showSettings()
{
    const qreal currentFont = m_lastMetrics.valid ? m_lastMetrics.fontSizePx : qQNaN();
    BaselineGridSettingsDialog dialog(m_settings, currentFont, Utility::IsDarkMode(), m_dialogParent);
    if (dialog.exec() == QDialog::Accepted) {
        applySettings(dialog.gridSettings(), true);
    }
}

void VisualTypesettingController::setCleanPreviewActive(bool active)
{
    m_cleanPreviewActive = active;
    m_overlay->setCleanPreviewActive(active);
    if (!active && m_settings.metricsEnabled) {
        m_metricsTimer->start();
    }
    updateMetricsText();
}

void VisualTypesettingController::requestCurrentMetrics()
{
    if (m_cleanPreviewActive || m_currentHierarchy.isEmpty()) {
        updateMetricsText();
        return;
    }
    m_probe->requestMetrics(m_currentHierarchy);
}

void VisualTypesettingController::documentLoaded()
{
    m_probe->invalidatePendingRequests();
    m_lastMetrics = PreviewLayoutMetrics();
    m_bodyOriginCssPx = 0.0;
    m_documentWritingMode.clear();
    m_overlay->setScrollPositionCssPx(m_preview->page()->scrollPosition().y());
    updateOverlayOrigin();
    m_probe->requestBodyContentOrigin();
    if (m_settings.metricsEnabled && !m_cleanPreviewActive) {
        m_metricsTimer->start();
    } else {
        updateMetricsText();
    }
}

void VisualTypesettingController::metricsReady(const PreviewLayoutMetrics &metrics)
{
    m_lastMetrics = metrics;
    if (m_calibrationPending) {
        m_calibrationPending = false;
        m_settings.referenceFontPx = metrics.fontSizePx;
        applySettings(m_settings, true);
        emit notificationRequested(
            tr("Grid reference font set to %1 px.").arg(number(metrics.fontSizePx)));
    }
    updateMetricsText();
}

void VisualTypesettingController::metricsUnavailable()
{
    if (m_calibrationPending) {
        m_calibrationPending = false;
        emit notificationRequested(tr("The current element's font size could not be measured."));
    }
    m_lastMetrics = PreviewLayoutMetrics();
    updateMetricsText();
}

void VisualTypesettingController::bodyContentOriginReady(qreal originCssPx, const QString &writingMode)
{
    m_bodyOriginCssPx = originCssPx;
    m_documentWritingMode = writingMode;
    updateOverlayOrigin();
    updateMetricsText();
}

void VisualTypesettingController::applySettings(const BaselineGridSettings &settings, bool persist)
{
    m_settings = settings;
    m_overlay->setGridSettings(m_settings);
    m_overlay->setCleanPreviewActive(m_cleanPreviewActive);
    updateOverlayOrigin();
    updateActions();
    if (persist) {
        persistSettings();
    }
    updateMetricsText();
}

void VisualTypesettingController::persistSettings()
{
    SettingsStore preferences;
    BaselineGridSettingsStore::save(preferences, m_settings);
}

void VisualTypesettingController::updateActions()
{
    const QSignalBlocker gridBlocker(m_showGridAction);
    const QSignalBlocker metricsBlocker(m_showMetricsAction);
    const QSignalBlocker cleanBlocker(m_cleanPreviewAction);
    m_showGridAction->setChecked(m_settings.enabled);
    m_showMetricsAction->setChecked(m_settings.metricsEnabled);
    m_cleanPreviewAction->setChecked(m_cleanPreviewActive);
    m_useCurrentElementAction->setEnabled(!m_currentHierarchy.isEmpty());
}

void VisualTypesettingController::updateOverlayOrigin()
{
    m_overlay->setOriginPositionCssPx(
        m_settings.origin == BaselineGridOrigin::BodyContentTop ? m_bodyOriginCssPx : 0.0);
}

QString VisualTypesettingController::gridSummary() const
{
    const qreal resolved = m_settings.resolvedStepCssPx();
    const QString prefix = (m_documentWritingMode.startsWith(QStringLiteral("vertical"))
                            || m_lastMetrics.writingMode.startsWith(QStringLiteral("vertical")))
        ? tr("Horizontal Grid") : tr("Grid");
    if (m_settings.unit == BaselineGridUnit::Em) {
        return tr("%1 %2px (%3em @ %4px)")
            .arg(prefix, number(resolved), number(m_settings.step), number(m_settings.referenceFontPx));
    }
    return tr("%1 %2px").arg(prefix, number(resolved));
}

QString VisualTypesettingController::metricsSummary(const PreviewLayoutMetrics &metrics) const
{
    const qreal gridStep = m_settings.resolvedStepCssPx();
    QStringList parts;
    parts << gridSummary()
          << metrics.tagName
          << tr("Font %1px").arg(number(metrics.fontSizePx));
    if (metrics.lineHeightNormal) {
        parts << tr("LH normal");
    } else if (metrics.hasLineHeightPx) {
        parts << tr("LH %1").arg(pxAndRatio(metrics.lineHeightPx, gridStep));
    }
    parts << tr("MBS %1").arg(pxAndRatio(metrics.marginBlockStartPx, gridStep))
          << tr("MBE %1").arg(pxAndRatio(metrics.marginBlockEndPx, gridStep));
    if (metrics.paddingBlockStartPx != 0.0 || metrics.paddingBlockEndPx != 0.0) {
        parts << tr("PBS %1").arg(pxAndRatio(metrics.paddingBlockStartPx, gridStep))
              << tr("PBE %1").arg(pxAndRatio(metrics.paddingBlockEndPx, gridStep));
    }
    if (!metrics.writingMode.isEmpty()
            && metrics.writingMode != QStringLiteral("horizontal-tb")) {
        parts << tr("WM %1").arg(metrics.writingMode);
    }
    return parts.join(QStringLiteral(" │ "));
}

void VisualTypesettingController::updateMetricsText()
{
    if (m_cleanPreviewActive || (!m_settings.enabled && !m_settings.metricsEnabled)) {
        emit metricsTextChanged(QString());
        return;
    }
    if (m_settings.metricsEnabled && m_lastMetrics.valid) {
        emit metricsTextChanged(metricsSummary(m_lastMetrics));
    } else if (m_settings.enabled) {
        emit metricsTextChanged(gridSummary());
    } else {
        emit metricsTextChanged(tr("Layout metrics unavailable"));
    }
}
