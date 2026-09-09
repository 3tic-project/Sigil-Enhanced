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

#include <algorithm>
#include <QApplication>
#include <QStringList>
#include <QStandardItem>
#include <QItemSelection>
#include <QItemSelectionRange>
#include <QMap>
#include <QKeyEvent>
#include <QLineEdit>
#include <QScrollBar>
#include <QScopedValueRollback>
#include <QSet>
#include <QSignalBlocker>
#include <QTimer>
#include <QUndoCommand>
#include <QUndoStack>

#include <functional>

#include "BookManipulation/Book.h"
#include "Dialogs/EditTOC.h"
#include "Dialogs/SelectHyperlink.h"
#include "Misc/SettingsStore.h"
#include "Misc/Utility.h"
#include "ResourceObjects/HTMLResource.h"
#include "ResourceObjects/OPFResource.h"
#include "ResourceObjects/NavProcessor.h"
#include "ResourceObjects/Resource.h"
#include "sigil_constants.h"

static const QString SETTINGS_GROUP   = "edit_toc";
static const int COLUMN_INDENTATION = 20;
static const int NODE_ID_ROLE = Qt::UserRole + 100;

namespace {

class TocTreeSnapshotCommand : public QUndoCommand
{
public:
    using Apply = std::function<void(const TocEditTree &, const QList<TocNodeId> &)>;

    TocTreeSnapshotCommand(const TocEditTree &before, const TocEditTree &after,
                           const QList<TocNodeId> &beforeSelection,
                           const QList<TocNodeId> &afterSelection,
                           const QString &text, const Apply &apply,
                           bool alreadyApplied)
        : QUndoCommand(text),
          m_before(before),
          m_after(after),
          m_beforeSelection(beforeSelection),
          m_afterSelection(afterSelection),
          m_apply(apply),
          m_skipFirstRedo(alreadyApplied)
    {}

    void undo() override { m_apply(m_before, m_beforeSelection); }
    void redo() override
    {
        if (m_skipFirstRedo) {
            m_skipFirstRedo = false;
            return;
        }
        m_apply(m_after, m_afterSelection);
    }

private:
    TocEditTree m_before;
    TocEditTree m_after;
    QList<TocNodeId> m_beforeSelection;
    QList<TocNodeId> m_afterSelection;
    Apply m_apply;
    bool m_skipFirstRedo;
};

}

EditTOC::EditTOC(QSharedPointer<Book> book, QList<Resource *> resources, QWidget *parent)
    :
    QDialog(parent),
    m_Book(book),
    m_Resources(resources),
    m_TableOfContents(new QStandardItemModel(this)),
    m_ContextMenu(new QMenu(this)),
    m_Rename(nullptr),
    m_Delete(nullptr),
    m_CollapseAll(nullptr),
    m_ExpandAll(nullptr),
    m_MoveDown(nullptr),
    m_MoveUp(nullptr),
    m_Undo(nullptr),
    m_Redo(nullptr),
    m_TOCModel(new TOCModel(this)),
    m_BaseResource(NULL),
    m_UndoStack(new QUndoStack(this)),
    m_NextNodeId(1),
    m_ApplyingTree(false)
{
    // first determine the base resource pointer we will be working with
    //  is it the ncx or the nav
    QString version = m_Book->GetConstOPF()->GetEpubVersion();
    if (version.startsWith("3")) {
        m_BaseResource = m_Book->GetConstOPF()->GetNavResource();
    } else {
        m_BaseResource = m_Book->GetNCX();
    }

    // Remove the Nav resource from list of HTMLResources if it exists (EPUB3)
    HTMLResource* nav_resource = m_Book->GetConstOPF()->GetNavResource();
    if (nav_resource) {
        m_Resources.removeOne(nav_resource);
    }

    ui.setupUi(this);
    ui.TOCTree->setContextMenuPolicy(Qt::CustomContextMenu);
    ui.TOCTree->installEventFilter(this);
    ui.TOCTree->setModel(m_TableOfContents);
    ui.TOCTree->setIndentation(COLUMN_INDENTATION);
    ui.TOCTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    qApp->installEventFilter(this);
    CreateContextMenuActions();
    ConnectSignalsToSlots();

    CreateTOCModel();
    m_InitialTree = ConvertTableToEditTree();
    m_CurrentTree = m_InitialTree;
    UpdateTreeViewDisplay();
    ReadSettings();
    const bool hasCompatibilityNcx = version.startsWith('3') && m_Book->GetNCX();
    ui.SyncNcx->setVisible(hasCompatibilityNcx);
    if (hasCompatibilityNcx) {
        ui.OperationStatus->setText(tr(
            "This EPUB also contains an NCX. It will stay unchanged unless synchronization is enabled."));
    }
    QTimer::singleShot(0, this, SLOT(MakeDefaultFirstSelection()));
}

EditTOC::~EditTOC()
{
    qApp->removeEventFilter(this);
    WriteSettings();
}

void EditTOC::UpdateTreeViewDisplay()
{
    ui.TOCTree->expandAll();
}

