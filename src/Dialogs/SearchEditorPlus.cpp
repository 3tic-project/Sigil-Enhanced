#include <QtCore/QSignalMapper>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QMessageBox>
#include <QtGui/QContextMenuEvent>
#include <QRegularExpression>
#include <QItemSelectionModel>
#include <QItemSelection>
#include <QDebug>

#include "Dialogs/SearchEditorItemDelegate.h"
#include "Dialogs/SearchEditorPlus.h"
#include "Misc/Utility.h"
#include "Misc/SettingsStoreExtend.h"


static const QString SETTINGS_GROUP = "saved_searches";
static const QString FILE_EXTENSION = "ini";

SearchEditorPlus::SearchEditorPlus(QWidget* parent)
    :
    QDialog(parent),
    m_LastFolderOpen(QString()),
    m_ContextMenu(new QMenu(this)),
    m_SearchToLoad(nullptr),
    m_CntrlDelegate(new SearchEditorItemDelegate())
{
    m_CntrlDelegate->setPlusMode(true);
    ui.setupUi(this);
    ui.FilterText->installEventFilter(this);
    ui.LoadSearch->setDefault(true);
    ui.CountsReportPB->hide();
    SetupSearchEditorTree();
    CreateContextMenuActions();
    ConnectSignalsSlots();
    ExpandAll();
}


SearchEditorPlus::~SearchEditorPlus()
{
    // clean up any existing copied saved search entries
    while (m_SavedSearchEntries.count()) {
        SearchEditorModelPlus::searchEntry * entry = m_SavedSearchEntries.at(0);
        if (entry) delete entry;
        m_SavedSearchEntries.removeAt(0);
    }

    // clean up current search entries
    while (m_CurrentSearchEntries.count()) {
        SearchEditorModelPlus::searchEntry * entry = m_CurrentSearchEntries.at(0);
        if (entry) delete entry;
        m_CurrentSearchEntries.removeAt(0);
    }

    // clean up m_SearchToLoad
    if (m_SearchToLoad) delete m_SearchToLoad;
    m_SearchToLoad = nullptr;
}


void SearchEditorPlus::SetupSearchEditorTree()
{
    m_SearchEditorModel = SearchEditorModelPlus::instance();
    ui.SearchEditorTree->setModel(m_SearchEditorModel);
    ui.SearchEditorTree->setContextMenuPolicy(Qt::CustomContextMenu);
    ui.SearchEditorTree->setSortingEnabled(false);
    ui.SearchEditorTree->setWordWrap(true);
    ui.SearchEditorTree->setAlternatingRowColors(true);
    ui.SearchEditorTree->installEventFilter(this);
    ui.SearchEditorTree->setItemDelegateForColumn(4, m_CntrlDelegate);
    ui.SearchEditorTree->header()->setStretchLastSection(true);
}

void SearchEditorPlus::ShowMessage(const QString &message)
{
    ui.Message->setText(message);
    ui.Message->repaint();
    QApplication::processEvents();
}

bool SearchEditorPlus::SaveData(QList<SearchEditorModelPlus::searchEntry *> entries, QString filename)
{
    QString message = m_SearchEditorModel->SaveData(entries, filename);

    if (!message.isEmpty()) {
        Utility::DisplayStdErrorDialog(tr("Cannot save entries.") + "\n\n" + message);
    }

    return message.isEmpty();
}

bool SearchEditorPlus::SaveTextData(QList<SearchEditorModelPlus::searchEntry *> entries, QString filename, QChar sep)
{
    QString message = m_SearchEditorModel->SaveTextData(entries, filename, sep);

    if (!message.isEmpty()) {
        Utility::DisplayStdErrorDialog(tr("Cannot save entries.") + "\n\n" + message);
    }

    return message.isEmpty();
}

void SearchEditorPlus::LoadFindReplace()
{
    SelectionChanged();
    emit RestartSearch();
    // ownership of the entry to load Find & Replace with remains here
    emit LoadSelectedSearchRequest(GetSelectedEntry(false));
}

void SearchEditorPlus::Find()
{
    emit FindSelectedSearchRequest();
}

void SearchEditorPlus::ReplaceCurrent()
{
    emit ReplaceCurrentSelectedSearchRequest();
}

void SearchEditorPlus::Replace()
{
    emit ReplaceSelectedSearchRequest();
}

void SearchEditorPlus::CountAll()
{
    emit CountAllSelectedSearchRequest();
}

void SearchEditorPlus::ReplaceAll()
{
    emit ReplaceAllSelectedSearchRequest();
}


void SearchEditorPlus::showEvent(QShowEvent *event)
{
    bool has_settings = ReadSettings();
    ui.FilterText->setFocus();

    // If the user has no persisted columns data yet, just resize automatically
    if (!has_settings) {
        for (int column = 0; column < ui.SearchEditorTree->header()->count(); column++) {
            ui.SearchEditorTree->resizeColumnToContents(column);
        }

        // Hitting an issue for first time user resizing the Find column to only width
        // of the heading. Just force an initial width instead.
        ui.SearchEditorTree->setColumnWidth(1, 150);
    }
}

