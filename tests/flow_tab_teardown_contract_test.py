#!/usr/bin/env python3

import pathlib
import sys


repo = pathlib.Path(sys.argv[1]).resolve()
flow_tab = (repo / "src/Tabs/FlowTab.cpp").read_text(encoding="utf-8")
code_view = (repo / "src/ViewEditors/CodeViewEditor.cpp").read_text(encoding="utf-8")

flow_destructor = flow_tab[flow_tab.index("FlowTab::~FlowTab()") : flow_tab.index("void FlowTab::UpdateCodeViewBookPath")]
clear_editor = flow_destructor.index("m_wCodeView = NULL")
delete_editor = flow_destructor.index("delete code_view")
if clear_editor >= delete_editor:
    raise AssertionError("FlowTab must clear its editor pointer before deleting the editor")

resource_modified = flow_tab[flow_tab.index("void FlowTab::ResourceModified()") : flow_tab.index("void FlowTab::HandleViewImage")]
null_guard = resource_modified.index("if (!m_wCodeView)")
caret_update = resource_modified.index("m_wCodeView->ExecuteCaretUpdate")
if null_guard >= caret_update:
    raise AssertionError("ResourceModified must reject teardown re-entry before editor access")

code_destructor = code_view[code_view.index("CodeViewEditor::~CodeViewEditor()") : code_view.index("QString CodeViewEditor::cursor_selected_text")]
blocker = code_destructor.index("QSignalBlocker blocker(highlighted_document)")
detach = code_destructor.index("m_Highlighter->setDocument(nullptr)")
delete_highlighter = code_destructor.index("delete m_Highlighter")
if not blocker < detach < delete_highlighter:
    raise AssertionError("CodeViewEditor must detach and delete its highlighter under signal blocking")
