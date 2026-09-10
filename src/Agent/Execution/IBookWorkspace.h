/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef SIGIL_AGENT_IBOOK_WORKSPACE_H
#define SIGIL_AGENT_IBOOK_WORKSPACE_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "BookManipulation/TocTreeTransform.h"

namespace SigilAgent
{

struct BookOpResult {
    bool ok = true;
    QString code;
    QString message;
    QJsonObject data;
    bool previewOnly = false;
    bool applied = false;

    static BookOpResult success(const QJsonObject &data, bool applied = false, bool preview = false)
    {
        BookOpResult result;
        result.data = data;
        result.applied = applied;
        result.previewOnly = preview;
        return result;
    }

    static BookOpResult error(const QString &code, const QString &message,
                              const QJsonObject &data = QJsonObject())
    {
        BookOpResult result;
        result.ok = false;
        result.code = code;
        result.message = message;
        result.data = data;
        return result;
    }
};

class IBookWorkspace
{
public:
    virtual ~IBookWorkspace() = default;

    virtual quint64 revision() const = 0;
    virtual QJsonObject summary() const = 0;
    virtual QJsonArray resources() const = 0;
    virtual QJsonArray spine() const = 0;
    virtual QJsonArray toc() const = 0;
    virtual TocEditTree tocHierarchy() const = 0;
    virtual QString tocHierarchyIdentity() const = 0;
    virtual QJsonObject metadata() const = 0;
    virtual QJsonArray search(const QString &query, int max_matches) const = 0;
    virtual BookOpResult readFragment(const QString &resource_id, int offset, int limit) const = 0;
    virtual QJsonArray stylesheets() const = 0;
    virtual QJsonObject fontInventory() const = 0;
    virtual QJsonObject validate() const = 0;

    virtual bool hasOpenTransaction() const = 0;
    virtual BookOpResult beginTransaction(const QString &label) = 0;
    virtual BookOpResult previewTransaction() const = 0;
    virtual BookOpResult commitTransaction(quint64 expected_revision) = 0;
    virtual BookOpResult rollbackTransaction() = 0;
    virtual BookOpResult patchFragment(const QString &resource_id,
                                       int start,
                                       int end,
                                       const QString &text,
                                       quint64 expected_resource_revision,
                                       const QString &expected_text,
                                       int start_line = -1) = 0;
    virtual BookOpResult replaceText(const QString &resource_id,
                                     const QString &text,
                                     quint64 expected_resource_revision) = 0;
    virtual BookOpResult updateCss(const QString &resource_id,
                                   const QString &text,
                                   quint64 expected_resource_revision) = 0;
    virtual BookOpResult updateMetadata(const QJsonObject &patch) = 0;
    virtual BookOpResult createResource(const QString &book_path,
                                        const QString &kind,
                                        const QString &text,
                                        bool add_to_spine,
                                        const QString &after_resource_id) = 0;
    virtual BookOpResult copyResource(const QString &source_id,
                                      const QString &book_path,
                                      bool add_to_spine) = 0;
    virtual BookOpResult deleteResource(const QString &resource_id) = 0;
    virtual BookOpResult renameResource(const QString &resource_id, const QString &book_path) = 0;
    virtual BookOpResult updateSpine(const QStringList &resource_ids) = 0;
    virtual BookOpResult updateToc(const QJsonArray &entries) = 0;
    virtual BookOpResult updateTocHierarchy(const TocEditTree &before,
                                            const TocEditTree &after) = 0;
    virtual BookOpResult runLivePython(const QString &script, int timeout_ms)
    {
        Q_UNUSED(script);
        Q_UNUSED(timeout_ms);
        return BookOpResult::error(QStringLiteral("LIVE_PYTHON_UNAVAILABLE"),
                                   QStringLiteral("Live Python v2 requires the Sigil GUI and the in-memory Book, not a snapshot."));
    }
    virtual BookOpResult createCheckpoint(const QString &label) = 0;
    virtual QJsonArray listCheckpoints() const = 0;
    virtual BookOpResult restoreCheckpoint(const QString &checkpoint_id) = 0;
    virtual QString resourceText(const QString &resource_id) const = 0;
    virtual QString workingText(const QString &resource_id) const = 0;
    virtual quint64 resourceRevision(const QString &resource_id) const = 0;

    virtual void setInjectedFailureIndex(int index) { Q_UNUSED(index); }
};

} // namespace SigilAgent

#endif
