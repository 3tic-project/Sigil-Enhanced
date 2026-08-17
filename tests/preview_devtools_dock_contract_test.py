#!/usr/bin/env python3

import sys
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


repo = Path(sys.argv[1]).resolve()
inspector_h = (repo / "src/Dialogs/Inspector.h").read_text(encoding="utf-8")
inspector_cpp = (repo / "src/Dialogs/Inspector.cpp").read_text(encoding="utf-8")
preview_h = (repo / "src/MainUI/PreviewWindow.h").read_text(encoding="utf-8")
preview = (repo / "src/MainUI/PreviewWindow.cpp").read_text(encoding="utf-8")
main_window = (repo / "src/MainUI/MainWindow.cpp").read_text(encoding="utf-8")
main_window_h = (repo / "src/MainUI/MainWindow.h").read_text(encoding="utf-8")

require(
    "class Inspector : public QWidget" in inspector_h
    and "class Inspector : public QDialog" not in inspector_h,
    "Inspector must be an embeddable QWidget, not a floating QDialog",
)
require(
    "void CloseRequested();" in inspector_h,
    "Inspector close must ask Preview to hide the pane",
)
require(
    "void Inspector::StopInspection()" in inspector_cpp
    and "setInspectedPage(nullptr)" in inspector_cpp,
    "Inspector must still disconnect the inspected page on teardown",
)
require(
    "QSplitter(Qt::Vertical" in preview
    and "previewDevToolsSplitter" in preview
    and "setChildrenCollapsible(false)" in preview
    and "m_Layout->addWidget(m_Splitter, 1)" in preview
    and "m_Layout->addLayout(m_buttons)" in preview,
    "Preview must host DevTools in a splitter above the toolbar so the preview fills leftover space",
)
require(
    "setStretchFactor(0, 1)" in preview
    and "setCollapsible(1, true)" in preview
    and "CollapseDevToolsSplitter" in preview,
    "hidden DevTools must collapse to zero height instead of leaving a blank pane",
)
require(
    "Do not StopInspection() when the Preview dock is hidden or tabified." in preview,
    "tabifying Preview with TOC must not tear down the inspected page binding",
)
require(
    "m_Inspector->hide();" in preview
    and "Hide keeps the WebEngine page and inspected binding." in preview,
    "toggling DevTools must hide the pane without destroying the WebEngine page",
)
require(
    'settings.setValue("devToolsVisible"' in preview
    and 'settings.setValue("devToolsSplitterState"' in preview,
    "Preview must persist DevTools visibility and splitter state only",
)
require(
    "void SetDevToolsVisible(bool visible);" in preview_h
    and "void SaveLayoutSettings();" in preview_h
    and "void DevToolsVisibilityChanged(bool visible);" in preview_h,
    "Preview must expose show/hide, layout save, and visibility signals",
)
require(
    "QWebEnginePage::InspectElement" in preview
    and "triggerPageAction" in preview
    and "ShowPreviewContextMenu" in preview,
    "Preview right-click must use Qt InspectElement instead of a custom CDP",
)
require(
    'tr("Developer Tools")' in main_window
    and "ToggleDeveloperTools" in main_window
    and "m_PreviewWindow->SetDevToolsVisible(show)" in main_window,
    "View menu Developer Tools must show Preview and the embedded Inspector",
)
require(
    "void ToggleDeveloperTools(bool show);" in main_window_h
    and "void InspectHTML();" in main_window_h,
    "InspectHTML remains the programmatic entry and must open docked DevTools",
)
require(
    "m_PreviewWindow->SaveLayoutSettings();" in main_window,
    "MainWindow must persist Preview/DevTools layout on exit",
)
require(
    '"MainWindow.DeveloperTools"' in main_window,
    "Developer Tools must register with KeyboardShortcutManager without a default shortcut",
)

print("preview_devtools_dock_contract: ok")