bool SearchEditorPlus::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == ui.FilterText) {
        if (event->type() == QEvent::KeyPress) {
            QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
            int key = keyEvent->key();

            if (key == Qt::Key_Down) {
                ui.SearchEditorTree->setFocus();
                return true;
            }
        }
    }

    // pass the event on to the parent class
    return QDialog::eventFilter(obj, event);
}

void SearchEditorPlus::SettingsFileModelUpdated()
{
    ui.SearchEditorTree->expandAll();
    emit ShowStatusMessageRequest(tr("Saved Searches loaded from file."));
}

void SearchEditorPlus::ModelItemDropped(const QModelIndex &index)
{
    if (index.isValid()) {
        ui.SearchEditorTree->expand(index);
    }
}

int SearchEditorPlus::SelectedRowsCount()
{
    int count = 0;

    if (ui.SearchEditorTree->selectionModel()->hasSelection()) {
        count = ui.SearchEditorTree->selectionModel()->selectedRows(0).count();
    }

    return count;
}

// sets m_SearchToLoad to the currently selected entry and returns it
SearchEditorModelPlus::searchEntry* SearchEditorPlus::GetSelectedEntry(bool show_warning)
{
    // Note: a SeachEditorModel::searchEntry is a simple struct that is created
    // by new in SearchEditorModelPlus GetEntry() and GetEntries()
    // These must be manually deleted when done to prevent memory leaks

    SearchEditorModelPlus::searchEntry *entry = NULL;

    if (ui.SearchEditorTree->selectionModel()->hasSelection()) {
        QStandardItem *item = NULL;
        QModelIndexList selected_indexes = ui.SearchEditorTree->selectionModel()->selectedRows(0);

        if (selected_indexes.count() == 1) {
            item = m_SearchEditorModel->itemFromIndex(selected_indexes.first());
        } else if (show_warning) {
            Utility::DisplayStdErrorDialog(tr("You cannot select more than one entry when using this action."));
            return entry;
        }

        if (item) {
            if (!m_SearchEditorModel->ItemIsGroup(item)) {
                entry = m_SearchEditorModel->GetEntry(item);
                if (m_SearchToLoad) delete m_SearchToLoad;
                m_SearchToLoad = entry;
            } else if (show_warning) {
                Utility::DisplayStdErrorDialog(tr("You cannot select a group for this action."));
            }
        }
    }

    return entry;
}

void SearchEditorPlus::SetCurrentEntriesFromFullName(const QString &name)
{

    while (m_CurrentSearchEntries.count()) {
        SearchEditorModelPlus::searchEntry * entry = m_CurrentSearchEntries.at(0);
        if (entry) delete entry;
        m_CurrentSearchEntries.removeAt(0);
    }

    QList<SearchEditorModelPlus::searchEntry *> selected_entries;


    QStandardItem * nameditem = m_SearchEditorModel->GetItemFromName(name);
    if (nameditem) {
        QList<QStandardItem *> items = m_SearchEditorModel->GetNonGroupItems(nameditem);
        if (!ItemsAreUnique(items)) {
            return;
        }

        foreach(SearchEditorModelPlus::searchEntry* entry, m_SearchEditorModel->GetEntries(items)) {
            m_CurrentSearchEntries << entry;
        }
    }
}



QList<SearchEditorModelPlus::searchEntry *> SearchEditorPlus::GetSelectedEntries()
{
    // Note: a SeachEditorModel::searchEntry is a simple struct that is created
    // by new in SearchEditorModelPlus GetEntry() and GetEntries()
    // These must be manually deleted when done to prevent memory leaks

    QList<SearchEditorModelPlus::searchEntry *> selected_entries;

    if (ui.SearchEditorTree->selectionModel()->hasSelection()) {
        QList<QStandardItem *> items = m_SearchEditorModel->GetNonGroupItems(GetSelectedItems());

        if (!ItemsAreUnique(items)) {
            return selected_entries;
        }

        selected_entries = m_SearchEditorModel->GetEntries(items);
    }

    return selected_entries;
}


QList<QStandardItem *> SearchEditorPlus::GetSelectedItems()
{
    // Shift-click order is top to bottom regardless of starting position
    // Ctrl-click order is first clicked to last clicked (included shift-clicks stay ordered as is)
    QModelIndexList selected_indexes = ui.SearchEditorTree->selectionModel()->selectedRows(0);
    QList<QStandardItem *> selected_items;
    foreach(QModelIndex index, selected_indexes) {
        selected_items.append(m_SearchEditorModel->itemFromIndex(index));
    }
    return selected_items;
}

bool SearchEditorPlus::ItemsAreUnique(QList<QStandardItem *> items)
{
    // Although saving a group and a sub item works, it could be confusing to users to
    // have and entry appear twice so its more predictable just to prevent it and warn the user
    QList<QStandardItem *> nodupitems = items;
    std::sort(nodupitems.begin(), nodupitems.end());
    nodupitems.erase(std::unique(nodupitems.begin(), nodupitems.end()), nodupitems.end());
    if (nodupitems.count() != items.count()) {
        ShowMessage(tr("Do not select Groups and Entries together, or most operations will fail!"));
        return false;
    }

    return true;
}

