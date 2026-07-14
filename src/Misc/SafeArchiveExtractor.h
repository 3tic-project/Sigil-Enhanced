#pragma once
#ifndef SAFEARCHIVEEXTRACTOR_H
#define SAFEARCHIVEEXTRACTOR_H

#include <functional>

#include <QCoreApplication>
#include <QDateTime>
#include <QList>
#include <QString>

class SafeArchiveExtractor
{
    Q_DECLARE_TR_FUNCTIONS(SafeArchiveExtractor)

public:
    struct Limits {
        quint64 maxEntries = 100000;
        quint64 maxSingleFileBytes = 2ULL * 1024 * 1024 * 1024;
        quint64 maxTotalBytes = 8ULL * 1024 * 1024 * 1024;
        quint64 maxCompressionRatio = 200;
        int maxPathDepth = 64;
        int maxPathLength = 1024;
    };

    struct Entry {
        QString path;
        quint64 uncompressedSize = 0;
        quint32 crc = 0;
        QDateTime modified;
        bool directory = false;
    };

    enum class Error {
        None,
        CannotOpenArchive,
        InvalidDestination,
        InvalidPath,
        DuplicatePath,
        SymbolicLink,
        EntryCountLimit,
        SingleFileLimit,
        TotalSizeLimit,
        CompressionRatioLimit,
        CannotCreateDirectory,
        CannotWriteFile,
        CorruptArchive,
        Cancelled
    };

    struct Result {
        bool ok = false;
        Error error = Error::None;
        QString detail;
        QList<Entry> entries;
        quint64 totalBytes = 0;
    };

    using CancelCheck = std::function<bool()>;

    static Result extract(const QString& archivePath,
                          const QString& destinationRoot);

    static Result extract(const QString& archivePath,
                          const QString& destinationRoot,
                          const Limits& limits,
                          const CancelCheck& cancelled = CancelCheck());

    static bool safeArchivePath(const QString& destinationRoot,
                                const QString& archivePath,
                                QString* targetPath = nullptr,
                                QString* normalizedPath = nullptr);

    static QString errorMessage(const Result& result);
};

#endif // SAFEARCHIVEEXTRACTOR_H
