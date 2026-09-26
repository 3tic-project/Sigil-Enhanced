/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef SIGIL_AGENT_LOCATIONS_H
#define SIGIL_AGENT_LOCATIONS_H

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

namespace SigilAgent
{

struct AgentLocationResource {
    QString resourceId;
    QString bookPath;
};

// Read-only view of the open book used to issue and verify conversation links.
class AgentLocationSource
{
public:
    virtual ~AgentLocationSource() = default;
    virtual QString bookSessionId() const = 0;
    virtual QList<AgentLocationResource> textResources() const = 0;
    virtual bool resourceText(const QString &resource_id,
                              QString *book_path,
                              QString *text) const = 0;
};

enum class AgentLocationKind {
    File,
    SourceLine
};

struct AgentLocation {
    QString id;
    QString bookSessionId;
    QString resourceId;
    QString bookPath;
    AgentLocationKind kind = AgentLocationKind::File;
    int line = -1;
    QByteArray contentSha256;
};

enum class AgentLocationStatus {
    Exact,
    ContentChanged,
    ResourceMissing,
    OtherBook,
    Unknown,
    Unavailable
};

QString agentLocationStatusName(AgentLocationStatus status);

struct AgentLocationCheck {
    AgentLocationStatus status = AgentLocationStatus::Unknown;
    AgentLocation location;
    QString currentBookPath;
};

struct AgentLinkifyResult {
    QString markdown;
    int fileLinks = 0;
    int lineLinks = 0;
    int unboundLineRefs = 0;
    int outOfRangeLineRefs = 0;
};

class AgentLocationTable
{
public:
    // Adds links only for exact current book paths and for L<number> references
    // that bind to one unambiguous file; the input text is never altered otherwise.
    AgentLinkifyResult linkify(const QString &markdown, const AgentLocationSource &source);
    const AgentLocation *find(const QString &id) const;
    AgentLocationCheck check(const QString &id, const AgentLocationSource *source) const;
    void revokeOtherBooks(const QString &book_session_id);
    void clear();
    int size() const { return m_locations.size(); }

private:
    QString issue(const QString &book_session_id,
                  const AgentLocationResource &resource,
                  AgentLocationKind kind,
                  int line,
                  const QByteArray &content_sha256);

    QHash<QString, AgentLocation> m_locations;
    QHash<QString, QString> m_idsByKey;
    QSet<QString> m_revoked;
};

} // namespace SigilAgent

#endif
