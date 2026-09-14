/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef SIGIL_AGENT_PROMPT_ASSEMBLER_H
#define SIGIL_AGENT_PROMPT_ASSEMBLER_H

#include <QStringList>

#include "Agent/AgentTypes.h"
#include "Agent/Core/AgentSession.h"
#include "Agent/Execution/IBookWorkspace.h"
#include "Agent/Model/HistoryAssembler.h"
#include "Agent/Model/IModelProvider.h"
#include "Agent/Tools/ToolRegistry.h"

namespace SigilAgent
{

class PermissionPolicy;

class PromptAssembler
{
public:
    QString systemPrompt(AgentMode mode, int remainingToolCalls = -1) const;
    QString contextBlock(IBookWorkspace *workspace, const QStringList &handles,
                         const AgentSession *session = nullptr) const;
    ModelRequest build(const AgentSession &session,
                       IBookWorkspace *workspace,
                       const ToolRegistry &tools,
                       AgentMode mode,
                       const QString &model,
                       bool thinking,
                       const QString &reasoning_effort,
                       const QStringList &handles,
                       int historyPreviousTurnBudgetBytes =
                           DEFAULT_PREVIOUS_TURN_HISTORY_BUDGET_BYTES,
                       const PermissionPolicy *permissionPolicy = nullptr,
                       int remainingToolCalls = -1) const;
};

} // namespace SigilAgent

#endif
