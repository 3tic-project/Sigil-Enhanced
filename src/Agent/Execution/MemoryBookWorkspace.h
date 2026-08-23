/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef SIGIL_AGENT_MEMORY_BOOK_WORKSPACE_H
#define SIGIL_AGENT_MEMORY_BOOK_WORKSPACE_H

#include <QHash>
#include <QStringList>
#include <memory>

#include "Agent/Execution/IBookWorkspace.h"
#include "PluginAPI/PluginTextTransaction.h"

namespace SigilAgent
{

struct MemoryResource {
    QString id;
    QString bookPath;
    QString mediaType;
    QString kind;
    QString text;
    QByteArray binary;
    quint64 revision = 1;
};

struct MemoryCheckpoint {
    QString id;
    QString label;
    quint64 bookRevision = 0;
    QHash<QString, MemoryResource> resources;
    QJsonObject metadata;
    QStringList spineIds;
    QJsonArray toc;
};

class MemoryBookWorkspace : public IBookWorkspace
{
public:
    MemoryBookWorkspace();

    static MemoryBookWorkspace samplePhysicsBook();

    void addResource(const MemoryResource &resource);
    void setMetadata(const QJsonObject &metadata);
    void setSpine(const QStringList &ids);
    void setToc(const QJsonArray &toc);
    void setEpubVersion(const QString &version);
    void bumpRevision();

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
    BookOpResult renameResource(const QString &resource_id, const QString &book_path) override;
    BookOpResult updateSpine(const QStringList &resource_ids) override;
    BookOpResult updateToc(const QJsonArray &entries) override;
    BookOpResult createCheckpoint(const QString &label) override;
    QJsonArray listCheckpoints() const override;
    BookOpResult restoreCheckpoint(const QString &checkpoint_id) override;
    QString resourceText(const QString &resource_id) const override;
    QString workingText(const QString &resource_id) const override;
    quint64 resourceRevision(const QString &resource_id) const override;
    void setInjectedFailureIndex(int index) override;

private:
    MemoryResource *findResource(const QString &id_or_path);
    const MemoryResource *findResource(const QString &id_or_path) const;
    BookOpResult ensureTransaction();
    QString currentText(const MemoryResource &resource) const;
    QJsonObject resourceJson(const MemoryResource &resource) const;
    QStringList extractFontFamilies(const QString &css) const;
    QStringList allBookPaths() const;
    BookOpResult stageAddition(const QString &book_path,
                               const QString &kind,
                               const QString &text,
                               bool add_to_spine,
                               const QString &after_resource_id);

    quint64 m_revision = 1;
    QString m_epubVersion = QStringLiteral("3.0");
    QJsonObject m_metadata;
    QStringList m_spineIds;
    QJsonArray m_toc;
    QHash<QString, MemoryResource> m_resources;
    std::unique_ptr<PluginApi::TextTransaction> m_transaction;
    QJsonObject m_stagedMetadata;
    bool m_hasStagedMetadata = false;
    QStringList m_stagedMetadataRemove;
    QStringList m_stagedRemovals;
    QStringList m_stagedSpine;
    bool m_hasStagedSpine = false;
    QJsonArray m_stagedToc;
    bool m_hasStagedToc = false;
    QList<MemoryCheckpoint> m_checkpoints;
    int m_failAfter = -1;
    QHash<QString, QString> m_stagedAfterIds;
};

} // namespace SigilAgent

#endif
