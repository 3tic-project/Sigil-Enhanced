#pragma once

#include <limits>

#include <QByteArray>
#include <QList>
#include <QString>

namespace FontSubset
{

class FontSubsetTransaction
{
public:
    enum class State {
        Empty,
        Staged,
        Committed,
        RolledBack,
        Failed
    };

    bool Stage(const QString& path,
               const QByteArray& expectedOriginal,
               const QByteArray& replacement,
               QString* error = nullptr);
    bool Commit(QString* error = nullptr);
    void Clear();

    State GetState() const;
    int EntryCount() const;

    void SetFailureAfterWritesForTesting(int successfulWrites);

private:
    struct Entry {
        QString path;
        QByteArray original;
        QByteArray replacement;
    };

    bool WriteAtomically(const QString& path, const QByteArray& bytes,
                         QString* error) const;
    bool RollBack(int writtenCount, QString* error);

    QList<Entry> m_Entries;
    State m_State = State::Empty;
    int m_FailureAfterWrites = std::numeric_limits<int>::max();
};

}
