/************************************************************************
**
**  Copyright (C) 2022 Kevin B. Hendricks, Stratford, Ontario
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

#include <QKeySequence>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeView>
#include <QPainter>
#include <QModelIndex>
#include <QTextBrowser>
#include <QStyledItemDelegate>
#include "Misc/NumericItem.h"
#include "Misc/SettingsStoreExtend.h"
#include "Misc/Utility.h"
#include "ResourceObjects/Resource.h"
#include "ResourceObjects/HTMLResource.h"
#include "ResourceObjects/TextResource.h"
#include "PCRE2/PCRECache.h"
#include "PCRE2/SPCRE.h"
#include "Dialogs/ReplacementPreviewPlus.h"
#include "MainUI/FindReplacePlus.h"

static const QString SETTINGS_GROUP = "replacement_preview";

// These need to be changed if columns added or deleted
static const int BEFORE_COL = 0;
static const int AFTER_COL = 1;

static const int ISGROUP_ROLE = Qt::UserRole + 1;
static const int OFFSET_ROLE = Qt::UserRole + 2;
static const int SELECTION_START_ROLE = Qt::UserRole + 3;
static const int SELECTION_END_ROLE = Qt::UserRole + 4;
static const int SELECTED_TEXT_ROLE = Qt::UserRole + 5;

ReplacementPreviewPlus::ReplacementPreviewPlus(QWidget* parent)
    :
    QDialog(parent),
    m_ItemModel(new QStandardItemModel),
    m_TextDelegate(new StyledTextDelegatePlus()),
    m_ContextMenu(new QMenu(this)),
    m_replacement_count(0),
    m_current_count(0)
{
    m_FindReplacePlus = qobject_cast<FindReplacePlus*>(parent);
    ui.setupUi(this);
    ui.amtcb->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    ui.amtcb->addItem("10", 10);
    ui.amtcb->addItem("20", 20);
    ui.amtcb->addItem("30", 30);
    ui.amtcb->addItem("40", 40);
    ui.amtcb->addItem("50", 50);
    ui.amtcb->setEditable(false);
    //ReadSettings();
    CreateContextMenuActions();
    connectSignalsSlots();
    ui.PreviewTree->setSortingEnabled(false);
    ui.PreviewTree->setTextElideMode(Qt::ElideLeft);
}


ReplacementPreviewPlus::~ReplacementPreviewPlus()
{
    m_ItemModel->clear();
    delete m_ItemModel;
}

void ReplacementPreviewPlus::InitItemsProperties()
{
    QModelIndex index = ui.PreviewTree->rootIndex();
    QModelIndexList index_stack = { index };
    while (!index_stack.isEmpty()) {
        index = index_stack.takeFirst();
        if (index.data(ISGROUP_ROLE).toBool()) {
            ui.PreviewTree->setFirstColumnSpanned(index.row(), index.parent(), true);
        }
        if (index.row() != -1) {
            for (int i = 0; i < m_ItemModel->columnCount(); i++) {
                QModelIndex index_ = index.siblingAtColumn(i);
                QStandardItem* item = m_ItemModel->itemFromIndex(index_);
                item->setEditable(false);
            }
        }
        for (int i = 0; i < m_ItemModel->rowCount(index); i++) {
            index_stack << m_ItemModel->index(i, 0, index);
        }
    }
}

void ReplacementPreviewPlus::closeEvent(QCloseEvent *e)
{
    //WriteSettings();
    QDialog::closeEvent(e);
}

void ReplacementPreviewPlus::reject()
{
    //WriteSettings();
    QDialog::reject();
}

void ReplacementPreviewPlus::CreateTable()
{
    m_ItemModel->clear();
    m_Resources.clear();
    QStringList header;
    header.append(tr("Before"));
    header.append(tr("After"));

    m_ItemModel->setHorizontalHeaderLabels(header);
    ui.PreviewTree->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui.PreviewTree->setModel(m_ItemModel);
    ui.PreviewTree->setContextMenuPolicy(Qt::CustomContextMenu);
    QList<Resource*> resources;
    QString presearch_regex, search_regex, replace_text;

    resources = m_FindReplacePlus->GetAllResourcesToSearch();
    presearch_regex = m_FindReplacePlus->GetPreSearchRegex();
    search_regex = m_FindReplacePlus->GetSearchRegex();
    replace_text = m_FindReplacePlus->GetReplace();


    m_current_count = 0;
    foreach(Resource* resource, resources ) {
        qApp->processEvents();
        QString bookpath = resource->GetRelativePath();
        QString text;
        HTMLResource *html_resource = qobject_cast<HTMLResource *>(resource);
        TextResource *text_resource = qobject_cast<TextResource *>(resource);
        if (html_resource) {
            QReadLocker locker(&html_resource->GetLock());
            text = html_resource->GetText();
        } else if (text_resource) {
            QReadLocker locker(&text_resource->GetLock());
            text = text_resource->GetText();
        }
        if (!text.isEmpty()) {

            // Book Path
            QStandardItem* bkpath_item;
            bkpath_item = new QStandardItem();
            bkpath_item->setData(true, ISGROUP_ROLE);
            bkpath_item->setText(bookpath);
            if (!m_Resources.contains(bookpath)) m_Resources[bookpath] = resource;

            // search the text using the search_regex and get all matches

            SPCRE *spcre = PCRECache::instance()->getObject(search_regex);
            QList<Utility::MatchInfo> match_info = Utility::GetSearchInfoWithPreSearch(presearch_regex, search_regex, text);

            for (int i = 0; i < match_info.count(); ++i) {
                m_current_count++;

                QString match_segment = Utility::Substring(match_info.at(i).offset.first,
                                                           match_info.at(i).offset.second,
                                                           text);
                QString new_text;
                bool can_replace = spcre->replaceText(match_segment, match_info.at(i).capture_groups_offsets,
                                                      replace_text, new_text);

                // set pre and post context strings
                QString prior_context  = GetPriorContext(match_info.at(i).offset.first, text, m_context_amt);
                QString post_context = GetPostContext(match_info.at(i).offset.second, text, m_context_amt);

                // finally create before and after snippets
                QString orig_snip = prior_context + match_segment + post_context;
                QString new_snip;
                if (can_replace) {
                    new_snip = prior_context + new_text + post_context;
                } else {
                    new_snip = orig_snip;
                    new_text = match_segment;
                }
                int start = match_info.at(i).offset.first;

                // finally add a row to the table
                QList<QStandardItem *> rowItems;
                QStandardItem *item=new QStandardItem();

                // Before
                item = new QStandardItem();
                item->setText(orig_snip);
                item->setData(false,ISGROUP_ROLE);
                item->setData(start,OFFSET_ROLE);
                item->setData(prior_context.length(), SELECTION_START_ROLE);
                item->setData(prior_context.length() + match_segment.length(), SELECTION_END_ROLE);
                item->setData(match_segment, SELECTED_TEXT_ROLE);
                rowItems << item;

                // After
                item = new QStandardItem();
                item ->setText(new_snip);
                item->setData(false, ISGROUP_ROLE);
                item->setData(prior_context.length(), SELECTION_START_ROLE);
                item->setData(prior_context.length() + new_text.length(), SELECTION_END_ROLE);
                item->setData(new_text, SELECTED_TEXT_ROLE);
                rowItems << item;

                bkpath_item->appendRow(rowItems);
            }
            // Add item to table
            if (bkpath_item->hasChildren()) {
                m_ItemModel->appendRow(bkpath_item);
            }
        }
    }

    InitItemsProperties();
    // display the current count above the table (with a buffer to the right)
    ui.cntamt->setText(QString::number(m_current_count) + "   ");

    // set styled text item delegate for columns 2 (before) and 3 (after)
    ui.PreviewTree->setItemDelegateForColumn(BEFORE_COL, m_TextDelegate);
    ui.PreviewTree->setItemDelegateForColumn(AFTER_COL, m_TextDelegate);

    for (int i = 0; i < ui.PreviewTree->header()->count(); i++) {
        ui.PreviewTree->resizeColumnToContents(i);
    }
    ui.PreviewTree->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui.PreviewTree->setRootIsDecorated(false); // disable collapse button
    ui.PreviewTree->setColumnWidth(0, QVariant(this->width() / 2).toInt());
    ui.PreviewTree->expandAll();
}


QString ReplacementPreviewPlus::GetPriorContext(int match_start, const QString& text, int amt)
{
    int context_start = match_start - amt;
    if  (context_start < 0) context_start = 0;
    // find first whitespace to break at
    while (context_start < match_start && !text.at(context_start).isSpace()) {
        context_start++;
    }
    QString prior_context = Utility::Substring(context_start, match_start, text);
    //prior_context.replace('\n',' ');
    return prior_context;
}


QString ReplacementPreviewPlus::GetPostContext(int match_end, const QString& text, int amt)
{
    int context_end = match_end + amt;
    int end_pos = text.length();
    if  (context_end > end_pos) context_end = end_pos;
    // find last whitespace to break at
    while (context_end > match_end && !text.at(context_end-1).isSpace()) {
        context_end--;
    }
    QString post_context = Utility::Substring(match_end, context_end, text);
    //post_context.replace('\n',' ');
    return post_context;
}


void ReplacementPreviewPlus::ApplyReplacements()
{
    // order of replacements is crucial
    // replacements must be made in reverse offset order (bottom to top) of file
    int rows = m_ItemModel->rowCount();
    for (int i=0; i < rows; i++) {
        QString bookpath = m_ItemModel->item(i, 0)->text();
        QString match_segment = m_ItemModel->item(i,2)->data(Qt::UserRole+3).toString();
        int startpos = m_ItemModel->item(i,1)->text().toInt();
        int n = match_segment.length();
        QString new_text = m_ItemModel->item(i,3)->data(Qt::UserRole+3).toString();
        Resource * resource = m_Resources[bookpath];
        HTMLResource *html_resource = qobject_cast<HTMLResource *>(resource);
        TextResource *text_resource = qobject_cast<TextResource *>(resource);
        if (html_resource) {
            QWriteLocker locker(&html_resource->GetLock());
            QString text = html_resource->GetText();
            QString updated_text = text.replace(startpos, n, new_text);
            html_resource->SetText(updated_text);
            m_replacement_count++;
        } else if (text_resource) {
            QWriteLocker locker(&text_resource->GetLock());
            QString text = text_resource->GetText();
            QString updated_text = text.replace(startpos, n, new_text);
            text_resource->SetText(updated_text);
            m_replacement_count++;
        }
    }
    close();
}

void ReplacementPreviewPlus::DeleteSelectedRows()
{
    if (!ui.PreviewTree->selectionModel()->hasSelection()) return;
    // This QTreeView is limited to ContiguousSelection mode
    // so should be able to delete in blocks
    QModelIndex index = ui.PreviewTree->selectionModel()->selectedRows(0).first();
    int count = ui.PreviewTree->selectionModel()->selectedRows(0).count();
    int row = index.row();
    QModelIndex parent_index = index.parent();
    m_ItemModel->removeRows(row, count, parent_index);
    m_current_count = m_current_count - count;

    // display the current count above the table (with a buffer to the right)
    ui.cntamt->setText(QString::number(m_current_count) + "   ");

}

void ReplacementPreviewPlus::ReadSettings()
{
    SettingsStoreExtend settings;
    settings.beginGroup(SETTINGS_GROUP);
    QByteArray geometry = settings.value("geometry").toByteArray();
    if (!geometry.isNull()) {
        restoreGeometry(geometry);
    }
    m_context_amt = settings.value("context",20).toInt();
    SetContextCB(m_context_amt);
    settings.endGroup();
}

void ReplacementPreviewPlus::SetContextCB(int val)
{
    int index = 0;
    if (val >= 20) index = 1;
    if (val >= 30) index = 2;
    if (val >= 40) index = 3;
    if (val >= 50) index = 4;
    ui.amtcb->setCurrentIndex(index);
}

void ReplacementPreviewPlus::ChangeContext()
{
    int val = ui.amtcb->currentData().toInt();
    if (val != m_context_amt) {
        m_context_amt = val;
        CreateTable();
    }
}

void ReplacementPreviewPlus::WriteSettings()
{
    SettingsStoreExtend settings;
    settings.beginGroup(SETTINGS_GROUP);
    settings.setValue("geometry", saveGeometry());
    settings.setValue("context", m_context_amt);
    settings.endGroup();
}

void ReplacementPreviewPlus::CreateContextMenuActions()
{
    m_Delete    =   new QAction(tr("Delete Selected Rows"),  this);
    QList<QKeySequence> shortcuts;
    shortcuts << QKeySequence::Delete << QKeySequence::Cut << QKeySequence(Qt::Key_Backspace);
    m_Delete->setShortcuts(shortcuts);
    addAction(m_Delete);
}

void ReplacementPreviewPlus::OpenContextMenu(const QPoint &point)
{
    SetupContextMenu(point);
    m_ContextMenu->exec(ui.PreviewTree->viewport()->mapToGlobal(point));
    if (!m_ContextMenu.isNull()) {
        m_ContextMenu->clear();
        m_Delete->setEnabled(true);
     }
}

void ReplacementPreviewPlus::SetupContextMenu(const QPoint &point)
{
    m_ContextMenu->addAction(m_Delete);
    m_Delete->setEnabled(ui.PreviewTree->selectionModel()->hasSelection());
}


void ReplacementPreviewPlus::connectSignalsSlots()
{
    connect(ui.leFilter,  SIGNAL(textChanged(QString)), this, SLOT(FilterEditTextChangedSlot(QString)));
    connect(m_Delete, SIGNAL(triggered()), this, SLOT(DeleteSelectedRows()));
    connect(ui.Apply, SIGNAL(clicked()), this, SLOT(ApplyReplacements()));
    connect(ui.btnClose->button(QDialogButtonBox::Close), SIGNAL(clicked()), this, SLOT(close()));
    connect(ui.amtcb, SIGNAL(currentIndexChanged(int)), this, SLOT(ChangeContext()));
    connect(ui.PreviewTree, SIGNAL(customContextMenuRequested(const QPoint &)),
            this, SLOT(OpenContextMenu(const QPoint &)));
}

void StyledTextDelegatePlus::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    return QStyledItemDelegate::paint(painter, option, index);
}
