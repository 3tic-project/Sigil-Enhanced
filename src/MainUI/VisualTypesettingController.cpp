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
      m_useCurrentElementAction(new QAction(
          tr("Use Current Element Font Size as Grid Reference"), this)),
      m_settingsAction(new QAction(tr("Baseline Grid Settings…"), this)),
      m_cleanPreviewAction(new QAction(tr("Clean Preview"), this))
{
    SettingsStore preferences;
    m_settings = BaselineGridSettingsStore::load(preferences, usesDarkPreviewTheme());

    m_showGridAction->setObjectName(QStringLiteral("actionShowBaselineGrid"));
    m_showGridAction->setCheckable(true);
    m_showGridAction->setToolTip(tr("Show a non-exported rhythm grid anchored to the Preview document."));
    m_showMetricsAction->setObjectName(QStringLiteral("actionShowLayoutMetrics"));
    m_showMetricsAction->setCheckable(true);
    m_showMetricsAction->setToolTip(tr("Show computed typography and spacing for the current element."));
    m_useCurrentElementAction->setObjectName(QStringLiteral("actionUseCurrentElementAsGridReference"));
    m_useCurrentElementAction->setToolTip(
        tr("Measure the current element once and use its font size as the fixed em reference."));
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
    connect(m_preview->page(), &QWebEnginePage::loadStarted,
            this, &VisualTypesettingController::documentLoading);
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
    const QString key = hierarchyKey(hierarchy);
    if (key == m_currentHierarchyKey) {
        return;
    }
    m_currentHierarchy = hierarchy;
    m_currentHierarchyKey = key;
    m_transientInspectionActive = false;
    m_requests.cancel(PreviewMetricsRequestPurpose::Status);
    m_requests.cancel(PreviewMetricsRequestPurpose::Inspection);
    if (m_lastMetricsKey != m_currentHierarchyKey) {
        m_lastMetrics = PreviewLayoutMetrics();
        m_lastMetricsKey.clear();
    }
    m_useCurrentElementAction->setEnabled(!hierarchy.isEmpty());
    if (m_settings.metricsEnabled && !m_cleanPreviewActive) {
        m_metricsTimer->start();
    }
    updateMetricsText();
}

void VisualTypesettingController::refreshThemeDefaults()
{
    if (m_settings.colorsCustomized) {
        return;
    }
    const BaselineGridSettings defaults = BaselineGridSettings::defaults(usesDarkPreviewTheme());
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
    if (!enabled) {
        m_metricsTimer->stop();
        m_requests.cancel(PreviewMetricsRequestPurpose::Status);
        m_requests.cancel(PreviewMetricsRequestPurpose::Inspection);
        m_transientInspectionActive = false;
    }
    applySettings(m_settings, true);
    if (enabled && !m_cleanPreviewActive) {
        if (hasFreshCurrentMetrics()) {
            updateMetricsText();
        } else {
            m_metricsTimer->start();
        }
    }
}

void VisualTypesettingController::useCurrentElementAsReference()
{
    if (m_currentHierarchy.isEmpty()) {
        emit notificationRequested(tr("No current Preview element is available for grid calibration."));
        return;
    }
    if (!beginCurrentElementRequest(PreviewMetricsRequestPurpose::Calibration)) {
        emit notificationRequested(tr("The current element's font size could not be measured."));
    }
}

void VisualTypesettingController::useElementAtPreviewPosition(const QPoint &viewportPosition)
{
    if (!beginPreviewPointRequest(viewportPosition, PreviewMetricsRequestPurpose::Calibration)) {
        emit notificationRequested(tr("The selected element's font size could not be measured."));
    }
}

void VisualTypesettingController::inspectElementAtPreviewPosition(
    const QPoint &viewportPosition)
{
    if (!beginPreviewPointRequest(viewportPosition, PreviewMetricsRequestPurpose::Inspection)) {
        emit notificationRequested(tr("Layout metrics are unavailable for the selected element."));
    }
}

void VisualTypesettingController::showSettings()
{
    const qreal currentFont = hasFreshCurrentMetrics() ? m_lastMetrics.fontSizePx : qQNaN();
    BaselineGridSettingsDialog dialog(
        m_settings, currentFont, usesDarkPreviewTheme(), m_dialogParent);
    m_activeSettingsDialog = &dialog;
    if (!hasFreshCurrentMetrics() && !m_currentHierarchy.isEmpty()) {
        beginCurrentElementRequest(PreviewMetricsRequestPurpose::Settings);
    }
    const int result = dialog.exec();
    m_activeSettingsDialog = nullptr;
    m_requests.cancel(PreviewMetricsRequestPurpose::Settings);
    if (result == QDialog::Accepted) {
        applySettings(dialog.gridSettings(), true);
    }
}

