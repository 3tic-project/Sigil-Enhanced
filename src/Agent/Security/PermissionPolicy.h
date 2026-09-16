/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef SIGIL_AGENT_PERMISSION_POLICY_H
#define SIGIL_AGENT_PERMISSION_POLICY_H

#include "Agent/AgentTypes.h"
#include "Agent/Tools/IAgentTool.h"

namespace SigilAgent
{

class PermissionPolicy
{
public:
    PermissionAction evaluate(AgentMode mode, const AgentToolDescriptor &tool) const;
    QString denyReason(AgentMode mode, const AgentToolDescriptor &tool) const;
};

struct ApprovalDecision {
    bool approved = false;
    QJsonObject argumentOverrides;
};

class IApprovalGate
{
public:
    virtual ~IApprovalGate() = default;
    virtual ApprovalDecision waitForApproval(const QString &toolCallId,
                                             const QString &name,
                                             const QJsonObject &arguments,
                                             const QString &impact) = 0;
};

class AutoApprovalGate : public IApprovalGate
{
public:
    explicit AutoApprovalGate(bool approve = true);
    ApprovalDecision waitForApproval(const QString &toolCallId,
                                     const QString &name,
                                     const QJsonObject &arguments,
                                     const QString &impact) override;
    int requestCount() const;
    QString lastName() const;
    QString lastImpact() const;

private:
    bool m_approve;
    int m_requestCount = 0;
    QString m_lastName;
    QString m_lastImpact;
};

} // namespace SigilAgent

#endif
