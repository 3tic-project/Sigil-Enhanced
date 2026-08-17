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

#pragma once
#ifndef TABGROUP_H
#define TABGROUP_H

#include <QtCore/QList>
#include <QtWidgets/QTabWidget>

class ContentTab;
class Resource;
class WellFormedContent;

/**
 * One editor tab strip. TabManager owns one or more groups and keeps
 * resource routing above this widget.
 */
class TabGroup : public QTabWidget
{
    Q_OBJECT

public:
    explicit TabGroup(QWidget *parent = 0);

    ContentTab *CurrentTab() const;
    QList<ContentTab *> Tabs() const;
    int TabCount() const;
    ContentTab *TabAt(int index) const;
    int IndexOfTab(const ContentTab *tab) const;

    int ResourceTabIndex(const Resource *resource) const;
    ContentTab *TabForResource(const Resource *resource) const;
    WellFormedContent *WellFormedAt(int index) const;

    int AddContentTab(ContentTab *tab, bool precede_current_tab);
    void TakeTab(ContentTab *tab);
    void ActivateTab(ContentTab *tab);
    void UpdateTabName(ContentTab *tab);

    void NextTab();
    void PreviousTab();
    void RemoveCurrentTabWidget();

    void SetKeepLastTab(bool keep);
    bool KeepLastTab() const;
    bool CanMoveTab(int index) const;
    void SetActiveAppearance(bool active);

signals:
    void TabBarClicked();
    void CloseOtherTabsRequest(int tab_index);
    void MoveToOtherGroupRequest(int tab_index);
    void TabInserted();

protected:
    void tabInserted(int index) override;

private:
    bool m_KeepLastTab;
};

#endif // TABGROUP_H
