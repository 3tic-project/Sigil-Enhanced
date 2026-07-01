#include "MainUI/ValidationResultsView.h"

#include "BookManipulation/Book.h"
#include "BookManipulation/FolderKeeper.h"
#include "BookManipulation/XhtmlDoc.h"
#include "BuiltinPlugins/EpubStructureNormalizer.h"
#include "MainUI/BookBrowser.h"
#include "Misc/Utility.h"
#include "ResourceObjects/Resource.h"

//------------------------modified: well formed check----------------

void ValidationResultsView::ValidateCurrentBook_M()
{
    ClearResults();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_Book->SaveAllResourcesToDisk();
    QList<ValidationResult> results = validateXhtml() + correctOPF();
    QApplication::restoreOverrideCursor();
    DisplayResults(results);
    show();
    raise();
}
//-------------------------------------------------------------------

//------------------------modified: validateXhtml--------------------------
QList<ValidationResult> ValidationResultsView::validateXhtml()
{
    QList<ValidationResult> results;
    QList<Resource*> resources = m_Book->GetFolderKeeper()->GetResourceList();
    foreach(Resource * resource, resources) {
        if (resource->Type() == Resource::HTMLResourceType) {
            QString source = Utility::ReadUnicodeTextFile(resource->GetFullPath());
            XhtmlDoc::WellFormedError error = XhtmlDoc::WellFormedErrorForSource(source);
            if (error.line != -1) {
                ValidationResult::ResType vtype = ValidationResult::ResType_Error;
                QString bookpath = resource->GetRelativePath();
                int lineno = error.line;
                int colno = error.column;
                QString msg = error.message;
                results.append(ValidationResult(vtype, bookpath, lineno, colno, msg));
            }
        }
    }
    return results;
}
//-------------------------------------------------------------------------

//------------------------modified: correctOPF-------------------------
void ValidationResultsView::SetBookBrowser(BookBrowser* bookbrowser)
{
    m_BookBrowser = bookbrowser;
    ClearResults();
}
//---------------------------------------------------------------------

//----------------------------------------------------modified: correctOPF-------------------------------------------------
QList<ValidationResult> ValidationResultsView::correctOPF()
{
    BuiltinPlugins::EpubStructureNormalizer normalizer(m_Book.data());
    BuiltinPlugins::EpubStructureNormalizer::Result result = normalizer.normalizeOpfManifest();

    if (result.bookBrowserRefreshRequired && m_BookBrowser) {
        m_BookBrowser->Refresh();
    }

    return result.validationResults;
}
//-------------------------------------------------------------------------------------------------------------------------
