/************************************************************************
**
**  This file is part of Sigil.
**
*************************************************************************/

#include "PluginAPI/PluginPackageUpdate.h"
#include "ResourceObjects/OPFSourcePatch.h"

#include <exception>

namespace {

bool UpdateSource(const QString &source, const QString &operation,
                  const QJsonObject &payload, QString *updated, QString *error)
{
    try {
        // Do not touch the caller's output on failure (it may alias source).
        *updated = OPFSourcePatch::ApplyPackageUpdate(source, operation, payload);
        return true;
    } catch (const std::exception &failure) {
        if (error) *error = QString::fromUtf8(failure.what());
        return false;
    }
}

}

namespace PluginApi {

bool ReadPackageModel(const QString &source, QString *model, QString *error)
{
    try {
        *model = OPFSourcePatch::ModelXml(source);
        return true;
    } catch (const std::exception &failure) {
        if (error) *error = QString::fromUtf8(failure.what());
        return false;
    }
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