QStandardItem *SearchEditorPlus::AddEntry(bool is_group, SearchEditorModelPlus::searchEntry *search_entry, bool insert_after)
{
    QStandardItem *parent_item = NULL;
    QStandardItem *new_item = NULL;
    int row = 0;

    // If adding a new/blank entry add it after the selected entries.
    if (insert_after) {
        if (ui.SearchEditorTree->selectionModel()->hasSelection()) {
            parent_item = GetSelectedItems().last();

            if (!parent_item) {
                return parent_item;
            }

            if (!m_SearchEditorModel->ItemIsGroup(parent_item)) {
                row = parent_item->row() + 1;
                parent_item = parent_item->parent();
            }
        }
    }

    // Make sure the new entry can be seen
    if (parent_item) {
        ui.SearchEditorTree->expand(parent_item->index());
    }

    new_item = m_SearchEditorModel->AddEntryToModel(search_entry, is_group, parent_item, row);
    QModelIndex new_index = new_item->index();
    // Select the added item and set it for editing
    ui.SearchEditorTree->selectionModel()->clear();
    ui.SearchEditorTree->setCurrentIndex(new_index);
    ui.SearchEditorTree->selectionModel()->select(new_index, QItemSelectionModel::SelectCurrent | QItemSelectionModel::Rows);
    ui.SearchEditorTree->edit(new_index);
    ui.SearchEditorTree->setCurrentIndex(new_index);
    ui.SearchEditorTree->selectionModel()->select(new_index, QItemSelectionModel::SelectCurrent | QItemSelectionModel::Rows);
    return new_item;
}

QStandardItem *SearchEditorPlus::AddGroup()
{
    // will clean up after itself since entry is NULL
    return AddEntry(true);
}


void SearchEditorPlus::Edit()
{
    ui.SearchEditorTree->edit(ui.SearchEditorTree->currentIndex());
}

void SearchEditorPlus::Cut()
{
    if (Copy()) {
        Delete();
    }
}


// NOTE: Provides the remembered state needed by downstream F&R routines
void SearchEditorPlus::RecordEntryAsCompleted(SearchEditorModelPlus::searchEntry* entry)
{

    for (int i = 0; i < m_CurrentSearchEntries.count(); i++) {
        if (m_CurrentSearchEntries.at(i) == entry) {
            m_CurrentSearchEntries.removeAt(i);
            delete entry;
        }
    }
}


// NOTE: Ownership of these pointers remains here with the Search Editor
// This is just a copy of the remaining current search entry list
QList<SearchEditorModelPlus::searchEntry*> SearchEditorPlus::GetCurrentEntries()
{
    QList<SearchEditorModelPlus::searchEntry*> entries;
    for(int i=0; i < m_CurrentSearchEntries.count(); i++) {
        entries << m_CurrentSearchEntries.at(i);
    }
    return entries;
}

void SearchEditorPlus::SelectionChanged()
{
    // any time the current selection changes update m_CurrentSearchEntries
    while (m_CurrentSearchEntries.count()) {
        SearchEditorModelPlus::searchEntry * entry = m_CurrentSearchEntries.at(0);
        if (entry) delete entry;
        m_CurrentSearchEntries.removeAt(0);
    }
    foreach(SearchEditorModelPlus::searchEntry* entry, GetSelectedEntries()) {
        m_CurrentSearchEntries << entry;
    }
}


bool SearchEditorPlus::Copy()
{
    if (SelectedRowsCount() < 1) {
        return false;
    }


    // verify user is not trying to copy groups
    foreach(QStandardItem * item, GetSelectedItems()) {
        SearchEditorModelPlus::searchEntry *entry = m_SearchEditorModel->GetEntry(item);
        bool is_group = entry->is_group;
        delete entry;
        if (is_group) {
            Utility::DisplayStdErrorDialog(tr("You cannot Copy or Cut groups - use drag-and-drop.")) ;
            return false;
        }
    }

    // clean up previous copied entries
    while (m_SavedSearchEntries.count()) {
        SearchEditorModelPlus::searchEntry * entry = m_SavedSearchEntries.at(0);
        if (entry) delete entry;
        m_SavedSearchEntries.removeAt(0);
    }


    QList<SearchEditorModelPlus::searchEntry *> entries = GetSelectedEntries();
    if (!entries.count()) {
        return false;
    }

    foreach(SearchEditorModelPlus::searchEntry * entry, entries) {
        m_SavedSearchEntries.append(entry);
    }
    return true;
}

void SearchEditorPlus::Paste()
{
    foreach(SearchEditorModelPlus::searchEntry * entry, m_SavedSearchEntries) {
        AddEntry(entry->is_group, entry);
    }
}

void SearchEditorPlus::Delete()
{
    if (SelectedRowsCount() < 1) {
        return;
    }

    // Delete one at a time as selection may not be contiguous
    int row = -1;
    QModelIndex parent_index;

    while (ui.SearchEditorTree->selectionModel()->hasSelection()) {
        QModelIndex index = ui.SearchEditorTree->selectionModel()->selectedRows(0).first();

        if (index.isValid()) {
            row = index.row();
            parent_index = index.parent();
            m_SearchEditorModel->removeRows(row, 1, parent_index);
        }
    }

    // Select the nearest row in the group, or the group if no rows left
    int parent_row_count;

    if (parent_index.isValid()) {
        parent_row_count = m_SearchEditorModel->itemFromIndex(parent_index)->rowCount();
    } else {
        parent_row_count = m_SearchEditorModel->invisibleRootItem()->rowCount();
    }

    if (parent_row_count && row >= parent_row_count) {
        row = parent_row_count - 1;
    }

    if (parent_row_count == 0) {
        row = parent_index.row();
        parent_index = parent_index.parent();
    }

    QModelIndex select_index = m_SearchEditorModel->index(row, 0, parent_index);
    ui.SearchEditorTree->setCurrentIndex(select_index);
    ui.SearchEditorTree->selectionModel()->select(select_index, QItemSelectionModel::SelectCurrent | QItemSelectionModel::Rows);
}

