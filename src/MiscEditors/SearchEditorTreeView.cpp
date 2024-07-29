/************************************************************************
**
**  Copyright (C) 2015-2024 Kevin B. Hendricks, Stratford Ontario Canada
**  Copyright (C) 2012      John Schember <john@nachtimwald.com>
**  Copyright (C) 2012      Dave Heiland
**  Copyright (C) 2012      Grant Drake
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

#include <QHeaderView>

#include "MiscEditors/SearchEditorTreeView.h"

SearchEditorTreeView::SearchEditorTreeView(QWidget *parent)
    : QTreeView(parent)
{
    setDragEnabled(true);
    setAcceptDrops(false);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::InternalMove);
    setAutoScroll(true);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSortingEnabled(false);
    setTabKeyNavigation(false);
    setAutoExpandDelay(200);
}

SearchEditorTreeView::~SearchEditorTreeView()
{
}

QModelIndex SearchEditorTreeView::moveCursor(CursorAction cursorAction, Qt::KeyboardModifiers modifiers)
{
    if (cursorAction == QAbstractItemView::MoveNext) {
        QModelIndex index = currentIndex();

        // Only the first column of a group is editable
        if (!model()->data(index.sibling(index.row(), 0), Qt::UserRole + 1).toBool()) {
            // Move to next column in row if there is one
            if (index.column() < (header()->count() - 1)) {
                return model()->index(index.row(), index.column() + 1, index.parent());
            }
        }

        // Reset to first column in row so that default moveCursor moves it down one
        if (indexBelow(index).isValid()) {
            setCurrentIndex(model()->index(index.row(), 0, index.parent()));
        }
    } else if (cursorAction == QAbstractItemView::MovePrevious) {
        QModelIndex index = currentIndex();

        // Only the first column of a group is editable
        if (!model()->data(index.sibling(index.row(), 0), Qt::UserRole + 1).toBool()) {
            // Move to previous column in row if there is one
            if (index.column() > 0) {
                return model()->index(index.row(), index.column() - 1, index.parent());
            }
        }

        if (indexAbove(index).isValid()) {
            // If row above is a group always reset to first column otherwise last column
            if (model()->data(indexAbove(index).sibling(indexAbove(index).row(), 0), Qt::UserRole + 1).toBool()) {
                setCurrentIndex(model()->index(index.row(), 0, index.parent()));
            } else {
                setCurrentIndex(model()->index(index.row(), header()->count() - 1, index.parent()));
            }
        }
    }

    return QTreeView::moveCursor(cursorAction, modifiers);
}


//-------------- modified: SavedSearchPlus ------------------
void SearchEditorTreeView::setGroupsSpan(const QModelIndex& top)
{
    int IsGroupRole = Qt::UserRole + 1;
    QModelIndexList index_stack = { top };
    while (!index_stack.isEmpty()) {
        QModelIndex index = index_stack.takeFirst();
        if (index.data(IsGroupRole).toBool()) { // Is a group
            this->setFirstColumnSpanned(index.row(), index.parent(), true);
        }
        for (int i = 0; i < model()->rowCount(index); i++) {
            index_stack << model()->index(i, 0, index);
        }
    }
}

void SearchEditorTreeView::setModel(QAbstractItemModel* model)
{
    QTreeView::setModel(model);
    setGroupsSpan(this->rootIndex());
}

void SearchEditorTreeView::rowsInserted(const QModelIndex& parent, int start, int end)
{
    QTreeView::rowsInserted(parent, start, end);

    m_InsertedRowsToBeHandled.clear();

    for (int i = start; i <= end; i++) {
        QModelIndex index = model()->index(i, 0, parent);
        if (index.data(Qt::UserRole + 1) == QVariant(QVariant::Invalid)) {
            m_InsertedRowsToBeHandled << index;
        }
        else if (index.data(Qt::UserRole + 1).toBool()) {
            setGroupsSpan(index);
        }
    }
}
void SearchEditorTreeView::rowsAboutToBeRemoved(const QModelIndex& parent, int start, int end)
{
    QTreeView::rowsAboutToBeRemoved(parent, start, end);

    if (m_InsertedRowsToBeHandled.count() == end - start + 1) {
        for (int i = start, j = 0; i <= end; i++, j++) {
            if (model()->index(i, 0, parent).data(Qt::UserRole + 1).toBool()) {
                setGroupsSpan(m_InsertedRowsToBeHandled.at(j));
            }
        }
    }
    if (!m_InsertedRowsToBeHandled.isEmpty())
        m_InsertedRowsToBeHandled.clear();
}
