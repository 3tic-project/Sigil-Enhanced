/************************************************************************
**
**  This file is part of Sigil.
**
*************************************************************************/

#pragma once
#ifndef ATOMICFILEWRITE_H
#define ATOMICFILEWRITE_H

#include <QByteArray>
#include <QString>

// Replace destination with bytes.
// QSaveFile commits via rename, or ReplaceFileW on Windows, so a failed
// write leaves the existing file untouched. If that replace is rejected
// because another handle still has the file open, fall back to an in-place
// overwrite. When both fail the caller keeps the bytes and the EPUB export
// writes them into the new publication copy.
namespace AtomicFile
{
bool WriteBytesReplacing(const QString &destination, const QByteArray &bytes, QString *error = nullptr);
}

#endif
