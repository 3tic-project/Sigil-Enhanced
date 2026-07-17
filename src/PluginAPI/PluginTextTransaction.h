/************************************************************************
**
**  This file is part of Sigil.
**
*************************************************************************/

#pragma once
#ifndef PLUGINTEXTTRANSACTION_H
#define PLUGINTEXTTRANSACTION_H

#include <QByteArray>
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

struct StagedBinaryChange {
    QString resourceId;
    QByteArray originalData;
    QByteArray stagedData;
    quint64 baseRevision = 0;
};

struct StagedPackageChange {
    QString resourceId;
    QString originalText;
    QString stagedText;
    quint64 baseRevision = 0;
};

struct StagedArchiveChange {
    QString bookPath;
    QByteArray originalData;
    QByteArray stagedData;
    QString baseFingerprint;
    bool remove = false;
};

struct StagedResourceAddition {
    QString stagingId;
    QString bookPath;
    QString mediaType;
    QString manifestId;
    QString properties;
    QString fallback;
    QString overlay;
    QByteArray data;
    quint64 stagedRevision = 0;
    bool manifested = true;
    bool addToSpine = true;
    bool isText = false;
};

struct StagedResourceRemoval {
    QString resourceId;
    quint64 baseRevision = 0;
};

struct StagedResourceRelocation {
    QString resourceId;
    QString originalBookPath;
    QString targetBookPath;
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
    bool HasBinaryChange(const QString &resource_id) const;
    bool HasPackageChange() const;
    bool HasArchiveChange(const QString &book_path) const;

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
    bool ReadAddedText(const QString &staging_id,
                       QString *text,
                       quint64 *revision) const;
    bool ReplaceAddedText(const QString &staging_id,
                          quint64 expected_revision,
                          const QString &replacement,
                          QString *error);
    bool ApplyAddedTextEdits(const QString &staging_id,
                             quint64 expected_revision,
                             const QJsonArray &edit_values,
                             QString *error);

    QByteArray ReadBinary(const QString &resource_id,
                          const QByteArray &current_data,
                          quint64 current_revision,
                          quint64 *revision) const;
    bool ReplaceBinary(const QString &resource_id,
                       const QByteArray &current_data,
                       quint64 current_revision,
                       quint64 expected_revision,
                       const QByteArray &replacement,
                       QString *error);

    bool ReplacePackage(const QString &resource_id,
                        const QString &current_text,
                        quint64 current_revision,
                        quint64 expected_revision,
                        const QString &replacement,
                        QString *error);
    StagedPackageChange PackageChange() const;

    bool ReplaceArchiveFile(const QString &book_path,
                            const QByteArray &current_data,
                            const QString &current_fingerprint,
                            const QString &expected_fingerprint,
                            const QByteArray &replacement,
                            QString *error);
    bool RemoveArchiveFile(const QString &book_path,
                           const QByteArray &current_data,
                           const QString &current_fingerprint,
                           const QString &expected_fingerprint,
                           QString *error);
    QList<StagedArchiveChange> ArchiveChanges() const;

    bool AddResource(const StagedResourceAddition &addition, QString *error);
    bool RemoveResource(const QString &resource_id, quint64 base_revision, QString *error);
    bool RelocateResource(const QString &resource_id,
                          const QString &original_book_path,
                          const QString &target_book_path,
                          quint64 base_revision,
                          QString *error);

    QList<StagedTextChange> Changes() const;
    QList<StagedBinaryChange> BinaryChanges() const;
    QList<StagedResourceAddition> Additions() const;
    QList<StagedResourceRemoval> Removals() const;
    QList<StagedResourceRelocation> Relocations() const;
    void Clear();

private:
    QString m_Id;
    QString m_Label;
    QString m_CheckpointPolicy;
    quint64 m_BaseBookRevision;
    QHash<QString, StagedTextChange> m_Changes;
    QHash<QString, StagedBinaryChange> m_BinaryChanges;
    StagedPackageChange m_PackageChange;
    bool m_HasPackageChange = false;
    QHash<QString, StagedArchiveChange> m_ArchiveChanges;
    QHash<QString, StagedResourceAddition> m_Additions;
    QHash<QString, StagedResourceRemoval> m_Removals;
    QHash<QString, StagedResourceRelocation> m_Relocations;
};

} // namespace PluginApi

#endif // PLUGINTEXTTRANSACTION_H
