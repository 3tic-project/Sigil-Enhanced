#!/usr/bin/env python3

import sys
import xml.etree.ElementTree as ET
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


repo = Path(sys.argv[1]).resolve()
main_ui = ET.parse(repo / "src/Form_Files/main.ui").getroot()
main_window = (repo / "src/MainUI/MainWindow.cpp").read_text(encoding="utf-8")
main_window_ext = (repo / "src/MainUI/MainWindowExt.cpp").read_text(encoding="utf-8")
book_browser_ext = (repo / "src/MainUI/BookBrowserExt.cpp").read_text(encoding="utf-8")
controller = (repo / "src/BuiltinPlugins/KfxImportController.cpp").read_text(encoding="utf-8")

action_name = "actionConvertKfx"
enhancement_menu = main_ui.find(".//widget[@name='menuEnhancement']")
require(enhancement_menu is not None, "Enhancement menu must exist")
menu_actions = [item.get("name") for item in enhancement_menu.findall("addaction")]
require(menu_actions.count(action_name) == 1, "Enhancement must expose one KFX entry")
require(
    main_ui.find(f".//action[@name='{action_name}']") is not None,
    "missing KFX conversion action",
)
require(
    "connect(ui.actionConvertKfx, SIGNAL(triggered()), this, SLOT(ConvertKfx()));"
    in main_window,
    "single KFX action must open the conversion workflow",
)
require(
    'tr("Save EPUB As...")' in main_window_ext
    and 'tr("Open in New Window")' in main_window_ext,
    "the single menu workflow must offer Save As and Open in New Window",
)

require(
    "KfxImportProtocol::isKfxPath(filepath)" in main_window
    and "ConvertKfxFile(filepath, true)" in main_window,
    "top-level drops must classify KFX and default to opening converted EPUBs",
)
require(
    "KfxImportProtocol::isKfxPath(filepath)" in book_browser_ext
    and "main_window->AddDroppedFiles(filepaths)" in book_browser_ext,
    "Book Browser drops must route KFX through the MainWindow conversion prompt",
)
require(
    "LoadConvertedEpub(result.outputPath, suggested_name, result.warnings)"
    in main_window_ext,
    "open-in-new-window must use the derived EPUB loader",
)
require(
    "m_SourceEpubSnapshot = EpubFileSnapshot::capture(temporaryEpub)" in main_window
    and "setWindowModified(true)" in main_window,
    "derived documents must retain an exact-copy baseline while prompting for first save",
)

require(
    'QStringLiteral("-m")' in controller
    and 'QStringLiteral("sigil_kfx_import.worker")' in controller,
    "the converter must run through the isolated worker module",
)
require(
    'environment.insert(QStringLiteral("PYTHONPATH"), python_root)' in controller
    and "inherited_python_path" not in controller,
    "the worker must not inherit user-controlled Python module paths",
)
require(
    "QTemporaryFile output" in controller
    and "EpubFileSnapshot::capture(result.outputPath)" in main_window_ext,
    "conversion must stage output before atomically copying it to Save As destinations",
)
cancel_handler = controller.index(
    "QObject::connect(&progress, &QProgressDialog::canceled"
)
cancel_guard = controller.index(
    "if (process.state() == QProcess::NotRunning)", cancel_handler
)
cancel_assignment = controller.index("result.cancelled = true", cancel_handler)
require(
    cancel_guard < cancel_assignment,
    "programmatic progress-dialog close after worker completion must not report cancellation",
)
