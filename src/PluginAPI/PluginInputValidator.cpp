/************************************************************************
**
**  This file is part of Sigil.
**
*************************************************************************/

#include "PluginAPI/PluginInputValidator.h"

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QXmlStreamReader>

#include "Misc/SafeArchiveExtractor.h"

namespace PluginApi
{

bool ValidateInputEpub(const QString &path, QString *error)
{
    QTemporaryDir extracted;
    if (!extracted.isValid()) {
        if (error) *error = QStringLiteral("Could not create a validation directory");
        return false;
    }

    const SafeArchiveExtractor::Result result = SafeArchiveExtractor::extract(path, extracted.path());
    if (!result.ok) {
        if (error) *error = SafeArchiveExtractor::errorMessage(result);
        return false;
    }

    QFile mimetype(extracted.filePath(QStringLiteral("mimetype")));
    if (!mimetype.open(QIODevice::ReadOnly)
        || mimetype.readAll() != QByteArrayLiteral("application/epub+zip")) {
        if (error) *error = QStringLiteral("EPUB mimetype is missing or invalid");
        return false;
    }

    QFile container(extracted.filePath(QStringLiteral("META-INF/container.xml")));
    if (!container.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("EPUB container.xml is missing");
        return false;
    }
    QXmlStreamReader container_xml(&container);
    QString package_path;
    while (!container_xml.atEnd()) {
        container_xml.readNext();
        if (container_xml.isStartElement()
            && container_xml.name() == QStringLiteral("rootfile")) {
            package_path = container_xml.attributes().value(QStringLiteral("full-path")).toString();
            if (!package_path.isEmpty()) break;
        }
    }
    QString package_file;
    if (container_xml.hasError() || package_path.isEmpty()
        || !SafeArchiveExtractor::safeArchivePath(extracted.path(), package_path, &package_file)
        || !QFileInfo(package_file).isFile()) {
        if (error) *error = QStringLiteral("EPUB container.xml does not reference a safe package document");
        return false;
    }

    QFile package(package_file);
    if (!package.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("EPUB package document cannot be read");
        return false;
    }
    QXmlStreamReader package_xml(&package);
    bool package_seen = false;
    bool metadata_seen = false;
    bool manifest_seen = false;
    bool spine_seen = false;
    while (!package_xml.atEnd()) {
        package_xml.readNext();
        if (!package_xml.isStartElement()) continue;
        const QString name = package_xml.name().toString();
        if (!package_seen) {
            if (name != QStringLiteral("package")) break;
            package_seen = true;
        } else if (name == QStringLiteral("metadata")) {
            metadata_seen = true;
        } else if (name == QStringLiteral("manifest")) {
            manifest_seen = true;
        } else if (name == QStringLiteral("spine")) {
            spine_seen = true;
        }
    }
    if (package_xml.hasError() || !package_seen || !metadata_seen || !manifest_seen || !spine_seen) {
        if (error) *error = QStringLiteral("EPUB package document is invalid or incomplete");
        return false;
    }
    return true;
}

}
