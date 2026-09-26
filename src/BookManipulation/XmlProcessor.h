#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

// Native ports of python3lib/xmlprocessor.py for documents lxml parses without
// recovery. Each function returns false when the input needs the legacy
// recovering parser, leaving *out untouched.
namespace XmlProcessor {

bool RepairXML(const QString &data, const QString &mediaType, QString *out);
bool PerformOPFSourceUpdates(const QString &data, const QString &newBookPath, const QString &oldBookPath,
                             const QHash<QString, QString> &updates, QString *out);
bool PerformNCXSourceUpdates(const QString &data, const QString &newBookPath, const QString &oldBookPath,
                             const QHash<QString, QString> &updates, QString *out);
bool PerformSMILUpdates(const QString &data, const QString &newBookPath, const QString &oldBookPath,
                        const QHash<QString, QString> &updates, QString *out);
bool PerformPageMapUpdates(const QString &data, const QString &newBookPath, const QString &oldBookPath,
                           const QHash<QString, QString> &updates, QString *out);
bool AnchorNCXUpdates(const QString &data, const QString &ncxBookPath, const QString &originatingBookPath,
                      const QHash<QString, QString> &idLocations, QString *out);
bool AnchorNCXUpdatesAfterMerge(const QString &data, const QString &ncxBookPath, const QString &sinkBookPath,
                                const QStringList &mergedBookPaths, QString *out);

// hrefutils equivalents, exposed for the parity test.
QString UrlEncodePart(const QString &part);
QString UrlDecodePart(const QString &part);
QString BuildBookPath(const QString &destination, const QString &startFolder);
QString BuildRelativePath(const QString &fromBookPath, const QString &toBookPath);
QString StartingDir(const QString &filePath);

// Opf_Parser(data).rebuild_opfxml() from opf_newparser.py; false where the
// legacy parser raises (no package or metadata element).
bool RebuildOpfXml(const QString &data, QString *out);

}
