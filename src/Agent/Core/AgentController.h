/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef SIGIL_AGENT_CONTROLLER_H
#define SIGIL_AGENT_CONTROLLER_H

#include <memory>

#include "Agent/Core/AgentCancellation.h"
#include "Agent/Core/AgentRunner.h"
#include "Agent/Core/AgentSession.h"
#include "Agent/Execution/IBookWorkspace.h"
#include "Agent/Model/IModelProvider.h"
#include "Agent/Security/PermissionPolicy.h"
#include "Agent/Tools/ToolRegistry.h"
#include "Agent/UI/GuiApprovalGate.h"

namespace SigilAgent
{

class AgentController
{
public:
    AgentController();

    void setWorkspace(IBookWorkspace *workspace);
    void setProvider(std::unique_ptr<IModelProvider> provider);
    void setMode(AgentMode mode);
    void setModel(const QString &model);
    void setThinking(bool enabled, const QString &effort);

    AgentSession *session();
    AgentRunner *runner();
    AgentCancellation *cancellation();
    GuiApprovalGate *approvalGate();
    IBookWorkspace *workspace();
    ToolRegistry *tools();

    AgentRunResult send(const QString &text, const QStringList &handles);
    void stop();
    void newSession();
    void resolveApproval(const QString &toolCallId, bool approved);

private:
    void rebuildTools();

    AgentSession m_session;
    AgentCancellation m_cancellation;
    PermissionPolicy m_policy;
    GuiApprovalGate m_gate;
    ToolRegistry m_tools;
    IBookWorkspace *m_workspace = nullptr;
    std::unique_ptr<IModelProvider> m_provider;
    std::unique_ptr<AgentRunner> m_runner;
};

} // namespace SigilAgent

#endif
