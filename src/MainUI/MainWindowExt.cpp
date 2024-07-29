#include <QMessageBox>

#include "MainUI/MainWindow.h"
#include "MainUI/BookBrowser.h"
#include "MainUI/TableOfContents.h"
#include "Tabs/ContentTab.h"
#include "Tabs/FlowTab.h"
#include "Tabs/CSSTab.h"
#include "BookManipulation/EpubVersionConv.h" // modified: Epub3ToEpub2 Epub2ToEpub3

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

//modified: insertFileToEditor
void MainWindow::InsertFileFromBookBrowser()
{
    Resource* res = m_BookBrowser->GetCurrentResource();

    ContentTab* tab = GetCurrentContentTab();
    Resource* tab_res = tab->GetLoadedResource();
    bool insert_allowed = false;
    if (tab_res->Type() == Resource::HTMLResourceType) {
        FlowTab* flowtab = qobject_cast<FlowTab*>(tab);
        if (res->Type() & (Resource::ImageResourceType | Resource::SVGResourceType |
                           Resource::VideoResourceType | Resource::AudioResourceType))
        {
            insert_allowed = flowtab->InsertFileEnabled();
        }
    }
    else if (tab_res->Type() == Resource::CSSResourceType) {
        if (res->Type() & (Resource::ImageResourceType | Resource::SVGResourceType | Resource::FontResourceType))
            insert_allowed = true;
    }
    if (insert_allowed) {
        QString relative_path = res->GetRelativePathFromResource(tab_res);
        relative_path = Utility::URLEncodePath(relative_path);

        if (tab_res->Type() == Resource::CSSResourceType) {
            QString url = QString("url(\"%1\")").arg(relative_path);
            CSSTab* csstab = qobject_cast<CSSTab*>(tab);
            csstab->InsertFile(url);
            return;
        }
        else if (tab_res->Type() == Resource::HTMLResourceType) {
            QString filename = res->Filename();
            if (filename.contains(".")) {
                filename = filename.left(filename.lastIndexOf("."));
            }
            QString node;
            if (res->Type() == Resource::ImageResourceType || res->Type() == Resource::SVGResourceType) {
                node = QString("<img alt=\"%1\" src=\"%2\"/>").arg(filename).arg(relative_path);
            }
            else if (res->Type() == Resource::VideoResourceType) {
                node = QString("<video controls=\"controls\" src=\"%1\">%2</video>").arg(relative_path).arg(filename);
            }
            else if (res->Type() == Resource::AudioResourceType) {
                node = QString("<audio controls=\"controls\" src=\"%1\">%2</audio>").arg(relative_path).arg(filename);
            }
            FlowTab* flowtab = qobject_cast<FlowTab*>(tab);
            flowtab->InsertFile(node);
            return;
        }
    }
    else {
        QMessageBox::warning(this, tr("Sigil"), tr("You cannot insert a file at this position."));
    }
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
void MainWindow::changeFindReplaceMode()
{
    m_findReplaceMode = m_findReplaceMode == EnhancedMode ? OriginalMode : EnhancedMode;

    if (m_findReplaceMode == FindReplaceMode::EnhancedMode) {
        bool isShowed = m_FindReplace->isVisible();
        m_FindReplace->HideFindReplace();
        if (isShowed)
            m_FindReplacePlus->show();
        delete m_SearchEditor;
        m_SearchEditor = new SearchEditor(this, true);
        ConnectSignalsToSearchEditor();
        ConnectActionSignalsToFindReplace();
    }
    else if (m_findReplaceMode == FindReplaceMode::OriginalMode) {
        bool isShowed = m_FindReplacePlus->isVisible();
        m_FindReplacePlus->HideFindReplace();
        if (isShowed)
            m_FindReplace->show();
        delete m_SearchEditor;
        m_SearchEditor = new SearchEditor(this, false);
        ConnectSignalsToSearchEditor();
        ConnectActionSignalsToFindReplace();
    }
}

void MainWindow::ConnectSignalsToSearchEditor()
{
    QObject* findReplace;
    if (m_findReplaceMode == FindReplaceMode::EnhancedMode) {
        findReplace = qobject_cast<QObject*>(m_FindReplacePlus);
        connect(findReplace,SIGNAL(AskWhyGetEmptyEntries()), m_SearchEditor,SLOT(WhyEntriesEmpty()));
    } else {
        findReplace = qobject_cast<QObject*>(m_FindReplace);
    }
    connect(findReplace, SIGNAL(ShowMessageRequest(const QString&)), m_SearchEditor, SLOT(ShowMessage(const QString&)));
    connect(m_SearchEditor, SIGNAL(FindSelectedSearchRequest()), findReplace, SLOT(FindSearch()));
    connect(m_SearchEditor, SIGNAL(ReplaceCurrentSelectedSearchRequest()), findReplace, SLOT(ReplaceCurrentSearch()));
    connect(m_SearchEditor, SIGNAL(ReplaceSelectedSearchRequest()), findReplace, SLOT(ReplaceSearch()));
    connect(m_SearchEditor, SIGNAL(CountAllSelectedSearchRequest()), findReplace, SLOT(CountAllSearch()));
    connect(m_SearchEditor, SIGNAL(ReplaceAllSelectedSearchRequest()), findReplace, SLOT(ReplaceAllSearch()));
    connect(m_SearchEditor, SIGNAL(LoadSelectedSearchRequest(SearchEditorModel::searchEntry*)),
        findReplace, SLOT(LoadSearch(SearchEditorModel::searchEntry*)));
    connect(m_SearchEditor, SIGNAL(RestartSearch()), findReplace, SLOT(DoRestart()));
    connect(m_SearchEditor, SIGNAL(CountsReportCountRequest(SearchEditorModel::searchEntry*, int&)),
        findReplace, SLOT(CountsReportCount(SearchEditorModel::searchEntry*, int&)));
}

void MainWindow::ConnectActionSignalsToFindReplace()
{
    QObject* findReplace;
    QObject* hiddenFindReplace;
    if (m_findReplaceMode == FindReplaceMode::EnhancedMode) {
        findReplace = qobject_cast<QObject*>(m_FindReplacePlus);
        hiddenFindReplace = qobject_cast<QObject*>(m_FindReplace);
        connect(this, SIGNAL(UpdateSearchStateRequest()), findReplace, SLOT(DoRestart()));
    }
    else {
        findReplace = qobject_cast<QObject*>(m_FindReplace);
        hiddenFindReplace = qobject_cast<QObject*>(m_FindReplacePlus);
        disconnect(this, SIGNAL(UpdateSearchStateRequest()), hiddenFindReplace, SLOT(DoRestart()));
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
}
