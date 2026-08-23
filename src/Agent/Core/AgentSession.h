/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef SIGIL_AGENT_SESSION_H
#define SIGIL_AGENT_SESSION_H

#include <functional>

#include <QList>
#include <QMutex>
#include <QString>
#include <QUuid>

#include "Agent/AgentTypes.h"

namespace SigilAgent
{

class AgentSession
{
public:
    AgentSession();

    using Listener = std::function<void(const AgentEvent &)>;

    QString id() const;
    void setListener(Listener listener);
    AgentEvent append(AgentEventType type, const QJsonObject &payload = QJsonObject());
    QList<AgentEvent> events() const;
    QList<AgentEvent> eventsOf(AgentEventType type) const;
    void clear();
    bool containsSecret(const QString &secret) const;

private:
    mutable QMutex m_mutex;
    QString m_id;
    QList<AgentEvent> m_events;
    Listener m_listener;
};

} // namespace SigilAgent

#endif
