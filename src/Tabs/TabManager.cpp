/************************************************************************
**
**  Copyright (C) 2015-2024 Kevin B. Hendricks, Stratford Ontario Canada
**  Copyright (C) 2009-2011  Strahinja Markovic  <strahinja.markovic@gmail.com>
**
**  This file is part of Sigil.
**
**  Sigil is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  (at your option) any later version.
**
**  Sigil is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with Sigil.  If not, see <http://www.gnu.org/licenses/>.
**
*************************************************************************/

#include <QPalette>
#include <QApplication>
#include <QDebug>
#include <QVBoxLayout>
#include "BookManipulation/CleanSource.h"
#include "ResourceObjects/Resource.h"
#include "ResourceObjects/CSSResource.h"
#include "ResourceObjects/OPFResource.h"
#include "ResourceObjects/NCXResource.h"
#include "ResourceObjects/HTMLResource.h"
#include "ResourceObjects/ImageResource.h"
#include "ResourceObjects/MiscTextResource.h"
#include "ResourceObjects/XMLResource.h"
#include "ResourceObjects/SVGResource.h"
#include "ResourceObjects/PdfResource.h"
#include "Tabs/PdfTab.h"
#include "Tabs/AVTab.h"
#include "Tabs/FontTab.h"
#include "Tabs/CSSTab.h"
#include "Tabs/FlowTab.h"
#include "Tabs/ImageTab.h"
#include "Tabs/MiscTextTab.h"
#include "Tabs/XMLTab.h"
#include "Tabs/SVGTab.h"
#include "Tabs/NCXTab.h"
#include "Tabs/OPFTab.h"
#include "Tabs/TabManager.h"
#include "Tabs/TabGroup.h"
#include "Tabs/WellFormedContent.h"


TabManager::TabManager(QWidget *parent)
    :
    QWidget(parent),
    m_Primary(new TabGroup(this)),
    m_LastContentTab(NULL),
    m_TabsToDelete(QList<ContentTab*>()),
    m_tabs_deletion_in_use(false),
    m_newTab(NULL)
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_Primary, 1);

    connect(m_Primary, SIGNAL(TabBarClicked()),            this, SLOT(SetFocusInTab()));
    connect(m_Primary, SIGNAL(CloseOtherTabsRequest(int)), this, SLOT(CloseOtherTabs(int)));
    connect(m_Primary, SIGNAL(currentChanged(int)),        this, SLOT(EmitTabChanged(int)));
    connect(m_Primary, SIGNAL(tabCloseRequested(int)),     this, SLOT(CloseTab(int)));
    connect(m_Primary, SIGNAL(TabInserted()),              this, SIGNAL(TabCountChanged()));
}


ContentTab *TabManager::GetCurrentContentTab()
{
    // TODO: turn on this assert after you make sure a tab
    // is created before this is called in MainWindow constructor
    //Q_ASSERT( widget != NULL );
    return m_Primary->CurrentTab();
}

QList<ContentTab *> TabManager::GetContentTabs()
{
    return m_Primary->Tabs();
}

QList<Resource *> TabManager::GetTabResources()
{
    QList <ContentTab *> tabs = GetContentTabs();
    QList <Resource *> tab_resources;
    foreach(ContentTab *tab, tabs) {
        tab_resources.append(tab->GetLoadedResource());
    }
    return tab_resources;
}

QList<Resource *> TabManager::GetTabResourcesOfType(Resource::ResourceType resource_type)
{
    QList <ContentTab *> tabs = GetContentTabs();
    QList <Resource *> tab_resources;
    foreach(ContentTab *tab, tabs) {
        Resource* resource = tab->GetLoadedResource();
        if (resource->Type() == resource_type) {
            tab_resources.append(resource);
        }
    }
    return tab_resources;
}

int TabManager::GetTabCount()
{
    return m_Primary->TabCount();
}

void TabManager::CloseAllTabs(bool all)
{
    while (m_Primary->TabCount() > 0) {
        CloseTab(0, all);
    }
}

