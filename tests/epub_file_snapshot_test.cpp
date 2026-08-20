#include <QDateTime>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>

#include <cstdlib>
#include <iostream>

#include "Misc/EpubFileSnapshot.h"

namespace
{

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

void writeFile(const QString& path, const QByteArray& data)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "fixture must be writable");
    require(file.write(data) == data.size(), "fixture write must complete");
}

}

int main()
{
    QTemporaryDir directory;
    require(directory.isValid(), "temporary directory must be available");

    const QString source = directory.filePath(QStringLiteral("source.epub"));
    const QString copy = directory.filePath(QStringLiteral("copy.epub"));
    const QByteArray original("PK\x03\x04\0test-epub-bytes", 20);
    writeFile(source, original);

    QString error;
    const EpubFileSnapshot snapshot = EpubFileSnapshot::capture(source, &error);
    require(snapshot.isValid(), "snapshot capture must succeed");
    require(snapshot.matchesSource(&error), "an unchanged source must match its snapshot");
    require(snapshot.copyTo(copy, &error), "byte-preserving copy must succeed");

    QFile copied(copy);
    require(copied.open(QIODevice::ReadOnly), "copied EPUB must be readable");
    require(copied.readAll() == original, "copied EPUB must be byte-identical");

    QThread::msleep(5);
    writeFile(source, QByteArray("PK\x03\x04\0changed-epub", 18));
    require(!snapshot.matchesSource(&error), "an externally changed source must be rejected");

    return 0;
}