void SearchEditorPlus::Reload()
{
    QMessageBox::StandardButton button_pressed;
    button_pressed = Utility::warning(this, tr("Sigil"), tr("Are you sure you want to reload all entries?  This will overwrite any unsaved changes."), QMessageBox::Ok | QMessageBox::Cancel);

    if (button_pressed == QMessageBox::Ok) {
        m_SearchEditorModel->LoadInitialData();
    }
}

void SearchEditorPlus::Import()
{
    if (SelectedRowsCount() > 1) {
        return;
    }

    // Get the filename to import from
    QMap<QString,QString> file_filters;
    file_filters[ "ini" ] = tr("Sigil INI files (*.ini)");
    file_filters[ "csv" ] = tr("CSV files (*.csv)");
    file_filters[ "txt" ] = tr("Text files (*.txt)");
    QStringList filters = file_filters.values();
    QString filter_string = "";
    foreach(QString filter, filters) {
        filter_string += filter + ";;";
    }
    QString default_filter = file_filters.value("ini");

    QFileDialog::Options options = QFileDialog::Options();
#ifdef Q_OS_MAC
    options = options | QFileDialog::DontUseNativeDialog;
#endif
    QString filename = QFileDialog::getOpenFileName(this,
                       tr("Import Search Entries"),
                       m_LastFolderOpen,
                       filter_string,
                       &default_filter,
                       options);

    // Load the file and save the last folder opened
    if (!filename.isEmpty()) {
        QFileInfo fi(filename);
        QString ext = fi.suffix().toLower();
        QChar sep;
        if (ext == "txt") {
            sep = QChar(9);
        } else if (ext == "csv") {
            sep = QChar(',');
        }
        // Create a new group for the imported items after the selected item
        // Avoids merging with existing groups, etc.
        QStandardItem *item = AddGroup();

        if (item) {
            m_SearchEditorModel->Rename(item, "Imported");
            if (ext == "ini") {
                m_SearchEditorModel->LoadData(filename, item);
            } else {
                m_SearchEditorModel->LoadTextData(filename, item, sep);
            }
            m_LastFolderOpen = QFileInfo(filename).absolutePath();
            WriteSettings();
        }
    }
}

void SearchEditorPlus::ExportAll()
{
    QList<QStandardItem *> items;
    QStandardItem *item = m_SearchEditorModel->invisibleRootItem();
    QModelIndex parent_index;

    for (int row = 0; row < item->rowCount(); row++) {
        items.append(item->child(row, 0));
    }

    ExportItems(items);
}

void SearchEditorPlus::Export()
{
    if (SelectedRowsCount() < 1) {
        return;
    }

    QList<QStandardItem *> items = GetSelectedItems();

    if (!ItemsAreUnique(m_SearchEditorModel->GetNonParentItems(items))) {
        return;
    }

    ExportItems(items);
}

void SearchEditorPlus::ExportItems(QList<QStandardItem *> items)
{
    QList<SearchEditorModelPlus::searchEntry *> entries;
    foreach(QStandardItem * item, items) {
        // Get all subitems of an item not just the item itself
        QList<QStandardItem *> sub_items = m_SearchEditorModel->GetNonParentItems(item);
        // Get the parent path of the item
        QString parent_path = "";

        if (item->parent()) {
            parent_path = m_SearchEditorModel->GetFullName(item->parent());
        }

        foreach(QStandardItem * item, sub_items) {
            SearchEditorModelPlus::searchEntry *entry = m_SearchEditorModel->GetEntry(item);
            // Remove the top level paths since we're exporting a subset
            entry->fullname.replace(QRegularExpression(parent_path), "");
            entry->name = entry->fullname;
            entries.append(entry);
        }
    }
    // Get the filename to use
    QMap<QString,QString> file_filters;
    file_filters[ "ini" ] = tr("Sigil INI files (*.ini)");
    file_filters[ "csv" ] = tr("CSV files (*.csv)");
    file_filters[ "txt" ] = tr("Text files (*.txt)");
    QStringList filters = file_filters.values();
    QString filter_string = "";
    foreach(QString filter, filters) {
        filter_string += filter + ";;";
    }
    QString default_filter = file_filters.value("ini");

    QFileDialog::Options options = QFileDialog::Options();
#ifdef Q_OS_MAC
    options = options | QFileDialog::DontUseNativeDialog;
#endif

    QString filename = QFileDialog::getSaveFileName(this,
                       tr("Export Selected Searches"),
                       m_LastFolderOpen,
                       filter_string,
                       &default_filter,
                       options);

    if (!filename.isEmpty()) {
        QString ext = QFileInfo(filename).suffix().toLower();
        QChar sep;
        if (ext == "txt") {
            sep = QChar(9);
        } else if (ext == "csv") {
            sep = QChar(',');
        }

        if (ext == "ini") {
            // Save the data, and last folder opened if successful
            if (SaveData(entries, filename)) {
                m_LastFolderOpen = QFileInfo(filename).absolutePath();
                WriteSettings();
            }
        } else {
            if (SaveTextData(entries, filename, sep)) {
                m_LastFolderOpen = QFileInfo(filename).absolutePath();
                WriteSettings();
            }
        }
    }
    // clean up to prevent memory leaks
    foreach(SearchEditorModelPlus::searchEntry* entry, entries) {
        delete entry;
    }
}

