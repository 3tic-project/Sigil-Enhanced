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
#include <QSplitter>
#include <QLabel>
#include <QPair>
#include <QStackedLayout>
#include <QMimeData>
#include <QEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QTabBar>
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
#include "Tabs/TextTab.h"
#include "Tabs/FlowTab.h"
#include "Tabs/ImageTab.h"
#include "Tabs/MiscTextTab.h"
#include "Tabs/XMLTab.h"
#include "Tabs/SVGTab.h"
#include "Tabs/NCXTab.h"
#include "Tabs/OPFTab.h"
#include "Tabs/TabManager.h"
#include "Tabs/TabGroup.h"
#include "Tabs/TabBar.h"
#include "Tabs/WellFormedContent.h"
#include "Misc/SettingsStoreExtend.h"
#include "Misc/SettingsStore.h"


TabManager::TabManager(QWidget *parent)
    :
    QWidget(parent),
    m_Splitter(new QSplitter(Qt::Vertical, this)),
    m_Primary(new TabGroup(this)),
    m_SecondaryPane(0),
    m_Secondary(0),
    m_EmptyLabel(0),
    m_Active(0),
    m_LastContentTab(NULL),
    m_TabsToDelete(QList<ContentTab*>()),
    m_tabs_deletion_in_use(false),
    m_newTab(NULL)
{
    m_Active = m_Primary;
    m_Primary->setMinimumHeight(80);
    m_Primary->SetKeepLastTab(true);

    m_Splitter->setObjectName(QStringLiteral("editorGroupSplitter"));
    m_Splitter->setChildrenCollapsible(false);
    m_Splitter->addWidget(m_Primary);
    m_Splitter->setStretchFactor(0, 1);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_Splitter, 1);

    ConnectGroup(m_Primary);
    connect(qApp, SIGNAL(focusChanged(QWidget *, QWidget *)),
            this, SLOT(OnApplicationFocusChanged(QWidget *, QWidget *)));
    UpdateGroupAppearance();
}


ContentTab *TabManager::GetCurrentContentTab()
{
    if (m_Active && m_Active->CurrentTab()) {
        return m_Active->CurrentTab();
    }
    return m_Primary->CurrentTab();
}

QList<ContentTab *> TabManager::GetContentTabs()
{
    QList<ContentTab *> tabs = m_Primary->Tabs();
    if (m_Secondary) {
        tabs.append(m_Secondary->Tabs());
    }
    return tabs;
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
    int count = m_Primary->TabCount();
    if (m_Secondary) {
        count += m_Secondary->TabCount();
    }
    return count;
}

void TabManager::CloseAllTabs(bool all)
{
    while (true) {
        const int before = GetTabCount();
        if (m_Secondary && m_Secondary->TabCount() > 0) {
            CloseTabAt(m_Secondary, 0, all);
        } else if (m_Primary->TabCount() > 0) {
            CloseTabAt(m_Primary, 0, all);
        }
        if (GetTabCount() >= before) {
            break;
        }
    }
}

void TabManager::CloseTabForResource(const Resource *resource, bool force)
{
    ContentTab *tab = FindTab(resource);
    TabGroup *group = GroupContaining(tab);
    if (!tab || !group) {
        return;
    }
    CloseTabAt(group, group->IndexOfTab(tab), force);
}