void TabManager::CloseTabForResource(const Resource *resource, bool force)
{
    int index = ResourceTabIndex(resource);

    if (index != -1) {
        CloseTab(index, force);
    }
}

bool TabManager::IsAllTabDataWellFormed()
{
    QList<Resource *> resources = GetTabResources();
    foreach(Resource *resource, resources) {
        int index = ResourceTabIndex(resource);
        WellFormedContent *content = m_Primary->WellFormedAt(index);

        // Only check Xhtml for now.
        if (content && resource->Type() == Resource::HTMLResourceType) {
            if (!content->IsDataWellFormed()) {
                return false;
            }
        }
    }
    return true;
}

void TabManager::ReloadTabDataForResources(const QList<Resource *> &resources)
{
    foreach(Resource *resource, resources) {
        int index = ResourceTabIndex(resource);

        if (index != -1) {
            FlowTab *flow_tab = qobject_cast<FlowTab *>(m_Primary->TabAt(index));

            if (flow_tab) {
                flow_tab->LoadTabContent();
            }
        }
    }
}

void TabManager::ReopenTabs()
{
    ContentTab *currentTab = GetCurrentContentTab();
    QList<Resource *> resources = GetTabResources();
    foreach(Resource *resource, resources) {
        CloseTabForResource(resource, true);
        OpenResource(resource, -1, -1, QString());
    }
    OpenResource(currentTab->GetLoadedResource(), -1, -1, QString());
}

void TabManager::PerformThemeChangeRefresh()
{
    foreach(ContentTab *tab, m_Primary->Tabs()) {
        if (tab) {
            tab->ThemeChangeRefresh();
        }
    }
}

void TabManager::SaveTabData()
{
    foreach(ContentTab *tab, m_Primary->Tabs()) {
        if (tab) {
            tab->SaveTabContent();
        }
    }
}

void TabManager::LinkClicked(const QUrl &url)
{
    QString url_string = url.toString();

    if (url.toString().isEmpty()) {
        return;
    }
    
    ContentTab *tab = GetCurrentContentTab();

    if (url.isRelative()) {

        // we have a relative url, so build an internal
        // book: scheme url book:///bookpath#fragment
        QString attpath = url.path();
        QString fragment = "";
        if (url.hasFragment()) {
            fragment = url.fragment();
        }
        QString dest_bookpath;
        if (attpath.isEmpty()) {
            dest_bookpath = tab->GetLoadedResource()->GetRelativePath();
        } else {
            QString startdir = tab->GetLoadedResource()->GetFolder();
            dest_bookpath = Utility::buildBookPath(attpath, startdir);
        }
        url_string = "book:///" + Utility::buildRelativeHREF(dest_bookpath, fragment);
    // QUrl will take care of encoding the url path
    } else {
        // we have a scheme and are absolute
        if (url.scheme() == "file") {
            if (url_string.contains("/#")) {
                url_string.insert(url_string.indexOf("/#") + 1, tab->GetFilename());
            }
        }
    }

    emit OpenUrlRequest(QUrl(url_string));
}

void TabManager::OpenResource(Resource *resource,
                              int line_to_scroll_to,
                              int position_to_scroll_to,
                              const QString &caret_location_to_scroll_to,
                              const QUrl &fragment,
                              bool precede_current_tab)
{
    if (SwitchedToExistingTab(resource, line_to_scroll_to, position_to_scroll_to, caret_location_to_scroll_to, fragment)) {
        return;
    }

    bool grab_focus = !precede_current_tab;
    ContentTab *new_tab = CreateTabForResource(resource, line_to_scroll_to, position_to_scroll_to,
                          caret_location_to_scroll_to, fragment, grab_focus);

    if (new_tab) {
        // only set m_newTab if going to be current tab (ie focus was grabbed)
        // otherwise after injection of new tab preceding_current_tab will prevent
        // proper updating of preview
        if (grab_focus) {
            m_newTab = new_tab;
        }
        AddNewContentTab(new_tab, precede_current_tab);
        emit ShowStatusMessageRequest("");
    } else {
        QString message = tr("Cannot edit file") + ": " + resource->ShortPathName();
        emit ShowStatusMessageRequest(message);
    }

    // do not Scroll the Preview in response as new Flow Tabs have
    // delayed initialization.  Instead FlowTab itself will handle this
}