void SearchEditorPlus::FillControls()
{
    if (ui.SearchEditorTree->selectionModel()->hasSelection()) {
        QList<QStandardItem *> items = m_SearchEditorModel->GetNonGroupItems(GetSelectedItems());

        if (!ItemsAreUnique(items)) return;

        if (items.size() < 2) {
            ShowMessage(tr("FillControls requires selecting at least two entries,which filled base on the first."));
            return;
        }

        m_SearchEditorModel->FillControls(items);
    }
}

void SearchEditorPlus::CollapseAll()
{
    ui.SearchEditorTree->collapseAll();
}

void SearchEditorPlus::ExpandAll()
{
    ui.SearchEditorTree->expandAll();
}

bool SearchEditorPlus::FilterEntries(const QString &text, QStandardItem *item)
{
    const QString lowercaseText = text.toLower();
    bool hidden = false;
    QModelIndex parent_index;

    if (item && item->parent()) {
        parent_index = item->parent()->index();
    }

    if (item) {
        SearchEditorModelPlus::searchEntry* entry = m_SearchEditorModel->GetEntry(item);

        if (ui.Filter->currentIndex() == 0) {
            hidden = !(text.isEmpty() || entry->name.toLower().contains(lowercaseText));
        }
        else {
            hidden = !(text.isEmpty() || entry->name.toLower().contains(lowercaseText) ||
                entry->prefind.toLower().contains(lowercaseText) ||
                entry->find.toLower().contains(lowercaseText) ||
                entry->replace.toLower().contains(lowercaseText) ||
                entry->controls.toLower().contains(lowercaseText));
        }
        ui.SearchEditorTree->setRowHidden(item->row(), parent_index, hidden);

    } else {
        item = m_SearchEditorModel->invisibleRootItem();
    }

    // Recursively set children
    // Show group if any children are visible, but do not hide in case other children are visible
    for (int row = 0; row < item->rowCount(); row++) {
        if (!FilterEntries(text, item->child(row, 0))) {
            hidden = false;
            ui.SearchEditorTree->setRowHidden(item->row(), parent_index, hidden);
        }
    }

    return hidden;
}

void SearchEditorPlus::FilterEditTextChangedSlot(const QString &text)
{
    FilterEntries(text);
    ui.SearchEditorTree->expandAll();
    ui.SearchEditorTree->selectionModel()->clear();

    if (!text.isEmpty()) {
        SelectFirstVisibleNonGroup(m_SearchEditorModel->invisibleRootItem());
    }

    return;
}

bool SearchEditorPlus::SelectFirstVisibleNonGroup(QStandardItem *item)
{
    QModelIndex parent_index;

    if (item->parent()) {
        parent_index = item->parent()->index();
    }

    // If the item is not a group and its visible select it and finish
    if (item != m_SearchEditorModel->invisibleRootItem() && !ui.SearchEditorTree->isRowHidden(item->row(), parent_index)) {
        if (!m_SearchEditorModel->ItemIsGroup(item)) {
            ui.SearchEditorTree->selectionModel()->select(m_SearchEditorModel->index(item->row(), 0, parent_index), QItemSelectionModel::Select | QItemSelectionModel::Rows);
            ui.SearchEditorTree->setCurrentIndex(item->index());
            return true;
        }
    }

    // Recursively check children of any groups
    for (int row = 0; row < item->rowCount(); row++) {
        if (SelectFirstVisibleNonGroup(item->child(row, 0))) {
            return true;
        }
    }

    return false;
}

bool SearchEditorPlus::ReadSettings()
{
    SettingsStoreExtend settings;
    settings.beginGroup(SETTINGS_GROUP);
    QByteArray geometry = settings.value("geometry").toByteArray();

    if (!geometry.isNull()) {
        restoreGeometry(geometry);
    }
    int size = settings.beginReadArray("column_data");

    for (int column = 0; column < size && column < ui.SearchEditorTree->header()->count(); column++) {
        settings.setArrayIndex(column);
        int column_width = settings.value("width").toInt();

        if (column_width) {
            ui.SearchEditorTree->setColumnWidth(column, column_width);
        }
    }
    settings.endArray();
    m_LastFolderOpen = settings.value("last_folder_open").toString();
    settings.endGroup();
    return size > 0;
}

