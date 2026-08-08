#!/usr/bin/env python3

import re
import sys
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


repo = Path(sys.argv[1]).resolve()
operations = (repo / "src/Misc/SearchOperations.cpp").read_text(encoding="utf-8")

require(
    "qApp->processEvents();" not in operations,
    "search loops must not dispatch unrestricted application events",
)
require(
    "qApp->processEvents(QEventLoop::ExcludeUserInputEvents);" in operations,
    "search progress pumping must exclude user input",
)

loop_functions = (
    "CountInFiles",
    "ReplaceInAllFIles",
    "FunctionReplaceInAllFiles",
    "CountInFilesPlus",
    "ReplaceInAllFIlesPlus",
)
for index, function_name in enumerate(loop_functions):
    start = operations.index(f"SearchOperations::{function_name}")
    if index + 1 < len(loop_functions):
        end = operations.index(f"SearchOperations::{loop_functions[index + 1]}", start)
    else:
        end = len(operations)
    require(
        "ProcessSearchProgressEvents();" in operations[start:end],
        f"{function_name} must use the non-reentrant progress event pump",
    )

for source_name, class_name in (
    ("FindReplace.cpp", "FindReplace"),
    ("FindReplacePlus.cpp", "FindReplacePlus"),
):
    source = (repo / "src/MainUI" / source_name).read_text(encoding="utf-8-sig")
    for slot_name in ("FindClicked", "ReplaceClicked", "ReplaceAllClicked", "CountClicked"):
        match = re.search(
            rf"void {class_name}::{slot_name}\(\)\s*\{{(?P<body>.*?)\n\}}",
            source,
            re.DOTALL,
        )
        require(match is not None, f"could not inspect {class_name}::{slot_name}")
        body = match.group("body")
        require(
            "if (m_SearchRunning) return;" in body
            and "m_SearchRunning = true;" in body
            and "m_SearchRunning = false;" in body,
            f"{class_name}::{slot_name} must reject recursive activation",
        )

print("search operation reentrancy contract passed")
