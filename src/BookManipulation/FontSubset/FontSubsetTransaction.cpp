#include "BookManipulation/FontSubset/FontSubsetTransaction.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace FontSubset
{
namespace
{

void SetError(QString* error, const QString& message)
{
    if (error) {
        *error = message;
    }
}

}

bool FontSubsetTransaction::Stage(const QString& path,
                                  const QByteArray& expectedOriginal,
                                  const QByteArray& replacement,
                                  QString* error)
{
    if (error) {
        error->clear();
    }
    if (m_State != State::Empty && m_State != State::Staged) {
        SetError(error, QStringLiteral("The transaction cannot accept more entries."));
        return false;
    }
    const QFileInfo info(path);
    if (!info.isFile() || info.isSymLink()) {
        SetError(error, QStringLiteral("The font path is not a regular file: %1").arg(path));
        return false;
    }
    if (replacement.isEmpty()) {
        SetError(error, QStringLiteral("The replacement font is empty: %1").arg(path));
        return false;
    }
    if (replacement == expectedOriginal) {
        SetError(error, QStringLiteral("The replacement font is unchanged: %1").arg(path));
        return false;
    }
    const QString canonicalPath = info.canonicalFilePath();
    for (const Entry& entry : m_Entries) {
        if (entry.path == canonicalPath) {
            SetError(error, QStringLiteral("The font was staged more than once: %1").arg(path));
            return false;
        }
    }
    QFile file(canonicalPath);
    if (!file.open(QIODevice::ReadOnly)) {
        SetError(error, QStringLiteral("Could not read the font: %1").arg(path));
        return false;
    }
    const QByteArray current = file.readAll();
    if (current != expectedOriginal) {
        SetError(error, QStringLiteral("The font changed before it was staged: %1").arg(path));
        return false;
    }
    m_Entries.append({canonicalPath, current, replacement});
    m_State = State::Staged;
    return true;
}

bool FontSubsetTransaction::Commit(QString* error)
{
    if (error) {
        error->clear();
    }
    if (m_State != State::Staged || m_Entries.isEmpty()) {
        SetError(error, QStringLiteral("No font replacements are staged."));
        return false;
    }
    for (const Entry& entry : m_Entries) {
        QFile file(entry.path);
        if (!file.open(QIODevice::ReadOnly) || file.readAll() != entry.original) {
            m_State = State::Failed;
            SetError(error, QStringLiteral(
                "A font changed after analysis; no files were written: %1").arg(entry.path));
            return false;
        }
    }

    int writtenCount = 0;
    for (const Entry& entry : m_Entries) {
        if (writtenCount >= m_FailureAfterWrites) {
            QString rollbackError;
            const bool restored = RollBack(writtenCount, &rollbackError);
            SetError(error, restored
                ? QStringLiteral("Injected font transaction failure; changes were rolled back.")
                : rollbackError);
            return false;
        }
        QString writeError;
        if (!WriteAtomically(entry.path, entry.replacement, &writeError)) {
            QString rollbackError;
            const bool restored = RollBack(writtenCount, &rollbackError);
            SetError(error, restored ? writeError : writeError + QLatin1Char('\n') + rollbackError);
            return false;
        }
        ++writtenCount;
    }
    m_State = State::Committed;
    return true;
}

void FontSubsetTransaction::Clear()
{
    m_Entries.clear();
    m_State = State::Empty;
    m_FailureAfterWrites = std::numeric_limits<int>::max();
}

FontSubsetTransaction::State FontSubsetTransaction::GetState() const
{
    return m_State;
}

int FontSubsetTransaction::EntryCount() const
{
    return m_Entries.size();
}

void FontSubsetTransaction::SetFailureAfterWritesForTesting(int successfulWrites)
{
    m_FailureAfterWrites = successfulWrites < 0
        ? std::numeric_limits<int>::max() : successfulWrites;
}

bool FontSubsetTransaction::WriteAtomically(const QString& path,
                                            const QByteArray& bytes,
                                            QString* error) const
{
    const QFile::Permissions permissions = QFileInfo(path).permissions();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        SetError(error, QStringLiteral("Could not open a font for replacement: %1").arg(path));
        return false;
    }
    file.setPermissions(permissions);
    if (file.write(bytes) != bytes.size()) {
        file.cancelWriting();
        SetError(error, QStringLiteral("Could not write the complete font: %1").arg(path));
        return false;
    }
    if (!file.commit()) {
        SetError(error, QStringLiteral("Could not commit the font replacement: %1").arg(path));
        return false;
    }
    return true;
}

bool FontSubsetTransaction::RollBack(int writtenCount, QString* error)
{
    bool restored = true;
    QStringList failures;
    for (int i = writtenCount - 1; i >= 0; --i) {
        QString writeError;
        if (!WriteAtomically(m_Entries.at(i).path, m_Entries.at(i).original,
                             &writeError)) {
            restored = false;
            failures.append(writeError);
        }
    }
    m_State = restored ? State::RolledBack : State::Failed;
    if (!restored) {
        SetError(error, QStringLiteral("Font transaction rollback failed:\n%1")
                            .arg(failures.join(QLatin1Char('\n'))));
    }
    return restored;
}

}
