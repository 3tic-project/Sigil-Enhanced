/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef SIGIL_AGENT_SESSION_EXPORT_H
#define SIGIL_AGENT_SESSION_EXPORT_H

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include "Agent/Core/AgentSession.h"

namespace SigilAgent
{

struct SessionExportContext {
    QString sessionId;
    QString provider;
    QString chatUrl;
    QString modelsUrl;
    QString model;
    bool thinking = false;
    QString reasoningEffort;
    bool apiKeyPresent = false;
    QString mode;
    QString runState;
    QStringList secrets;
    QJsonArray httpTraces;
};

QString redactSecrets(QString text, const QStringList &secrets);
QJsonValue redactJsonValue(const QJsonValue &value, const QStringList &secrets);

QString exportConversationMarkdown(const AgentSession &session, const SessionExportContext &context);
QByteArray exportDebugJson(const AgentSession &session, const SessionExportContext &context);

} // namespace SigilAgent

#endif
