/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef SIGIL_AGENT_SIGIL_BOOK_WORKSPACE_H
#define SIGIL_AGENT_SIGIL_BOOK_WORKSPACE_H

#include <functional>
#include <memory>

#include <QHash>
#include <QSharedPointer>
#include <QStringList>

#include "Agent/Execution/IBookWorkspace.h"
#include "PluginAPI/PluginTextTransaction.h"

class Book;
class Resource;
class TextResource;

namespace SigilAgent
{

class SigilBookWorkspace : public IBookWorkspace
{
public:
    void setBook(QSharedPointer<Book> book);
    QSharedPointer<Book> book() const;

    quint64 revision() const override;
    QJsonObject summary() const override;
    QJsonArray resources() const override;
    QJsonArray spine() const override;
    QJsonArray toc() const override;
    QJsonObject metadata() const override;
    QJsonArray search(const QString &query, int max_matches) const override;
    BookOpResult readFragment(const QString &resource_id, int offset, int limit) const override;
    QJsonArray stylesheets() const override;
    QJsonObject fontInventory() const override;
    QJsonObject validate() const override;

    bool hasOpenTransaction() const override;
    BookOpResult beginTransaction(const QString &label) override;
    BookOpResult previewTransaction() const override;
    BookOpResult commitTransaction(quint64 expected_revision) override;
    BookOpResult rollbackTransaction() override;
    BookOpResult patchFragment(const QString &resource_id,
                               int start,
                               int end,
                               const QString &text,
                               quint64 expected_resource_revision,
                               const QString &expected_text,
                               int start_line = -1) override;
    BookOpResult replaceText(const QString &resource_id,
                             const QString &text,
                             quint64 expected_resource_revision) override;
    BookOpResult updateCss(const QString &resource_id,
                           const QString &text,
                           quint64 expected_resource_revision) override;
    BookOpResult updateMetadata(const QJsonObject &patch) override;
    BookOpResult createResource(const QString &book_path,
                                const QString &kind,
                                const QString &text,
                                bool add_to_spine,
                                const QString &after_resource_id) override;
    BookOpResult copyResource(const QString &source_id,
                              const QString &book_path,
                              bool add_to_spine) override;
    BookOpResult deleteResource(const QString &resource_id) override;
    BookOpResult updateSpine(const QStringList &resource_ids) override;
    BookOpResult updateToc(const QJsonArray &entries) override;
    BookOpResult createCheckpoint(const QString &label) override;
    QJsonArray listCheckpoints() const override;
    BookOpResult restoreCheckpoint(const QString &checkpoint_id) override;
    QString resourceText(const QString &resource_id) const override;
    QString workingText(const QString &resource_id) const override;
    quint64 resourceRevision(const QString &resource_id) const override;

private:
    struct TrackedResource {
        QString lastText;
        quint64 revision = 1;
    };
    struct Checkpoint {
        QString id;
        QString label;
        quint64 bookRevision = 0;
        QHash<QString, QString> texts;
    };

    BookOpResult invokeOp(const std::function<BookOpResult()> &fn) const;
    QJsonObject invokeJson(const std::function<QJsonObject()> &fn) const;
    QJsonArray invokeArray(const std::function<QJsonArray()> &fn) const;
    QString invokeString(const std::function<QString()> &fn) const;

    Resource *findResource(const QString &id_or_path) const;
    TextResource *textResource(const QString &id_or_path) const;
    QString liveText(TextResource *resource) const;
    QString currentText(TextResource *resource) const;
    QString kindOf(Resource *resource) const;
    quint64 trackedRevision(Resource *resource) const;
    void noteText(Resource *resource, const QString &text) const;
    BookOpResult ensureTransaction();
    QStringList extractFontFamilies(const QString &css) const;
    QJsonObject resourceJson(Resource *resource) const;
    QStringList allBookPaths() const;
    BookOpResult stageAddition(const QString &book_path,
                               const QString &kind,
                               const QString &text,
                               bool add_to_spine,
                               const QString &after_resource_id);

    QSharedPointer<Book> m_book;
    quint64 m_revision = 1;
    mutable QHash<QString, TrackedResource> m_tracked;
    std::unique_ptr<PluginApi::TextTransaction> m_transaction;
    QList<Checkpoint> m_checkpoints;
    QJsonObject m_stagedMetadata;
    bool m_hasStagedMetadata = false;
    QStringList m_stagedMetadataRemove;
    QStringList m_stagedRemovals;
    QStringList m_stagedSpine;
    bool m_hasStagedSpine = false;
    QJsonArray m_stagedToc;
    bool m_hasStagedToc = false;
    QHash<QString, QString> m_stagedAfterIds;
};

} // namespace SigilAgent

#endif
