#include "Tabs/FlowTab.h"
#include "ViewEditors/CodeViewEditor.h"

//------------- modified: AddPasteRichText ---------------
bool FlowTab::PasteRichTextEnabled()
{
    if (m_wCodeView) {
        return m_wCodeView->canPaste();
    }
    return false;
}
//--------------------------------------------------------

//------------------- modified: AddPasteRichText ----------------
void FlowTab::PasteRichText()
{
    if (m_wCodeView) {

        m_wCodeView->PasteRichText();
    }
}
//---------------------------------------------------------------

// -------------- modified: SplitTagOrAddBreak ----------------
void FlowTab::SplitTagOrAddBreak()
{
    if (!IsDataWellFormed()) return;
    m_wCodeView->SplitTagOrAddBreak();
}
// --------------------------------------------------------------

//-------------- modified: Add Lables On Multiple Lines -------------------
void FlowTab::HeadingStylePlus(const QString& heading_type, bool preserve_attributes)
{
    if (m_wCodeView) {
        QChar last_char = heading_type[heading_type.length() - 1];
        if (last_char.isDigit()) {
            m_wCodeView->FormatBlock_multiline("h" % QString(last_char), preserve_attributes);
        }
        else if (heading_type == "Normal") {
            m_wCodeView->FormatBlock_multiline("p", preserve_attributes);
        }
        else if (heading_type == "Division") {
            m_wCodeView->FormatBlock_multiline("div", preserve_attributes);
        }
    }
}
//-------------------------------------------------------------------------

//---------------------- modified: MergeNextElement ------------------------
void FlowTab::MergeNextElement() {
    if (!IsDataWellFormed()) return;
    m_wCodeView->MergeNextElement();
}
//--------------------------------------------------------------------------