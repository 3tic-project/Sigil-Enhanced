/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/Model/AgentModelCatalog.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace SigilAgent
{

namespace
{

bool listContains(const QStringList &values, const QString &needle)
{
    return values.contains(needle, Qt::CaseInsensitive);
}

QStringList parametersFromValue(const QJsonValue &value)
{
    QStringList parameters;
    if (value.isArray()) {
        for (const QJsonValue &item : value.toArray()) {
            const QString name = item.toString();
            if (!name.isEmpty()) parameters.append(name);
        }
    } else if (value.isString()) {
        const QStringList parts = value.toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (QString part : parts) {
            part = part.trimmed();
            if (!part.isEmpty()) parameters.append(part);
        }
    }
    return parameters;
}

qint64 contextFromObject(const QJsonObject &object)
{
    if (object.contains(QStringLiteral("context_length"))) {
        return static_cast<qint64>(object.value(QStringLiteral("context_length")).toDouble());
    }
    if (object.contains(QStringLiteral("context_window"))) {
        return static_cast<qint64>(object.value(QStringLiteral("context_window")).toDouble());
    }
    if (object.contains(QStringLiteral("max_context_length"))) {
        return static_cast<qint64>(object.value(QStringLiteral("max_context_length")).toDouble());
    }
    const QJsonObject top = object.value(QStringLiteral("top_provider")).toObject();
    if (top.contains(QStringLiteral("context_length"))) {
        return static_cast<qint64>(top.value(QStringLiteral("context_length")).toDouble());
    }
    return 0;
}

QJsonArray modelsArrayFromDocument(const QJsonDocument &document)
{
    if (document.isArray()) return document.array();
    if (!document.isObject()) return QJsonArray();
    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("data")).isArray()) {
        return object.value(QStringLiteral("data")).toArray();
    }
    if (object.value(QStringLiteral("models")).isArray()) {
        return object.value(QStringLiteral("models")).toArray();
    }
    return QJsonArray();
}

QString httpErrorMessage(int status, const QByteArray &body, const QString &networkError)
{
    QString snippet = QString::fromUtf8(body.left(800)).simplified();
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parse_error);
    if (parse_error.error == QJsonParseError::NoError) {
        const QJsonObject error = document.object().value(QStringLiteral("error")).toObject();
        const QString message = error.value(QStringLiteral("message")).toString();
        if (!message.isEmpty()) snippet = message;
    }
    if (status > 0) {
        return QStringLiteral("HTTP %1: %2").arg(status).arg(
            snippet.isEmpty() ? networkError : snippet);
    }
    if (!networkError.isEmpty() && !snippet.isEmpty()) {
        return QStringLiteral("%1 %2").arg(networkError, snippet);
    }
    return snippet.isEmpty() ? networkError : snippet;
}

} // namespace

CatalogModel AgentModelCatalog::modelFromJson(const QJsonObject &object)
{
    CatalogModel model;
    model.id = object.value(QStringLiteral("id")).toString();
    if (model.id.isEmpty()) {
        model.id = object.value(QStringLiteral("model")).toString();
    }
    if (model.id.isEmpty()) {
        model.id = object.value(QStringLiteral("name")).toString();
    }
    model.name = object.value(QStringLiteral("name")).toString();
    if (model.name.isEmpty()) model.name = model.id;
    model.contextLength = contextFromObject(object);
    if (object.contains(QStringLiteral("contextLength"))) {
        model.contextLength = static_cast<qint64>(object.value(QStringLiteral("contextLength")).toDouble());
    }
    model.supportedParameters = parametersFromValue(object.value(QStringLiteral("supported_parameters")));
    if (model.supportedParameters.isEmpty()) {
        model.supportedParameters = parametersFromValue(object.value(QStringLiteral("supportedParameters")));
    }
    if (object.contains(QStringLiteral("tools"))) {
        model.tools = object.value(QStringLiteral("tools")).toBool();
    } else {
        model.tools = listContains(model.supportedParameters, QStringLiteral("tools"));
    }
    if (object.contains(QStringLiteral("reasoning"))) {
        const QJsonValue reasoning = object.value(QStringLiteral("reasoning"));
        if (reasoning.isBool()) {
            model.reasoning = reasoning.toBool();
        } else if (reasoning.isObject()) {
            model.reasoning = true;
        }
    }
    if (!model.reasoning) {
        model.reasoning = listContains(model.supportedParameters, QStringLiteral("reasoning"))
            || listContains(model.supportedParameters, QStringLiteral("include_reasoning"))
            || listContains(model.supportedParameters, QStringLiteral("thinking"))
            || listContains(model.supportedParameters, QStringLiteral("reasoning_effort"));
    }
    return model;
}

