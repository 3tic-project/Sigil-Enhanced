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
cmake = (repo / "src/qt6sigil.cmake").read_text(encoding="utf-8")

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
    main_window_ext.count('tr("Also normalize EPUB structure after conversion")') == 1
    and "choice.setCheckBox(normalize_checkbox)" in main_window_ext
    and "normalize_checkbox->setChecked(false)" in main_window_ext
    and "choice.setMinimumSize(680, 220)" in main_window_ext,
    "the menu prompt must offer optional structure normalization without crowding",
)
require(
    "ConvertKfxFile(source, false, normalize_structure)" in main_window_ext
    and "ConvertKfxFile(source, true, normalize_structure)" in main_window_ext,
    "both menu delivery modes must preserve the normalization choice",
)

require(
    "KfxImportProtocol::isKfxPath(filepath)" in main_window
    and "ConvertKfxFile(filepath, true, normalize_structure)" in main_window,
    "top-level drops must classify KFX and default to opening converted EPUBs",
)
require(
    'tr("Also normalize EPUB structure after conversion")' in main_window
    and "msgbox.setCheckBox(normalize_checkbox)" in main_window
    and "normalize_checkbox->setChecked(false)" in main_window
    and "msgbox.setMinimumSize(680, 240)" in main_window,
    "the drop prompt must offer the same roomy normalization option",
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
load_converted = main_window_ext.index(
    "new_window->LoadConvertedEpub(result.outputPath, suggested_name, result.warnings)"
)
normalize_converted = main_window_ext.index(
    "new_window->RunEpubStructureNormalization()", load_converted
)
show_converted = main_window_ext.index("new_window->show()", normalize_converted)
require(
    load_converted < normalize_converted < show_converted,
    "selected structure normalization must finish before the converted window is shown",
)
require(
    "staging_window->RunEpubStructureNormalization()" in main_window_ext
    and "staging_window->ExportCurrentBookCopy(normalized_path)" in main_window_ext
    and "normalized_epub.copyTo(destination, &copy_error)" in main_window_ext,
    "Save As normalization must reuse the built-in implementation and atomically deliver its EPUB",
)
require(
    "m_SourceEpubSnapshot = EpubFileSnapshot::capture(temporaryEpub)" in main_window
    and "setWindowModified(true)" in main_window,
    "derived documents must retain an exact-copy baseline while prompting for first save",
)

require(
    'QStringLiteral("-I")' in controller
    and 'QStringLiteral("-S")' in controller
    and "worker_bootstrap" in controller,
    "the converter must run through the isolated internal bootstrap",
)
require(
    '<< QStringLiteral("PYTHONPATH")' in controller
    and 'environment.insert(QStringLiteral("PYTHONPATH")' not in controller,
    "the worker must remove and not restore user-controlled Python module paths",
)
require(
    "link_debug_python.cmake" in cmake
    and "SIGIL_DEBUG_PYTHON_BIN" in cmake,
    "macOS Debug builds must expose the CMake interpreter through the app bundle",
)
require(
    'QStringLiteral("../python-runtime/bin/python3")' in controller,
    "the KFX controller must prefer the Debug app's internal runtime entry",
)
require(
    "QTemporaryFile output" in controller
    and "const EpubFileSnapshot converted = EpubFileSnapshot::capture(" in main_window_ext
    and "converted.copyTo(destination, &copy_error)" in main_window_ext,
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

kfx_guide = (repo / "docs/KfxImport.md").read_text(encoding="utf-8")
require(
    "https://github.com/2778995958/kfx2epub" in kfx_guide,
    "KFX user guide must credit kfx2epub",
)
require(
    not kfx_guide.lstrip().startswith("# ") or "PRD" not in kfx_guide.splitlines()[0],
    "KFX user guide must not be a PRD",
)
