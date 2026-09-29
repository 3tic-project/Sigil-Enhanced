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

// Read-only release preflight over the current working text (live book plus staged edits).
// One run is a frozen snapshot; issue pages are served from it and refused once the book changes.
class PreflightAudit
{
public:
    static QJsonArray profiles();
    ToolResult run(IBookWorkspace *workspace, const QJsonObject &arguments);
    ToolResult issues(IBookWorkspace *workspace, const QJsonObject &arguments);

private:
    QString m_runId;
    QString m_profileId;
    QString m_sessionId;
    QString m_snapshotDigest;
    QJsonArray m_issues;
    QJsonObject m_summary;
};

QString preflightSnapshotDigest(IBookWorkspace *workspace);
QJsonArray preflightIssues(IBookWorkspace *workspace, const QString &profile_id);
// Spine-ordered role candidates with evidence, first heading, matching TOC entry, and heading/TOC differences.
QJsonObject inspectStructure(IBookWorkspace *workspace);

} // namespace SigilAgent

#endif
