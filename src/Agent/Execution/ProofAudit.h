/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef SIGIL_AGENT_PROOF_AUDIT_H
#define SIGIL_AGENT_PROOF_AUDIT_H

#include <QJsonArray>
#include <QJsonObject>
#include <QHash>
#include <QString>

#include "Agent/Tools/IAgentTool.h"

namespace SigilAgent
{

class IBookWorkspace;

// One cache belongs to one tool registry. A continuation rechecks source hashes,
// but reuses the already scanned candidates when the snapshot is unchanged.
class ProofAudit
{
public:
    ToolResult audit(IBookWorkspace *workspace, const QJsonObject &arguments);
    ToolResult decide(IBookWorkspace *workspace, const QJsonObject &arguments);
    ToolResult plan(IBookWorkspace *workspace, const QJsonObject &arguments);
    ToolResult apply(IBookWorkspace *workspace, const QJsonObject &arguments);
    ToolResult settings(IBookWorkspace *workspace);
    ToolResult configure(IBookWorkspace *workspace, const QJsonObject &arguments);

private:
    QString m_snapshotId;
    QJsonArray m_issues;
    int m_resourceCount = 0;
    int m_visibleCharacters = 0;
    QJsonObject m_styleCounts;
    QString m_bookKey;
    QHash<QString, QJsonObject> m_decisions;
    QHash<QString, QJsonObject> m_records;
    QJsonObject m_config;
    QHash<QString, QString> m_sourceDigests;
    QString m_sessionId;
    quint64 m_auditRevision = 0;
    QString m_planId;
    QString m_planDigest;
    QString m_planSnapshot;
    quint64 m_planRevision = 0;
    QJsonArray m_planItems;
    QHash<QString, QString> m_planSourceDigests;
    int m_reviewedThrough = 0;

    bool sourceSnapshotCurrent(IBookWorkspace *workspace) const;
    void loadBookState(IBookWorkspace *workspace);
};

} // namespace SigilAgent

#endif
