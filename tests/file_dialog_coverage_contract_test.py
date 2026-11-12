#!/usr/bin/env python3

import re
import sys
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


repo = Path(sys.argv[1]).resolve()
src = repo / "src"
get_call = re.compile(
    r"QFileDialog::get(?:OpenFileName|OpenFileNames|SaveFileName|ExistingDirectory)\s*\("
)
direct_dialog = re.compile(r"\bQFileDialog\s+\w+\s*\(")
forwarded = src / "Dialogs/PreviewFileDialog.cpp"


def statement_at(text: str, start: int) -> str:
    depth = 0
    for index in range(start, len(text)):
        char = text[index]
        if char == "(":
            depth += 1
        elif char == ")":
            depth = max(0, depth - 1)
        elif char == ";" and depth == 0:
            return text[start:index]
    raise AssertionError("Unterminated file dialog call")


def function_before(text: str, start: int) -> str:
    previous = text.rfind("\n}", 0, start)
    return text[0 if previous < 0 else previous:start]


failures = []
for path in sorted(src.rglob("*.cpp")):
    text = path.read_text(encoding="utf-8")
    relative = path.relative_to(repo).as_posix()
    for match in get_call.finditer(text):
        call = statement_at(text, match.start())
        if path == forwarded:
            if "options" not in call:
                failures.append(f"{relative}: preview dialog dropped its caller options")
            continue
        if "Utility::DlgOptions(" in call or "Utility::DlgOptions(" in function_before(text, match.start()):
            if "Utility::DlgOptions(" in call or re.search(r"\boptions\b", call):
                continue
        failures.append(f"{relative}: file dialog does not use Utility::DlgOptions()")
    if path == forwarded:
        continue
    for match in direct_dialog.finditer(text):
        window = text[match.start():match.start() + 600]
        if "setOptions(Utility::DlgOptions(" not in window:
            failures.append(f"{relative}: constructed file dialog does not apply Utility::DlgOptions()")

require(not failures, "\n".join(failures))

utility = (src / "Misc/Utility.cpp").read_text(encoding="utf-8")
settings = (src / "Misc/SettingsStore.cpp").read_text(encoding="utf-8")
ui = (src / "Form_Files/PGeneralSettingsWidget.ui").read_text(encoding="utf-8")
require(
    "settings.fileDialogStyle()" in utility
    and "FileDialogPolicy::Options(" in utility
    and "SIGIL_FORCE_NATIVE_FILE_DIALOG" in utility
    and "Qt::AA_DontUseNativeDialogs" in utility,
    "DlgOptions must read the preference and preserve the test attribute",
)
require(
    'KEY_FILE_DIALOG_STYLE = SETTINGS_GROUP + "/file_dialog_style"' in settings
    and 'QStringLiteral("native")' in settings,
    "The saved dialog style must default to the system dialog",
)
require(
    'name="FileDialogStyle"' in ui
    and ">System native<" in ui
    and ">Qt (previous)<" in ui,
    "Preferences must offer the system dialog and the previous Qt dialog",
)
require(
    "QFileDialog::DontUseNativeDialog" not in utility,
    "Platform-specific non-native overrides must not return",
)

print("file_dialog_coverage_contract: ok")
