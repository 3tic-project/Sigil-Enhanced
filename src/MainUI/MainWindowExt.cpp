#include <QApplication>
#include <QHash>
#include <QMessageBox>

#include "MainUI/MainWindow.h"
#include "MainUI/BookBrowser.h"
#include "MainUI/TableOfContents.h"
#include "MainUI/ValidationResultsView.h"
#include "BuiltinPlugins/EpubStructureNormalizer.h"
#include "BookManipulation/FolderKeeper.h"
#include "ResourceObjects/Resource.h"
#include "Tabs/ContentTab.h"
#include "Tabs/FlowTab.h"
#include "Tabs/CSSTab.h"
#include "BookManipulation/EpubVersionConv.h" // modified: Epub3ToEpub2 Epub2ToEpub3
#include "Misc/ResourceInsertion.h"
#include "Misc/Utility.h"

namespace
{

QHash<Resource*, QString> CaptureResourcePaths(Book* book)
{
    QHash<Resource*, QString> paths;
    if (!book || !book->GetFolderKeeper()) {
        return paths;
    }

    foreach(Resource* resource, book->GetFolderKeeper()->GetResourceList()) {
        if (resource) {
            paths.insert(resource, resource->GetRelativePath());
        }
    }
    return paths;
}

QHash<QString, QString> BuildResourcePathMap(Book* book, const QHash<Resource*, QString>& old_paths)
{
    QHash<QString, QString> path_map;
    if (!book || !book->GetFolderKeeper()) {
        return path_map;
    }

    foreach(Resource* resource, book->GetFolderKeeper()->GetResourceList()) {
        if (!resource || !old_paths.contains(resource)) {
            continue;
        }
        const QString old_path = old_paths.value(resource);
        const QString new_path = resource->GetRelativePath();
        if (old_path != new_path) {
            path_map.insert(old_path, new_path);
        }
    }
    return path_map;
}

QList<ValidationResult> RebaseValidationResultPaths(const QList<ValidationResult>& results,
                                                    const QHash<QString, QString>& path_map)
{
    if (path_map.isEmpty()) {
        return results;
    }

    QList<ValidationResult> rebased;
    foreach(ValidationResult result, results) {
        QString bookpath = result.BookPath();
        if (path_map.contains(bookpath)) {
            bookpath = path_map.value(bookpath);
        }
        rebased << ValidationResult(result.Type(), bookpath, result.LineNumber(),
                                    result.CharOffset(), result.Message());
    }
    return rebased;
}

}

//-----modified: Epub3ToEpub2------
void MainWindow::Epub3ToEpub2()
{
    QString epubversion = m_Book->GetConstOPF()->GetEpubVersion();
    if (epubversion.startsWith("2")) {
        QMessageBox::warning(this, tr("Sigil"),
            tr("This Epub is already the version 2.0 !"), QMessageBox::Ok);
        return;
    }
    if (!StandardizeEpub()) return;
    GenerateNCXGuideFromNav();
    EpubVersionConv* evc = new EpubVersionConv(m_Book);
    evc->convert_to_epub2();
    m_TableOfContents->SetBook(m_Book); // set the TOCModel's m_EpubVersion to 2.0
    ResourcesAddedOrDeletedOrMoved(); // Change the main window's title to show 2.0 version
    m_BookBrowser->Refresh();
}

//-----modified: Epub2ToEpub3------
void MainWindow::Epub2ToEpub3()
{
    QString epubversion = m_Book->GetConstOPF()->GetEpubVersion();
    if (epubversion.startsWith("3")) {
        QMessageBox::warning(this, tr("Sigil"),
            tr("This Epub is already the version 3.0 !"), QMessageBox::Ok);
        return;
    }
    if (!StandardizeEpub()) return;
    EpubVersionConv* evc = new EpubVersionConv(m_Book);
    evc->convert_to_epub3();
    RemoveNCXGuideFromEpub3();
    m_TableOfContents->SetBook(m_Book); // set the TOCModel's m_EpubVersion to 3.0
    ResourcesAddedOrDeletedOrMoved(); // Change the main window's title to show 3.0 version
    m_BookBrowser->Refresh();
}

