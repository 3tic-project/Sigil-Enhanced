/************************************************************************
**
**  Copyright (C) 2015-2026 Kevin B. Hendricks, Stratford, Ontario
**  Copyright (C) 2009-2011 Strahinja Markovic  <strahinja.markovic@gmail.com>
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

#include "Tabs/TabGroup.h"

#include "ResourceObjects/Resource.h"
#include "Tabs/ContentTab.h"
#include "Tabs/TabBar.h"
#include "Tabs/WellFormedContent.h"

TabGroup::TabGroup(QWidget *parent)
    : QTabWidget(parent)
{
    TabBar *tab_bar = new TabBar(this);
    setTabBar(tab_bar);
    connect(tab_bar, SIGNAL(TabBarClicked()),            this, SIGNAL(TabBarClicked()));
    connect(tab_bar, SIGNAL(CloseOtherTabsRequest(int)), this, SIGNAL(CloseOtherTabsRequest(int)));
    setDocumentMode(true);
    setMovable(true);
    setTabsClosable(true);
    setUsesScrollButtons(true);
}

ContentTab *TabGroup::CurrentTab() const
{
    return qobject_cast<ContentTab *>(currentWidget());
}

QList<ContentTab *> TabGroup::Tabs() const
{
    QList<ContentTab *> tabs;
    for (int i = 0; i < count(); ++i) {
        tabs.append(qobject_cast<ContentTab *>(widget(i)));
    }
    return tabs;
}

int TabGroup::TabCount() const
{
    return count();
}

ContentTab *TabGroup::TabAt(int index) const
{
    if (index < 0 || index >= count()) {
        return 0;
    }
    return qobject_cast<ContentTab *>(widget(index));
}

int TabGroup::IndexOfTab(const ContentTab *tab) const
{
    return indexOf(const_cast<ContentTab *>(tab));
}

int TabGroup::ResourceTabIndex(const Resource *resource) const
{
    if (!resource) {
        return -1;
    }
    const QString identifier(resource->GetIdentifier());
    for (int i = 0; i < count(); ++i) {
        ContentTab *tab = qobject_cast<ContentTab *>(widget(i));
        if (tab && tab->GetLoadedResource() &&
            tab->GetLoadedResource()->GetIdentifier() == identifier) {
            return i;
        }
    }
    return -1;
}

ContentTab *TabGroup::TabForResource(const Resource *resource) const
{
    return TabAt(ResourceTabIndex(resource));
}

WellFormedContent *TabGroup::WellFormedAt(int index) const
{
    return dynamic_cast<WellFormedContent *>(widget(index));
}

int TabGroup::AddContentTab(ContentTab *new_tab, bool precede_current_tab)
{
    if (!new_tab) {
        return -1;
    }

    int idx = -1;
    QString safeName = new_tab->GetShortPathName();
    safeName.replace("&", "&&");

    if (!precede_current_tab) {
#if defined(Q_OS_MAC)
        // drop use of icons to workaround Qt Bugs: QTBUG-61235, QTBUG-61742, QTBUG-63445, QTBUG-64630
        idx = addTab(new_tab, safeName);
#else
        idx = addTab(new_tab, new_tab->GetIcon(), safeName);
#endif
        setCurrentWidget(new_tab);
        new_tab->setFocus();
    } else {
#if defined(Q_OS_MAC)
        idx = insertTab(currentIndex(), new_tab, safeName);
#else
        idx = insertTab(currentIndex(), new_tab, new_tab->GetIcon(), safeName);
#endif
    }
    setTabToolTip(idx, new_tab->GetShortPathName());
    return idx;
}

void TabGroup::TakeTab(ContentTab *tab)
{
    if (!tab) {
        return;
    }
    const int idx = indexOf(tab);
    if (idx != -1) {
        removeTab(idx);
    }
}

void TabGroup::ActivateTab(ContentTab *tab)
{
    if (!tab) {
        return;
    }
    const int idx = indexOf(tab);
    if (idx != -1) {
        setCurrentIndex(idx);
        tab->setFocus();
    }
}

void TabGroup::UpdateTabName(ContentTab *renamed_tab)
{
    if (!renamed_tab) {
        return;
    }
    const int idx = indexOf(renamed_tab);
    if (idx == -1) {
        return;
    }
    QString rawName = renamed_tab->GetShortPathName();
    setTabToolTip(idx, rawName);
    QString safeName = rawName.replace("&", "&&");
    setTabText(idx, safeName);
}

void TabGroup::NextTab()
{
    const int current_index = currentIndex();
    if (current_index == -1) {
        return;
    }
    const int next_index = current_index != count() - 1 ? current_index + 1 : 0;
    if (widget(next_index) != 0 && current_index != next_index) {
        setCurrentIndex(next_index);
    }
}

void TabGroup::PreviousTab()
{
    const int current_index = currentIndex();
    if (current_index == -1) {
        return;
    }
    const int previous_index = current_index != 0 ? current_index - 1 : count() - 1;
    if (widget(previous_index) != 0 && current_index != previous_index) {
        setCurrentIndex(previous_index);
    }
}

void TabGroup::RemoveCurrentTabWidget()
{
    removeTab(currentIndex());
}

void TabGroup::tabInserted(int index)
{
    QTabWidget::tabInserted(index);
    emit TabInserted();
}
