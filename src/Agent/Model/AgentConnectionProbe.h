/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef SIGIL_AGENT_CONNECTION_PROBE_H
#define SIGIL_AGENT_CONNECTION_PROBE_H

#include <atomic>

#include <QString>

#include "Agent/Model/OpenAICompatibleProvider.h"

namespace SigilAgent
{

struct AgentConnectionProbeResult {
    bool ok = false;
    QString error;
    QString model;
    QString finishReason;
    qint64 durationMs = 0;
    int httpStatus = 0;
};

AgentConnectionProbeResult probeAgentConnection(
    const OpenAIProviderConfig &config, int timeout_ms = 15000,
    const std::atomic_bool *cancelled = nullptr);

} // namespace SigilAgent

#endif
