/************************************************************************
**
**  This file is part of Sigil.
**
**  Sigil is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  (at your option) any later version.
**
*************************************************************************/

#ifdef _WIN32
#define NOMINMAX
#endif

#include "unzip.h"
#ifdef _WIN32
#include "iowin32.h"
#endif

#include <limits>
#include <string>

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStringDecoder>

#include "Misc/SafeArchiveExtractor.h"

namespace
{

constexpr int READ_BUFFER_SIZE = 8192;

class ArchiveHandle
{
public:
    explicit ArchiveHandle(unzFile handle) : m_Handle(handle) {}
    ~ArchiveHandle()
    {
        if (m_Handle) {
            unzClose(m_Handle);
        }
    }

    unzFile get() const { return m_Handle; }

private:
    unzFile m_Handle;
};

bool isReservedWindowsName(const QString& segment)
{
    const QString base = segment.section('.', 0, 0).toUpper();
    static const QRegularExpression reserved(
        QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$"));
    return reserved.match(base).hasMatch();
}

bool isSymbolicLink(const unz_file_info64& info)
{
    constexpr quint32 unixFileTypeMask = 0170000;
    constexpr quint32 unixSymbolicLink = 0120000;
    const quint32 unixMode = (info.external_fa >> 16) & 0xffff;
    return (unixMode & unixFileTypeMask) == unixSymbolicLink;
}

QString decodeCp437(const QByteArray& bytes)
{
    static const ushort highCharacters[] = {
        0x00c7, 0x00fc, 0x00e9, 0x00e2, 0x00e4, 0x00e0, 0x00e5, 0x00e7,
        0x00ea, 0x00eb, 0x00e8, 0x00ef, 0x00ee, 0x00ec, 0x00c4, 0x00c5,
        0x00c9, 0x00e6, 0x00c6, 0x00f4, 0x00f6, 0x00f2, 0x00fb, 0x00f9,
        0x00ff, 0x00d6, 0x00dc, 0x00a2, 0x00a3, 0x00a5, 0x20a7, 0x0192,
        0x00e1, 0x00ed, 0x00f3, 0x00fa, 0x00f1, 0x00d1, 0x00aa, 0x00ba,
        0x00bf, 0x2310, 0x00ac, 0x00bd, 0x00bc, 0x00a1, 0x00ab, 0x00bb,
        0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
        0x2555, 0x2563, 0x2551, 0x2557, 0x255d, 0x255c, 0x255b, 0x2510,
        0x2514, 0x2534, 0x252c, 0x251c, 0x2500, 0x253c, 0x255e, 0x255f,
        0x255a, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256c, 0x2567,
        0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256b,
        0x256a, 0x2518, 0x250c, 0x2588, 0x2584, 0x258c, 0x2590, 0x2580,
        0x03b1, 0x00df, 0x0393, 0x03c0, 0x03a3, 0x03c3, 0x00b5, 0x03c4,
        0x03a6, 0x0398, 0x03a9, 0x03b4, 0x221e, 0x03c6, 0x03b5, 0x2229,
        0x2261, 0x00b1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00f7, 0x2248,
        0x00b0, 0x2219, 0x00b7, 0x221a, 0x207f, 0x00b2, 0x25a0, 0x00a0
    };

    QString decoded;
    decoded.reserve(bytes.size());
    for (const char byte : bytes) {
        const uchar value = static_cast<uchar>(byte);
        decoded.append(value < 0x80 ? QChar(value) : QChar(highCharacters[value - 0x80]));
    }
    return decoded;
}

QString decodedFileName(unzFile archive,
                        const unz_file_info64& info,
                        bool* valid)
{
    *valid = false;
    if (info.size_filename == 0 ||
        info.size_filename > static_cast<quint64>(std::numeric_limits<int>::max() - 1)) {
        return QString();
    }

    QByteArray bytes(static_cast<int>(info.size_filename) + 1, '\0');
    unz_file_info64 currentInfo;
    if (unzGetCurrentFileInfo64(archive, &currentInfo, bytes.data(),
                                static_cast<uLong>(bytes.size()), nullptr, 0,
                                nullptr, 0) != UNZ_OK) {
        return QString();
    }
    bytes.chop(1);

    QString path;
    if (info.flag & (1 << 11)) {
        QStringDecoder utf8(QStringDecoder::Utf8);
        path = utf8.decode(bytes);
        if (utf8.hasError()) {
            return QString();
        }
    } else {
        path = decodeCp437(bytes);
    }
    *valid = !path.isNull();
    return path.normalized(QString::NormalizationForm_C);
}

bool ratioExceeded(quint64 uncompressed, quint64 compressed, quint64 ratio)
{
    if (uncompressed == 0) {
        return false;
    }
    if (compressed == 0 || ratio == 0) {
        return true;
    }
    if (compressed > std::numeric_limits<quint64>::max() / ratio) {
        return false;
    }
    return uncompressed > compressed * ratio;
}

quint64 totalBudget(const QString& archivePath,
                    const SafeArchiveExtractor::Limits& limits)
{
    const quint64 archiveSize = static_cast<quint64>(QFileInfo(archivePath).size());
    quint64 ratioBudget = limits.maxTotalBytes;
    if (limits.maxCompressionRatio == 0) {
        ratioBudget = 0;
    } else if (archiveSize <= std::numeric_limits<quint64>::max() /
                              limits.maxCompressionRatio) {
        ratioBudget = archiveSize * limits.maxCompressionRatio;
    }
    return qMin(limits.maxTotalBytes, ratioBudget);
}

bool ensureDirectory(const QString& root,
                     const QString& relativeDirectory,
                     QStringList* createdDirectories)
{
    QString current = QDir(root).absolutePath();
    const QStringList segments = relativeDirectory.split('/', Qt::SkipEmptyParts);
    for (const QString& segment : segments) {
        current = QDir(current).filePath(segment);
        const QFileInfo info(current);
        if (info.exists()) {
            if (!info.isDir() || info.isSymLink()) {
                return false;
            }
            continue;
        }
        if (!QDir().mkdir(current)) {
            return false;
        }
        createdDirectories->append(current);
    }
    return true;
}

void cleanCreatedContent(const QStringList& files, const QStringList& directories)
{
    for (auto it = files.crbegin(); it != files.crend(); ++it) {
        QFile::remove(*it);
    }
    for (auto it = directories.crbegin(); it != directories.crend(); ++it) {
        QDir().rmdir(*it);
    }
}

} // namespace

bool SafeArchiveExtractor::safeArchivePath(const QString& destinationRoot,
                                           const QString& archivePath,
                                           QString* targetPath,
                                           QString* normalizedPath)
{
    if (archivePath.isEmpty() || archivePath.size() > 1024 ||
        archivePath.contains(QChar::Null) || archivePath.contains('\\') ||
        archivePath.startsWith('/') || archivePath.startsWith("//") ||
        QRegularExpression(QStringLiteral("^[A-Za-z]:")).match(archivePath).hasMatch()) {
        return false;
    }

    QString relative = archivePath.normalized(QString::NormalizationForm_C);
    if (relative.endsWith('/')) {
        relative.chop(1);
    }
    const QStringList segments = relative.split('/', Qt::KeepEmptyParts);
    if (segments.isEmpty()) {
        return false;
    }
    for (const QString& segment : segments) {
        if (segment.isEmpty() || segment == "." || segment == ".." ||
            segment.contains(':') || segment.endsWith('.') || segment.endsWith(' ') ||
            isReservedWindowsName(segment)) {
            return false;
        }
    }

    const QString cleanRelative = QDir::cleanPath(relative);
    if (cleanRelative != relative) {
        return false;
    }

    const QString root = QDir::cleanPath(QDir(destinationRoot).absolutePath());
    const QFileInfo rootInfo(root);
    if (!rootInfo.exists() || !rootInfo.isDir() || rootInfo.isSymLink()) {
        return false;
    }
    const QString target = QDir::cleanPath(QDir(root).absoluteFilePath(cleanRelative));
    const QString prefix = root.endsWith('/') ? root : root + '/';
    if (!target.startsWith(prefix)) {
        return false;
    }

    QString current = root;
    for (int i = 0; i + 1 < segments.size(); ++i) {
        current = QDir(current).filePath(segments.at(i));
        const QFileInfo info(current);
        if (info.exists() && info.isSymLink()) {
            return false;
        }
    }

    if (targetPath) {
        *targetPath = target;
    }
    if (normalizedPath) {
        *normalizedPath = cleanRelative;
    }
    return true;
}

SafeArchiveExtractor::Result SafeArchiveExtractor::extract(
    const QString& archivePath,
    const QString& destinationRoot)
{
    return extract(archivePath, destinationRoot, Limits(), CancelCheck());
}

SafeArchiveExtractor::Result SafeArchiveExtractor::extract(
    const QString& archivePath,
    const QString& destinationRoot,
    const Limits& limits,
    const CancelCheck& cancelled)
{
    Result result;
    QStringList createdFiles;
    QStringList createdDirectories;
    auto fail = [&](Error error, const QString& detail) {
        result.ok = false;
        result.error = error;
        result.detail = detail;
        cleanCreatedContent(createdFiles, createdDirectories);
        result.entries.clear();
        result.totalBytes = 0;
        return result;
    };

    const QFileInfo rootInfo(destinationRoot);
    if (!rootInfo.exists() || !rootInfo.isDir() || rootInfo.isSymLink()) {
        return fail(Error::InvalidDestination, destinationRoot);
    }

#ifdef Q_OS_WIN32
    zlib_filefunc64_def fileFunctions;
    fill_win32_filefunc64W(&fileFunctions);
    const std::wstring nativePath = QDir::toNativeSeparators(archivePath).toStdWString();
    ArchiveHandle archive(unzOpen2_64(nativePath.c_str(), &fileFunctions));
#else
    ArchiveHandle archive(unzOpen64(QDir::toNativeSeparators(archivePath).toUtf8().constData()));
#endif
    if (!archive.get() || !QFileInfo(archivePath).isReadable()) {
        return fail(Error::CannotOpenArchive, archivePath);
    }

    const quint64 effectiveTotalBudget = totalBudget(archivePath, limits);
    quint64 declaredTotal = 0;
    quint64 entryCount = 0;
    QSet<QString> seenPaths;
    int status = unzGoToFirstFile(archive.get());
    if (status == UNZ_END_OF_LIST_OF_FILE) {
        result.ok = true;
        return result;
    }
    if (status != UNZ_OK) {
        return fail(Error::CorruptArchive, archivePath);
    }

    do {
        if (cancelled && cancelled()) {
            return fail(Error::Cancelled, QString());
        }
        if (++entryCount > limits.maxEntries) {
            return fail(Error::EntryCountLimit, QString::number(limits.maxEntries));
        }

        unz_file_info64 info;
        if (unzGetCurrentFileInfo64(archive.get(), &info, nullptr, 0,
                                    nullptr, 0, nullptr, 0) != UNZ_OK) {
            return fail(Error::CorruptArchive, archivePath);
        }
        bool nameValid = false;
        const QString rawPath = decodedFileName(archive.get(), info, &nameValid);
        if (!nameValid || rawPath.size() > limits.maxPathLength) {
            return fail(Error::InvalidPath, rawPath);
        }
        QString targetPath;
        QString relativePath;
        if (!safeArchivePath(destinationRoot, rawPath, &targetPath, &relativePath) ||
            relativePath.count('/') + 1 > limits.maxPathDepth) {
            return fail(Error::InvalidPath, rawPath);
        }

#if defined(Q_OS_WIN32) || defined(Q_OS_MAC)
        const QString collisionKey = relativePath.toCaseFolded();
#else
        const QString collisionKey = relativePath;
#endif
        if (seenPaths.contains(collisionKey)) {
            return fail(Error::DuplicatePath, relativePath);
        }
        seenPaths.insert(collisionKey);

        if (isSymbolicLink(info)) {
            return fail(Error::SymbolicLink, relativePath);
        }
        const bool isDirectory = rawPath.endsWith('/');
        if (!isDirectory) {
            const quint64 uncompressed = static_cast<quint64>(info.uncompressed_size);
            const quint64 compressed = static_cast<quint64>(info.compressed_size);
            if (uncompressed > limits.maxSingleFileBytes) {
                return fail(Error::SingleFileLimit, relativePath);
            }
            if (ratioExceeded(uncompressed, compressed, limits.maxCompressionRatio)) {
                return fail(Error::CompressionRatioLimit, relativePath);
            }
            if (declaredTotal > effectiveTotalBudget ||
                uncompressed > effectiveTotalBudget - declaredTotal) {
                return fail(Error::TotalSizeLimit, relativePath);
            }
            declaredTotal += uncompressed;
        }

        Entry extracted;
        extracted.path = relativePath;
        extracted.uncompressedSize = static_cast<quint64>(info.uncompressed_size);
        extracted.crc = static_cast<quint32>(info.crc);
        extracted.modified = QDateTime(
            QDate(info.tmu_date.tm_year, info.tmu_date.tm_mon + 1, info.tmu_date.tm_mday),
            QTime(info.tmu_date.tm_hour, info.tmu_date.tm_min, info.tmu_date.tm_sec));
        extracted.directory = isDirectory;

        if (isDirectory) {
            if (!ensureDirectory(destinationRoot, relativePath, &createdDirectories)) {
                return fail(Error::CannotCreateDirectory, relativePath);
            }
            result.entries.append(extracted);
            status = unzGoToNextFile(archive.get());
            continue;
        }

        const QString parent = QFileInfo(relativePath).path();
        if (parent != "." &&
            !ensureDirectory(destinationRoot, parent, &createdDirectories)) {
            return fail(Error::CannotCreateDirectory, relativePath);
        }
        if (QFileInfo::exists(targetPath)) {
            return fail(Error::DuplicatePath, relativePath);
        }
        if (unzOpenCurrentFile(archive.get()) != UNZ_OK) {
            return fail(Error::CorruptArchive, relativePath);
        }

        QSaveFile output(targetPath);
        output.setDirectWriteFallback(false);
        if (!output.open(QIODevice::WriteOnly)) {
            unzCloseCurrentFile(archive.get());
            return fail(Error::CannotWriteFile, relativePath);
        }

        char buffer[READ_BUFFER_SIZE];
        int bytesRead = 0;
        quint64 fileBytes = 0;
        while ((bytesRead = unzReadCurrentFile(archive.get(), buffer, sizeof(buffer))) > 0) {
            if (cancelled && cancelled()) {
                output.cancelWriting();
                unzCloseCurrentFile(archive.get());
                return fail(Error::Cancelled, relativePath);
            }
            const quint64 chunk = static_cast<quint64>(bytesRead);
            if (fileBytes > limits.maxSingleFileBytes ||
                chunk > limits.maxSingleFileBytes - fileBytes) {
                output.cancelWriting();
                unzCloseCurrentFile(archive.get());
                return fail(Error::SingleFileLimit, relativePath);
            }
            if (result.totalBytes > effectiveTotalBudget ||
                chunk > effectiveTotalBudget - result.totalBytes) {
                output.cancelWriting();
                unzCloseCurrentFile(archive.get());
                return fail(Error::TotalSizeLimit, relativePath);
            }
            if (output.write(buffer, bytesRead) != bytesRead) {
                output.cancelWriting();
                unzCloseCurrentFile(archive.get());
                return fail(Error::CannotWriteFile, relativePath);
            }
            fileBytes += chunk;
            result.totalBytes += chunk;
        }

        const int closeStatus = unzCloseCurrentFile(archive.get());
        if (bytesRead < 0 || closeStatus != UNZ_OK || fileBytes != extracted.uncompressedSize) {
            output.cancelWriting();
            return fail(Error::CorruptArchive, relativePath);
        }
        if (!output.commit()) {
            return fail(Error::CannotWriteFile, relativePath);
        }
        createdFiles.append(targetPath);
        QFile::setPermissions(targetPath,
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                              QFileDevice::ReadUser | QFileDevice::WriteUser |
                              QFileDevice::ReadOther);
        result.entries.append(extracted);
        status = unzGoToNextFile(archive.get());
    } while (status == UNZ_OK);

    if (status != UNZ_END_OF_LIST_OF_FILE) {
        return fail(Error::CorruptArchive, archivePath);
    }
    result.ok = true;
    result.error = Error::None;
    return result;
}

QString SafeArchiveExtractor::errorMessage(const Result& result)
{
    QString message;
    switch (result.error) {
    case Error::None: message = tr("No error"); break;
    case Error::CannotOpenArchive: message = tr("Cannot open archive"); break;
    case Error::InvalidDestination: message = tr("Invalid extraction destination"); break;
    case Error::InvalidPath: message = tr("Unsafe archive path"); break;
    case Error::DuplicatePath: message = tr("Duplicate archive path"); break;
    case Error::SymbolicLink: message = tr("Archive links are not allowed"); break;
    case Error::EntryCountLimit: message = tr("Archive file-count limit exceeded"); break;
    case Error::SingleFileLimit: message = tr("Archive single-file size limit exceeded"); break;
    case Error::TotalSizeLimit: message = tr("Archive total-size limit exceeded"); break;
    case Error::CompressionRatioLimit: message = tr("Archive compression-ratio limit exceeded"); break;
    case Error::CannotCreateDirectory: message = tr("Cannot create extraction directory"); break;
    case Error::CannotWriteFile: message = tr("Cannot write extracted file"); break;
    case Error::CorruptArchive: message = tr("Archive is corrupt or truncated"); break;
    case Error::Cancelled: message = tr("Archive extraction cancelled"); break;
    }
    if (!result.detail.isEmpty()) {
        message += QStringLiteral(": ") + result.detail;
    }
    return message;
}
