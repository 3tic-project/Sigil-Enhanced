/************************************************************************
**
**  This file is part of Sigil.
**
*************************************************************************/

#pragma once
#ifndef PLUGINPACKAGEUPDATE_H
#define PLUGINPACKAGEUPDATE_H

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace PluginApi
{

struct PackageManifestAddition {
    QString manifestId;
    QString href;
    QString mediaType;
    QString properties;
    QString fallback;
    QString overlay;
};

struct PackageManifestRelocation {
    QString originalHref;
    QString targetHref;
};

// Read-only model for the flat structured API. Never persist this projection.
bool ReadPackageModel(const QString &source, QString *model, QString *error);
// Return source-preserving plans; failures leave the output argument untouched.
bool ApplyMetadataUpdate(const QString &source, const QJsonArray &entries,
                         QString *updated, QString *error);
bool ApplyManifestAdditions(const QString &source,
                            const QList<PackageManifestAddition> &additions,
                            QString *updated,
                            QString *error);
bool ApplyManifestChanges(const QString &source,
                          const QStringList &removals,
                          const QList<PackageManifestRelocation> &relocations,
                          const QList<PackageManifestAddition> &additions,
                          QString *updated,
                          QString *error);
bool ApplySpineUpdate(const QString &source, const QJsonArray &items,
                      const QJsonObject &attributes, QString *updated, QString *error);

} // namespace PluginApi

#endif // PLUGINPACKAGEUPDATE_H