void VisualTypesettingController::setCleanPreviewActive(bool active)
{
    m_cleanPreviewActive = active;
    m_overlay->setCleanPreviewActive(active);
    if (active) {
        m_metricsTimer->stop();
        m_requests.cancel(PreviewMetricsRequestPurpose::Status);
    }
    if (!active && m_settings.metricsEnabled) {
        m_metricsTimer->start();
    }
    updateMetricsText();
}

void VisualTypesettingController::requestCurrentMetrics()
{
    if (m_cleanPreviewActive || !m_settings.metricsEnabled || m_currentHierarchy.isEmpty()) {
        updateMetricsText();
        return;
    }
    if (hasFreshCurrentMetrics()) {
        updateMetricsText();
        return;
    }
    if (!beginCurrentElementRequest(PreviewMetricsRequestPurpose::Status)) {
        m_lastMetrics = PreviewLayoutMetrics();
        m_lastMetricsKey.clear();
        updateMetricsText();
    }
}

void VisualTypesettingController::documentLoading()
{
    m_metricsTimer->stop();
    m_probe->invalidatePendingRequests();
    m_requests.clear();
    m_lastMetrics = PreviewLayoutMetrics();
    m_lastMetricsKey.clear();
    m_transientInspectionActive = false;
    if (m_activeSettingsDialog) {
        m_activeSettingsDialog->setCurrentElementFontPx(qQNaN());
    }
    updateMetricsText();
}

