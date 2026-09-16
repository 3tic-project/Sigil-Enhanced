/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef SIGIL_AGENT_CANCELLATION_H
#define SIGIL_AGENT_CANCELLATION_H

#include <atomic>

#include <QString>

namespace SigilAgent
{

enum class AgentCancellationReason {
    None,
    UserStop,
    BookChanged,
    WindowClosing,
    NewSession
};

inline QString cancellationReasonName(AgentCancellationReason reason)
{
    switch (reason) {
        case AgentCancellationReason::None: return QStringLiteral("none");
        case AgentCancellationReason::UserStop: return QStringLiteral("user_stop");
        case AgentCancellationReason::BookChanged: return QStringLiteral("book_changed");
        case AgentCancellationReason::WindowClosing: return QStringLiteral("window_closing");
        case AgentCancellationReason::NewSession: return QStringLiteral("new_session");
    }
    return QStringLiteral("user_stop");
}

class AgentCancellation
{
public:
    void request(AgentCancellationReason reason = AgentCancellationReason::UserStop)
    {
        AgentCancellationReason expected = AgentCancellationReason::None;
        m_reason.compare_exchange_strong(expected, reason);
    }
    void reset() { m_reason.store(AgentCancellationReason::None); }
    bool isCancelled() const { return reason() != AgentCancellationReason::None; }
    AgentCancellationReason reason() const { return m_reason.load(); }

private:
    std::atomic<AgentCancellationReason> m_reason { AgentCancellationReason::None };
};

} // namespace SigilAgent

#endif
