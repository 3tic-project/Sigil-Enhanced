/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/Tools/ToolRegistry.h"

#include <QJsonObject>

namespace SigilAgent
{

void ToolRegistry::add(std::unique_ptr<IAgentTool> tool)
{
    if (!tool) return;
    IAgentTool *raw = tool.get();
    const QString name = raw->descriptor().name;
    m_byName.insert(name, raw);
    m_tools.push_back(std::move(tool));
}

IAgentTool *ToolRegistry::find(const QString &name) const
{
    return m_byName.value(name, nullptr);
}

QList<AgentToolDescriptor> ToolRegistry::descriptors() const
{
    QList<AgentToolDescriptor> list;
    for (const auto &tool : m_tools) {
        list.append(tool->descriptor());
    }
    return list;
}

QJsonArray ToolRegistry::openaiToolSchemas() const
{
    QJsonArray array;
    for (const auto &tool : m_tools) {
        const AgentToolDescriptor descriptor = tool->descriptor();
        array.append(QJsonObject {
            { QStringLiteral("type"), QStringLiteral("function") },
            { QStringLiteral("function"), QJsonObject {
                { QStringLiteral("name"), descriptor.name },
                { QStringLiteral("description"), descriptor.description },
                { QStringLiteral("parameters"), descriptor.inputSchema }
            } }
        });
    }
    return array;
}

} // namespace SigilAgent
