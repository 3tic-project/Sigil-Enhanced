/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef SIGIL_AGENT_PREFLIGHT_AUDIT_H
#define SIGIL_AGENT_PREFLIGHT_AUDIT_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "Agent/Tools/IAgentTool.h"

namespace SigilAgent
{

class IBookWorkspace;
class AgentCancellation;

// Read-only release preflight over the current working text (live book plus staged edits).
// One run is a frozen snapshot; issue pages are served from it and refused once the book changes.
class PreflightAudit
{
public:
    explicit PreflightAudit(AgentCancellation *cancellation = nullptr) : m_cancellation(cancellation) {}
    static QJsonArray profiles();
    ToolResult run(IBookWorkspace *workspace, const QJsonObject &arguments);
    ToolResult issues(IBookWorkspace *workspace, const QJsonObject &arguments);
    // Stages mechanical fixes of the current run and editor-supplied alt decisions; never commits.
    ToolResult applyFixes(IBookWorkspace *workspace, const QJsonObject &arguments);
    // Structure map reused across continuation pages while the snapshot digest is unchanged.
    ToolResult structure(IBookWorkspace *workspace, QString *snapshot_digest, QJsonObject *result);

private:
    AgentCancellation *m_cancellation = nullptr;
    QString m_runId;
    QString m_profileId;
    QString m_sessionId;
    QString m_snapshotDigest;
    QJsonArray m_issues;
    QJsonObject m_summary;
    QString m_structureDigest;
    QJsonObject m_structure;
};

QString preflightSnapshotDigest(IBookWorkspace *workspace);
QJsonArray preflightIssues(IBookWorkspace *workspace, const QString &profile_id,
                           const AgentCancellation *cancellation = nullptr, bool *cancelled = nullptr);
// Spine-ordered role candidates with evidence, first heading, matching TOC entry, and heading/TOC differences.
QJsonObject inspectStructure(IBookWorkspace *workspace, const AgentCancellation *cancellation = nullptr,
                             bool *cancelled = nullptr);

} // namespace SigilAgent

#endif