bool MainWindow::NormalizeEpubStructure()
{
    SaveTabData();

    QMessageBox::StandardButton button_pressed;
    button_pressed = Utility::warning(
        this,
        tr("Sigil-Enhanced"),
        tr("Normalize this EPUB structure?\n\n"
           "This will repair OPF manifest issues, correct internal link path casing, "
           "and move resources to Sigil's standard folder layout."),
        QMessageBox::Ok | QMessageBox::Cancel);
    if (button_pressed != QMessageBox::Ok) {
        return false;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    if (!CanStandardizeEpubLayout(tr("Normalize EPUB structure"))) {
        QApplication::restoreOverrideCursor();
        return false;
    }

    BuiltinPlugins::EpubStructureNormalizer normalizer(m_Book.data());
    BuiltinPlugins::EpubStructureNormalizer::Result result = normalizer.normalize();
    QHash<Resource*, QString> paths_before_layout = CaptureResourcePaths(m_Book.data());
    bool layout_modified = ApplyStandardEpubLayout();
    result.validationResults = RebaseValidationResultPaths(
        result.validationResults,
        BuildResourcePathMap(m_Book.data(), paths_before_layout));
    QApplication::restoreOverrideCursor();

    if (result.bookBrowserRefreshRequired && !layout_modified) {
        m_BookBrowser->Refresh();
    }
    if (result.modified && !layout_modified) {
        m_Book->SetModified();
    }

    bool modified = result.modified || layout_modified;
    m_ValidationResultsView->LoadResults(result.validationResults);
    ShowMessageOnStatusBar(modified ?
                           tr("EPUB structure normalization completed.") :
                           tr("No EPUB structure changes needed."));
    return true;
}

//modified: insertFileToEditor
void MainWindow::InsertFileFromBookBrowser()
{
    Resource* res = m_BookBrowser->GetCurrentResource();

    ContentTab* tab = GetCurrentContentTab();
    if (!res || !tab) {
        QMessageBox::warning(this, tr("Sigil"), tr("You cannot insert a file at this position."));
        return;
    }

    Resource* tab_res = tab->GetLoadedResource();
    ResourceInsertion::Context context;
    if (!ResourceInsertion::ContextFromTargetResource(tab_res, context) ||
        !ResourceInsertion::CanInsertResource(res, context)) {
        QMessageBox::warning(this, tr("Sigil"), tr("You cannot insert a file at this position."));
        return;
    }

    QString insert_text = ResourceInsertion::TextForResource(res, tab_res, context);
    if (insert_text.isEmpty()) {
        QMessageBox::warning(this, tr("Sigil"), tr("You cannot insert a file at this position."));
        return;
    }

    if (context == ResourceInsertion::Context::CSS) {
        CSSTab* csstab = qobject_cast<CSSTab*>(tab);
        if (csstab) {
            csstab->InsertFile(insert_text);
            return;
        }
    } else {
        FlowTab* flowtab = qobject_cast<FlowTab*>(tab);
        if (flowtab && flowtab->InsertFileEnabled()) {
            flowtab->InsertFile(insert_text);
            return;
        }
    }

    QMessageBox::warning(this, tr("Sigil"), tr("You cannot insert a file at this position."));
}

//modified: Add Lables On Multiple Lines
void MainWindow::ApplyHeadingStyleToTab_Plus(QAction* act)
{
    FlowTab* flow_tab = GetCurrentFlowTab();

    QString heading_type;
    QString name = act->objectName();
    if (name == "actionHeadingNormal") {
        heading_type = "Normal";
    }
    else if (name == "actionHeadingDivision") {
        heading_type = "Division";
    }
    else {
        heading_type = name[name.length() - 1];
    }

    if (flow_tab) {
        flow_tab->HeadingStylePlus(heading_type, m_preserveHeadingAttributes);
    }
}

//modified: FindReplacePlus
MainWindow::FindReplaceMode MainWindow::GetFindReplaceMode()
{
    return m_findReplaceMode;
}

//modified: FindReplacePlus
QList<SearchEditorModelPlus::searchEntry*> MainWindow::SearchEditorGetCurrentEntriesPlus()
{
    return m_SearchEditorPlus->GetCurrentEntries();
}

//modified: FindReplacePlus
void MainWindow::SearchEditorRecordEntryAsCompletedPlus(SearchEditorModelPlus::searchEntry* entry)
{
    m_SearchEditorPlus->RecordEntryAsCompleted(entry);
}

//modified: FindReplacePlus
void MainWindow::changeFindReplaceMode()
{
    m_findReplaceMode = m_findReplaceMode == EnhancedMode ? OriginalMode : EnhancedMode;

    if (m_findReplaceMode == FindReplaceMode::EnhancedMode) {
        bool isShowed = m_FindReplace->isVisible();
        m_FindReplace->HideFindReplace();
        if (isShowed)
            m_FindReplacePlus->show();
        delete m_SearchEditor;
        m_SearchEditorPlus = new SearchEditorPlus(this);
        ConnectSignalsToSearchEditor();
        ConnectSignalsToFindReplace();
    }
    else if (m_findReplaceMode == FindReplaceMode::OriginalMode) {
        bool isShowed = m_FindReplacePlus->isVisible();
        m_FindReplacePlus->HideFindReplace();
        if (isShowed)
            m_FindReplace->show();
        delete m_SearchEditorPlus;
        m_SearchEditor = new SearchEditor(this);
        ConnectSignalsToSearchEditor();
        ConnectSignalsToFindReplace();
    }
}

//modified: SavedSearchPlus
void MainWindow::SearchEditorDialogPlus(SearchEditorModelPlus::searchEntry* search_entry)
{
    // non-modal dialog
    m_SearchEditorPlus->show();
    m_SearchEditorPlus->raise();
    m_SearchEditorPlus->activateWindow();

    if (search_entry) {
        m_SearchEditorPlus->AddEntry(search_entry->is_group, search_entry, false);
    }
}

void MainWindow::ConnectSignalsToSearchEditor()
{
    QObject* findReplace;
    QObject* searchEditor;
    if (m_findReplaceMode == FindReplaceMode::EnhancedMode) {
        findReplace = qobject_cast<QObject*>(m_FindReplacePlus);
        searchEditor = qobject_cast<QObject*>(m_SearchEditorPlus);
        connect(findReplace,SIGNAL(AskWhyGetEmptyEntries()), m_SearchEditorPlus,SLOT(WhyEntriesEmpty()));
        disconnect(ui.actionSearchEditor, SIGNAL(triggered()), this, SLOT(SearchEditorDialog()));
        connect(ui.actionSearchEditor, SIGNAL(triggered()), this, SLOT(SearchEditorDialogPlus()));
        connect(m_SearchEditorPlus, SIGNAL(LoadSelectedSearchRequest(SearchEditorModelPlus::searchEntry*)),
                m_FindReplacePlus, SLOT(LoadSearch(SearchEditorModelPlus::searchEntry*)));
    } else {
        findReplace = qobject_cast<QObject*>(m_FindReplace);
        searchEditor = qobject_cast<QObject*>(m_SearchEditor);
        connect(m_SearchEditor, SIGNAL(CountsReportCountRequest(SearchEditorModel::searchEntry*, int&)),
                findReplace, SLOT(CountsReportCount(SearchEditorModel::searchEntry*, int&)));
        disconnect(ui.actionSearchEditor, SIGNAL(triggered()), this, SLOT(SearchEditorDialogPlus()));
        connect(ui.actionSearchEditor, SIGNAL(triggered()), this, SLOT(SearchEditorDialog()));
        connect(m_SearchEditor, SIGNAL(LoadSelectedSearchRequest(SearchEditorModel::searchEntry*)),
                m_FindReplace, SLOT(LoadSearch(SearchEditorModel::searchEntry*)));
    }
    connect(findReplace, SIGNAL(ShowMessageRequest(const QString&)), searchEditor, SLOT(ShowMessage(const QString&)));
    connect(searchEditor, SIGNAL(ShowStatusMessageRequest(const QString&)), this, SLOT(ShowMessageOnStatusBar(const QString&)));
    connect(searchEditor, SIGNAL(FindSelectedSearchRequest()), findReplace, SLOT(FindSearch()));
    connect(searchEditor, SIGNAL(ReplaceCurrentSelectedSearchRequest()), findReplace, SLOT(ReplaceCurrentSearch()));
    connect(searchEditor, SIGNAL(ReplaceSelectedSearchRequest()), findReplace, SLOT(ReplaceSearch()));
    connect(searchEditor, SIGNAL(CountAllSelectedSearchRequest()), findReplace, SLOT(CountAllSearch()));
    connect(searchEditor, SIGNAL(ReplaceAllSelectedSearchRequest()), findReplace, SLOT(ReplaceAllSearch()));
    connect(searchEditor, SIGNAL(RestartSearch()), findReplace, SLOT(DoRestart()));

}

void MainWindow::ConnectSignalsToFindReplace()
{
    QObject* findReplace;
    QObject* hiddenFindReplace;
    if (m_findReplaceMode == FindReplaceMode::EnhancedMode) {
        findReplace = qobject_cast<QObject*>(m_FindReplacePlus);
        hiddenFindReplace = qobject_cast<QObject*>(m_FindReplace);
        connect(this, SIGNAL(UpdateSearchStateRequest()), m_FindReplacePlus, SLOT(DoRestart()));
        connect(m_FindReplacePlus, SIGNAL(OpenSearchEditorRequest(SearchEditorModelPlus::searchEntry*)),
                this, SLOT(SearchEditorDialogPlus(SearchEditorModelPlus::searchEntry*)));
        disconnect(m_FindReplace, SIGNAL(OpenSearchEditorRequest(SearchEditorModel::searchEntry*)),
                this, SLOT(SearchEditorDialog(SearchEditorModel::searchEntry*)));
    }
    else {
        findReplace = qobject_cast<QObject*>(m_FindReplace);
        hiddenFindReplace = qobject_cast<QObject*>(m_FindReplacePlus);
        disconnect(this, SIGNAL(UpdateSearchStateRequest()), m_FindReplacePlus, SLOT(DoRestart()));
        disconnect(m_FindReplacePlus, SIGNAL(OpenSearchEditorRequest(SearchEditorModelPlus::searchEntry*)),
                   this, SLOT(SearchEditorDialogPlus(SearchEditorModelPlus::searchEntry*)));
        connect(m_FindReplace, SIGNAL(OpenSearchEditorRequest(SearchEditorModel::searchEntry*)),
                   this, SLOT(SearchEditorDialog(SearchEditorModel::searchEntry*)));
    }

    disconnect(ui.actionFind, SIGNAL(triggered()), this, SLOT(Find()));
    disconnect(ui.actionFindNext, SIGNAL(triggered()), hiddenFindReplace, SLOT(DoFindNext()));
    disconnect(ui.actionFindPrevious, SIGNAL(triggered()), hiddenFindReplace, SLOT(DoFindPrevious()));
    disconnect(ui.actionReplaceNext, SIGNAL(triggered()), hiddenFindReplace, SLOT(DoReplaceNext()));
    disconnect(ui.actionReplacePrevious, SIGNAL(triggered()), hiddenFindReplace, SLOT(DoReplacePrevious()));
    disconnect(ui.actionReplaceCurrent, SIGNAL(triggered()), hiddenFindReplace, SLOT(ReplaceCurrent()));
    disconnect(ui.actionReplaceAll, SIGNAL(triggered()), hiddenFindReplace, SLOT(ReplaceAll()));
    disconnect(ui.actionCount, SIGNAL(triggered()), hiddenFindReplace, SLOT(Count()));
    disconnect(ui.actionDryRun, SIGNAL(triggered()), hiddenFindReplace, SLOT(PerformDryRunReplace()));
    disconnect(ui.actionFilterReplaceAll, SIGNAL(triggered()), hiddenFindReplace, SLOT(ChooseReplacements()));
    disconnect(ui.actionFindNextInFile, SIGNAL(triggered()), hiddenFindReplace, SLOT(FindNextInFile()));
    disconnect(ui.actionReplaceNextInFile, SIGNAL(triggered()), hiddenFindReplace, SLOT(ReplaceNextInFile()));
    disconnect(ui.actionReplaceAllInFile, SIGNAL(triggered()), hiddenFindReplace, SLOT(ReplaceAllInFile()));
    disconnect(ui.actionCountInFile, SIGNAL(triggered()), hiddenFindReplace, SLOT(CountInFile()));
    disconnect(hiddenFindReplace, SIGNAL(FROpenFileRequest(QString, int, int)), this, SLOT(OpenFile(QString, int, int)));
    disconnect(hiddenFindReplace, SIGNAL(ClipboardSaveRequest()), m_ClipboardHistorySelector, SLOT(SaveClipboardState()));
    disconnect(hiddenFindReplace, SIGNAL(ClipboardRestoreRequest()), m_ClipboardHistorySelector, SLOT(RestoreClipboardState()));

    connect(ui.actionFind, SIGNAL(triggered()), this, SLOT(Find()));
    connect(ui.actionFindNext, SIGNAL(triggered()), findReplace, SLOT(DoFindNext()));
    connect(ui.actionFindPrevious, SIGNAL(triggered()), findReplace, SLOT(DoFindPrevious()));
    connect(ui.actionReplaceNext, SIGNAL(triggered()), findReplace, SLOT(DoReplaceNext()));
    connect(ui.actionReplacePrevious, SIGNAL(triggered()), findReplace, SLOT(DoReplacePrevious()));
    connect(ui.actionReplaceCurrent, SIGNAL(triggered()), findReplace, SLOT(ReplaceCurrent()));
    connect(ui.actionReplaceAll, SIGNAL(triggered()), findReplace, SLOT(ReplaceAll()));
    connect(ui.actionCount, SIGNAL(triggered()), findReplace, SLOT(Count()));
    connect(ui.actionDryRun, SIGNAL(triggered()), findReplace, SLOT(PerformDryRunReplace()));
    connect(ui.actionFilterReplaceAll, SIGNAL(triggered()), findReplace, SLOT(ChooseReplacements()));
    connect(ui.actionFindNextInFile, SIGNAL(triggered()), findReplace, SLOT(FindNextInFile()));
    connect(ui.actionReplaceNextInFile, SIGNAL(triggered()), findReplace, SLOT(ReplaceNextInFile()));
    connect(ui.actionReplaceAllInFile, SIGNAL(triggered()), findReplace, SLOT(ReplaceAllInFile()));
    connect(ui.actionCountInFile, SIGNAL(triggered()), findReplace, SLOT(CountInFile()));
    connect(findReplace, SIGNAL(FROpenFileRequest(QString, int, int)), this, SLOT(OpenFile(QString, int, int)));
    connect(findReplace, SIGNAL(ClipboardSaveRequest()), m_ClipboardHistorySelector, SLOT(SaveClipboardState()));
    connect(findReplace, SIGNAL(ClipboardRestoreRequest()), m_ClipboardHistorySelector, SLOT(RestoreClipboardState()));
}
