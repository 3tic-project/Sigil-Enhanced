#!/usr/bin/env python3

import sys
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


repo = Path(sys.argv[1]).resolve()
model = (repo / "src/ViewEditors/BaselineGridModel.cpp").read_text(encoding="utf-8")
overlay_h = (repo / "src/ViewEditors/BaselineGridOverlay.h").read_text(encoding="utf-8")
overlay = (repo / "src/ViewEditors/BaselineGridOverlay.cpp").read_text(encoding="utf-8")
overlay_base = (repo / "src/ViewEditors/Overlay.h").read_text(encoding="utf-8")
store = (repo / "src/ViewEditors/BaselineGridSettingsStore.cpp").read_text(encoding="utf-8")
probe = (repo / "src/ViewEditors/PreviewMetricsProbe.cpp").read_text(encoding="utf-8")
request_tracker = (
    repo / "src/ViewEditors/PreviewMetricsRequestTracker.cpp"
).read_text(encoding="utf-8")
probe_javascript = (repo / "src/ViewEditors/PreviewMetricsJavascript.cpp").read_text(encoding="utf-8")
metrics = (repo / "src/ViewEditors/PreviewLayoutMetrics.cpp").read_text(encoding="utf-8")
controller = (repo / "src/MainUI/VisualTypesettingController.cpp").read_text(encoding="utf-8")
preview = (repo / "src/MainUI/PreviewWindow.cpp").read_text(encoding="utf-8")
main_window = (repo / "src/MainUI/MainWindow.cpp").read_text(encoding="utf-8")
dialog = (repo / "src/Dialogs/BaselineGridSettingsDialog.cpp").read_text(encoding="utf-8")

require(
    "resolvedStepCssPx" in model
    and "resolvedVerticalStepCssPx" in model
    and "step * referenceFontPx" in model
    and "currentElement" not in model,
    "em grids must resolve only from the fixed reference font",
)
require(
    "originCssPx + axisOffset - scrollCssPx" in model
    and "* zoomFactor" in model
    and "devicePixelRatio" not in model,
    "grid phase must be document-anchored, zoom once, and leave DPR to QPainter",
)
require(
    "minimumZoomPercent" in model
    and "showMinor" in model
    and "visibleStep < 1.0" in model,
    "minor lines must honor the view threshold and suppress sub-pixel moire",
)
require(
    "public OverlayWidget" in overlay_h
    and "Qt::WA_TransparentForMouseEvents" in overlay_base
    and "setCosmetic(true)" in overlay,
    "grid overlay must be screen-only, input-transparent, and HiDPI-safe",
)
require(
    "BaselineGridAxis::Horizontal" in overlay
    and "BaselineGridAxis::Vertical" in overlay
    and "m_scrollCssPx.x()" in overlay
    and "m_scrollCssPx.y()" in overlay
    and "QPointF(line.position, 0.0)" in overlay,
    "the overlay must draw independently spaced horizontal and vertical document grids",
)
require(
    "wl->addWidget(m_overlayBase)" in preview
    and "m_Preview(new ViewPreview(m_overlayBase))" in preview
    and "new BaselineGridOverlay(overlayParent)" in controller,
    "Preview and grid must remain sibling layers in the overlay container",
)

for key in (
    "baseline_grid_enabled",
    "layout_metrics_enabled",
    "horizontal_grid_enabled",
    "vertical_grid_enabled",
    "grid_unit",
    "grid_step",
    "vertical_grid_step",
    "grid_reference_font_px",
    "grid_origin",
    "grid_offset_px",
    "grid_major_every",
    "grid_minor_color",
    "grid_minor_opacity",
    "grid_major_color",
    "grid_major_opacity",
    "grid_view_threshold",
):
    require(key in store, f"missing persistent visual-aids setting: {key}")
require(
    "grid_colors_customized" in store and "defaults(darkTheme)" in store,
    "default grid colors must follow the application theme until customized",
)

