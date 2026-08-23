/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef SIGIL_AGENT_MODEL_CATALOG_H
#define SIGIL_AGENT_MODEL_CATALOG_H

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include "Agent/Model/AgentProviderPreset.h"

namespace SigilAgent
{

struct CatalogModel {
    QString id;
    QString name;
    qint64 contextLength = 0;
    QStringList supportedParameters;
    bool tools = false;
    bool reasoning = false;
};

struct CatalogResult {
    QList<CatalogModel> models;
    QString error;
    int httpStatus = 0;
    QString sourceUrl;
};

class AgentModelCatalog
{
public:
    static CatalogResult parseModelsJson(const QByteArray &body);
    static CatalogResult parseModelsJson(const QJsonDocument &document);
    static void applyProviderDefaults(CatalogResult *result, AgentProviderKind kind);
    static CatalogModel modelFromJson(const QJsonObject &object);
    static QJsonObject modelToJson(const CatalogModel &model);
    static QJsonObject toCacheJson(const CatalogResult &result, AgentProviderKind kind);
    static CatalogResult fromCacheJson(const QJsonObject &object);
    static CatalogResult fetch(const QString &url,
                               const QString &apiKey,
                               const QString &referer = QString(),
                               const QString &title = QString(),
                               int timeoutMs = 30000);
};

} // namespace SigilAgent

#endif