bool TabManager::IsAllTabDataWellFormed()
{
    foreach(ContentTab *tab, GetContentTabs()) {
        if (!tab || !tab->GetLoadedResource()) {
            continue;
        }
        WellFormedContent *content = dynamic_cast<WellFormedContent *>(tab);
        if (content && tab->GetLoadedResource()->Type() == Resource::HTMLResourceType) {
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
        FlowTab *flow_tab = qobject_cast<FlowTab *>(FindTab(resource));
        if (flow_tab) {
            flow_tab->LoadTabContent();
        }
    }
}

void TabManager::ReopenTabs()
{
    ContentTab *currentTab = GetCurrentContentTab();
    Resource *current_resource = currentTab ? currentTab->GetLoadedResource() : 0;
    TabGroup *current_group = currentTab ? GroupContaining(currentTab) : m_Primary;

    QList<QPair<Resource *, bool> > items;
    foreach(ContentTab *tab, m_Primary->Tabs()) {
        if (tab && tab->GetLoadedResource()) {
            items.append(qMakePair(tab->GetLoadedResource(), false));
        }
    }
    if (m_Secondary) {
        foreach(ContentTab *tab, m_Secondary->Tabs()) {
            if (tab && tab->GetLoadedResource()) {
                items.append(qMakePair(tab->GetLoadedResource(), true));
            }
        }
    }

    for (const auto &item : items) {
        CloseTabForResource(item.first, true);
    }

    for (const auto &item : items) {
        if (!item.first) {
            continue;
        }
        TabGroup *target = (item.second && m_Secondary) ? m_Secondary : m_Primary;
        ContentTab *new_tab = CreateTabForResource(item.first, -1, -1, QString(), QUrl(), false, target);
        if (new_tab) {
            AddNewContentTab(new_tab, false, target);
        }
    }

    if (current_resource) {
        SwitchedToExistingTab(current_resource, -1, -1, QString(), QUrl());
        if (current_group) {
            SetActiveGroup(current_group);
        }
    }
}

void TabManager::PerformThemeChangeRefresh()
{
    foreach(ContentTab *tab, GetContentTabs()) {
        if (tab) {
            tab->ThemeChangeRefresh();
        }
    }
}

void TabManager::SaveTabData()
{
    foreach(ContentTab *tab, GetContentTabs()) {
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
    OpenWithDisposition(resource, line_to_scroll_to, position_to_scroll_to,
                        caret_location_to_scroll_to, fragment, precede_current_tab,
                        OpenDisposition::ActiveGroup);
}

void TabManager::OpenResourceInOtherGroup(Resource *resource,
                                          int line_to_scroll_to,
                                          int position_to_scroll_to,
                                          const QString &caret_location_to_scroll_to,
                                          const QUrl &fragment)
{
    OpenWithDisposition(resource, line_to_scroll_to, position_to_scroll_to,
                        caret_location_to_scroll_to, fragment, false,
                        OpenDisposition::OtherGroup);
}

void TabManager::OpenWithDisposition(Resource *resource,
                                     int line_to_scroll_to,
                                     int position_to_scroll_to,
                                     const QString &caret_location_to_scroll_to,
                                     const QUrl &fragment,
                                     bool precede_current_tab,
                                     OpenDisposition disposition)
{
    ContentTab *existing = FindTab(resource);
    if (existing) {
        const bool in_other = GroupContaining(existing) &&
                              GroupContaining(existing) != ActiveGroup();
        SwitchedToExistingTab(resource, line_to_scroll_to, position_to_scroll_to,
                              caret_location_to_scroll_to, fragment);
        if (disposition == OpenDisposition::OtherGroup && in_other) {
            emit ShowStatusMessageRequest(
                tr("%1 is already open in the other editor group.").arg(resource->ShortPathName()));
        }
        return;
    }

    TabGroup *target = ResolveTargetGroup(disposition);
    bool grab_focus = !precede_current_tab;
    ContentTab *new_tab = CreateTabForResource(resource, line_to_scroll_to, position_to_scroll_to,
                          caret_location_to_scroll_to, fragment, grab_focus, target);

    if (new_tab) {
        if (grab_focus) {
            m_newTab = new_tab;
        }
        AddNewContentTab(new_tab, precede_current_tab, target);
        SetActiveGroup(target);
        emit ShowStatusMessageRequest("");
    } else {
        QString message = tr("Cannot edit file") + ": " + resource->ShortPathName();
        emit ShowStatusMessageRequest(message);
    }
}


void TabManager::NextTab()
{
    ActiveGroup()->NextTab();
}


void TabManager::PreviousTab()
{
    ActiveGroup()->PreviousTab();
}


void TabManager::RemoveTab()
{
    // Can leave window with no tabs, so re-open a tab asap
    ActiveGroup()->RemoveCurrentTabWidget();
}


void TabManager::CloseTab()
{
    TabGroup *group = ActiveGroup();
    CloseTabAt(group, group->currentIndex(), false);
}

void TabManager::CloseOtherTabs()
{
    CloseAllTabsExcept(GetCurrentContentTab());
}

void TabManager::CloseOtherTabs(int index)
{
    CloseAllTabsExcept(m_Primary->TabAt(index));
}


void TabManager::MakeCentralTab(ContentTab *tab)
{
    Q_ASSERT(tab);
    TabGroup *group = GroupContaining(tab);
    if (!group) {
        return;
    }
    SetActiveGroup(group);
    group->ActivateTab(tab);
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
    TabGroup *from = qobject_cast<TabGroup *>(sender());
    if (from && from != m_Active) {
        return;
    }
    ContentTab *current_tab = GetCurrentContentTab();
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
        TabGroup *group = GroupContaining(tab);
        if (!group) {
            group = m_Primary;
        }
        disconnect(group, SIGNAL(currentChanged(int)), this, SLOT(EmitTabChanged(int)));
        group->TakeTab(tab);
        connect(group, SIGNAL(currentChanged(int)), this, SLOT(EmitTabChanged(int)));
        UpdateEmptyState();

        // Only the current tab is ever connected to the main ui
        // so do our own version of EmitTabChanged() only if needed
        // to disconnect and reconnect ui signals
        ContentTab *next_tab = GetCurrentContentTab();
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
    CloseTabAt(m_Primary, tab_index, force);
}


void TabManager::UpdateTabName(ContentTab *renamed_tab)
{
    Q_ASSERT(renamed_tab);
    TabGroup *group = GroupContaining(renamed_tab);
    if (group) {
        group->UpdateTabName(renamed_tab);
    }
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



// Returns the index of the tab in the primary group, -1 if it isn't there.
int TabManager::ResourceTabIndex(const Resource *resource) const
{
    ContentTab *tab = FindTab(resource);
    if (!tab) {
        return -1;
    }
    return m_Primary->IndexOfTab(tab);
}


bool TabManager::SwitchedToExistingTab(const Resource *resource,
                                       int line_to_scroll_to,
                                       int position_to_scroll_to,
                                       const QString &caret_location_to_scroll_to,
                                       const QUrl &fragment)
{
    ContentTab *tab = FindTab(resource);
    if (!tab) {
        return false;
    }

    TabGroup *group = GroupContaining(tab);
    if (group) {
        SetActiveGroup(group);
        group->ActivateTab(tab);
    }
    ApplyExistingTabLocation(tab, line_to_scroll_to, position_to_scroll_to,
                             caret_location_to_scroll_to, fragment);
    return true;
}


ContentTab *TabManager::CreateTabForResource(Resource *resource,
        int line_to_scroll_to,
        int position_to_scroll_to,
        const QString &caret_location_to_scroll_to,
        const QUrl &fragment,
        bool grab_focus,
        QWidget *tab_parent)
{
    ContentTab *tab = NULL;
    if (!tab_parent) {
        tab_parent = m_Primary;
    }

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
                              tab_parent);
            connect(tab,  SIGNAL(LinkClicked(const QUrl &)), this, SLOT(LinkClicked(const QUrl &)));
            connect(tab,  SIGNAL(OldTabRequest(QString, HTMLResource *)),
                    this, SIGNAL(OldTabRequest(QString, HTMLResource *)));
            break;
        }

        case Resource::CSSResourceType: {
            tab = new CSSTab(qobject_cast<CSSResource *>(resource), line_to_scroll_to, position_to_scroll_to, tab_parent);
            break;
        }

        case Resource::ImageResourceType: {
            tab = new ImageTab(qobject_cast<ImageResource *>(resource), tab_parent);
            break;
        }

        case Resource::MiscTextResourceType: {
            tab = new MiscTextTab(qobject_cast<MiscTextResource *>(resource), line_to_scroll_to, position_to_scroll_to, tab_parent);
            break;
        }

        case Resource::SVGResourceType: {
            tab = new SVGTab(qobject_cast<SVGResource *>(resource), line_to_scroll_to, position_to_scroll_to, tab_parent);
            break;
        }

        case Resource::OPFResourceType: {
            tab = new OPFTab(qobject_cast<OPFResource *>(resource), line_to_scroll_to, position_to_scroll_to, tab_parent);
            break;
        }

        case Resource::NCXResourceType: {
            tab = new NCXTab(qobject_cast<NCXResource *>(resource), line_to_scroll_to, position_to_scroll_to, tab_parent);
            break;
        }

        case Resource::XMLResourceType: {
            tab = new XMLTab(qobject_cast<XMLResource *>(resource), line_to_scroll_to, position_to_scroll_to, tab_parent);
            break;
        }

        case Resource::TextResourceType: {
            tab = new TextTab(qobject_cast<TextResource *>(resource), CodeViewEditor::Highlight_NONE, 
                              line_to_scroll_to, position_to_scroll_to, tab_parent);
            break;
        }

        case Resource::AudioResourceType:
        case Resource::VideoResourceType: {
            tab = new AVTab(qobject_cast<Resource *>(resource), tab_parent);
            break;
        }

        case Resource::PdfResourceType: {
            tab = new PdfTab(qobject_cast<Resource *>(resource), tab_parent);
            break;
        }

        case Resource::FontResourceType: {
            tab = new FontTab(qobject_cast<Resource *>(resource), tab_parent);
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


bool TabManager::AddNewContentTab(ContentTab *new_tab, bool precede_current_tab, TabGroup *target)
{
    if (new_tab == NULL) {
        return false;
    }
    if (!target) {
        target = m_Primary;
    }

    // before adding a tab make sure m_LastContentTab has been
    // properly updated to reflect the current tab
    m_LastContentTab = GetCurrentContentTab();

    if (target->AddContentTab(new_tab, precede_current_tab) < 0) {
        return false;
    }

    connect(new_tab, SIGNAL(DeleteMe(ContentTab *)), this, SLOT(DeleteTab(ContentTab *)));
    connect(new_tab, SIGNAL(TabRenamed(ContentTab *)), this, SLOT(UpdateTabName(ContentTab *)));
    return true;
}

void TabManager::UpdateTabDisplay()
{
    foreach(ContentTab *tab, GetContentTabs()) {
        if (tab) {
            tab->UpdateDisplay();
        }
    }
}

bool TabManager::CloseOPFTabIfOpen()
{
    foreach(ContentTab *tab, GetContentTabs()) {
        if (tab && tab->GetLoadedResource() &&
            tab->GetLoadedResource()->Type() == Resource::OPFResourceType) {
            TabGroup *group = GroupContaining(tab);
            if (group) {
                CloseTabAt(group, group->IndexOfTab(tab), false);
            }
            return true;
        }
    }
    return false;
}

bool TabManager::IsSplit() const
{
    return m_SecondaryPane && m_SecondaryPane->isVisible();
}

void TabManager::SplitEditorDown()
{
    if (IsSplit()) {
        return;
    }
    EnsureSecondary();
    m_SecondaryPane->show();
    UpdateEmptyState();
    const int total = qMax(m_Splitter->height(), 200);
    m_Splitter->setSizes(QList<int>() << (total / 2) << (total / 2));
    UpdateGroupAppearance();
    emit SplitChanged();
}

void TabManager::JoinEditorGroups()
{
    if (!IsSplit() || !m_Secondary) {
        return;
    }

    ContentTab *keep = GetCurrentContentTab();

    disconnect(m_Primary, SIGNAL(currentChanged(int)), this, SLOT(EmitTabChanged(int)));
    disconnect(m_Secondary, SIGNAL(currentChanged(int)), this, SLOT(EmitTabChanged(int)));
    const QList<ContentTab *> moving = m_Secondary->Tabs();
    foreach(ContentTab *tab, moving) {
        if (!tab) {
            continue;
        }
        m_Secondary->TakeTab(tab);
        m_Primary->AddContentTab(tab, false);
    }
    connect(m_Primary, SIGNAL(currentChanged(int)), this, SLOT(EmitTabChanged(int)));
    connect(m_Secondary, SIGNAL(currentChanged(int)), this, SLOT(EmitTabChanged(int)));

    HideSecondary();
    SetActiveGroup(m_Primary);
    if (keep && GroupContaining(keep) == m_Primary) {
        m_Primary->ActivateTab(keep);
    }
    emit SplitChanged();
    emit TabCountChanged();
}

void TabManager::ConnectGroup(TabGroup *group)
{
    connect(group, SIGNAL(TabBarClicked()),            this, SLOT(OnGroupActivated()));
    connect(group, SIGNAL(CloseOtherTabsRequest(int)), this, SLOT(OnCloseOtherTabsRequested(int)));
    connect(group, SIGNAL(currentChanged(int)),        this, SLOT(EmitTabChanged(int)));
    connect(group, SIGNAL(tabCloseRequested(int)),     this, SLOT(OnTabCloseRequested(int)));
    connect(group, SIGNAL(TabInserted()),              this, SLOT(OnTabInserted()));
    connect(group, SIGNAL(MoveToOtherGroupRequest(int)), this, SLOT(OnMoveToOtherGroupRequested(int)));
    connect(group, SIGNAL(TabDropRequest(QWidget *, int)),
            this, SLOT(OnTabDropRequested(QWidget *, int)));
}

void TabManager::EnsureSecondary()
{
    if (m_SecondaryPane) {
        return;
    }

    m_SecondaryPane = new QWidget(this);
    m_Secondary = new TabGroup(m_SecondaryPane);
    m_Secondary->SetKeepLastTab(false);
    m_EmptyLabel = new QLabel(tr("Open a file from Book Browser, or drop an editor tab here"), m_SecondaryPane);
    m_EmptyLabel->setAlignment(Qt::AlignCenter);
    m_EmptyLabel->setWordWrap(true);
    m_EmptyLabel->setFocusPolicy(Qt::ClickFocus);
    m_EmptyLabel->setAcceptDrops(true);
    m_EmptyLabel->installEventFilter(this);
    m_SecondaryPane->setAcceptDrops(true);
    m_SecondaryPane->installEventFilter(this);
    m_Secondary->setMinimumHeight(80);
    m_SecondaryPane->setMinimumHeight(80);

    QStackedLayout *stack = new QStackedLayout(m_SecondaryPane);
    stack->setContentsMargins(0, 0, 0, 0);
    stack->addWidget(m_Secondary);
    stack->addWidget(m_EmptyLabel);

    ConnectGroup(m_Secondary);
    m_Splitter->addWidget(m_SecondaryPane);
    m_Splitter->setStretchFactor(1, 1);
}

void TabManager::HideSecondary()
{
    if (m_SecondaryPane) {
        m_SecondaryPane->hide();
    }
    UpdateEmptyState();
}

void TabManager::UpdateEmptyState()
{
    if (!m_SecondaryPane || !m_Secondary || !m_EmptyLabel) {
        return;
    }
    QStackedLayout *stack = qobject_cast<QStackedLayout *>(m_SecondaryPane->layout());
    if (!stack) {
        return;
    }
    if (m_Secondary->TabCount() == 0) {
        stack->setCurrentWidget(m_EmptyLabel);
    } else {
        stack->setCurrentWidget(m_Secondary);
    }
}

TabGroup *TabManager::ActiveGroup() const
{
    return m_Active ? m_Active : m_Primary;
}

TabGroup *TabManager::GroupContaining(const ContentTab *tab) const
{
    if (!tab) {
        return 0;
    }
    if (m_Primary->IndexOfTab(tab) != -1) {
        return m_Primary;
    }
    if (m_Secondary && m_Secondary->IndexOfTab(tab) != -1) {
        return m_Secondary;
    }
    return 0;
}

ContentTab *TabManager::FindTab(const Resource *resource) const
{
    ContentTab *tab = m_Primary->TabForResource(resource);
    if (tab) {
        return tab;
    }
    if (m_Secondary) {
        return m_Secondary->TabForResource(resource);
    }
    return 0;
}

void TabManager::CloseTabAt(TabGroup *group, int tab_index, bool force)
{
    if (!group || group->TabCount() == 0) {
        return;
    }
    const bool last_primary = (group == m_Primary && group->TabCount() <= 1);
    if (!force && last_primary) {
        return;
    }

    ContentTab *tab = group->TabAt(tab_index);
    if (tab) {
        tab->Close();
    }
    UpdateEmptyState();
    emit TabCountChanged();
}

void TabManager::CloseAllTabsExcept(ContentTab *keep)
{
    if (!keep) {
        return;
    }

    QList<ContentTab *> closing;
    foreach(ContentTab *tab, GetContentTabs()) {
        if (tab && tab != keep) {
            if (GroupContaining(tab) == m_Primary && m_Primary->TabCount() <= 1) {
                continue;
            }
            closing.append(tab);
        }
    }
    foreach(ContentTab *tab, closing) {
        TabGroup *group = GroupContaining(tab);
        if (!group) {
            continue;
        }
        const bool last_primary = (group == m_Primary && group->TabCount() <= 1);
        if (last_primary) {
            continue;
        }
        tab->Close();
    }
    UpdateEmptyState();
    emit TabCountChanged();
}

void TabManager::ApplyExistingTabLocation(ContentTab *tab,
                                          int line_to_scroll_to,
                                          int position_to_scroll_to,
                                          const QString &caret_location_to_scroll_to,
                                          const QUrl &fragment)
{
    FlowTab *flow_tab = qobject_cast<FlowTab *>(tab);
    if (flow_tab) {
        if (!caret_location_to_scroll_to.isEmpty()) {
            flow_tab->ScrollToCaretLocation(caret_location_to_scroll_to);
        } else if (position_to_scroll_to >= 0) {
            flow_tab->ScrollToPosition(position_to_scroll_to);
        } else if (!fragment.toString().isEmpty()) {
            flow_tab->ScrollToFragment(fragment.toString());
        } else if (line_to_scroll_to > 0) {
            flow_tab->ScrollToLine(line_to_scroll_to);
        }
        flow_tab->EmitScrollPreviewImmediately();
        return;
    }

    TextTab *text_tab = qobject_cast<TextTab *>(tab);
    if (text_tab) {
        if (position_to_scroll_to >= 0) {
            text_tab->ScrollToPosition(position_to_scroll_to);
        } else {
            text_tab->ScrollToLine(line_to_scroll_to);
        }
    }
}

void TabManager::OnGroupActivated()
{
    SetActiveGroup(qobject_cast<TabGroup *>(sender()));
    SetFocusInTab();
}

void TabManager::OnTabCloseRequested(int tab_index)
{
    TabGroup *group = qobject_cast<TabGroup *>(sender());
    CloseTabAt(group ? group : m_Primary, tab_index, false);
}

void TabManager::OnCloseOtherTabsRequested(int tab_index)
{
    TabGroup *group = qobject_cast<TabGroup *>(sender());
    if (!group) {
        group = m_Primary;
    }
    CloseAllTabsExcept(group->TabAt(tab_index));
}

void TabManager::OnTabInserted()
{
    UpdateEmptyState();
    emit TabCountChanged();
}

TabGroup *TabManager::ResolveTargetGroup(OpenDisposition disposition)
{
    if (disposition == OpenDisposition::ActiveGroup) {
        return ActiveGroup();
    }

    if (!IsSplit()) {
        SplitEditorDown();
        EnsureSecondary();
        return m_Secondary;
    }

    SettingsStoreExtend settings;
    const QString target = settings.getOtherGroupTarget();
    if (target == QLatin1String("lower")) {
        return m_Secondary;
    }
    if (target == QLatin1String("upper")) {
        return m_Primary;
    }
    return (ActiveGroup() == m_Primary) ? m_Secondary : m_Primary;
}

bool TabManager::CanMoveTabToOtherGroup(const ContentTab *tab) const
{
    TabGroup *group = GroupContaining(tab);
    if (!group) {
        return false;
    }
    return group->CanMoveTab(group->IndexOfTab(tab));
}

bool TabManager::MoveTabToOtherGroup(ContentTab *tab)
{
    if (!CanMoveTabToOtherGroup(tab)) {
        return false;
    }
    TabGroup *source = GroupContaining(tab);
    if (!IsSplit()) {
        SplitEditorDown();
    }
    EnsureSecondary();
    TabGroup *dest = (source == m_Primary) ? m_Secondary : m_Primary;
    return MoveTabToGroup(tab, dest, dest->TabCount());
}

bool TabManager::MoveTabToGroup(ContentTab *tab, TabGroup *dest, int dest_index)
{
    TabGroup *source = GroupContaining(tab);
    if (!tab || !source || !dest) {
        return false;
    }

    if (source == dest) {
        const int from = source->IndexOfTab(tab);
        if (from < 0) {
            return false;
        }
        if (dest_index > from) {
            dest_index--;
        }
        if (dest_index < 0) {
            dest_index = 0;
        }
        if (dest_index >= source->TabCount()) {
            dest_index = source->TabCount() - 1;
        }
        if (from != dest_index) {
            source->tabBar()->moveTab(from, dest_index);
        }
        SetActiveGroup(dest);
        dest->ActivateTab(tab);
        return true;
    }

    if (!source->CanMoveTab(source->IndexOfTab(tab))) {
        return false;
    }

    if (!IsSplit()) {
        SplitEditorDown();
        EnsureSecondary();
        dest = m_Secondary;
        dest_index = dest->TabCount();
    }

    source->TakeTab(tab);
    dest->InsertContentTab(tab, dest_index);
    SetActiveGroup(dest);
    dest->ActivateTab(tab);
    UpdateEmptyState();
    emit TabCountChanged();
    return true;
}

void TabManager::OnMoveToOtherGroupRequested(int tab_index)
{
    TabGroup *group = qobject_cast<TabGroup *>(sender());
    if (!group) {
        return;
    }
    MoveTabToOtherGroup(group->TabAt(tab_index));
}

void TabManager::OnTabDropRequested(QWidget *tab_widget, int insert_index)
{
    TabGroup *dest = qobject_cast<TabGroup *>(sender());
    MoveTabToGroup(qobject_cast<ContentTab *>(tab_widget), dest, insert_index);
}

bool TabManager::AcceptsEditorTabDrop(const QMimeData *mime) const
{
    ContentTab *tab = qobject_cast<ContentTab *>(TabBar::DecodeTab(mime));
    return tab && CanMoveTabToOtherGroup(tab);
}

bool TabManager::eventFilter(QObject *object, QEvent *event)
{
    if (object != m_EmptyLabel && object != m_SecondaryPane) {
        return QWidget::eventFilter(object, event);
    }

    if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
        QDropEvent *drag = static_cast<QDropEvent *>(event);
        if (AcceptsEditorTabDrop(drag->mimeData())) {
            drag->acceptProposedAction();
            return true;
        }
    } else if (event->type() == QEvent::Drop) {
        QDropEvent *drop = static_cast<QDropEvent *>(event);
        ContentTab *tab = qobject_cast<ContentTab *>(TabBar::DecodeTab(drop->mimeData()));
        if (tab && m_Secondary && MoveTabToGroup(tab, m_Secondary, m_Secondary->TabCount())) {
            drop->acceptProposedAction();
            return true;
        }
    }
    return QWidget::eventFilter(object, event);
}

void TabManager::SetActiveGroup(TabGroup *group)
{
    if (!group || group == m_Active) {
        return;
    }
    ContentTab *old_tab = GetCurrentContentTab();
    m_Active = group;
    UpdateGroupAppearance();
    ContentTab *new_tab = GetCurrentContentTab();
    if (old_tab != new_tab) {
        m_LastContentTab = new_tab;
        emit TabChanged(old_tab, new_tab);
        emit UpdatePreviewAfterExistingTabSwitch();
    }
}

TabGroup *TabManager::GroupFromWidget(QWidget *widget) const
{
    while (widget) {
        if (widget == m_Primary) {
            return m_Primary;
        }
        if (m_Secondary && (widget == m_Secondary || widget == m_SecondaryPane || widget == m_EmptyLabel)) {
            return m_Secondary;
        }
        widget = widget->parentWidget();
    }
    return 0;
}

void TabManager::UpdateGroupAppearance()
{
    m_Primary->SetActiveAppearance(m_Active == m_Primary);
    if (m_Secondary) {
        m_Secondary->SetActiveAppearance(m_Active == m_Secondary);
    }
}

void TabManager::OnApplicationFocusChanged(QWidget *old_widget, QWidget *now)
{
    Q_UNUSED(old_widget);
    TabGroup *group = GroupFromWidget(now);
    if (group) {
        SetActiveGroup(group);
    }
}

void TabManager::FocusUpperEditorGroup()
{
    SetActiveGroup(m_Primary);
    SetFocusInTab();
}

void TabManager::FocusLowerEditorGroup()
{
    if (!IsSplit()) {
        return;
    }
    EnsureSecondary();
    SetActiveGroup(m_Secondary);
    if (ContentTab *tab = m_Secondary->CurrentTab()) {
        tab->setFocus();
    } else if (m_EmptyLabel) {
        m_EmptyLabel->setFocus();
    }
}

void TabManager::SaveLayoutSettings()
{
    SettingsStore settings;
    settings.beginGroup(QStringLiteral("editorGroups"));
    settings.setValue(QStringLiteral("enabled"), IsSplit());
    if (IsSplit()) {
        settings.setValue(QStringLiteral("splitterState"), m_Splitter->saveState());
    }
    settings.setValue(QStringLiteral("activeGroup"),
                      (m_Active == m_Secondary) ? QStringLiteral("secondary")
                                                : QStringLiteral("primary"));
    settings.endGroup();
}

void TabManager::RestoreLayoutSettings()
{
    SettingsStore settings;
    settings.beginGroup(QStringLiteral("editorGroups"));
    const bool enabled = settings.value(QStringLiteral("enabled"), false).toBool();
    const QByteArray state = settings.value(QStringLiteral("splitterState")).toByteArray();
    settings.endGroup();

    if (enabled) {
        SplitEditorDown();
        if (!state.isEmpty()) {
            m_Splitter->restoreState(state);
        }
    }
    // Files are not restored. Keep the first opened HTML in the primary group.
    SetActiveGroup(m_Primary);
}
