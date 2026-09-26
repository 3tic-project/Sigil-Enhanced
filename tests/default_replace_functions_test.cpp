#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>

#include <cstdio>

#include "Misc/DefaultReplaceFunctions.h"

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString resourcePath = QStringLiteral(":/replace_functions/defaults.json");
    QFile resource(resourcePath);
    if (!resource.open(QIODevice::ReadOnly)) {
        std::fputs("Default replacement resource is missing\n", stderr);
        return 1;
    }
    const QByteArray expected = resource.readAll();
    if (expected.size() != 1722
        || QCryptographicHash::hash(expected, QCryptographicHash::Sha256).toHex()
            != QByteArray("79335fe21dfce2f64589d4e2a152f83f60f60e669ef5ea4391453d325a125b6a")) {
        std::fputs("Default replacement resource differs from the Python golden file\n", stderr);
        return 1;
    }

    QTemporaryDir directory;
    if (!directory.isValid()) return 1;
    const QString destination = directory.filePath(QStringLiteral("replace_functions.json"));
    if (!DefaultReplaceFunctions::CreateIfMissing(destination)) {
        std::fputs("Could not copy default replacement resource\n", stderr);
        return 1;
    }
    QFile copied(destination);
    if (!copied.open(QIODevice::ReadWrite)) {
        std::fprintf(stderr, "Copied replacement file cannot be edited: %s\n",
                     copied.errorString().toUtf8().constData());
        return 1;
    }
    const QByteArray copiedBytes = copied.readAll();
    if (copiedBytes != expected) {
        std::fprintf(stderr, "Copied replacement file differs from the resource: %lld versus %lld bytes\n",
                     static_cast<long long>(copiedBytes.size()),
                     static_cast<long long>(expected.size()));
        return 1;
    }
    if (!copied.resize(0) || copied.write("user override") != 13) return 1;
    copied.close();
    if (DefaultReplaceFunctions::CreateIfMissing(destination)) {
        std::fputs("Copy overwrote a user edited replacement file\n", stderr);
        return 1;
    }
    if (!copied.open(QIODevice::ReadOnly) || copied.readAll() != "user override") return 1;
    return 0;
}
