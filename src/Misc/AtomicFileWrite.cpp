/************************************************************************
**
**  This file is part of Sigil.
**
*************************************************************************/

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include "Misc/AtomicFileWrite.h"

namespace AtomicFile
{

bool WriteBytesReplacing(const QString &destination, const QByteArray &bytes, QString *error)
{
    auto assignError = [error](const QString &text) {
        if (error) {
            *error = text;
        }
    };

    QString atomicError;
    {
        QSaveFile file(destination);
        if (!file.open(QIODevice::WriteOnly)) {
            atomicError = file.errorString();
        } else if (file.write(bytes) != static_cast<qint64>(bytes.size())) {
            atomicError = file.errorString();
            file.cancelWriting();
        } else if (!file.commit()) {
            // Windows: ReplaceFileW fails with a sharing violation when
            // Preview, a thumbnail decoder, or the shell still has the
            // file mapped. The original bytes are still in place.
            atomicError = file.errorString();
        } else {
            return true;
        }
    }

    QFile inplace(destination);
    if (inplace.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const qint64 written = inplace.write(bytes);
        const bool flushed = written == static_cast<qint64>(bytes.size()) && inplace.flush();
        const QString inplaceError = inplace.errorString();
        inplace.close();
        if (flushed && QFileInfo(destination).size() == static_cast<qint64>(bytes.size())) {
            return true;
        }
        assignError(inplaceError.isEmpty() ? atomicError : inplaceError);
        return false;
    }

    QString inplaceError = inplace.errorString();
    if (inplaceError.isEmpty()) {
        inplaceError = atomicError;
    }
    assignError(inplaceError);
    return false;
}

}
