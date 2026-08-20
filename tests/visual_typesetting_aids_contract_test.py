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
probe_javascript = (repo / "src/ViewEditors/PreviewMetricsJavascript.cpp").read_text(encoding="utf-8")
metrics = (repo / "src/ViewEditors/PreviewLayoutMetrics.cpp").read_text(encoding="utf-8")
controller = (repo / "src/MainUI/VisualTypesettingController.cpp").read_text(encoding="utf-8")
preview = (repo / "src/MainUI/PreviewWindow.cpp").read_text(encoding="utf-8")
main_window = (repo / "src/MainUI/MainWindow.cpp").read_text(encoding="utf-8")
dialog = (repo / "src/Dialogs/BaselineGridSettingsDialog.cpp").read_text(encoding="utf-8")

require(
    "resolvedStepCssPx" in model
    and "step * referenceFontPx" in model
    and "currentElement" not in model,
    "em grids must resolve only from the fixed reference font",
)
require(
    "originCssPx + settings.offsetCssPx - scrollCssPx" in model
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
    "wl->addWidget(m_overlayBase)" in preview
    and "m_Preview(new ViewPreview(m_overlayBase))" in preview
    and "new BaselineGridOverlay(overlayParent)" in controller,
    "Preview and grid must remain sibling layers in the overlay container",
)

for key in (
    "baseline_grid_enabled",
    "layout_metrics_enabled",
    "grid_unit",
    "grid_step",
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
    "cleanPreview" not in store and "clean_preview" not in store,
    "Clean Preview must remain session state and preserve enabled preferences",
)
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
    and "m_metricsGeneration" in probe,
    "computed metrics must use cancellable asynchronous ApplicationWorld queries",
)
require(
    "setInterval(75)" in controller
    and "m_metricsTimer->start()" in controller,
    "caret-driven metrics must be debounced within the PRD's 50-100ms range",
)

for action in (
    "Show Baseline Grid",
    "Show Layout Metrics",
    "Use Current Element as Grid Reference",
    "Baseline Grid Settings…",
    "Clean Preview",
):
    require(action in controller, f"View menu is missing {action}")
require(
    "Use This Element as Grid Reference" in preview
    and "useElementAtPreviewPosition(pos)" in preview,
    "Preview context menu must support one-shot element calibration",
)
require(
    "MainWindow.ShowBaselineGrid" in main_window
    and "MainWindow.ShowLayoutMetrics" in main_window
    and "MainWindow.UseCurrentElementAsGridReference" in main_window
    and "MainWindow.BaselineGridSettings" in main_window
    and "MainWindow.CleanPreview" in main_window,
    "all visual-aids actions must be user-shortcut configurable",
)
require(
    "layoutMetricsStatus" in main_window
    and "metricsTextChanged" in controller
    and "LayoutMetricsTextChanged" in preview,
    "computed metrics must reach an accessible persistent status-bar label",
)
require(
    "setMinimumSize(560, 520)" in dialog
    and "QDialogButtonBox::RestoreDefaults" in dialog,
    "grid settings dialog must be readable and provide defaults restoration",
)

for source in (model, overlay, store, probe, metrics, controller, dialog):
    require(
        "BookManipulation" not in source
        and "ResourceObjects" not in source
        and "SetModified" not in source,
        "visual aids must not depend on or mutate EPUB/book resources",
    )

print("visual_typesetting_aids_contract: ok")
