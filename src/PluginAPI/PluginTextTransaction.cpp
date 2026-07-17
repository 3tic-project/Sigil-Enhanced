/************************************************************************
**
**  This file is part of Sigil.
**
*************************************************************************/

#include "PluginAPI/PluginTextTransaction.h"

#include <algorithm>
#include <QTextDocument>

#include "PluginAPI/PluginTextEdit.h"

namespace PluginApi
{

TextTransaction::TextTransaction(const QString &id,
                                 const QString &label,
                                 const QString &checkpoint_policy,
                                 quint64 base_book_revision) :
    m_Id(id),
    m_Label(label),
    m_CheckpointPolicy(checkpoint_policy),
    m_BaseBookRevision(base_book_revision)
{
}

QString TextTransaction::Id() const
{
    return m_Id;
}

QString TextTransaction::Label() const
{
    return m_Label;
}

QString TextTransaction::CheckpointPolicy() const
{
    return m_CheckpointPolicy;
}

quint64 TextTransaction::BaseBookRevision() const
{
    return m_BaseBookRevision;
}

bool TextTransaction::IsEmpty() const
{
    return m_Changes.isEmpty() && m_BinaryChanges.isEmpty() && m_Additions.isEmpty()
        && m_Removals.isEmpty() && m_Relocations.isEmpty() && m_ArchiveChanges.isEmpty()
        && !m_HasPackageChange;
}

int TextTransaction::Size() const
{
    return m_Changes.size() + m_BinaryChanges.size() + m_Additions.size()
        + m_Removals.size() + m_Relocations.size() + m_ArchiveChanges.size()
        + (m_HasPackageChange ? 1 : 0);
}

bool TextTransaction::HasChange(const QString &resource_id) const
{
    return m_Changes.contains(resource_id);
}

bool TextTransaction::HasBinaryChange(const QString &resource_id) const
{
    return m_BinaryChanges.contains(resource_id);
}

bool TextTransaction::HasPackageChange() const
{
    return m_HasPackageChange;
}

bool TextTransaction::HasArchiveChange(const QString &book_path) const
{
    return m_ArchiveChanges.contains(book_path);
}

QString TextTransaction::ReadText(const QString &resource_id,
                                  const QString &current_text,
                                  quint64 current_revision,
                                  quint64 *revision) const
{
    const auto found = m_Changes.constFind(resource_id);
    if (found == m_Changes.constEnd()) {
        if (revision) *revision = current_revision;
        return current_text;
    }
    if (revision) *revision = found->baseRevision;
    return found->stagedText;
}

bool TextTransaction::ReplaceText(const QString &resource_id,
                                  const QString &current_text,
                                  quint64 current_revision,
                                  quint64 expected_revision,
                                  const QString &replacement,
                                  QString *error)
{
    auto found = m_Changes.find(resource_id);
    const quint64 required_revision = found == m_Changes.end()
        ? current_revision : found->baseRevision;
    if (expected_revision != required_revision) {
        if (error) *error = QStringLiteral("Revision conflict");
        return false;
    }

    if (found == m_Changes.end()) {
        StagedTextChange change;
        change.resourceId = resource_id;
        change.originalText = current_text;
        change.stagedText = replacement;
        change.baseRevision = current_revision;
        m_Changes.insert(resource_id, change);
    } else {
        found->stagedText = replacement;
    }
    return true;
}

bool TextTransaction::ApplyEdits(const QString &resource_id,
                                 const QString &current_text,
                                 quint64 current_revision,
                                 quint64 expected_revision,
                                 const QJsonArray &edit_values,
                                 QString *error)
{
    quint64 staged_revision = 0;
    const QString source = ReadText(resource_id, current_text, current_revision, &staged_revision);
    if (expected_revision != staged_revision) {
        if (error) *error = QStringLiteral("Revision conflict");
        return false;
    }

    QList<TextEdit> edits;
    if (!ParseTextEdits(edit_values, source, &edits, error)) return false;
    QTextDocument document;
    document.setPlainText(source);
    ApplyTextEdits(&document, edits);
    return ReplaceText(resource_id, current_text, current_revision, expected_revision,
                       document.toRawText(), error);
}

bool TextTransaction::ReadAddedText(const QString &staging_id,
                                    QString *text,
                                    quint64 *revision) const
{
    const auto found = m_Additions.constFind(staging_id);
    if (found == m_Additions.constEnd() || !found->isText) return false;
    if (text) *text = QString::fromUtf8(found->data);
    if (revision) *revision = found->stagedRevision;
    return true;
}

bool TextTransaction::ReplaceAddedText(const QString &staging_id,
                                       quint64 expected_revision,
                                       const QString &replacement,
                                       QString *error)
{
    auto found = m_Additions.find(staging_id);
    if (found == m_Additions.end() || !found->isText) {
        if (error) *error = QStringLiteral("Staged text resource not found");
        return false;
    }
    if (expected_revision != found->stagedRevision) {
        if (error) *error = QStringLiteral("Revision conflict");
        return false;
    }
    found->data = replacement.toUtf8();
    ++found->stagedRevision;
    return true;
}

bool TextTransaction::ApplyAddedTextEdits(const QString &staging_id,
                                          quint64 expected_revision,
                                          const QJsonArray &edit_values,
                                          QString *error)
{
    QString source;
    quint64 revision = 0;
    if (!ReadAddedText(staging_id, &source, &revision)) {
        if (error) *error = QStringLiteral("Staged text resource not found");
        return false;
    }
    if (expected_revision != revision) {
        if (error) *error = QStringLiteral("Revision conflict");
        return false;
    }
    QList<TextEdit> edits;
    if (!ParseTextEdits(edit_values, source, &edits, error)) return false;
    QTextDocument document;
    document.setPlainText(source);
    ApplyTextEdits(&document, edits);
    return ReplaceAddedText(staging_id, expected_revision, document.toRawText(), error);
}

QByteArray TextTransaction::ReadBinary(const QString &resource_id,
                                       const QByteArray &current_data,
                                       quint64 current_revision,
                                       quint64 *revision) const
{
    const auto found = m_BinaryChanges.constFind(resource_id);
    if (found == m_BinaryChanges.constEnd()) {
        if (revision) *revision = current_revision;
        return current_data;
    }
    if (revision) *revision = found->baseRevision;
    return found->stagedData;
}

bool TextTransaction::ReplaceBinary(const QString &resource_id,
                                    const QByteArray &current_data,
                                    quint64 current_revision,
                                    quint64 expected_revision,
                                    const QByteArray &replacement,
                                    QString *error)
{
    auto found = m_BinaryChanges.find(resource_id);
    const quint64 required_revision = found == m_BinaryChanges.end()
        ? current_revision : found->baseRevision;
    if (expected_revision != required_revision) {
        if (error) *error = QStringLiteral("Revision conflict");
        return false;
    }
    if (found == m_BinaryChanges.end()) {
        StagedBinaryChange change;
        change.resourceId = resource_id;
        change.originalData = current_data;
        change.stagedData = replacement;
        change.baseRevision = current_revision;
        m_BinaryChanges.insert(resource_id, change);
    } else {
        found->stagedData = replacement;
    }
    return true;
}

bool TextTransaction::ReplacePackage(const QString &resource_id,
                                     const QString &current_text,
                                     quint64 current_revision,
                                     quint64 expected_revision,
                                     const QString &replacement,
                                     QString *error)
{
    const quint64 required_revision = m_HasPackageChange
        ? m_PackageChange.baseRevision : current_revision;
    if (expected_revision != required_revision) {
        if (error) *error = QStringLiteral("Revision conflict");
        return false;
    }
    if (!m_HasPackageChange) {
        m_PackageChange.resourceId = resource_id;
        m_PackageChange.originalText = current_text;
        m_PackageChange.baseRevision = current_revision;
        m_HasPackageChange = true;
    }
    m_PackageChange.stagedText = replacement;
    return true;
}

StagedPackageChange TextTransaction::PackageChange() const
{
    return m_PackageChange;
}

bool TextTransaction::ReplaceArchiveFile(const QString &book_path,
                                         const QByteArray &current_data,
                                         const QString &current_fingerprint,
                                         const QString &expected_fingerprint,
                                         const QByteArray &replacement,
                                         QString *error)
{
    auto found = m_ArchiveChanges.find(book_path);
    const QString required_fingerprint = found == m_ArchiveChanges.end()
        ? current_fingerprint : found->baseFingerprint;
    if (expected_fingerprint != required_fingerprint) {
        if (error) *error = QStringLiteral("Archive file fingerprint conflict");
        return false;
    }
    if (found == m_ArchiveChanges.end()) {
        StagedArchiveChange change;
        change.bookPath = book_path;
        change.originalData = current_data;
        change.baseFingerprint = current_fingerprint;
        change.stagedData = replacement;
        m_ArchiveChanges.insert(book_path, change);
    } else {
        found->stagedData = replacement;
        found->remove = false;
    }
    return true;
}

bool TextTransaction::RemoveArchiveFile(const QString &book_path,
                                        const QByteArray &current_data,
                                        const QString &current_fingerprint,
                                        const QString &expected_fingerprint,
                                        QString *error)
{
    if (!ReplaceArchiveFile(book_path, current_data, current_fingerprint,
                            expected_fingerprint, QByteArray(), error)) {
        return false;
    }
    m_ArchiveChanges[book_path].remove = true;
    return true;
}

QList<StagedArchiveChange> TextTransaction::ArchiveChanges() const
{
    QList<StagedArchiveChange> result = m_ArchiveChanges.values();
    std::sort(result.begin(), result.end(), [](const StagedArchiveChange &left,
                                               const StagedArchiveChange &right) {
        return left.bookPath < right.bookPath;
    });
    return result;
}

bool TextTransaction::AddResource(const StagedResourceAddition &addition, QString *error)
{
    if (addition.stagingId.isEmpty() || addition.bookPath.isEmpty()
        || addition.mediaType.isEmpty()
        || (addition.manifested && addition.manifestId.isEmpty())) {
        if (error) *error = QStringLiteral("Added resource fields are incomplete");
        return false;
    }
    for (const StagedResourceAddition &existing : m_Additions) {
        if (existing.bookPath == addition.bookPath
            || (addition.manifested && existing.manifested
                && existing.manifestId == addition.manifestId)) {
            if (error) *error = QStringLiteral("Added resource path or manifest ID is duplicated");
            return false;
        }
    }
    m_Additions.insert(addition.stagingId, addition);
    return true;
}

bool TextTransaction::RemoveResource(const QString &resource_id,
                                     quint64 base_revision,
                                     QString *error)
{
    if (resource_id.isEmpty() || m_Relocations.contains(resource_id)) {
        if (error) *error = QStringLiteral("Resource cannot be removed after relocation");
        return false;
    }
    m_Removals.insert(resource_id, StagedResourceRemoval { resource_id, base_revision });
    return true;
}

bool TextTransaction::RelocateResource(const QString &resource_id,
                                       const QString &original_book_path,
                                       const QString &target_book_path,
                                       quint64 base_revision,
                                       QString *error)
{
    if (resource_id.isEmpty() || original_book_path.isEmpty() || target_book_path.isEmpty()
        || m_Removals.contains(resource_id)) {
        if (error) *error = QStringLiteral("Resource cannot be relocated");
        return false;
    }
    for (const StagedResourceRelocation &existing : m_Relocations) {
        if (existing.resourceId != resource_id && existing.targetBookPath == target_book_path) {
            if (error) *error = QStringLiteral("Relocation target is duplicated");
            return false;
        }
    }
    m_Relocations.insert(resource_id, StagedResourceRelocation {
        resource_id, original_book_path, target_book_path, base_revision
    });
    return true;
}

QList<StagedTextChange> TextTransaction::Changes() const
{
    QList<StagedTextChange> result = m_Changes.values();
    std::sort(result.begin(), result.end(), [](const StagedTextChange &left,
                                               const StagedTextChange &right) {
        return left.resourceId < right.resourceId;
    });
    return result;
}

QList<StagedBinaryChange> TextTransaction::BinaryChanges() const
{
    QList<StagedBinaryChange> result = m_BinaryChanges.values();
    std::sort(result.begin(), result.end(), [](const StagedBinaryChange &left,
                                               const StagedBinaryChange &right) {
        return left.resourceId < right.resourceId;
    });
    return result;
}

QList<StagedResourceAddition> TextTransaction::Additions() const
{
    QList<StagedResourceAddition> result = m_Additions.values();
    std::sort(result.begin(), result.end(), [](const StagedResourceAddition &left,
                                               const StagedResourceAddition &right) {
        return left.bookPath < right.bookPath;
    });
    return result;
}

QList<StagedResourceRemoval> TextTransaction::Removals() const
{
    QList<StagedResourceRemoval> result = m_Removals.values();
    std::sort(result.begin(), result.end(), [](const StagedResourceRemoval &left,
                                               const StagedResourceRemoval &right) {
        return left.resourceId < right.resourceId;
    });
    return result;
}

QList<StagedResourceRelocation> TextTransaction::Relocations() const
{
    QList<StagedResourceRelocation> result = m_Relocations.values();
    std::sort(result.begin(), result.end(), [](const StagedResourceRelocation &left,
                                               const StagedResourceRelocation &right) {
        return left.resourceId < right.resourceId;
    });
    return result;
}

void TextTransaction::Clear()
{
    m_Changes.clear();
    m_BinaryChanges.clear();
    m_PackageChange = StagedPackageChange();
    m_HasPackageChange = false;
    m_ArchiveChanges.clear();
    m_Additions.clear();
    m_Removals.clear();
    m_Relocations.clear();
}

} // namespace PluginApi
