/************************************************************************
**
**  Copyright (C) 2016-2026 Kevin B. Hendricks, Stratford, Ontario, Canada
**  Copyright (C) 2013      Dave Heiland
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
#ifndef EDITTOC_H
#define EDITTOC_H

#include <QList>
#include <QSharedPointer>
#include <QDialog>
#include <QStandardItemModel>
#include <QModelIndex>
#include <QAction>
#include <QMenu>
#include <QPointer>
#include <QHash>

#include "MainUI/TOCModel.h"
#include "BookManipulation/Headings.h"
#include "BookManipulation/TocTreeTransform.h"
#include "ResourceObjects/NCXResource.h"

#include "ui_EditTOC.h"

class Book;
class QStandardItem;
class Resource;
class QUndoStack;

class EditTOC : public QDialog
{
    Q_OBJECT


    struct ContiguousRange {
        QModelIndex parent;
        int startRow;
        int endRow;
    };

public:

    EditTOC(QSharedPointer<Book> book, QList<Resource *> resources, QWidget *parent = 0);

    ~EditTOC();

    bool DidSaveChanges() const;

        
protected:
    bool eventFilter(QObject *obj, QEvent *event);

private slots:
    void Save();

    void Rename();
    void CollapseAll();
    void ExpandAll();

    void AddEntryAbove();
    void AddEntryBelow();
    void DeleteEntry();
    void MoveLeft();
    void MoveRight();
    void MoveUp();
    void MoveDown();
    void SelectTarget();
    void MakeDefaultFirstSelection();
    void OpenContextMenu(const QPoint &point);
    void UpdateMoveButtons();
    void RecordItemEdit(QStandardItem *item);
    
private:

    void sortItemSelectionRanges(QTreeView* treeView, QItemSelection& selection);
    void StructureUserSelections();
    
    QList<ContiguousRange> getContiguousRanges(const QList<QStandardItem*> &items);

    void AddEntry(bool above);
    QModelIndex CheckSelection(int row);

    TOCModel::TOCEntry ConvertTableToEntries();
    TOCModel::TOCEntry ConvertItemToEntry(QStandardItem *item);
    TocEditTree ConvertTableToEditTree() const;
    void AddItemToEditTree(QStandardItem *item, TocNodeId parentId,
                           TocEditTree &tree) const;
    QList<TocNodeId> SelectedNodeIds() const;
    void ApplyEditTree(const TocEditTree &tree,
                       const QList<TocNodeId> &selectedIds);
    void AddEditNodeToParentItem(const TocEditTree &tree, TocNodeId id,
                                 QStandardItem *parent);
    void ApplyHierarchyTransform(const TocTransformResult &result,
                                 const QString &undoText);
    void PushSnapshot(const TocEditTree &before, const TocEditTree &after,
                      const QList<TocNodeId> &beforeSelection,
                      const QList<TocNodeId> &afterSelection,
                      const QString &undoText, bool alreadyApplied);
    void RecordAppliedEdit(const TocEditTree &before,
                           const QList<TocNodeId> &beforeSelection,
                           const QString &undoText);
    void ShowTransformError(const TocTransformResult &result);

    void BuildModel(const TOCModel::TOCEntry &root_entry);
    void AddEntryToParentItem(const TOCModel::TOCEntry &entry, QStandardItem *parent, int level);

    void ExpandChildren(QStandardItem *item);
    void ReselectAndExpandItems(const QList<QStandardItem*> &items);

    void CreateContextMenuActions();
    void SetupContextMenu(const QPoint &point);

    void UpdateTreeViewDisplay();

    void CreateTOCModel();

    void ReadSettings();
    void WriteSettings();

    void ConnectSignalsToSlots();

    ///////////////////////////////
    // PRIVATE MEMBER VARIABLES
    ///////////////////////////////

    QSharedPointer<Book> m_Book;

    QList<Resource *> m_Resources;

    QStandardItemModel *m_TableOfContents;

    QPointer<QMenu> m_ContextMenu;

    QAction *m_Rename;
    QAction *m_Delete;
    QAction *m_CollapseAll;
    QAction *m_ExpandAll;
    QAction *m_MoveDown;
    QAction *m_MoveUp;
    QAction *m_Undo;
    QAction *m_Redo;

    TOCModel *m_TOCModel;

    Resource * m_BaseResource;

    QUndoStack *m_UndoStack;
    TocEditTree m_InitialTree;
    TocEditTree m_CurrentTree;
    TocNodeId m_NextNodeId;
    QHash<TocNodeId, QStandardItem *> m_ItemsById;
    bool m_ApplyingTree;
    bool m_SavedChanges;

    Ui::EditTOC ui;
};

#endif // EDITTOC_H
