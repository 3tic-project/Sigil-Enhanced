/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef SIGIL_AGENT_IAGENT_TOOL_H
#define SIGIL_AGENT_IAGENT_TOOL_H

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "Agent/AgentTypes.h"

namespace SigilAgent
{

struct AgentToolDescriptor {
    QString name;
    QString description;
    QJsonObject inputSchema;
    ToolRisk risk = ToolRisk::Read;
    bool mutatesBook = false;
    bool supportsPreview = false;
};

struct ToolResult {
    bool ok = true;
    QString code;
    QString message;
    QJsonObject data;
    bool previewOnly = false;
    bool applied = false;
    bool executed = false;

    static ToolResult success(const QJsonObject &data, bool applied = false, bool preview_only = false)
    {
        ToolResult result;
        result.ok = true;
        result.data = data;
        result.applied = applied;
        result.previewOnly = preview_only;
        result.executed = true;
        return result;
    }

    static ToolResult failure(const QString &code, const QString &message,
                              const QJsonObject &data = QJsonObject())
    {
        ToolResult result;
        result.ok = false;
        result.code = code;
        result.message = message;
        result.data = data;
        result.executed = true;
        return result;
    }

    static ToolResult denied(const QString &message)
    {
        ToolResult result;
        result.ok = false;
        result.code = QStringLiteral("PERMISSION_DENIED");
        result.message = message;
        result.executed = false;
        return result;
    }

    static ToolResult cancelled()
    {
        ToolResult result;
        result.ok = false;
        result.code = QStringLiteral("CANCELLED");
        result.message = QStringLiteral("cancelled");
        result.executed = false;
        return result;
    }

    QJsonObject toJson() const
    {
        QJsonObject object;
        object.insert(QStringLiteral("ok"), ok);
        if (!code.isEmpty()) object.insert(QStringLiteral("code"), code);
        if (!message.isEmpty()) object.insert(QStringLiteral("message"), message);
        object.insert(QStringLiteral("data"), data);
        object.insert(QStringLiteral("preview_only"), previewOnly);
        object.insert(QStringLiteral("applied"), applied);
        return object;
    }
};

class IAgentTool
{
public:
    virtual ~IAgentTool() = default;
    virtual AgentToolDescriptor descriptor() const = 0;
    virtual ToolResult execute(const QJsonObject &arguments) = 0;
};

} // namespace SigilAgent

#endif
