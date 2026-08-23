/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef SIGIL_AGENT_CANCELLATION_H
#define SIGIL_AGENT_CANCELLATION_H

#include <atomic>

namespace SigilAgent
{

class AgentCancellation
{
public:
    void request() { m_cancelled.store(true); }
    void reset() { m_cancelled.store(false); }
    bool isCancelled() const { return m_cancelled.load(); }

private:
    std::atomic<bool> m_cancelled { false };
};

} // namespace SigilAgent

#endif
