/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef SIGIL_AGENT_TOOL_REGISTRY_H
#define SIGIL_AGENT_TOOL_REGISTRY_H

#include <memory>
#include <vector>

#include <QHash>
#include <QJsonArray>
#include <QList>

#include "Agent/Tools/IAgentTool.h"

namespace SigilAgent
{

class ToolRegistry
{
public:
    void add(std::unique_ptr<IAgentTool> tool);
    IAgentTool *find(const QString &name) const;
    QList<AgentToolDescriptor> descriptors() const;
    QJsonArray openaiToolSchemas() const;

private:
    std::vector<std::unique_ptr<IAgentTool>> m_tools;
    QHash<QString, IAgentTool *> m_byName;
};

} // namespace SigilAgent

#endif
