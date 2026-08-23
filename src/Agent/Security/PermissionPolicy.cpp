/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/Security/PermissionPolicy.h"

namespace SigilAgent
{

PermissionAction PermissionPolicy::evaluate(AgentMode mode, const AgentToolDescriptor &tool) const
{
    if (!tool.mutatesBook && tool.risk == ToolRisk::Read) {
        return PermissionAction::Allow;
    }

    const bool is_preview = tool.name.endsWith(QLatin1String(".preview"))
        || tool.name == QLatin1String("transaction.preview");
    const bool is_begin_or_rollback = tool.name == QLatin1String("transaction.begin")
        || tool.name == QLatin1String("transaction.rollback");
    const bool is_checkpoint_read = tool.name == QLatin1String("checkpoint.list");

    if (mode == AgentMode::Ask) {
        return tool.mutatesBook ? PermissionAction::Deny : PermissionAction::Allow;
    }
    if (mode == AgentMode::Plan) {
        const bool is_commit = tool.name == QLatin1String("transaction.commit")
            || tool.name == QLatin1String("checkpoint.restore");
        if (is_commit) return PermissionAction::Deny;
        if (is_preview || is_begin_or_rollback || is_checkpoint_read) return PermissionAction::Allow;
        if (tool.supportsPreview) return PermissionAction::Allow;
        if (tool.mutatesBook) return PermissionAction::Deny;
        return PermissionAction::Allow;
    }

    if (tool.risk == ToolRisk::Read) return PermissionAction::Allow;
    if (is_preview || is_begin_or_rollback) return PermissionAction::Allow;
    if (tool.risk == ToolRisk::ReversibleEdit) return PermissionAction::Ask;
    return PermissionAction::Ask;
}

QString PermissionPolicy::denyReason(AgentMode mode, const AgentToolDescriptor &tool) const
{
    if (mode == AgentMode::Ask && tool.mutatesBook) {
        return QStringLiteral("Ask mode is read-only and cannot run %1").arg(tool.name);
    }
    if (mode == AgentMode::Plan
        && (tool.name == QLatin1String("transaction.commit")
            || tool.name == QLatin1String("checkpoint.restore"))) {
        return QStringLiteral("Plan mode can stage and preview but cannot commit %1").arg(tool.name);
    }
    if (mode == AgentMode::Plan && tool.mutatesBook && !tool.supportsPreview
        && tool.name != QLatin1String("transaction.begin")
        && tool.name != QLatin1String("transaction.rollback")
        && tool.name != QLatin1String("transaction.preview")) {
        return QStringLiteral("Plan mode can preview but cannot apply %1").arg(tool.name);
    }
    return QStringLiteral("Tool %1 is denied by the current permission policy").arg(tool.name);
}

AutoApprovalGate::AutoApprovalGate(bool approve) :
    m_approve(approve)
{
}

bool AutoApprovalGate::waitForApproval(const QString &,
                                       const QString &name,
                                       const QJsonObject &,
                                       const QString &impact)
{
    ++m_requestCount;
    m_lastName = name;
    m_lastImpact = impact;
    return m_approve;
}

int AutoApprovalGate::requestCount() const
{
    return m_requestCount;
}

QString AutoApprovalGate::lastName() const
{
    return m_lastName;
}

QString AutoApprovalGate::lastImpact() const
{
    return m_lastImpact;
}

} // namespace SigilAgent