void EditTOC::CreateTOCModel()
{
    m_TOCModel->SetBook(m_Book);

    TOCModel::TOCEntry toc_entry = m_TOCModel->GetRootTOCEntry();

    m_TableOfContents->clear();
    m_ItemsById.clear();
    m_NextNodeId = 1;
    QStringList header;
    header.append(tr("TOC Entry"));
    header.append(tr("Target"));
    m_TableOfContents->setHorizontalHeaderLabels(header);

    BuildModel(toc_entry);
}

void EditTOC::Save()
{
    if (TocTreeTransform::Equal(m_InitialTree, ConvertTableToEditTree())) return;
    QString version = m_Book->GetConstOPF()->GetEpubVersion();
    if (version.startsWith('3')) {
        NavProcessor navproc(m_Book->GetConstOPF()->GetNavResource());
        navproc.GenerateNavTOCFromTOCEntries(ConvertTableToEntries());
        if (ui.SyncNcx->isChecked() && m_Book->GetNCX()) {
            m_Book->GetNCX()->GenerateNCXFromTOCEntries(
                m_Book.data(), ConvertTableToEntries());
        }
    } else {
        // this is safe as all epub2's must hve an ncx (if not we made one for them)
        m_Book->GetNCX()->GenerateNCXFromTOCEntries(m_Book.data(), ConvertTableToEntries());
    }
}

TOCModel::TOCEntry EditTOC::ConvertTableToEntries()
{
    return ConvertItemToEntry(m_TableOfContents->invisibleRootItem());
}

TOCModel::TOCEntry EditTOC::ConvertItemToEntry(QStandardItem *item)
{
    TOCModel::TOCEntry entry;

    if (item != m_TableOfContents->invisibleRootItem()) {
        entry.text = item->text();
        QStandardItem *parent_item = item->parent();
        if (!parent_item) {
            parent_item = m_TableOfContents->invisibleRootItem();
        }
        entry.target = parent_item->child(item->row(), 1)->text();
    } else {
        entry.is_root = true;
    }

    if (!item->hasChildren()) {
        return entry;
    }

    for (int row = 0; row < item->rowCount(); row++) {
        entry.children.append(ConvertItemToEntry(item->child(row, 0)));
    }
    return entry;
}

TocEditTree EditTOC::ConvertTableToEditTree() const
{
    TocEditTree tree;
    TocEditNode root;
    root.id = 0;
    root.parentId = 0;
    tree.nodes.insert(0, root);
    QStandardItem *rootItem = m_TableOfContents->invisibleRootItem();
    for (int row = 0; row < rootItem->rowCount(); ++row) {
        AddItemToEditTree(rootItem->child(row, 0), 0, tree);
    }
    return tree;
}

void EditTOC::AddItemToEditTree(QStandardItem *item, TocNodeId parentId,
                                TocEditTree &tree) const
{
    if (!item) return;
    TocEditNode node;
    node.id = item->data(NODE_ID_ROLE).toULongLong();
    node.parentId = parentId;
    node.label = item->text();
    QStandardItem *parentItem = item->parent();
    if (!parentItem) parentItem = m_TableOfContents->invisibleRootItem();
    QStandardItem *targetItem = parentItem->child(item->row(), 1);
    if (targetItem) node.target = targetItem->text();
    tree.nodes.insert(node.id, node);
    tree.nodes[parentId].children.append(node.id);
    for (int row = 0; row < item->rowCount(); ++row) {
        AddItemToEditTree(item->child(row, 0), node.id, tree);
    }
}

QList<TocNodeId> EditTOC::SelectedNodeIds() const
{
    QList<TocNodeId> ids;
    const QModelIndexList rows = ui.TOCTree->selectionModel()->selectedRows(0);
    for (const QModelIndex &index : rows) {
        QStandardItem *item = m_TableOfContents->itemFromIndex(index);
        if (item) ids.append(item->data(NODE_ID_ROLE).toULongLong());
    }
    return ids;
}

void EditTOC::ApplyEditTree(const TocEditTree &tree,
                            const QList<TocNodeId> &selectedIds)
{
    QScopedValueRollback<bool> applyingTree(m_ApplyingTree, true);
    QSet<TocNodeId> expandedIds;
    for (auto it = m_ItemsById.cbegin(); it != m_ItemsById.cend(); ++it) {
        if (ui.TOCTree->isExpanded(it.value()->index())) expandedIds.insert(it.key());
    }
    const int verticalPosition = ui.TOCTree->verticalScrollBar()->value();
    const int horizontalPosition = ui.TOCTree->horizontalScrollBar()->value();
    QSignalBlocker blockSelection(ui.TOCTree->selectionModel());

    m_TableOfContents->clear();
    m_ItemsById.clear();
    QStringList header;
    header.append(tr("TOC Entry"));
    header.append(tr("Target"));
    m_TableOfContents->setHorizontalHeaderLabels(header);
    const QList<TocNodeId> roots = tree.nodes.value(tree.rootId).children;
    for (TocNodeId id : roots) {
        AddEditNodeToParentItem(tree, id, m_TableOfContents->invisibleRootItem());
    }

    for (TocNodeId id : expandedIds) {
        if (m_ItemsById.contains(id)) {
            ui.TOCTree->setExpanded(m_ItemsById.value(id)->index(), true);
        }
    }
    ui.TOCTree->selectionModel()->clearSelection();
    bool currentSet = false;
    for (TocNodeId id : selectedIds) {
        QStandardItem *item = m_ItemsById.value(id, nullptr);
        if (!item) continue;
        ui.TOCTree->selectionModel()->select(
            item->index(), QItemSelectionModel::Select | QItemSelectionModel::Rows);
        if (!currentSet) {
            ui.TOCTree->selectionModel()->setCurrentIndex(
                item->index(), QItemSelectionModel::NoUpdate);
            currentSet = true;
        }
    }
    ui.TOCTree->verticalScrollBar()->setValue(verticalPosition);
    ui.TOCTree->horizontalScrollBar()->setValue(horizontalPosition);
    m_CurrentTree = tree;
    UpdateMoveButtons();
}