void SearchEditorPlus::WriteSettings()
{
    SettingsStoreExtend settings;
    settings.beginGroup(SETTINGS_GROUP);
    settings.setValue("geometry", saveGeometry());
    settings.beginWriteArray("column_data");
    for (int column = 0; column < ui.SearchEditorTree->header()->count(); column++) {
        settings.setArrayIndex(column);
        settings.setValue("width", ui.SearchEditorTree->columnWidth(column));
    }
    settings.endArray();
    settings.setValue("last_folder_open", m_LastFolderOpen);
    settings.endGroup();
}

void SearchEditorPlus::CreateContextMenuActions()
{
    m_AddEntry  =   new QAction(tr("Add Entry"),          this);
    m_AddGroup  =   new QAction(tr("Add Group"),          this);
    m_Edit      =   new QAction(tr("Edit"),               this);
    m_Cut       =   new QAction(tr("Cut"),                this);
    m_Copy      =   new QAction(tr("Copy"),               this);
    m_Paste     =   new QAction(tr("Paste"),              this);
    m_Delete    =   new QAction(tr("Delete"),             this);
    m_Import    =   new QAction(tr("Import") + "...",     this);
    m_Reload    =   new QAction(tr("Reload") + "...",     this);
    m_Export    =   new QAction(tr("Export") + "...",     this);
    m_ExportAll =   new QAction(tr("Export All") + "...", this);
    m_CollapseAll = new QAction(tr("Collapse All"),       this);
    m_ExpandAll =   new QAction(tr("Expand All"),         this);
    m_FillIn    =   new QAction(tr("Fill Controls"),      this);
    m_AddEntry->setShortcut(QKeySequence(Qt::ControlModifier | Qt::Key_E));
    m_AddGroup->setShortcut(QKeySequence(Qt::ControlModifier | Qt::Key_G));
    m_Edit->setShortcut(QKeySequence(Qt::Key_F2));
    m_Cut->setShortcut(QKeySequence(Qt::ControlModifier | Qt::Key_X));
    m_Copy->setShortcut(QKeySequence(Qt::ControlModifier | Qt::Key_C));
    m_Paste->setShortcut(QKeySequence(Qt::ControlModifier | Qt::Key_V));
    m_Delete->setShortcut(QKeySequence::Delete);
    // Has to be added to the dialog itself for the keyboard shortcut to work.
    addAction(m_AddEntry);
    addAction(m_AddGroup);
    addAction(m_Edit);
    addAction(m_Cut);
    addAction(m_Copy);
    addAction(m_Paste);
    addAction(m_Delete);
}

void SearchEditorPlus::OpenContextMenu(const QPoint &point)
{
    SetupContextMenu(point);
    m_ContextMenu->exec(ui.SearchEditorTree->viewport()->mapToGlobal(point));
    if (!m_ContextMenu.isNull()) {
        m_ContextMenu->clear();
        // Make sure every action is enabled - in case shortcut is used after context menu disables some.
        m_AddEntry->setEnabled(true);
        m_AddGroup->setEnabled(true);
        m_Edit->setEnabled(true);
        m_Cut->setEnabled(true);
        m_Copy->setEnabled(true);
        m_Paste->setEnabled(true);
        m_Delete->setEnabled(true);
        m_Import->setEnabled(true);
        m_Reload->setEnabled(true);
        m_Export->setEnabled(true);
        m_ExportAll->setEnabled(true);
        m_CollapseAll->setEnabled(true);
        m_ExpandAll->setEnabled(true);
        m_FillIn->setEnabled(true);
    }
}

void SearchEditorPlus::SetupContextMenu(const QPoint &point)
{
    int selected_rows_count = SelectedRowsCount();
    m_ContextMenu->addAction(m_AddEntry);
    m_ContextMenu->addAction(m_AddGroup);
    m_ContextMenu->addSeparator();
    m_ContextMenu->addAction(m_Edit);
    m_ContextMenu->addSeparator();
    m_ContextMenu->addAction(m_Cut);
    m_Cut->setEnabled(selected_rows_count > 0);
    m_ContextMenu->addAction(m_Copy);
    m_Copy->setEnabled(selected_rows_count > 0);
    m_ContextMenu->addAction(m_Paste);
    m_Paste->setEnabled(m_SavedSearchEntries.count());
    m_ContextMenu->addSeparator();
    m_ContextMenu->addAction(m_Delete);
    m_Delete->setEnabled(selected_rows_count > 0);
    m_ContextMenu->addSeparator();
    m_ContextMenu->addAction(m_Import);
    m_Import->setEnabled(selected_rows_count <= 1);
    m_ContextMenu->addAction(m_Reload);
    m_ContextMenu->addSeparator();
    m_ContextMenu->addAction(m_Export);
    m_Export->setEnabled(selected_rows_count > 0);
    m_ContextMenu->addAction(m_ExportAll);
    m_ContextMenu->addSeparator();
    m_ContextMenu->addAction(m_CollapseAll);
    m_ContextMenu->addAction(m_ExpandAll);
    m_ContextMenu->addAction(m_FillIn);
}

void SearchEditorPlus::Apply()
{
    LoadFindReplace();
}

bool SearchEditorPlus::Save()
{
    if (SaveData()) {
        emit ShowStatusMessageRequest(tr("Search entries saved."));
        return true;
    }

    return false;
}

void SearchEditorPlus::reject()
{
    WriteSettings();

    if (MaybeSaveDialogSaysProceed(false)) {
        QDialog::reject();
    }
}