QJsonObject AgentModelCatalog::modelToJson(const CatalogModel &model)
{
    QJsonArray parameters;
    for (const QString &parameter : model.supportedParameters) {
        parameters.append(parameter);
    }
    return QJsonObject {
        { QStringLiteral("id"), model.id },
        { QStringLiteral("name"), model.name },
        { QStringLiteral("context_length"), model.contextLength },
        { QStringLiteral("supported_parameters"), parameters },
        { QStringLiteral("tools"), model.tools },
        { QStringLiteral("reasoning"), model.reasoning }
    };
}

void AgentModelCatalog::applyProviderDefaults(CatalogResult *result, AgentProviderKind kind)
{
    if (!result) return;
    for (CatalogModel &model : result->models) {
        if (kind == AgentProviderKind::DeepSeek) {
            model.tools = true;
            model.reasoning = true;
            if (model.contextLength <= 0) model.contextLength = 128000;
        } else if (kind == AgentProviderKind::OpenCodeGo) {
            if (model.supportedParameters.isEmpty()) model.tools = true;
        }
    }
}

CatalogResult AgentModelCatalog::parseModelsJson(const QByteArray &body)
{
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parse_error);
    if (parse_error.error != QJsonParseError::NoError) {
        CatalogResult result;
        result.error = QStringLiteral("Invalid models JSON: %1").arg(parse_error.errorString());
        return result;
    }
    return parseModelsJson(document);
}

CatalogResult AgentModelCatalog::parseModelsJson(const QJsonDocument &document)
{
    CatalogResult result;
    const QJsonArray array = modelsArrayFromDocument(document);
    if (array.isEmpty() && document.isObject()
        && document.object().contains(QStringLiteral("error"))) {
        const QJsonObject error = document.object().value(QStringLiteral("error")).toObject();
        result.error = error.value(QStringLiteral("message")).toString();
        if (result.error.isEmpty()) result.error = QStringLiteral("Provider error listing models");
        return result;
    }
    for (const QJsonValue &value : array) {
        CatalogModel model;
        if (value.isString()) {
            model.id = value.toString();
            model.name = model.id;
        } else if (value.isObject()) {
            model = modelFromJson(value.toObject());
        }
        if (model.id.isEmpty()) continue;
        result.models.append(model);
    }
    if (result.models.isEmpty()) {
        result.error = QStringLiteral("Models response did not contain any model ids");
    }
    return result;
}

QJsonObject AgentModelCatalog::toCacheJson(const CatalogResult &result, AgentProviderKind kind)
{
    QJsonArray models;
    for (const CatalogModel &model : result.models) {
        models.append(modelToJson(model));
    }
    return QJsonObject {
        { QStringLiteral("provider"), providerKindName(kind) },
        { QStringLiteral("url"), result.sourceUrl },
        { QStringLiteral("models"), models }
    };
}

CatalogResult AgentModelCatalog::fromCacheJson(const QJsonObject &object)
{
    CatalogResult result;
    result.sourceUrl = object.value(QStringLiteral("url")).toString();
    if (object.value(QStringLiteral("models")).isArray()) {
        for (const QJsonValue &value : object.value(QStringLiteral("models")).toArray()) {
            if (!value.isObject()) continue;
            CatalogModel model = modelFromJson(value.toObject());
            if (!model.id.isEmpty()) result.models.append(model);
        }
        return result;
    }
    return parseModelsJson(QJsonDocument(object));
}

CatalogResult AgentModelCatalog::fetch(const QString &url,
                                       const QString &apiKey,
                                       const QString &referer,
                                       const QString &title,
                                       int timeoutMs)
{
    CatalogResult result;
    result.sourceUrl = url;
    if (url.trimmed().isEmpty()) {
        result.error = QStringLiteral("Models URL is not configured");
        return result;
    }

    QNetworkRequest http{QUrl(url)};
    http.setRawHeader("Accept", "application/json");
    if (!apiKey.isEmpty()) {
        http.setRawHeader("Authorization", QByteArray("Bearer ") + apiKey.toUtf8());
    }
    if (!referer.isEmpty()) {
        http.setRawHeader("HTTP-Referer", referer.toUtf8());
    }
    if (!title.isEmpty()) {
        http.setRawHeader("X-Title", title.toUtf8());
    }

    QNetworkAccessManager manager;
    QNetworkReply *reply = manager.get(http);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, reply, [reply]() { reply->abort(); });
    timeout.start(qMax(1000, timeoutMs));
    loop.exec();
    timeout.stop();

    const QByteArray body = reply->readAll();
    result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError
        && reply->error() != QNetworkReply::OperationCanceledError) {
        result.error = httpErrorMessage(result.httpStatus, body, reply->errorString());
        reply->deleteLater();
        return result;
    }
    if (reply->error() == QNetworkReply::OperationCanceledError) {
        result.error = QStringLiteral("Models request timed out");
        reply->deleteLater();
        return result;
    }
    reply->deleteLater();
    CatalogResult parsed = parseModelsJson(body);
    parsed.httpStatus = result.httpStatus;
    parsed.sourceUrl = url;
    return parsed;
}

} // namespace SigilAgent