void EditTOC::AddEditNodeToParentItem(const TocEditTree &tree, TocNodeId id,
                                      QStandardItem *parent)
{
    const TocEditNode node = tree.nodes.value(id);
    QStandardItem *entryItem = new QStandardItem(node.label);
    QStandardItem *targetItem = new QStandardItem(node.target);
    entryItem->setData(QVariant::fromValue<qulonglong>(id), NODE_ID_ROLE);
    parent->appendRow({entryItem, targetItem});
    m_ItemsById.insert(id, entryItem);
    m_NextNodeId = qMax(m_NextNodeId, id + 1);
    for (TocNodeId child : node.children) {
        AddEditNodeToParentItem(tree, child, entryItem);
    }
}

void EditTOC::ApplyHierarchyTransform(const TocTransformResult &result,
                                      const QString &undoText)
{
    if (!result.succeeded()) {
        ShowTransformError(result);
        return;
    }
    const TocEditTree before = ConvertTableToEditTree();
    const QList<TocNodeId> beforeSelection = SelectedNodeIds();
    PushSnapshot(before, result.tree, beforeSelection,
                 result.normalizedSelection, undoText, false);
}

void EditTOC::PushSnapshot(const TocEditTree &before, const TocEditTree &after,
                           const QList<TocNodeId> &beforeSelection,
                           const QList<TocNodeId> &afterSelection,
                           const QString &undoText, bool alreadyApplied)
{
    if (TocTreeTransform::Equal(before, after)) return;
    auto apply = [this](const TocEditTree &tree,
                        const QList<TocNodeId> &selection) {
        ApplyEditTree(tree, selection);
    };
    m_UndoStack->push(new TocTreeSnapshotCommand(
        before, after, beforeSelection, afterSelection, undoText, apply,
        alreadyApplied));
    if (alreadyApplied) m_CurrentTree = after;
}

void EditTOC::RecordAppliedEdit(const TocEditTree &before,
                                const QList<TocNodeId> &beforeSelection,
                                const QString &undoText)
{
    PushSnapshot(before, ConvertTableToEditTree(), beforeSelection,
                 SelectedNodeIds(), undoText, true);
}

void EditTOC::RecordItemEdit(QStandardItem *item)
{
    if (m_ApplyingTree || !item) return;
    const TocEditTree after = ConvertTableToEditTree();
    const QString undoText = item->column() == 0
        ? tr("Edit TOC entry") : tr("Edit TOC target");
    PushSnapshot(m_CurrentTree, after, SelectedNodeIds(), SelectedNodeIds(),
                 undoText, true);
}

void EditTOC::ShowTransformError(const TocTransformResult &result)
{
    switch (result.error) {
        case TocTransformError::AlreadyTopLevel:
            ui.OperationStatus->setText(
                tr("Cannot promote: a selected entry is already at the top level."));
            break;
        case TocTransformError::NoPreviousSibling:
            ui.OperationStatus->setText(
                tr("Cannot demote: a selected range has no previous sibling."));
            break;
        case TocTransformError::OverlappingPlans:
            ui.OperationStatus->setText(
                tr("The selected hierarchy changes overlap; no entries were moved."));
            break;
        default:
            ui.OperationStatus->setText(
                tr("The TOC hierarchy is inconsistent; no entries were moved."));
            break;
    }
}

void EditTOC::ExpandChildren(QStandardItem *item)
{
    QModelIndexList indexes;

    if (item->hasChildren()) {
        for (int i = 0; i < item->rowCount(); i++) {
            ExpandChildren(item->child(i, 0));
        }
    }

    ui.TOCTree->expand(item->index());
}