for computed_property in (
    "fontSize",
    "lineHeight",
    "marginBlockStart",
    "marginBlockEnd",
    "paddingBlockStart",
    "paddingBlockEnd",
    "writingMode",
    "display",
):
    require(computed_property in probe_javascript, f"metrics probe omits {computed_property}")
require(
    "lineHeight === 'normal'" in probe_javascript
    and "lineHeightNormal" in metrics
    and "1.2" not in probe_javascript,
    "line-height normal must not be replaced by an invented value",
)
require(
    "runJavaScript" in probe
    and "QWebEngineScript::ApplicationWorld" in probe
    and "QPointer<PreviewMetricsProbe>" in probe
    and "m_pageGeneration" in probe
    and "requestId" in probe,
    "computed metrics must use cancellable asynchronous ApplicationWorld queries",
)
require(
    "PreviewMetricsRequestPurpose::Calibration" in controller
    and "PreviewMetricsRequestPurpose::Status" in controller
    and "m_requests.take(requestId)" in controller
    and "m_requests.clear()" in controller
    and "token.id == id" in request_tracker,
    "status, calibration, inspection, and settings results must retain request identity",
)
require(
    "QWebEnginePage::loadStarted" in controller
    and "documentLoading" in controller
    and "m_activeSettingsDialog->setCurrentElementFontPx(qQNaN())" in controller,
    "navigation must invalidate pending metrics before the replacement document finishes loading",
)
require(
    "setInterval(75)" in controller
    and "m_metricsTimer->start()" in controller,
    "caret-driven metrics must be debounced within the PRD's 50-100ms range",
)

require(
    "m_menu->addAction(m_showGridAction)" in controller
    and "m_menu->addAction(m_settingsAction)" in controller
    and "m_menu->addAction(m_showMetricsAction)" not in controller
    and "m_menu->addAction(m_useCurrentElementAction)" not in controller
    and "m_menu->addAction(m_cleanPreviewAction)" not in controller,
    "the visual-aids menu must expose only Show Grid and Grid Settings",
)
require(
    'tr("Enable Grid")' in preview
    and 'tr("Disable Grid")' in preview
    and "grid_action->setChecked(!grid_action->isChecked())" in preview
    and 'tr("Inspect Layout")' not in preview
    and "useElementAtPreviewPosition(pos)" not in preview,
    "Preview context menu must toggle the grid without exposing dormant element tools",
)
require(
    "ui.menuEnhancement->addMenu" in main_window
    and "ui.menuView->addMenu(m_PreviewWindow->VisualTypesettingMenu())" not in main_window,
    "visual grid actions must live under Enhancement instead of View",
)
require(
    "MainWindow.ShowBaselineGrid" in main_window
    and "MainWindow.BaselineGridSettings" in main_window
    and '"MainWindow.ShowLayoutMetrics"' not in main_window
    and '"MainWindow.UseCurrentElementAsGridReference"' not in main_window
    and '"MainWindow.CleanPreview"' not in main_window,
    "only the two visible grid actions should be shortcut configurable",
)
require(
    "m_settings.metricsEnabled = false" in controller
    and "loaded.metricsEnabled = false" in store
    and "m_showMetricsAction->setEnabled(false)" in controller
    and "m_cleanPreviewAction->setEnabled(false)" in controller,
    "layout metrics and Clean Preview must remain disabled behind the grid-only interface",
)
require(
    "m_horizontalGrid" in dialog
    and "m_verticalGrid" in dialog
    and "m_verticalStep" in dialog
    and "settings.metricsEnabled = false" in dialog
    and "setMinimumSize(560, 560)" in dialog
    and "QDialogButtonBox::RestoreDefaults" in dialog,
    "grid settings must independently enable and adjust horizontal and vertical lines",
)

for source in (model, overlay, store, probe, request_tracker, metrics, controller, dialog):
    require(
        "BookManipulation" not in source
        and "ResourceObjects" not in source
        and "SetModified" not in source,
        "visual aids must not depend on or mutate EPUB/book resources",
    )

print("visual_typesetting_aids_contract: ok")