void SearchEditorPlus::ForceClose()
{
    MaybeSaveDialogSaysProceed(true);
    close();
}

void SearchEditorPlus::WhyEntriesEmpty()
{
    if (ui.SearchEditorTree->selectionModel()->hasSelection()) {
        ShowMessage(tr("Searches seleted have ended,you might click \"Load Search\" to reload them if run again."));
    }
    else {
        ShowMessage(tr("No searches selected"));
    }
}

bool SearchEditorPlus::MaybeSaveDialogSaysProceed(bool is_forced)
{
    if (m_SearchEditorModel->IsDataModified()) {
        QMessageBox::StandardButton button_pressed;
        QMessageBox::StandardButtons buttons = is_forced ? QMessageBox::Save | QMessageBox::Discard
                                               : QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel;
        button_pressed = Utility::warning(this,
                                              tr("Sigil: Saved Searches"),
                                              tr("The Search entries may have been modified.\n"
                                                      "Do you want to save your changes?"),
                                              buttons
                                             );

        if (button_pressed == QMessageBox::Save) {
            return Save();
        } else if (button_pressed == QMessageBox::Cancel) {
            return false;
        } else {
            m_SearchEditorModel->LoadInitialData();
            ui.SearchEditorTree->expandAll();
        }
    }

    return true;
}

void SearchEditorPlus::MoveUp()
{
    MoveVertical(false);
}

void SearchEditorPlus::MoveDown()
{
    MoveVertical(true);
}

void SearchEditorPlus::MoveVertical(bool move_down)
{
    if (!ui.SearchEditorTree->selectionModel()->hasSelection()) {
        return;
    }

    QModelIndexList selected_indexes = ui.SearchEditorTree->selectionModel()->selectedRows(0);

    if (selected_indexes.count() > 1) {
        return;
    }

    // Identify the selected item
    QModelIndex index = selected_indexes.first();
    int row = index.row();
    QStandardItem *item = m_SearchEditorModel->itemFromIndex(index);
    QStandardItem *source_parent_item = item->parent();

    if (!source_parent_item) {
        source_parent_item = m_SearchEditorModel->invisibleRootItem();
    }

    QStandardItem *destination_parent_item = source_parent_item;
    int destination_row;

    if (move_down) {
        if (row >= source_parent_item->rowCount() - 1) {
            // We are the last child for this group.
            if (source_parent_item == m_SearchEditorModel->invisibleRootItem()) {
                // Can't go any lower than this
                return;
            }

            // Make this the next child of the parent, as though the user hit Left
            destination_parent_item = source_parent_item->parent();

            if (!destination_parent_item) {
                destination_parent_item = m_SearchEditorModel->invisibleRootItem();
            }

            destination_row = source_parent_item->index().row() + 1;
        } else {
            destination_row = row + 1;
        }
    } else {
        if (row == 0) {
            // We are the first child for this parent.
            if (source_parent_item == m_SearchEditorModel->invisibleRootItem()) {
                // Can't go any higher than this
                return;
            }

            // Make this the previous child of the parent, as though the user hit Left and Up
            destination_parent_item = source_parent_item->parent();

            if (!destination_parent_item) {
                destination_parent_item = m_SearchEditorModel->invisibleRootItem();
            }

            destination_row = source_parent_item->index().row();
        } else {
            destination_row = row - 1;
        }
    }

    // Swap the item rows
    QList<QStandardItem *> row_items = source_parent_item->takeRow(row);
    destination_parent_item->insertRow(destination_row, row_items);
    // Get index
    QModelIndex destination_index = destination_parent_item->child(destination_row, 0)->index();
    // Make sure the path to the item is updated
    QStandardItem *destination_item = m_SearchEditorModel->itemFromIndex(destination_index);
    m_SearchEditorModel->UpdateFullName(destination_item);
    // Select the item row again
    ui.SearchEditorTree->selectionModel()->clear();
    ui.SearchEditorTree->setCurrentIndex(destination_index);
    ui.SearchEditorTree->selectionModel()->select(destination_index, QItemSelectionModel::SelectCurrent | QItemSelectionModel::Rows);
    ui.SearchEditorTree->expand(destination_parent_item->index());
}

void SearchEditorPlus::MoveLeft()
{
    MoveHorizontal(true);
}

void SearchEditorPlus::MoveRight()
{
    MoveHorizontal(false);
}