void EditTOC::sortItemSelectionRanges(QTreeView* treeView, QItemSelection& selection)
{
    if (!treeView || selection.isEmpty())  return;

    std::sort(selection.begin(), selection.end(), 
              [treeView](const QItemSelectionRange& a, 
                         const QItemSelectionRange& b) {
        QModelIndex indexA = a.topLeft();
        QModelIndex indexB = b.topLeft();
        if (indexA == indexB) return false;
#if 0
        // with a large tree this would be slow
        
        // Traversal check: See if B comes somewhere below A
        QModelIndex nextBelowA = indexA;
        while (nextBelowA.isValid()) {
            nextBelowA = treeView->indexBelow(nextBelowA);
            if (nextBelowA == indexB) {
                return true; // indexA comes first (higher up), so a < b is true
            }
        }
        return false;
#else
        // compare paths from root to node to determine order
        // path to root no matter what will be short in the number of nodes
        // so this should be much faster than a full tree traversal
        QList<size_t> pathA;
        QList<size_t> pathB;
        while(indexA.isValid()) {
            pathA.prepend(indexA.row() + 1);
            indexA = indexA.parent();
        }
        while(indexB.isValid()) {
            pathB.prepend(indexB.row() + 1);
            indexB = indexB.parent();
        }
        size_t n = qMin(pathA.size(), pathB.size());
        size_t i = 0;
        while (i < n) {
            if (pathA.at(i) < pathB.at(i)) return true;
            if (pathA.at(i) > pathB.at(i)) return false;
            // only if equal check the next unit of path
            i++;
        }
        // the shorter node list must be  higher in the tree
        return pathA.size() <  pathB.size();
#endif
        
    });
}

// Users can select a range of contiguous cells in many ways
// but Qt's internal selction mechanism does NOT try to group selections into ranges
// that did not start that way.  This causes broken editing of the TOC
// Prevent this by always structuring the user's selection to grow contiguous
// ranges so this code can work correctly
void EditTOC::StructureUserSelections()
{
    if (!ui.TOCTree->selectionModel()->hasSelection()) return;

    QModelIndexList selected_indexes = ui.TOCTree->selectionModel()->selectedRows();
    QList<QStandardItem*> items_selected;
    foreach(QModelIndex index, selected_indexes) {
        if (index.isValid()) {
            QStandardItem *item = m_TableOfContents->itemFromIndex(index);
            items_selected << item;
        }
    }
    QItemSelection nselection;
    QList<EditTOC::ContiguousRange>SelectedRanges = getContiguousRanges(items_selected);
    ui.TOCTree->selectionModel()->clear();
    foreach(const EditTOC::ContiguousRange& arange, SelectedRanges) {
        QModelIndex parent_index = arange.parent;
        QModelIndex topleft = m_TableOfContents->index(arange.startRow, 0, parent_index);
        QModelIndex bottomright = m_TableOfContents->index(arange.endRow, 1, parent_index);
        QItemSelection aselection;
        aselection.select(topleft, bottomright);
        nselection.merge(aselection,QItemSelectionModel::Select);
    }
    ui.TOCTree->selectionModel()->select(nselection, QItemSelectionModel::Select);
}

void EditTOC::ReselectAndExpandItems(const QList<QStandardItem*> &items)
{
    // Reselect the now moved items
    // but you must keep contigous groups together just like originally
    // selected by the user.
    ui.TOCTree->selectionModel()->clear();
    QItemSelection nselection;
    QList<EditTOC::ContiguousRange>SelectedRanges = getContiguousRanges(items);
    foreach(const EditTOC::ContiguousRange& arange, SelectedRanges) {
        QModelIndex parent_index = arange.parent;
        QModelIndex topleft = m_TableOfContents->index(arange.startRow, 0, parent_index);
        QModelIndex bottomright = m_TableOfContents->index(arange.endRow, 1, parent_index);
        QItemSelection aselection;
        aselection.select(topleft, bottomright);
        nselection.merge(aselection,QItemSelectionModel::Select);
    }
    // ui.TOCTree->selectionModel()->select(nselection, QItemSelectionModel::Select | QItemSelectionModel::Current);
    ui.TOCTree->selectionModel()->select(nselection, QItemSelectionModel::Select);
    // Expand Children
    foreach(QStandardItem* item, items) {
        ExpandChildren(item);
    }
}

QList<EditTOC::ContiguousRange> EditTOC::getContiguousRanges(const QList<QStandardItem*> &items)
{
    QList<EditTOC::ContiguousRange> ranges;
    if (items.isEmpty()) {
        return ranges;
    }
    // Key: Parent ModelIndex
    // Value: List of row numbers
    QMap<QModelIndex, QList<int>> groupedRows;

    // Group rows by their unique parent
    for (const QStandardItem* item: items) {
        QModelIndex index = item->index();
        if (index.isValid()) {
            auto key = index.parent();
            groupedRows[key].append(index.row());
        }
    }

   // Find contiguous segments within each group
    auto it = groupedRows.constBegin();
    while (it != groupedRows.constEnd()) {
        QModelIndex parent = it.key();
        QList<int> rows = it.value();

        // Sort rows to easily find gaps
        std::sort(rows.begin(), rows.end());

        int startRow = rows[0];
        int prevRow = rows[0];

        for (int i = 1; i < rows.size(); ++i) {
            // Check if the current row is not consecutive
            if (rows[i] != prevRow + 1) {
                ranges.append({parent, startRow, prevRow});
                startRow = rows[i];
            }
            prevRow = rows[i];
        }
        // Append the final range for this group
        ranges.append({parent, startRow, prevRow});
        ++it;
    }
    return ranges;
}

