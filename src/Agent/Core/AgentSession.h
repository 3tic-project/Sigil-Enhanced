/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef SIGIL_AGENT_SESSION_H
#define SIGIL_AGENT_SESSION_H

#include <functional>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
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

    void remember(const QString &key, const QJsonValue &value);
    QJsonValue recall(const QString &key) const;
    QJsonObject memory() const;
    QString addTask(const QString &title, const QString &note = QString());
    bool updateTask(const QString &id, const QString &status, const QString &note);
    QJsonArray tasks() const;

private:
    struct SessionTask {
        QString id;
        QString title;
        QString status;
        QString note;
    };

    mutable QMutex m_mutex;
    QString m_id;
    QList<AgentEvent> m_events;
    Listener m_listener;
    QJsonObject m_memory;
    QList<SessionTask> m_tasks;
};

} // namespace SigilAgent

#endif
