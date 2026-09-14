/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef SIGIL_AGENT_HISTORY_ASSEMBLER_H
#define SIGIL_AGENT_HISTORY_ASSEMBLER_H

#include <QJsonArray>
#include <QList>

#include "Agent/AgentTypes.h"

namespace SigilAgent
{

struct HistoryAssemblyStats {
    int budgetBytes = 0;
    int totalTurnCount = 0;
    int includedTurnCount = 0;
    int omittedTurnCount = 0;
    int includedMessageCount = 0;
    int omittedMessageCount = 0;
    qint64 totalPreviousTurnBytes = 0;
    qint64 includedPreviousTurnBytes = 0;
    qint64 currentTurnBytes = 0;

    QJsonObject toJson() const;
};

class HistoryAssembler
{
public:
    QList<ChatMessage> assemble(
        const QList<AgentEvent> &events,
        bool includeTools,
        int maxPreviousTurnBytes = 0,
        HistoryAssemblyStats *stats = nullptr) const;
    QJsonArray toOpenAIMessages(const QList<ChatMessage> &messages, bool includeTools) const;
};

} // namespace SigilAgent

#endif