void EditTOC::MoveLeft()
{
    const QList<TocNodeId> selection = SelectedNodeIds();
    if (selection.isEmpty()) return;
    const TocTransformResult result = TocTreeTransform::Promote(
        ConvertTableToEditTree(), selection,
        ui.PromoteAdoptsFollowing->isChecked());
    if (!result.succeeded()) {
        ShowTransformError(result);
        return;
    }
    ApplyHierarchyTransform(result, tr("Promote TOC entries"));
    ui.OperationStatus->setText(
        tr("Promoted %1 item(s); reassigned %2 following item(s).")
            .arg(result.normalizedSelection.size()).arg(result.adoptedCount));
}

void EditTOC::MoveRight()
{
    const QList<TocNodeId> selection = SelectedNodeIds();
    if (selection.isEmpty()) return;
    const TocTransformResult result = TocTreeTransform::Demote(
        ConvertTableToEditTree(), selection);
    if (!result.succeeded()) {
        ShowTransformError(result);
        return;
    }
    ApplyHierarchyTransform(result, tr("Demote TOC entries"));
    ui.OperationStatus->setText(
        tr("Demoted %1 item(s).").arg(result.normalizedSelection.size()));
}

void EditTOC::MoveUp()
{
    // qDebug() << "In MoveUp with hasSelection: " << ui.TOCTree->selectionModel()->hasSelection();
    if (!ui.TOCTree->selectionModel()->hasSelection()) {
        return;
    }
    const TocEditTree before = ConvertTableToEditTree();
    const QList<TocNodeId> beforeSelection = SelectedNodeIds();
    QList<QStandardItem*> moved_items;
    StructureUserSelections();
    QItemSelection selection = ui.TOCTree->selectionModel()->selection();
    sortItemSelectionRanges(ui.TOCTree, selection);
    // for (const QItemSelectionRange& range: selection) {
    //     qDebug() << "range: " << range.parent().row() << range.top() << range.bottom();
    // }

    // first walk selection to see if any items at at the boundary and abort the move
    // Can't move up if this row is already the top most row of its parent
    bool at_boundary = false;
    for (const QItemSelectionRange& range : selection) {
        if (range.top() == 0) {
            at_boundary	= true;
            break;
	    }
    }
    if (at_boundary) return;
    
    for (const QItemSelectionRange& range : selection) {
        QModelIndex parent = range.parent();
	    QStandardItem *parent_item = m_TableOfContents->itemFromIndex(parent);
        if (!parent_item) {
            parent_item = m_TableOfContents->invisibleRootItem();
        }
        int top_row = range.top();
        int bottom_row = range.bottom();
        for (int r = top_row; r <= bottom_row; r++) {
            QStandardItem* item = parent_item->child(r, 0);
            QStandardItem *parent_item = item->parent();
            if (!parent_item) {
                parent_item = m_TableOfContents->invisibleRootItem();
            }
            int item_row = item->row();
            // Can't move up if this row is already the top one
            if (item_row == 0) continue;
            QList<QStandardItem *> row_items = parent_item->takeRow(item_row);
            parent_item->insertRow(item_row - 1, row_items);
            moved_items << item;        
        }
    }
    ReselectAndExpandItems(moved_items);
    RecordAppliedEdit(before, beforeSelection, tr("Move TOC entries up"));
}

void EditTOC::MoveDown()
{
    // qDebug() << "In MoveDown with hasSelection: " << ui.TOCTree->selectionModel()->hasSelection();
    if (!ui.TOCTree->selectionModel()->hasSelection()) {
        return;
    }
    const TocEditTree before = ConvertTableToEditTree();
    const QList<TocNodeId> beforeSelection = SelectedNodeIds();
    QList<QStandardItem*> moved_items;
    StructureUserSelections();
    QItemSelection selection = ui.TOCTree->selectionModel()->selection();
    sortItemSelectionRanges(ui.TOCTree, selection);
    // for (const QItemSelectionRange& range: selection) {
    //     qDebug() << "range: " << range.parent().row() << range.top() << range.bottom();
    // }

    // first walk selection to see if any items at the boundary and abort the move
    // can't move down if this row is already the last one of its parent
    bool at_boundary = false;
    for (const QItemSelectionRange& range : selection) {
        QModelIndex parent = range.parent();
        QStandardItem *parent_item = m_TableOfContents->itemFromIndex(parent);
        if (!parent_item) {
            parent_item = m_TableOfContents->invisibleRootItem();
        }
        if (range.bottom() == parent_item->rowCount() - 1) {
            at_boundary = true;
            break;
        }
    }
    if (at_boundary) return;

    for (const QItemSelectionRange& range : selection) {
        QModelIndex parent = range.parent();
        QStandardItem *parent_item = m_TableOfContents->itemFromIndex(parent);
        if (!parent_item) {
            parent_item = m_TableOfContents->invisibleRootItem();
        }
        int top_row = range.top();
        int bottom_row = range.bottom();
        for (int r = bottom_row; r >= top_row; r--) {
            int item_row = r;
            QStandardItem* item = parent_item->child(item_row, 0);
            // Can't move down if this row is already the last one of its parent
            if (item_row == parent_item->rowCount() - 1) continue;
            QList<QStandardItem *> row_items = parent_item->takeRow(item_row);
            parent_item->insertRow(item_row + 1, row_items);
            moved_items << item;
        }
    }
    ReselectAndExpandItems(moved_items);
    RecordAppliedEdit(before, beforeSelection, tr("Move TOC entries down"));
}

