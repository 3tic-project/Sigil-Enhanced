/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef SIGIL_AGENT_BOOK_TOOLS_H
#define SIGIL_AGENT_BOOK_TOOLS_H

#include "Agent/Core/AgentSession.h"
#include "Agent/Execution/IBookWorkspace.h"
#include "Agent/Tools/ToolRegistry.h"

namespace SigilAgent
{

class AgentCancellation;

void registerBookTools(ToolRegistry *registry, IBookWorkspace *workspace, AgentSession *session = nullptr,
                       AgentCancellation *cancellation = nullptr);
QString humanReadableImpact(const QString &name, const QJsonObject &arguments);

} // namespace SigilAgent

#endif
