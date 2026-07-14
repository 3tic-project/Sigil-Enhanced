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
    return m_Changes.isEmpty() && m_BinaryChanges.isEmpty();
}

int TextTransaction::Size() const
{
    return m_Changes.size() + m_BinaryChanges.size();
}

bool TextTransaction::HasChange(const QString &resource_id) const
{
    return m_Changes.contains(resource_id);
}

bool TextTransaction::HasBinaryChange(const QString &resource_id) const
{
    return m_BinaryChanges.contains(resource_id);
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

void TextTransaction::Clear()
{
    m_Changes.clear();
    m_BinaryChanges.clear();
}

} // namespace PluginApi
