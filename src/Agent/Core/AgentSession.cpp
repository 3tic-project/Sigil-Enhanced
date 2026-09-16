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
    m_memoryKeys.clear();
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

bool AgentSession::remember(const QString &key, const QJsonValue &value)
{
    const QString normalized_key = key.trimmed();
    const int value_length = value.isString()
        ? value.toString().size()
        : QString::fromUtf8(QJsonDocument(QJsonArray { value })
                                .toJson(QJsonDocument::Compact)).size();
    if (normalized_key.isEmpty()
        || normalized_key.size() > MAX_SESSION_MEMORY_KEY_LENGTH
        || value_length > MAX_SESSION_MEMORY_VALUE_LENGTH) {
        return false;
    }
    QMutexLocker locker(&m_mutex);
    const bool existing = m_memory.contains(normalized_key);
    if (!existing && m_memory.size() >= MAX_SESSION_MEMORY_ENTRIES) return false;
    m_memory.insert(normalized_key, value);
    if (existing) m_memoryKeys.removeAll(normalized_key);
    m_memoryKeys.append(normalized_key);
    return true;
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

QStringList AgentSession::memoryKeys() const
{
    QMutexLocker locker(&m_mutex);
    return m_memoryKeys;
}

QString AgentSession::addTask(const QString &title, const QString &note)
{
    if (title.trimmed().isEmpty()
        || title.size() > MAX_SESSION_TASK_TITLE_LENGTH
        || note.size() > MAX_SESSION_TASK_NOTE_LENGTH) {
        return QString();
    }
    SessionTask task;
    task.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    task.title = title;
    task.status = QStringLiteral("pending");
    task.note = note;
    QMutexLocker locker(&m_mutex);
    if (m_tasks.size() >= MAX_SESSION_TASKS) return QString();
    m_tasks.append(task);
    return task.id;
}

bool AgentSession::updateTask(const QString &id, const QString &status, const QString &note)
{
    if (note.size() > MAX_SESSION_TASK_NOTE_LENGTH
        || (!status.isEmpty()
            && status != QLatin1String("pending")
            && status != QLatin1String("in_progress")
            && status != QLatin1String("done")
            && status != QLatin1String("cancelled"))) {
        return false;
    }
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