void EditTOC::AddEntryAbove()
{
    AddEntry(true);
}

void EditTOC::AddEntryBelow()
{
    AddEntry(false);
}

void EditTOC::AddEntry(bool above)
{
    QModelIndex index = CheckSelection(0);
    if (!index.isValid()) {
        return;
    }

    QStandardItem *item = m_TableOfContents->itemFromIndex(index);
    const TocEditTree before = ConvertTableToEditTree();
    const QList<TocNodeId> beforeSelection = SelectedNodeIds();

    QStandardItem *parent_item = item->parent();
    if (!parent_item) {
        parent_item = m_TableOfContents->invisibleRootItem();
    }

    // Add a new empty row of items
    QStandardItem *entry_item = new QStandardItem();
    QStandardItem *target_item = new QStandardItem();
    const TocNodeId id = m_NextNodeId++;
    entry_item->setData(QVariant::fromValue<qulonglong>(id), NODE_ID_ROLE);
    m_ItemsById.insert(id, entry_item);
    QList<QStandardItem *> row_items;
    row_items << entry_item << target_item ;
    int location = 1;
    if (above) {
        location = 0;
    }
    parent_item->insertRow(item->row() + location,row_items);

    // Select the new row
    ui.TOCTree->selectionModel()->clear();
    ui.TOCTree->setCurrentIndex(entry_item->index());
    ui.TOCTree->selectionModel()->select(entry_item->index(), QItemSelectionModel::SelectCurrent | QItemSelectionModel::Rows);
    RecordAppliedEdit(before, beforeSelection, tr("Add TOC entry"));
}


void EditTOC::MakeDefaultFirstSelection()
{
    QStandardItem *root_item = m_TableOfContents->invisibleRootItem();
    // Set the default to the first item
    if (root_item->rowCount() > 0) {
        ui.TOCTree->selectionModel()->clear();
        ui.TOCTree->selectionModel()->select(
            m_TableOfContents->index(0, 0, QModelIndex()),
            QItemSelectionModel::Select | QItemSelectionModel::Rows);
    }
    UpdateMoveButtons();
}


QModelIndex EditTOC::CheckSelection(int row)
{
    if (!ui.TOCTree->selectionModel()->hasSelection()) {
        return QModelIndex();
    }

    QModelIndexList selected_indexes = ui.TOCTree->selectionModel()->selectedRows(row);
    if (selected_indexes.count() != 1) {
        return QModelIndex();
    }
    return selected_indexes.first();
}

void EditTOC::DeleteEntry()
{
    QModelIndex index = CheckSelection(0);
    if (!index.isValid()) {
        return;
    }

    QStandardItem *item = m_TableOfContents->itemFromIndex(index);
    const TocEditTree before = ConvertTableToEditTree();
    const QList<TocNodeId> beforeSelection = SelectedNodeIds();

    QStandardItem *parent_item = item->parent();
    if (!parent_item) {
        parent_item = m_TableOfContents->invisibleRootItem();
    }

    std::function<void(QStandardItem *)> removeIds = [&](QStandardItem *node) {
        if (!node) return;
        m_ItemsById.remove(node->data(NODE_ID_ROLE).toULongLong());
        for (int row = 0; row < node->rowCount(); ++row) {
            removeIds(node->child(row, 0));
        }
    };
    removeIds(item);
    const QList<QStandardItem *> deletedRow = parent_item->takeRow(item->row());
    qDeleteAll(deletedRow);

    // make sure at leat one empty row exits for editing purposes
    if (m_TableOfContents->rowCount() == 0) {
        QStandardItem *entry_item = new QStandardItem();
        QStandardItem *target_item = new QStandardItem();
        entry_item->setText(tr("[placeholder]"));
        const TocNodeId id = m_NextNodeId++;
        entry_item->setData(QVariant::fromValue<qulonglong>(id), NODE_ID_ROLE);
        m_ItemsById.insert(id, entry_item);
        QList<QStandardItem *> row_items;
        row_items << entry_item << target_item ;
        parent_item->insertRow(0,row_items);

        // Select the new row
        ui.TOCTree->selectionModel()->clear();
        ui.TOCTree->setCurrentIndex(entry_item->index());
        ui.TOCTree->selectionModel()->select(entry_item->index(),
                                             QItemSelectionModel::SelectCurrent | QItemSelectionModel::Rows);
    }
    RecordAppliedEdit(before, beforeSelection, tr("Delete TOC entry"));
}

