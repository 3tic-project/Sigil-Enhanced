/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/Core/AgentSession.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>

namespace SigilAgent
{

AgentSession::AgentSession() :
    m_id(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    append(AgentEventType::SessionCreated, QJsonObject {
        { QStringLiteral("session_id"), m_id }
    });
}

QString AgentSession::id() const
{
    return m_id;
}

void AgentSession::setListener(Listener listener)
{
    m_listener = std::move(listener);
}

AgentEvent AgentSession::append(AgentEventType type, const QJsonObject &payload)
{
    AgentEvent event;
    event.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    event.type = type;
    event.timestampMs = QDateTime::currentMSecsSinceEpoch();
    event.payload = payload;
    Listener listener;
    {
        QMutexLocker locker(&m_mutex);
        m_events.append(event);
        listener = m_listener;
    }
    if (listener) listener(event);
    return event;
}

QList<AgentEvent> AgentSession::events() const
{
    QMutexLocker locker(&m_mutex);
    return m_events;
}

QList<AgentEvent> AgentSession::eventsOf(AgentEventType type) const
{
    QMutexLocker locker(&m_mutex);
    QList<AgentEvent> matched;
    for (const AgentEvent &event : m_events) {
        if (event.type == type) matched.append(event);
    }
    return matched;
}

void AgentSession::clear()
{
    QMutexLocker locker(&m_mutex);
    m_events.clear();
    m_memory = QJsonObject();
    m_tasks.clear();
    m_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    locker.unlock();
    append(AgentEventType::SessionCreated, QJsonObject {
        { QStringLiteral("session_id"), m_id }
    });
}

bool AgentSession::containsSecret(const QString &secret) const
{
    if (secret.isEmpty()) return false;
    QMutexLocker locker(&m_mutex);
    for (const AgentEvent &event : m_events) {
        const QByteArray json = QJsonDocument(event.payload).toJson(QJsonDocument::Compact);
        if (QString::fromUtf8(json).contains(secret) || event.id.contains(secret)) {
            return true;
        }
    }
    return false;
}

void AgentSession::remember(const QString &key, const QJsonValue &value)
{
    if (key.trimmed().isEmpty()) return;
    QMutexLocker locker(&m_mutex);
    m_memory.insert(key.trimmed(), value);
}

QJsonValue AgentSession::recall(const QString &key) const
{
    QMutexLocker locker(&m_mutex);
    return m_memory.value(key);
}

QJsonObject AgentSession::memory() const
{
    QMutexLocker locker(&m_mutex);
    return m_memory;
}

QString AgentSession::addTask(const QString &title, const QString &note)
{
    SessionTask task;
    task.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    task.title = title;
    task.status = QStringLiteral("pending");
    task.note = note;
    QMutexLocker locker(&m_mutex);
    m_tasks.append(task);
    return task.id;
}

bool AgentSession::updateTask(const QString &id, const QString &status, const QString &note)
{
    QMutexLocker locker(&m_mutex);
    for (SessionTask &task : m_tasks) {
        if (task.id != id) continue;
        if (!status.isEmpty()) task.status = status;
        if (!note.isEmpty()) task.note = note;
        return true;
    }
    return false;
}

QJsonArray AgentSession::tasks() const
{
    QMutexLocker locker(&m_mutex);
    QJsonArray array;
    for (const SessionTask &task : m_tasks) {
        array.append(QJsonObject {
            { QStringLiteral("id"), task.id },
            { QStringLiteral("title"), task.title },
            { QStringLiteral("status"), task.status },
            { QStringLiteral("note"), task.note }
        });
    }
    return array;
}

} // namespace SigilAgent
