#pragma once
#ifndef SEARCHEDITORMODELPLUS_H
#define SEARCHEDITORMODELPLUS_H

#include <QtGui/QStandardItemModel>
#include <QFileSystemWatcher>
#include <QDropEvent>

#include "Misc/SettingsStore.h"

class SearchEditorModelPlus : public QStandardItemModel
{
    Q_OBJECT

public:
    static SearchEditorModelPlus* instance();

    struct searchEntry {
        bool is_group;
        QString fullname;
        QString name;
        QString prefind;
        QString find;
        QString replace;
        QString controls;
    };

    bool IsDataModified();

    bool ItemIsGroup(QStandardItem *item);

    QString GetFullName(QStandardItem *item);

    void LoadInitialData();
    void LoadData(const QString &filename = QString(), QStandardItem *parent_item = NULL);
    void LoadTextData(const QString &filename = QString(), QStandardItem *parent_item = NULL, const QChar& sep = QChar(9));

    void AddFullNameEntry(SearchEditorModelPlus::searchEntry *entry = NULL, QStandardItem *parent_item = NULL, int row = -1);

    void FillControls(const QList<QStandardItem*> &items);

    QString BuildControlsToolTip(const QString& controls);

    QStandardItem *AddEntryToModel(SearchEditorModelPlus::searchEntry *entry, bool is_group = false, QStandardItem *parent_item = NULL, int row = -1);

    QString SaveData(QList<SearchEditorModelPlus::searchEntry *> entries = QList<SearchEditorModelPlus::searchEntry *>(),
                     const QString &filename = QString());

    QString SaveTextData(QList<SearchEditorModelPlus::searchEntry *> entries = QList<SearchEditorModelPlus::searchEntry *>(),
                         const QString &filename = QString(), const QChar& cep = QChar(9));

    QList<SearchEditorModelPlus::searchEntry *> GetEntries(QList<QStandardItem *> items);
    SearchEditorModelPlus::searchEntry *GetEntry(QStandardItem *item);
    SearchEditorModelPlus::searchEntry *GetEntryFromName(const QString &name, QStandardItem *parent_item = NULL);

    QStandardItem *GetItemFromName(const QString &name, QStandardItem *item = NULL);

    QList<QStandardItem *> GetNonGroupItems(QList<QStandardItem *> items);
    QList<QStandardItem *> GetNonGroupItems(QStandardItem *item);

    QList<QStandardItem *> GetNonParentItems(QList<QStandardItem *> items);
    QList<QStandardItem *> GetNonParentItems(QStandardItem *item);

    void Rename(QStandardItem *item, const QString &name = "");

    void UpdateFullName(QStandardItem *item);

    QVariant data(const QModelIndex &index, int role) const;

signals:
    void SettingsFileUpdated() const;
    void ItemDropped(const QModelIndex &) const;

private slots:
    void RowsRemovedHandler(const QModelIndex &parent, int start, int end);
    void ItemChangedHandler(QStandardItem *item);

    void SettingsFileChanged(const QString &path) const;

private:
    SearchEditorModelPlus(QObject* parent = 0);
    ~SearchEditorModelPlus();

    void SetDataModified(bool modified);

    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent);
    Qt::DropActions supportedDropActions() const;

    QStandardItem *GetItemFromId(quintptr id, int row, QStandardItem *item = NULL) const;

    void AddExampleEntries();

    static SearchEditorModelPlus *m_instance;

    QString m_SettingsPath;

    QFileSystemWatcher *m_FSWatcher;

    bool m_IsDataModified;
};

#endif // SEARCHEDITORMODELPLUS_H
