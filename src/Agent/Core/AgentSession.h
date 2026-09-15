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

constexpr int MAX_SESSION_MEMORY_ENTRIES = 64;
constexpr int MAX_SESSION_MEMORY_KEY_LENGTH = 64;
constexpr int MAX_SESSION_MEMORY_VALUE_LENGTH = 2048;
constexpr int MAX_SESSION_TASKS = 128;
constexpr int MAX_SESSION_TASK_TITLE_LENGTH = 256;
constexpr int MAX_SESSION_TASK_NOTE_LENGTH = 2048;

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

    bool remember(const QString &key, const QJsonValue &value);
    QJsonValue recall(const QString &key) const;
    QJsonObject memory() const;
    QStringList memoryKeys() const;
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
    QStringList m_memoryKeys;
    QList<SessionTask> m_tasks;
};

} // namespace SigilAgent

#endif
