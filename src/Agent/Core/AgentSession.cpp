/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/Core/AgentSession.h"

#include <QDateTime>
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

} // namespace SigilAgent
