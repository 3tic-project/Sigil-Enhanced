#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "Misc/AtomicFileWrite.h"

namespace
{

void Require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

QByteArray ReadAll(const QString &path)
{
    QFile file(path);
    Require(file.open(QIODevice::ReadOnly), "test file must open for reading");
    return file.readAll();
}

}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    Require(dir.isValid(), "temporary directory must be created");

    const QString path = dir.filePath(QStringLiteral("cover.png"));
    const QByteArray original("original-image");
    QString error;
    Require(AtomicFile::WriteBytesReplacing(path, original, &error),
            "creating an image file must succeed");
    Require(ReadAll(path) == original, "new file must contain the written bytes");

    const QByteArray rotated("rotated-image-bytes");
    Require(AtomicFile::WriteBytesReplacing(path, rotated, &error),
            "replacing an image file must succeed");
    Require(ReadAll(path) == rotated, "replaced file must contain the new bytes");
    const QStringList names = QDir(dir.path()).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
    Require(names == QStringList{QStringLiteral("cover.png")},
            "a finished replace must not leave a temporary file behind");

    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::ReadUser |
                                QFileDevice::ReadGroup | QFileDevice::ReadOther);
    const QByteArray rejected("should-not-land-partially");
    const bool replacedReadOnly = AtomicFile::WriteBytesReplacing(path, rejected, &error);
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                QFileDevice::ReadUser | QFileDevice::WriteUser);
    const QByteArray afterReadOnly = ReadAll(path);
    if (replacedReadOnly) {
        Require(afterReadOnly == rejected,
                "a reported replace of a read-only file must contain the full new bytes");
    } else {
        Require(afterReadOnly == rotated,
                "a refused replace must leave the previous bytes intact");
        Require(!error.isEmpty(), "a refused replace must report why");
    }

    const QString blocked = dir.filePath(QStringLiteral("missing-dir/cover.png"));
    QString blockedError;
    Require(!AtomicFile::WriteBytesReplacing(blocked, rejected, &blockedError),
            "a missing parent directory must fail");
    Require(!QFileInfo::exists(blocked), "a failed write must not create a partial file");
    Require(ReadAll(path) == afterReadOnly, "a failed write elsewhere must not touch the image");

    Require(!AtomicFile::WriteBytesReplacing(dir.path(), rejected, &blockedError),
            "replacing a directory must fail");
    Require(QFileInfo(dir.path()).isDir(), "a failed directory replace must leave the directory");

    const QString publication = dir.filePath(QStringLiteral("publication/cover.png"));
    QDir().mkpath(QFileInfo(publication).absolutePath());
    Require(AtomicFile::WriteBytesReplacing(publication, rotated, &error),
            "the publication copy must accept the image payload");
    Require(ReadAll(publication) == rotated, "publication copy must contain the edited image");
    Require(ReadAll(path) == afterReadOnly,
            "writing the publication copy must not change the extracted file");

    return EXIT_SUCCESS;
}
