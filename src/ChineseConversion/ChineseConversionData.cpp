/************************************************************************
**
**  Copyright (C) 2026 Sigil Enhanced contributors
**
*************************************************************************/

#include "ChineseConversion/ChineseConversionData.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace
{

bool IsOpenCCDataDirectory(const QString& path)
{
    const QDir directory(path);
    return QFileInfo(directory.filePath(QStringLiteral("s2t.json"))).isFile()
        && QFileInfo(directory.filePath(QStringLiteral("STCharacters.ocd2"))).isFile();
}

}

QStringList ChineseConversionData::CandidateDirectories()
{
    QStringList candidates;
#ifdef SIGIL_OPENCC_BUILD_DATA_DIR
    candidates << QString::fromUtf8(SIGIL_OPENCC_BUILD_DATA_DIR);
#endif

    const QDir applicationDirectory(QCoreApplication::applicationDirPath());
    candidates << applicationDirectory.filePath(QStringLiteral("opencc"));
#ifdef Q_OS_MACOS
    candidates << applicationDirectory.filePath(QStringLiteral("../opencc"));
#endif
#ifdef SIGIL_OPENCC_INSTALL_DATA_DIR
    candidates << QString::fromUtf8(SIGIL_OPENCC_INSTALL_DATA_DIR);
#endif
    candidates.removeDuplicates();
    return candidates;
}

QString ChineseConversionData::FindDataDirectory()
{
    const QStringList candidates = CandidateDirectories();
    for (const QString& candidate : candidates) {
        const QString cleanPath = QDir::cleanPath(candidate);
        if (IsOpenCCDataDirectory(cleanPath)) {
            return cleanPath;
        }
    }
    return QString();
}
