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
//---------------------------------

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
//---------------------------------

//------------------ modified: insertFileToEditor -------------------
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
//-------------------------------------------------------------------

//-------------- modified: Add Lables On Multiple Lines ------------
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
//------------------------------------------------------------------------------