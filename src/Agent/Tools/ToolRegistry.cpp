/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/Tools/ToolRegistry.h"

#include <QJsonObject>
#include <QRegularExpression>

namespace SigilAgent
{

QString ToolRegistry::toWireName(const QString &name)
{
    QString wire = name;
    wire.replace(QLatin1Char('.'), QLatin1Char('_'));
    return wire;
}

bool ToolRegistry::isValidWireName(const QString &name)
{
    static const QRegularExpression pattern(QStringLiteral("^[a-zA-Z0-9_-]+$"));
    return pattern.match(name).hasMatch();
}

void ToolRegistry::add(std::unique_ptr<IAgentTool> tool)
{
    if (!tool) return;
    IAgentTool *raw = tool.get();
    const QString name = raw->descriptor().name;
    m_byName.insert(name, raw);
    m_byWireName.insert(toWireName(name), raw);
    m_tools.push_back(std::move(tool));
}

IAgentTool *ToolRegistry::find(const QString &name) const
{
    if (IAgentTool *tool = m_byName.value(name, nullptr)) return tool;
    return m_byWireName.value(name, nullptr);
}

QList<AgentToolDescriptor> ToolRegistry::descriptors() const
{
    QList<AgentToolDescriptor> list;
    for (const auto &tool : m_tools) {
        list.append(tool->descriptor());
    }
    return list;
}

QJsonArray ToolRegistry::openaiToolSchemas(
    const std::function<bool(const AgentToolDescriptor &)> &include) const
{
    QJsonArray array;
    for (const auto &tool : m_tools) {
        const AgentToolDescriptor descriptor = tool->descriptor();
        if (include && !include(descriptor)) continue;
        array.append(QJsonObject {
            { QStringLiteral("type"), QStringLiteral("function") },
            { QStringLiteral("function"), QJsonObject {
                { QStringLiteral("name"), toWireName(descriptor.name) },
                { QStringLiteral("description"), descriptor.description },
                { QStringLiteral("parameters"), descriptor.inputSchema }
            } }
        });
    }
    return array;
}

} // namespace SigilAgent