void EditTOC::SelectTarget()
{
    QModelIndex index = CheckSelection(1);
    if (!index.isValid()) {
        return;
    }

    QStandardItem *item = m_TableOfContents->itemFromIndex(index);
    // convert bookpath (epub root relative href) to relative to ncx or nav as appropriate
    QString ahref = item->text();
    if (ahref.indexOf(':') == -1) {
        std::pair<QString,QString> parts = Utility::parseRelativeHREF(ahref);
        ahref = Utility::buildRelativeHREF(Utility::buildRelativePath(m_BaseResource->GetRelativePath(),
                                                                      parts.first), parts.second);
    }

    SelectHyperlink select_target(ahref, m_BaseResource, "toc", m_Resources, m_Book, this);

    if (select_target.exec() == QDialog::Accepted) {
        QString href = select_target.GetTarget();
        // now convert ncx or nav relative path back to bookpath epub root relative)
        std::pair<QString,QString> parts = Utility::parseRelativeHREF(href);
        QString bookpath = Utility::buildBookPath(parts.first, m_BaseResource->GetFolder());
        item->setText(Utility::buildRelativeHREF(bookpath, parts.second));
    }
}

void EditTOC::ReadSettings()
{
    SettingsStore settings;
    settings.beginGroup(SETTINGS_GROUP);
    // The size of the window and it's full screen status
    QByteArray geometry = settings.value("geometry").toByteArray();

    if (!geometry.isNull()) {
        restoreGeometry(geometry);
    }

    // Column widths
    int size = settings.beginReadArray("column_data");

    for (int column = 0; column < size && column < ui.TOCTree->header()->count(); column++) {
        settings.setArrayIndex(column);
        int column_width = settings.value("width").toInt();

        if (column_width) {
            ui.TOCTree->setColumnWidth(column, column_width);
        }
    }
    settings.endArray();
    ui.PromoteAdoptsFollowing->setChecked(
        settings.value("promote_adopts_following", true).toBool());

    settings.endGroup();
}

void EditTOC::WriteSettings()
{
    SettingsStore settings;
    settings.beginGroup(SETTINGS_GROUP);
    // The size of the window and it's full screen status
    settings.setValue("geometry", saveGeometry());

    // Column widths
    settings.beginWriteArray("column_data");

    for (int column = 0; column < ui.TOCTree->header()->count(); column++) {
        settings.setArrayIndex(column);
        settings.setValue("width", ui.TOCTree->columnWidth(column));
    }

    settings.endArray();
    settings.setValue("promote_adopts_following",
                      ui.PromoteAdoptsFollowing->isChecked());

    settings.endGroup();
}

void EditTOC::Rename()
{
    if (!ui.TOCTree->selectionModel()->hasSelection()) {
        return;
    }

    if (ui.TOCTree->selectionModel()->selectedRows(0).count() != 1) {
        return;
    }

    ui.TOCTree->edit(ui.TOCTree->currentIndex());
}

void EditTOC::CollapseAll()
{
    ui.TOCTree->collapseAll();
}

void EditTOC::ExpandAll()
{
    ui.TOCTree->expandAll();
}

void EditTOC::CreateContextMenuActions()
{
    m_Rename = new QAction(tr("Rename"),     this);
    m_Delete = new QAction(tr("Delete"),     this);
    m_Rename->setShortcut(QKeySequence(Qt::ControlModifier | Qt::Key_R));
    m_Delete->setShortcut(QKeySequence::Delete);
    // Has to be added to the dialog itself for the keyboard shortcut to work.
    addAction(m_Rename);
    addAction(m_Delete);

    m_MoveUp = new QAction(tr("Move Up"),       this);
    m_MoveDown = new QAction(tr("Move Down"),   this);
    m_MoveUp->setShortcut(QKeySequence(Qt::ControlModifier | Qt::Key_Up));
    m_MoveDown->setShortcut(QKeySequence(Qt::ControlModifier | Qt::Key_Down));
    addAction(m_MoveUp);
    addAction(m_MoveDown);

    m_ExpandAll= new QAction(tr("Expand All"),     this);
    m_CollapseAll = new QAction(tr("Collapse All"),  this);

    m_Undo = m_UndoStack->createUndoAction(this, tr("Undo"));
    m_Redo = m_UndoStack->createRedoAction(this, tr("Redo"));
    m_Undo->setShortcuts(QKeySequence::Undo);
    m_Redo->setShortcuts(QKeySequence::Redo);
    m_Undo->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_Redo->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addAction(m_Undo);
    addAction(m_Redo);
}

void EditTOC::OpenContextMenu(const QPoint &point)
{
    SetupContextMenu(point);
    m_ContextMenu->exec(ui.TOCTree->viewport()->mapToGlobal(point));
    if (!m_ContextMenu.isNull()) {
        m_ContextMenu->clear();
    }
}

void EditTOC::SetupContextMenu(const QPoint &point)
{
    Q_UNUSED(point)
    m_ContextMenu->addAction(m_Undo);
    m_ContextMenu->addAction(m_Redo);
    m_ContextMenu->addSeparator();
    m_ContextMenu->addAction(m_Rename);
    m_ContextMenu->addAction(m_Delete);
    m_ContextMenu->addSeparator();
    m_ContextMenu->addAction(m_CollapseAll);
    m_ContextMenu->addAction(m_ExpandAll);
}