void TabManager::NextTab()
{
    m_Primary->NextTab();
}


void TabManager::PreviousTab()
{
    m_Primary->PreviousTab();
}


void TabManager::RemoveTab()
{
    // Can leave window with no tabs, so re-open a tab asap
    m_Primary->RemoveCurrentTabWidget();
}


void TabManager::CloseTab()
{
    CloseTab(m_Primary->currentIndex());
}

void TabManager::CloseOtherTabs()
{
    CloseOtherTabs(m_Primary->currentIndex());
}

void TabManager::CloseOtherTabs(int index)
{
    if (m_Primary->TabCount() <= 1 || index < 0 || index >= m_Primary->TabCount()) {
        return;
    }

    int max_index = m_Primary->TabCount() - 1;

    // Close all tabs after the tab
    for (int i = index + 1; i <= max_index; i++) {
        CloseTab(index + 1);
    }

    // Close all tabs before the tab
    for (int i = 0; i < index; i++) {
        CloseTab(0);
    }
}


void TabManager::MakeCentralTab(ContentTab *tab)
{
    Q_ASSERT(tab);
    m_Primary->ActivateTab(tab);
}

// Note: m_LastContentTab was previously declared as follows:
//
//     QPointer<ContentTab> m_LastContentTab;
//
// instead of just a simple:
//
//     ContentTab * m_LastContentTab;
//
// but this caused many issues with fast deleting and updating of 
// this special pointer.
//
// This pointer only ever records the last curent ContentTab and it is 
// never shared outside the TabManager

void TabManager::EmitTabChanged(int new_index)
{
    Q_UNUSED(new_index);
    ContentTab *current_tab = m_Primary->CurrentTab();
    // the result of the qobject_cast can be NULL and that is okay
    if (m_LastContentTab != current_tab) {
        // qDebug() << "Emitting TabChanged Signal";
        ContentTab * prev_tab = m_LastContentTab;
        m_LastContentTab = current_tab;
        emit TabChanged(prev_tab, current_tab);
        if (current_tab != m_newTab) {
            // qDebug() << "Emitting UpdatePreviewAfterExistingTabSwitch";
            emit UpdatePreviewAfterExistingTabSwitch();
        }
    }
    m_newTab = NULL;
}


