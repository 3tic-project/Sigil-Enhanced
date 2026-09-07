/************************************************************************
**
**  This file is part of Sigil.
**
*************************************************************************/

#include "EmbedPython/EmbeddedPython.h" // Python before Qt's slots macro.
#include "PluginAPI/PluginPackageUpdate.h"

#include <QJsonDocument>

namespace {

bool UpdateSource(const QString &source, const QString &operation,
                  const QJsonObject &payload, QString *updated, QString *error)
{
    int result_code = 0;
    QString message;
    const QVariant result = EmbeddedPython::instance().runInPython(
        "opf_package_update", "apply_update",
        { source, operation, QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)) },
        &result_code, message, false, false);
    if (error) *error = message;
    if (result_code != 0) return false;
    // Do not touch the caller's output on failure (it may alias source).
    *updated = result.toString();
    return true;
}

}

namespace PluginApi {

bool ReadPackageModel(const QString &source, QString *model, QString *error)
{
    int result_code = 0;
    QString message;
    const QVariant result = EmbeddedPython::instance().runInPython(
        "opf_source", "model_xml", { source }, &result_code, message, false, false);
    if (error) *error = message;
    if (result_code != 0) return false;
    *model = result.toString();
    return true;
}

bool ApplyMetadataUpdate(const QString &source, const QJsonArray &entries,
                         QString *updated, QString *error)
{
    return UpdateSource(source, "metadata", {{ "items", entries }}, updated, error);
}

bool ApplyManifestChanges(const QString &source, const QStringList &removals,
                          const QList<PackageManifestRelocation> &relocations,
                          const QList<PackageManifestAddition> &additions,
                          QString *updated, QString *error)
{
    QJsonArray moves;
    for (const auto &item : relocations) {
        moves.append(QJsonObject {{ "original_href", item.originalHref }, { "target_href", item.targetHref }});
    }
    QJsonArray added;
    for (const auto &item : additions) {
        added.append(QJsonObject {
            { "id", item.manifestId }, { "href", item.href }, { "media-type", item.mediaType },
            { "properties", item.properties }, { "fallback", item.fallback }, { "media-overlay", item.overlay }
        });
    }
    return UpdateSource(source, "manifest", {
        { "removals", QJsonArray::fromStringList(removals) },
        { "relocations", moves }, { "additions", added }
    }, updated, error);
}

bool ApplyManifestAdditions(const QString &source,
                            const QList<PackageManifestAddition> &additions,
                            QString *updated, QString *error)
{
    return ApplyManifestChanges(source, {}, {}, additions, updated, error);
}

bool ApplySpineUpdate(const QString &source, const QJsonArray &items,
                      const QJsonObject &attributes, QString *updated, QString *error)
{
    return UpdateSource(source, "spine", {{ "items", items }, { "attributes", attributes }}, updated, error);
}

} // namespace PluginApi
