/************************************************************************
**
**  This file is part of Sigil.
**
*************************************************************************/

#pragma once
#ifndef PLUGINTEXTTRANSACTION_H
#define PLUGINTEXTTRANSACTION_H

#include <QHash>
#include <QJsonArray>
#include <QList>
#include <QString>

namespace PluginApi
{

struct StagedTextChange {
    QString resourceId;
    QString originalText;
    QString stagedText;
    quint64 baseRevision = 0;
};

class TextTransaction
{
public:
    TextTransaction(const QString &id,
                    const QString &label,
                    const QString &checkpoint_policy,
                    quint64 base_book_revision);

    QString Id() const;
    QString Label() const;
    QString CheckpointPolicy() const;
    quint64 BaseBookRevision() const;
    bool IsEmpty() const;
    int Size() const;
    bool HasChange(const QString &resource_id) const;

    QString ReadText(const QString &resource_id,
                     const QString &current_text,
                     quint64 current_revision,
                     quint64 *revision) const;
    bool ReplaceText(const QString &resource_id,
                     const QString &current_text,
                     quint64 current_revision,
                     quint64 expected_revision,
                     const QString &replacement,
                     QString *error);
    bool ApplyEdits(const QString &resource_id,
                    const QString &current_text,
                    quint64 current_revision,
                    quint64 expected_revision,
                    const QJsonArray &edit_values,
                    QString *error);

    QList<StagedTextChange> Changes() const;
    void Clear();

private:
    QString m_Id;
    QString m_Label;
    QString m_CheckpointPolicy;
    quint64 m_BaseBookRevision;
    QHash<QString, StagedTextChange> m_Changes;
};

} // namespace PluginApi

#endif // PLUGINTEXTTRANSACTION_H