bool EditTOC::eventFilter(QObject *obj, QEvent *event)
{
    QLineEdit *inlineEditor = qobject_cast<QLineEdit *>(obj);
    if (inlineEditor && ui.TOCTree->isAncestorOf(inlineEditor)
            && (event->type() == QEvent::ShortcutOverride
                || event->type() == QEvent::KeyPress)) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        const bool undo = keyEvent->matches(QKeySequence::Undo);
        const bool redo = keyEvent->matches(QKeySequence::Redo);
        if (undo || redo) {
            if (event->type() == QEvent::ShortcutOverride) {
                event->accept();
            } else if (undo) {
                inlineEditor->undo();
            } else {
                inlineEditor->redo();
            }
            return true;
        }
    }

    if (obj == ui.TOCTree) {
        if (event->type() == QEvent::KeyPress) {
            QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
            int key = keyEvent->key();

            if (key == Qt::Key_Left) {
                MoveLeft();
                return true;
            } else if (key == Qt::Key_Right) {
                MoveRight();
                return true;
            }
        }
    }

    // pass the event on to the parent class
    return QDialog::eventFilter(obj, event);
}

void EditTOC::BuildModel(const TOCModel::TOCEntry &root_entry)
{
    foreach(const TOCModel::TOCEntry& child_entry, root_entry.children) {
        AddEntryToParentItem(child_entry, m_TableOfContents->invisibleRootItem(), 1);
    }
}

void EditTOC::AddEntryToParentItem(const TOCModel::TOCEntry &entry, QStandardItem *parent, int level)
{
    Q_ASSERT(parent);
    QStandardItem *entry_item = new QStandardItem(entry.text);
    QStandardItem *target_item = new QStandardItem(entry.target);
    const TocNodeId id = m_NextNodeId++;
    entry_item->setData(QVariant::fromValue<qulonglong>(id), NODE_ID_ROLE);
    m_ItemsById.insert(id, entry_item);

    QList<QStandardItem *> row_items;
    row_items << entry_item << target_item ;
    parent->appendRow(row_items);

    foreach(const TOCModel::TOCEntry &child_entry, entry.children) {
        AddEntryToParentItem(child_entry, entry_item, level + 1);
    }
}

void EditTOC::ConnectSignalsToSlots()
{
    connect(this,               SIGNAL(accepted()),           this, SLOT(Save()));
    connect(ui.AddEntryAbove,   SIGNAL(clicked()),            this, SLOT(AddEntryAbove()));
    connect(ui.AddEntryBelow,   SIGNAL(clicked()),            this, SLOT(AddEntryBelow()));
    connect(ui.DeleteEntry,     SIGNAL(clicked()),            this, SLOT(DeleteEntry()));
    connect(ui.MoveLeft,        SIGNAL(clicked()),            this, SLOT(MoveLeft()));
    connect(ui.MoveRight,       SIGNAL(clicked()),            this, SLOT(MoveRight()));
    connect(ui.MoveUp,          SIGNAL(clicked()),            this, SLOT(MoveUp()));
    connect(ui.MoveDown,        SIGNAL(clicked()),            this, SLOT(MoveDown()));
    connect(m_MoveUp,           SIGNAL(triggered()),          this, SLOT(MoveUp()));
    connect(m_MoveDown,         SIGNAL(triggered()),          this, SLOT(MoveDown()));
    connect(ui.SelectTarget,    SIGNAL(clicked()),            this, SLOT(SelectTarget()));
    connect(ui.TOCTree,         SIGNAL(customContextMenuRequested(const QPoint &)),
            this,               SLOT(OpenContextMenu(const QPoint &)));
    connect(m_Rename,           SIGNAL(triggered()), this, SLOT(Rename()));
    connect(m_Delete,           SIGNAL(triggered()), this, SLOT(DeleteEntry()));
    connect(m_CollapseAll,      SIGNAL(triggered()), this, SLOT(CollapseAll()));
    connect(m_ExpandAll,        SIGNAL(triggered()), this, SLOT(ExpandAll()));
    connect(ui.TOCTree->selectionModel(),
            SIGNAL(selectionChanged(QItemSelection,QItemSelection)),
            this, SLOT(UpdateMoveButtons()));
    connect(ui.PromoteAdoptsFollowing, SIGNAL(toggled(bool)),
            this, SLOT(UpdateMoveButtons()));
    connect(m_TableOfContents, SIGNAL(itemChanged(QStandardItem*)),
            this, SLOT(RecordItemEdit(QStandardItem*)));
}

void EditTOC::UpdateMoveButtons()
{
    const QList<TocNodeId> selection = SelectedNodeIds();
    if (selection.isEmpty()) {
        ui.MoveLeft->setEnabled(false);
        ui.MoveRight->setEnabled(false);
        return;
    }

    const TocEditTree tree = ConvertTableToEditTree();
    ui.MoveLeft->setEnabled(TocTreeTransform::Promote(
        tree, selection, ui.PromoteAdoptsFollowing->isChecked()).succeeded());
    ui.MoveRight->setEnabled(
        TocTreeTransform::Demote(tree, selection).succeeded());
}