void TabManager::DeleteTab(ContentTab *tab_to_delete)
{

    if (!m_TabsToDelete.contains(tab_to_delete)) {
        m_TabsToDelete.prepend(tab_to_delete);
    }
    if (m_tabs_deletion_in_use) return;
    m_tabs_deletion_in_use = true;

    // qDebug() << "entering DeleteTab";

    // Important: This routine appears to be re-entered somehow
    // due to processEvents causing control to leave and return
    // *before* this routine can itself return so multiple
    // delete tab requests are being processed at the same time!

    // here is an actual sample debug output form deleting
    // tabs as fast as possible using Ctrl-W

      // Debug: entering DeleteTab
      // Debug: in ChangesSignalWhenTabChanges  FlowTab(0x128c78b30) FlowTab(0x13034af30)
      // Debug: exiting  DeleteTab
      // Debug: entering DeleteTab
      // Debug: in ChangesSignalWhenTabChanges  FlowTab(0x13034af30) FlowTab(0x119595e60)
      // Debug: exiting  DeleteTab

      // Debug: entering DeleteTab
      // Debug: in ChangesSignalWhenTabChanges  FlowTab(0x119595e60) FlowTab(0x115207f60)
      // Debug: entering DeleteTab

      // Debug: in ChangesSignalWhenTabChanges  FlowTab(0x115207f60) FlowTab(0x11958a620)
      // Debug: exiting  DeleteTab
      // Debug: entering DeleteTab
      // Debug: in ChangesSignalWhenTabChanges  FlowTab(0x11958a620) FlowTab(0x126c86e80)
      // Debug: exiting  DeleteTab
      // Debug: entering DeleteTab
      // Debug: in ChangesSignalWhenTabChanges  FlowTab(0x126c86e80) FlowTab(0x1195daaf0)
      // Debug: exiting  DeleteTab
      // Debug: entering DeleteTab
      // Debug: in ChangesSignalWhenTabChanges  FlowTab(0x1195daaf0) FlowTab(0x128c717d0)
      // Debug: exiting  DeleteTab
      // Debug: entering DeleteTab
      // Debug: in ChangesSignalWhenTabChanges  FlowTab(0x128c717d0) FlowTab(0x1291c9150)
      // Debug: exiting  DeleteTab
      // Debug: entering DeleteTab
      // Debug: in ChangesSignalWhenTabChanges  FlowTab(0x1291c9150) FlowTab(0x11955d210)
      // Debug: exiting  DeleteTab
      // Debug: exiting  DeleteTab
      // Debug: entering DeleteTab
      // Debug: in ChangesSignalWhenTabChanges  FlowTab(0x11955d210) FlowTab(0x128d01540)
      // Debug: entering DeleteTab
      // Debug: in ChangesSignalWhenTabChanges  FlowTab(0x128d01540) FlowTab(0x1094240b0)
      // Debug: exiting  DeleteTab
      // Debug: entering DeleteTab
      // Debug: in ChangesSignalWhenTabChanges  FlowTab(0x1094240b0) FlowTab(0x109763800)
      // Debug: exiting  DeleteTab
      // Debug: entering DeleteTab
      // Debug: in ChangesSignalWhenTabChanges  FlowTab(0x109763800) FlowTab(0x119ef3a50)
      // Debug: exiting  DeleteTab
      // Debug: exiting  DeleteTab


    while(!m_TabsToDelete.isEmpty()) {
    ContentTab *tab = m_TabsToDelete.takeLast();

        Q_ASSERT(tab);

        // to prevent segfaults, disconnect and reconnect the currentChanged()
        // signal and invoke EmitTabChanged() manually after QTabBar::removeTab(int) 
        // completes because QTabBar::setCurrentIndex(int) **somehow** invokes processEvents()
        // ***BEFORE*** properly setting the current index
        // this helps to prevent reentrancy.
        disconnect(m_Primary, SIGNAL(currentChanged(int)), this, SLOT(EmitTabChanged(int)));
        m_Primary->TakeTab(tab);
        connect(m_Primary, SIGNAL(currentChanged(int)), this, SLOT(EmitTabChanged(int)));

        // Only the current tab is ever connected to the main ui
        // so do our own version of EmitTabChanged() only if needed
        // to disconnect and reconnect ui signals
        ContentTab *next_tab = m_Primary->CurrentTab();
        if (m_LastContentTab != next_tab) {
            // move updating of m_LastContentTab to be upfront *before* emitting the signal
            ContentTab* prevtab = m_LastContentTab;
            m_LastContentTab = next_tab;
            // flow control is lost in following line
            emit TabChanged(prevtab,  next_tab);
            emit UpdatePreviewAfterExistingTabSwitch();
        }
        tab->deleteLater();
        m_tabs_deletion_in_use = !m_TabsToDelete.isEmpty();
    }
    // qDebug() << "exiting  DeleteTab";
}


void TabManager::CloseTab(int tab_index, bool force)
{
    if (m_Primary->TabCount() == 0) {
        return;
    }
    if (!force && m_Primary->TabCount() <= 1) {
        return;
    }

    ContentTab *tab = m_Primary->TabAt(tab_index);
    if (tab) tab->Close();
    emit TabCountChanged();
}


