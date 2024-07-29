/************************************************************************
**
**  Copyright (C) 2021      Kevin B. Hendricks, Stratford Ontario Canada 
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

#include <QChar>
#include <QString>
#include <QWidget>
#include <QStyledItemDelegate>

#include "Dialogs/Controls.h"
#include "Dialogs/SearchEditorItemDelegate.h"
#include "Dialogs/SearchControlsPlus.h" //modified: SavedSearchPlus


static const int COL_COMBOBOX = 3;


SearchEditorItemDelegate::SearchEditorItemDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}


SearchEditorItemDelegate::~SearchEditorItemDelegate()
{
}


QWidget *SearchEditorItemDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    //modified: SavedSearchPlus
    if (m_PlusMode) {
        int ctrl_col = 4;
        if (index.column() != ctrl_col)
            return QStyledItemDelegate::createEditor(parent, option, index);
        SearchControlsPlus* s = new SearchControlsPlus(parent);
        s->setModal(false);
        return s;
    }
    //-------------------------
    // ComboBox only in designated column
    if (index.column() != COL_COMBOBOX) {
        return QStyledItemDelegate::createEditor(parent, option, index);
    }

    Controls * c = new Controls(parent);
    c->setModal(false);
    return c;
}


void SearchEditorItemDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
{
    //modified: SavedSearchPlus
    if (m_PlusMode) {
        int ctrl_col = 4;
        if (index.column() != ctrl_col)
            return QStyledItemDelegate::setEditorData(editor, index);
        if (SearchControlsPlus* s = qobject_cast<SearchControlsPlus*>(editor)) {
            QString currentCode = index.data(Qt::EditRole).toString();
            s->UpdateSearchControls(currentCode);
            s->show();
        }
        else {
            QStyledItemDelegate::setEditorData(editor, index);
        }
        return;
    }
    //-------------------------
    if (index.column() != COL_COMBOBOX) {
        QStyledItemDelegate::setEditorData(editor, index);
        return;
    }
    if (Controls *c = qobject_cast<Controls *>(editor)) {
        QString currentCode = index.data(Qt::EditRole).toString();
        c->UpdateSearchControls(currentCode);
        c->show();
    } else {
        QStyledItemDelegate::setEditorData(editor, index);
    }
}


void SearchEditorItemDelegate::setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const
{
    //modified: SavedSearchPlus
    if (m_PlusMode) {
        if (SearchControlsPlus* s = qobject_cast<SearchControlsPlus*>(editor)) {
            QString newCode = s->GetControlsCode();
            model->setData(index, newCode, Qt::EditRole);
        }
        else {
            QStyledItemDelegate::setModelData(editor, model, index);
        }
        return;
    }
    //-------------------------
    if (Controls *c = qobject_cast<Controls *>(editor)) {
        QString newCode = c->GetControlsCode();
        model->setData(index, newCode, Qt::EditRole);
        // model->setData(index, cb->currentData(), Qt::ToolTipRole);
    } else {
        QStyledItemDelegate::setModelData(editor, model, index);
    }
}

//modified: SavedSearchPlus
void SearchEditorItemDelegate::setPlusMode(bool plus_mode)
{
    m_PlusMode = plus_mode;
}