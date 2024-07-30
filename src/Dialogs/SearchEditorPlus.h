#pragma once
#ifndef SEARCHEDITORPLUS_H
#define SEARCHEDITORPLUS_H

#include <QtWidgets/QDialog>
#include <QtGui/QStandardItemModel>
#include <QAction>
#include <QtWidgets/QMenu>
#include <QPointer>

#include "MiscEditors/SearchEditorModelPlus.h"
#include "MiscEditors/SearchEditorTreeView.h"

#include "ui_SearchEditor.h"

class SearchEditorItemDelegate;

/**
 * The editor used to create and modify saved searches
 */
class SearchEditorPlus : public QDialog
{
    Q_OBJECT

public:

    SearchEditorPlus(QWidget *parent);

    ~SearchEditorPlus();

    void ForceClose();

    void RecordEntryAsCompleted(SearchEditorModelPlus::searchEntry* entry);
    QList<SearchEditorModelPlus::searchEntry*> GetCurrentEntries();
    int GetCurrentEntriesCount() { return m_CurrentSearchEntries.count(); }

public slots:

    QStandardItem *AddEntry(bool is_group = false, SearchEditorModelPlus::searchEntry *search_entry = NULL, bool insert_after = true);

    void ShowMessage(const QString &message);

    void SelectionChanged();

    void SetCurrentEntriesFromFullName(const QString& name);

    void WhyEntriesEmpty();

signals:

    void LoadSelectedSearchRequest(SearchEditorModelPlus::searchEntry *search_entry);
    void FindSelectedSearchRequest();
    void ReplaceCurrentSelectedSearchRequest();
    void ReplaceSelectedSearchRequest();
    void CountAllSelectedSearchRequest();
    void ReplaceAllSelectedSearchRequest();
    void RestartSearch();
    void ShowStatusMessageRequest(const QString &message);

protected:
    bool eventFilter(QObject *obj, QEvent *ev);

protected slots:
    void reject();
    void showEvent(QShowEvent *event);

private slots:
    QStandardItem *AddGroup();
    void Edit();
    void Cut();
    bool Copy();
    void Paste();
    void Delete();
    void Import();
    void Reload();
    void Export();
    void ExportAll();
    void CollapseAll();
    void ExpandAll();
    void FillControls();

    void Apply();
    bool Save();

    void MoveUp();
    void MoveDown();
    void MoveLeft();
    void MoveRight();

    void LoadFindReplace();
    void Find();
    void ReplaceCurrent();
    void Replace();
    void CountAll();
    void ReplaceAll();

    void FilterEditTextChangedSlot(const QString &text);

    void OpenContextMenu(const QPoint &point);

    void SettingsFileModelUpdated();

    void ModelItemDropped(const QModelIndex &index);

private:

    bool MaybeSaveDialogSaysProceed(bool is_forced);
    void MoveVertical(bool move_down);
    void MoveHorizontal(bool move_left);

    void ExportItems(QList<QStandardItem *> items);

    void SetupSearchEditorTree();

    int SelectedRowsCount();

    SearchEditorModelPlus::searchEntry *GetSelectedEntry(bool show_warning = true);
    QList<SearchEditorModelPlus::searchEntry *> GetSelectedEntries();

    QList<QStandardItem *> GetSelectedItems();

    bool ItemsAreUnique(QList<QStandardItem *> items);

    bool SaveData(QList<SearchEditorModelPlus::searchEntry *> entries = QList<SearchEditorModelPlus::searchEntry *>() , QString filename = QString());

    bool SaveTextData(QList<SearchEditorModelPlus::searchEntry *> entries = QList<SearchEditorModelPlus::searchEntry *>() ,
                      QString filename = QString(), QChar sep=QChar(9));

    bool FilterEntries(const QString &text, QStandardItem *item = NULL);
    bool SelectFirstVisibleNonGroup(QStandardItem *item);

    bool ReadSettings();
    void WriteSettings();

    void CreateContextMenuActions();
    void SetupContextMenu(const QPoint &point);

    void ConnectSignalsSlots();

    QAction *m_AddEntry;
    QAction *m_AddGroup;
    QAction *m_Edit;
    QAction *m_Cut;
    QAction *m_Copy;
    QAction *m_Paste;
    QAction *m_Delete;
    QAction *m_Import;
    QAction *m_Reload;
    QAction *m_Export;
    QAction *m_ExportAll;
    QAction *m_CollapseAll;
    QAction *m_ExpandAll;
    QAction *m_FillIn;

    SearchEditorModelPlus *m_SearchEditorModel;

    QString m_LastFolderOpen;

    QPointer<QMenu> m_ContextMenu;

    // stores result of cut/copy for later paste
    QList<SearchEditorModelPlus::searchEntry *> m_SavedSearchEntries;

    // List of the remaining currently selected Entries updated  to remember state
    QList<SearchEditorModelPlus::searchEntry *> m_CurrentSearchEntries;

    SearchEditorModelPlus::searchEntry * m_SearchToLoad;

    SearchEditorItemDelegate * m_CntrlDelegate;

    Ui::SearchEditor ui;
};

#endif // SEARCHEDITORPLUS_H