void TabManager::UpdateTabName(ContentTab *renamed_tab)
{
    Q_ASSERT(renamed_tab);
    m_Primary->UpdateTabName(renamed_tab);
}

void TabManager::SetFocusInTab()
{
    ContentTab *tab = GetCurrentContentTab();

    if (tab != NULL) {
        tab->setFocus();
    }
}


WellFormedContent *TabManager::GetWellFormedContent(int index)
{
    return m_Primary->WellFormedAt(index);
}



// Returns the index of the tab the index is loaded in, -1 if it isn't
int TabManager::ResourceTabIndex(const Resource *resource) const
{
    return m_Primary->ResourceTabIndex(resource);
}


bool TabManager::SwitchedToExistingTab(const Resource *resource,
                                       int line_to_scroll_to,
                                       int position_to_scroll_to,
                                       const QString &caret_location_to_scroll_to,
                                       const QUrl &fragment)
{
    int resource_index = ResourceTabIndex(resource);

    // If the resource is already opened in
    // some tab, then we just switch to it
    if (resource_index != -1) {
        // the next line will cause TabChanged to be emitted which will update Preview
        // but to whatever location this tab has now now after scrolling
        QWidget *tab = m_Primary->TabAt(resource_index);
        Q_ASSERT(tab);
        m_Primary->ActivateTab(qobject_cast<ContentTab *>(tab));

        FlowTab *flow_tab = qobject_cast<FlowTab *>(tab);

        if (flow_tab != NULL) {
            if (!caret_location_to_scroll_to.isEmpty()) {
                flow_tab->ScrollToCaretLocation(caret_location_to_scroll_to);
            } else if (position_to_scroll_to >= 0) {
                flow_tab->ScrollToPosition(position_to_scroll_to);
            } else if (!fragment.toString().isEmpty()) {
                flow_tab->ScrollToFragment(fragment.toString());
            } else if (line_to_scroll_to > 0) {
                flow_tab->ScrollToLine(line_to_scroll_to);
            }


            // manually update the Preview Location
            flow_tab->EmitScrollPreviewImmediately();

            return true;
        }

        TextTab *text_tab = qobject_cast<TextTab *>(tab);

        if (text_tab != NULL) {
            if (position_to_scroll_to >= 0) {
                text_tab->ScrollToPosition(position_to_scroll_to);
            } else {
                text_tab->ScrollToLine(line_to_scroll_to);
            }
            return true;
        }

        ImageTab *image_tab = qobject_cast<ImageTab *>(tab);

        if (image_tab != NULL) {
            return true;
        }

        AVTab *av_tab = qobject_cast<AVTab *>(tab);

        if (av_tab != NULL) {
            return true;
        }

        PdfTab *pdf_tab = qobject_cast<PdfTab *>(tab);

        if (pdf_tab != NULL) {
            return true;
        }

        FontTab *font_tab = qobject_cast<FontTab *>(tab);

        if (font_tab != NULL) {
            return true;
        }


    }

    return false;
}