void SearchEditorPlus::MoveHorizontal(bool move_left)
{
    if (!ui.SearchEditorTree->selectionModel()->hasSelection()) {
        return;
    }

    QModelIndexList selected_indexes = ui.SearchEditorTree->selectionModel()->selectedRows(0);

    if (selected_indexes.count() > 1) {
        return;
    }

    // Identify the source information
    QModelIndex source_index = selected_indexes.first();
    int source_row = source_index.row();
    QStandardItem *source_item = m_SearchEditorModel->itemFromIndex(source_index);
    QStandardItem *source_parent_item = source_item->parent();

    if (!source_parent_item) {
        source_parent_item = m_SearchEditorModel->invisibleRootItem();
    }

    QStandardItem *destination_parent_item;
    int destination_row = 0;

    if (move_left) {
        // Skip if at root or otherwise at top level
        if (!source_parent_item || source_parent_item == m_SearchEditorModel->invisibleRootItem()) {
            return;
        }

        // Move below parent
        destination_parent_item = source_parent_item->parent();

        if (!destination_parent_item) {
            destination_parent_item = m_SearchEditorModel->invisibleRootItem();
        }

        destination_row = source_parent_item->index().row() + 1;
    } else {
        QModelIndex index_above = ui.SearchEditorTree->indexAbove(source_index);

        if (!index_above.isValid()) {
            return;
        }

        QStandardItem *item = m_SearchEditorModel->itemFromIndex(index_above);

        if (source_parent_item == item) {
            return;
        }

        SearchEditorModelPlus::searchEntry *entry = m_SearchEditorModel->GetEntry(item);

        // Only move right if immediately under a group
        if (entry ->is_group) {
            destination_parent_item = item;
        } else {
            // Or if the item above is in a different group
            if (item->parent() && item->parent() != source_parent_item) {
                destination_parent_item = item->parent();
            } else {
                return;
            }
        }

        destination_row = destination_parent_item->rowCount();
    }

    // Swap the item rows
    QList<QStandardItem *> row_items = source_parent_item->takeRow(source_row);
    destination_parent_item->insertRow(destination_row, row_items);
    QModelIndex destination_index = destination_parent_item->child(destination_row)->index();
    // Make sure the path to the item is updated
    QStandardItem *destination_item = m_SearchEditorModel->itemFromIndex(destination_index);
    m_SearchEditorModel->UpdateFullName(destination_item);
    // Select the item row again
    ui.SearchEditorTree->selectionModel()->clear();
    ui.SearchEditorTree->setCurrentIndex(destination_index);
    ui.SearchEditorTree->selectionModel()->select(destination_index, QItemSelectionModel::SelectCurrent | QItemSelectionModel::Rows);
}

void SearchEditorPlus::ConnectSignalsSlots()
{
    connect(ui.FilterText,      SIGNAL(textChanged(QString)), this, SLOT(FilterEditTextChangedSlot(QString)));
    connect(ui.LoadSearch,      SIGNAL(clicked()),            this, SLOT(LoadFindReplace()));
    connect(ui.Find,            SIGNAL(clicked()),            this, SLOT(Find()));
    connect(ui.ReplaceCurrent,  SIGNAL(clicked()),            this, SLOT(ReplaceCurrent()));
    connect(ui.Replace,         SIGNAL(clicked()),            this, SLOT(Replace()));
    connect(ui.CountAll,        SIGNAL(clicked()),            this, SLOT(CountAll()));
    connect(ui.ReplaceAll,      SIGNAL(clicked()),            this, SLOT(ReplaceAll()));
    connect(ui.MoveUp,     SIGNAL(clicked()),            this, SLOT(MoveUp()));
    connect(ui.MoveDown,   SIGNAL(clicked()),            this, SLOT(MoveDown()));
    connect(ui.MoveLeft,   SIGNAL(clicked()),            this, SLOT(MoveLeft()));
    connect(ui.MoveRight,  SIGNAL(clicked()),            this, SLOT(MoveRight()));
    connect(ui.buttonBox->button(QDialogButtonBox::Save), SIGNAL(clicked()), this, SLOT(Save()));
    connect(ui.buttonBox->button(QDialogButtonBox::Close), SIGNAL(clicked()), this, SLOT(reject()));
    connect(ui.SearchEditorTree, SIGNAL(customContextMenuRequested(const QPoint &)),
            this,                SLOT(OpenContextMenu(const QPoint &)));
    connect(m_AddEntry,    SIGNAL(triggered()), this, SLOT(AddEntry()));
    connect(m_AddGroup,    SIGNAL(triggered()), this, SLOT(AddGroup()));
    connect(m_Edit,        SIGNAL(triggered()), this, SLOT(Edit()));
    connect(m_Cut,         SIGNAL(triggered()), this, SLOT(Cut()));
    connect(m_Copy,        SIGNAL(triggered()), this, SLOT(Copy()));
    connect(m_Paste,       SIGNAL(triggered()), this, SLOT(Paste()));
    connect(m_Delete,      SIGNAL(triggered()), this, SLOT(Delete()));
    connect(m_Import,      SIGNAL(triggered()), this, SLOT(Import()));
    connect(m_Reload,      SIGNAL(triggered()), this, SLOT(Reload()));
    connect(m_Export,      SIGNAL(triggered()), this, SLOT(Export()));
    connect(m_ExportAll,   SIGNAL(triggered()), this, SLOT(ExportAll()));
    connect(m_CollapseAll, SIGNAL(triggered()), this, SLOT(CollapseAll()));
    connect(m_ExpandAll,   SIGNAL(triggered()), this, SLOT(ExpandAll()));
    connect(m_FillIn,      SIGNAL(triggered()), this, SLOT(FillControls()));
    connect(m_SearchEditorModel, SIGNAL(ItemDropped(const QModelIndex &)), this, SLOT(ModelItemDropped(const QModelIndex &)));
    connect(ui.SearchEditorTree->selectionModel(), SIGNAL(selectionChanged(const QItemSelection&, const QItemSelection&)), this, SLOT(SelectionChanged()));
}