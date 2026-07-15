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
#include <QString>

namespace PluginApi
{

bool ApplyMetadataUpdate(const QString &source, const QJsonArray &entries,
                         QString *updated, QString *error);
bool ApplySpineUpdate(const QString &source, const QJsonArray &items,
                      const QJsonObject &attributes, QString *updated, QString *error);

} // namespace PluginApi

#endif // PLUGINPACKAGEUPDATE_H
