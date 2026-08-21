/************************************************************************
**
**  Copyright (C) 2026 Sigil Enhanced contributors
**
**  This file is part of Sigil-Enhanced.
**
**  Sigil-Enhanced is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  (at your option) any later version.
**
*************************************************************************/

#include "Misc/EpubFileSnapshot.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QByteArrayView>

namespace
{

QByteArray hashFile(QFile& file, QString* error)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray buffer(1024 * 1024, Qt::Uninitialized);
    while (true) {
        const qint64 count = file.read(buffer.data(), buffer.size());
        if (count < 0) {
            if (error) {
                *error = file.errorString();
            }
            return QByteArray();
        }
        if (count == 0) {
            break;
        }
        hash.addData(QByteArrayView(buffer.constData(), count));
    }
    return hash.result();
}

bool samePath(const QString& left, const QString& right)
{
    const QString left_path = QFileInfo(left).absoluteFilePath();
    const QString right_path = QFileInfo(right).absoluteFilePath();
#ifdef Q_OS_WIN32
    return left_path.compare(right_path, Qt::CaseInsensitive) == 0;
#else
    return left_path == right_path;
#endif
}

}

EpubFileSnapshot::EpubFileSnapshot()
    : m_Size(-1),
      m_ModifiedMs(-1),
      m_Valid(false)
{
}

EpubFileSnapshot EpubFileSnapshot::capture(const QString& path, QString* error)
{
    EpubFileSnapshot snapshot;
    QFileInfo before(path);
    if (!before.exists() || !before.isFile() || !before.isReadable()) {
        if (error) {
            *error = QStringLiteral("Source EPUB is not a readable file: %1")
                .arg(QDir::toNativeSeparators(path));
        }
        return snapshot;
    }

    QFile file(before.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = file.errorString();
        }
        return snapshot;
    }

    QString hash_error;
    const QByteArray digest = hashFile(file, &hash_error);
    file.close();
    if (digest.isEmpty()) {
        if (error) {
            *error = hash_error;
        }
        return snapshot;
    }

    QFileInfo after(path);
    const qint64 before_modified = before.lastModified().toMSecsSinceEpoch();
    const qint64 after_modified = after.lastModified().toMSecsSinceEpoch();
    if (!after.exists() || before.size() != after.size()
        || before_modified != after_modified) {
        if (error) {
            *error = QStringLiteral("Source EPUB changed while its save snapshot was being created.");
        }
        return snapshot;
    }

    snapshot.m_Path = after.absoluteFilePath();
    snapshot.m_Size = after.size();
    snapshot.m_ModifiedMs = after_modified;
    snapshot.m_Sha256 = digest;
    snapshot.m_Valid = true;
    return snapshot;
}

bool EpubFileSnapshot::isValid() const
{
    return m_Valid;
}

QString EpubFileSnapshot::path() const
{
    return m_Path;
}

QByteArray EpubFileSnapshot::sha256() const
{
    return m_Sha256;
}

bool EpubFileSnapshot::matchesSource(QString* error) const
{
    if (!m_Valid) {
        if (error) {
            *error = QStringLiteral("No source EPUB snapshot is available.");
        }
        return false;
    }

    const QFileInfo current(m_Path);
    if (!current.exists() || !current.isFile() || !current.isReadable()) {
        if (error) {
            *error = QStringLiteral("The source EPUB is no longer readable: %1")
                .arg(QDir::toNativeSeparators(m_Path));
        }
        return false;
    }
    if (current.size() != m_Size
        || current.lastModified().toMSecsSinceEpoch() != m_ModifiedMs) {
        if (error) {
            *error = QStringLiteral("The source EPUB was modified outside Sigil-Enhanced.");
        }
        return false;
    }

    QFile file(m_Path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    QString hash_error;
    const QByteArray digest = hashFile(file, &hash_error);
    if (digest.isEmpty() || digest != m_Sha256) {
        if (error) {
            *error = hash_error.isEmpty()
                ? QStringLiteral("The source EPUB content changed outside Sigil-Enhanced.")
                : hash_error;
        }
        return false;
    }
    return true;
}

bool EpubFileSnapshot::copyTo(const QString& destination, QString* error) const
{
    if (!matchesSource(error)) {
        return false;
    }
    if (samePath(m_Path, destination)) {
        return true;
    }

    QFile source(m_Path);
    if (!source.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = source.errorString();
        }
        return false;
    }

    QSaveFile output(destination);
    if (!output.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = output.errorString();
        }
        return false;
    }

    QByteArray buffer(1024 * 1024, Qt::Uninitialized);
    while (true) {
        const qint64 count = source.read(buffer.data(), buffer.size());
        if (count < 0) {
            if (error) {
                *error = source.errorString();
            }
            output.cancelWriting();
            return false;
        }
        if (count == 0) {
            break;
        }
        if (output.write(buffer.constData(), count) != count) {
            if (error) {
                *error = output.errorString();
            }
            output.cancelWriting();
            return false;
        }
    }

    if (!output.commit()) {
        if (error) {
            *error = output.errorString();
        }
        return false;
    }
    return true;
}

void EpubFileSnapshot::clear()
{
    *this = EpubFileSnapshot();
}