ContentTab *TabManager::CreateTabForResource(Resource *resource,
        int line_to_scroll_to,
        int position_to_scroll_to,
        const QString &caret_location_to_scroll_to,
        const QUrl &fragment,
        bool grab_focus)
{
    ContentTab *tab = NULL;

    switch (resource->Type()) {
        case Resource::HTMLResourceType: {
            HTMLResource *html_resource = qobject_cast<HTMLResource *>(resource);
            if (!html_resource) {
                break;
            }
            tab = new FlowTab(html_resource,
                              fragment,
                              line_to_scroll_to,
                              position_to_scroll_to,
                              caret_location_to_scroll_to,
                              grab_focus,
                              m_Primary);
            connect(tab,  SIGNAL(LinkClicked(const QUrl &)), this, SLOT(LinkClicked(const QUrl &)));
            connect(tab,  SIGNAL(OldTabRequest(QString, HTMLResource *)),
                    this, SIGNAL(OldTabRequest(QString, HTMLResource *)));
            break;
        }

        case Resource::CSSResourceType: {
            tab = new CSSTab(qobject_cast<CSSResource *>(resource), line_to_scroll_to, position_to_scroll_to, m_Primary);
            break;
        }

        case Resource::ImageResourceType: {
            tab = new ImageTab(qobject_cast<ImageResource *>(resource), m_Primary);
            break;
        }

        case Resource::MiscTextResourceType: {
            tab = new MiscTextTab(qobject_cast<MiscTextResource *>(resource), line_to_scroll_to, position_to_scroll_to, m_Primary);
            break;
        }

        case Resource::SVGResourceType: {
            tab = new SVGTab(qobject_cast<SVGResource *>(resource), line_to_scroll_to, position_to_scroll_to, m_Primary);
            break;
        }

        case Resource::OPFResourceType: {
            tab = new OPFTab(qobject_cast<OPFResource *>(resource), line_to_scroll_to, position_to_scroll_to, m_Primary);
            break;
        }

        case Resource::NCXResourceType: {
            tab = new NCXTab(qobject_cast<NCXResource *>(resource), line_to_scroll_to, position_to_scroll_to, m_Primary);
            break;
        }

        case Resource::XMLResourceType: {
            tab = new XMLTab(qobject_cast<XMLResource *>(resource), line_to_scroll_to, position_to_scroll_to, m_Primary);
            break;
        }

        case Resource::TextResourceType: {
            tab = new TextTab(qobject_cast<TextResource *>(resource), CodeViewEditor::Highlight_NONE, 
                              line_to_scroll_to, position_to_scroll_to, m_Primary);
            break;
        }

        case Resource::AudioResourceType:
        case Resource::VideoResourceType: {
            tab = new AVTab(qobject_cast<Resource *>(resource), m_Primary);
            break;
        }

        case Resource::PdfResourceType: {
            tab = new PdfTab(qobject_cast<Resource *>(resource), m_Primary);
            break;
        }

        case Resource::FontResourceType: {
            tab = new FontTab(qobject_cast<Resource *>(resource), m_Primary);
            break;
        }

        default:
            break;
    }

    // Set whether to inform or auto correct well-formed errors.
    WellFormedContent *wtab = dynamic_cast<WellFormedContent *>(tab);

    if (wtab) {
        // In case of well-formed errors we want the tab to be focused.
        connect(tab,  SIGNAL(CentralTabRequest(ContentTab *)),
                this, SLOT(MakeCentralTab(ContentTab *)));    //, Qt::QueuedConnection );
    }

    return tab;
}


bool TabManager::AddNewContentTab(ContentTab *new_tab, bool precede_current_tab)
{
    if (new_tab == NULL) {
        return false;
    }

    // before adding a tab make sure m_LastContentTab has been
    // properly updated to reflect the current tab
    m_LastContentTab = m_Primary->CurrentTab();

    if (m_Primary->AddContentTab(new_tab, precede_current_tab) < 0) {
        return false;
    }

    connect(new_tab, SIGNAL(DeleteMe(ContentTab *)), this, SLOT(DeleteTab(ContentTab *)));
    connect(new_tab, SIGNAL(TabRenamed(ContentTab *)), this, SLOT(UpdateTabName(ContentTab *)));
    return true;
}

void TabManager::UpdateTabDisplay()
{
    foreach(ContentTab *tab, m_Primary->Tabs()) {
        if (tab) {
            tab->UpdateDisplay();
        }
    }
}

bool TabManager::CloseOPFTabIfOpen()
{
    QList<Resource *> resources = GetTabResources();
    foreach(Resource *resource, resources) {
        int index = ResourceTabIndex(resource);
        if (resource->Type() == Resource::OPFResourceType) {
            CloseTab(index);
            return true;
        }
    }
    return false;
}