void VisualTypesettingController::documentLoaded()
{
    m_probe->invalidatePendingRequests();
    m_requests.clear();
    m_lastMetrics = PreviewLayoutMetrics();
    m_lastMetricsKey.clear();
    m_transientInspectionActive = false;
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

void VisualTypesettingController::metricsReady(
    quint64 requestId,
    const PreviewLayoutMetrics &metrics)
{
    const std::optional<PreviewMetricsRequestToken> token = m_requests.take(requestId);
    if (!token.has_value()) {
        return;
    }

    switch (token->purpose) {
    case PreviewMetricsRequestPurpose::Status:
        if (token->elementKey != m_currentHierarchyKey) {
            return;
        }
        m_lastMetrics = metrics;
        m_lastMetricsKey = token->elementKey;
        break;
    case PreviewMetricsRequestPurpose::Calibration:
        tryApplyReferenceFont(metrics);
        if (token->elementKey == m_currentHierarchyKey && m_settings.metricsEnabled) {
            m_lastMetrics = metrics;
            m_lastMetricsKey = token->elementKey;
        }
        break;
    case PreviewMetricsRequestPurpose::Inspection:
        m_requests.cancel(PreviewMetricsRequestPurpose::Status);
        m_lastMetrics = metrics;
        m_lastMetricsKey = QStringLiteral("preview:") + metrics.domPath;
        m_transientInspectionActive = true;
        break;
    case PreviewMetricsRequestPurpose::Settings:
        if (token->elementKey == m_currentHierarchyKey && m_activeSettingsDialog) {
            m_activeSettingsDialog->setCurrentElementFontPx(metrics.fontSizePx);
            m_lastMetrics = metrics;
            m_lastMetricsKey = token->elementKey;
        }
        break;
    case PreviewMetricsRequestPurpose::Count:
        return;
    }
    updateMetricsText();
}

void VisualTypesettingController::metricsUnavailable(quint64 requestId)
{
    const std::optional<PreviewMetricsRequestToken> token = m_requests.take(requestId);
    if (!token.has_value()) {
        return;
    }
    handleUnavailableRequest(*token);
    updateMetricsText();
}

void VisualTypesettingController::bodyContentOriginReady(qreal originCssPx, const QString &writingMode)
{
    if (!qIsFinite(originCssPx)) {
        return;
    }
    m_bodyOriginCssPx = originCssPx;
    m_documentWritingMode = writingMode;
    updateOverlayOrigin();
    updateMetricsText();
}

bool VisualTypesettingController::beginCurrentElementRequest(
    PreviewMetricsRequestPurpose purpose)
{
    if (m_currentHierarchy.isEmpty()) {
        return false;
    }
    const quint64 requestId = m_probe->requestMetrics(m_currentHierarchy);
    if (requestId == 0) {
        return false;
    }
    m_requests.begin(requestId, purpose, m_currentHierarchyKey);
    return true;
}

bool VisualTypesettingController::beginPreviewPointRequest(
    const QPoint &viewportPosition,
    PreviewMetricsRequestPurpose purpose)
{
    if (!m_preview || m_preview->GetZoomFactor() <= 0.0) {
        return false;
    }
    const qreal zoom = m_preview->GetZoomFactor();
    const QPointF cssPoint(viewportPosition.x() / zoom, viewportPosition.y() / zoom);
    const quint64 requestId = m_probe->requestMetricsAtViewportPoint(cssPoint);
    if (requestId == 0) {
        return false;
    }
    const QString pointKey = QStringLiteral("preview-point:%1:%2")
        .arg(cssPoint.x(), 0, 'f', 3)
        .arg(cssPoint.y(), 0, 'f', 3);
    m_requests.begin(requestId, purpose, pointKey);
    return true;
}

void VisualTypesettingController::handleUnavailableRequest(
    const PreviewMetricsRequestToken &token)
{
    switch (token.purpose) {
    case PreviewMetricsRequestPurpose::Status:
        if (token.elementKey == m_currentHierarchyKey) {
            m_lastMetrics = PreviewLayoutMetrics();
            m_lastMetricsKey.clear();
        }
        break;
    case PreviewMetricsRequestPurpose::Calibration:
        if (token.elementKey.startsWith(QStringLiteral("preview-point:"))) {
            emit notificationRequested(tr("The selected element's font size could not be measured."));
        } else {
            emit notificationRequested(tr("The current element's font size could not be measured."));
        }
        break;
    case PreviewMetricsRequestPurpose::Inspection:
        m_transientInspectionActive = false;
        if (m_lastMetricsKey.startsWith(QStringLiteral("preview:"))) {
            m_lastMetrics = PreviewLayoutMetrics();
            m_lastMetricsKey.clear();
        }
        if (m_settings.metricsEnabled && !m_cleanPreviewActive) {
            m_metricsTimer->start();
        }
        emit notificationRequested(tr("Layout metrics are unavailable for the selected element."));
        break;
    case PreviewMetricsRequestPurpose::Settings:
        if (m_activeSettingsDialog) {
            m_activeSettingsDialog->setCurrentElementFontPx(qQNaN());
        }
        break;
    case PreviewMetricsRequestPurpose::Count:
        break;
    }
}

bool VisualTypesettingController::hasFreshCurrentMetrics() const
{
    return m_lastMetrics.valid
        && !m_currentHierarchyKey.isEmpty()
        && m_lastMetricsKey == m_currentHierarchyKey;
}

bool VisualTypesettingController::tryApplyReferenceFont(const PreviewLayoutMetrics &metrics)
{
    BaselineGridSettings candidate = m_settings;
    candidate.referenceFontPx = metrics.fontSizePx;
    if (!qIsFinite(metrics.fontSizePx)
            || metrics.fontSizePx < 0.25
            || metrics.fontSizePx > 1000.0
            || !candidate.isValid()) {
        emit notificationRequested(
            tr("The measured font size %1 px cannot be used with the current grid settings.")
                .arg(number(metrics.fontSizePx)));
        return false;
    }
    applySettings(candidate, true);
    emit notificationRequested(
        tr("Grid reference font set to %1 px.").arg(number(metrics.fontSizePx)));
    return true;
}

bool VisualTypesettingController::usesDarkPreviewTheme() const
{
    SettingsStore preferences;
    return Utility::IsDarkMode() && preferences.previewDark();
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
    const bool showMetrics = m_settings.metricsEnabled || m_transientInspectionActive;
    if (m_cleanPreviewActive || (!m_settings.enabled && !showMetrics)) {
        emit metricsTextChanged(QString());
        return;
    }
    if (showMetrics && m_lastMetrics.valid) {
        emit metricsTextChanged(metricsSummary(m_lastMetrics));
    } else if (m_settings.enabled) {
        emit metricsTextChanged(gridSummary());
    } else {
        emit metricsTextChanged(tr("Layout metrics unavailable"));
    }
}

QString VisualTypesettingController::hierarchyKey(const QList<ElementIndex> &hierarchy)
{
    QStringList parts;
    parts.reserve(hierarchy.size());
    for (const ElementIndex &element : hierarchy) {
        parts.append(element.name + QChar(':') + QString::number(element.index));
    }
    return parts.join(QChar('/'));
}
