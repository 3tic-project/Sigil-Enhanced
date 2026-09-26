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

constexpr int DEFAULT_PREVIOUS_TURN_HISTORY_BUDGET_BYTES = 32 * 1024;
constexpr int MAX_PREVIOUS_TURN_HISTORY_BUDGET_BYTES = 512 * 1024;
constexpr int DEFAULT_CURRENT_TURN_HISTORY_BUDGET_BYTES = 128 * 1024;
constexpr int MODEL_TOOL_RESULT_BYTES = 8 * 1024;
constexpr int DEFAULT_CHECKPOINT_TAIL_BYTES = 32 * 1024;

// A checkpoint is installed once and then reused. Replacing it is an explicit
// new plan; assemble() never rewrites the summary from later events.
struct HistoryCheckpoint {
    bool installed = false;
    QString summary;
    int insertBeforeEvent = -1;
    QList<int> omittedEventIndexes;
};

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
    int currentTurnBudgetBytes = 0;
    qint64 includedCurrentTurnBytes = 0;
    int omittedCurrentTurnMessages = 0;
    bool checkpointInstalled = false;

    QJsonObject toJson() const;
};

class HistoryAssembler
{
public:
    QList<ChatMessage> assemble(
        const QList<AgentEvent> &events,
        bool includeTools,
        int maxPreviousTurnBytes = 0,
        HistoryAssemblyStats *stats = nullptr,
        int maxCurrentTurnBytes = 0,
        const HistoryCheckpoint *checkpoint = nullptr) const;
    HistoryCheckpoint planCheckpoint(
        const QList<AgentEvent> &events,
        bool includeTools,
        int keepTailBytes = DEFAULT_CHECKPOINT_TAIL_BYTES) const;
    QJsonArray toOpenAIMessages(const QList<ChatMessage> &messages, bool includeTools) const;
};

} // namespace SigilAgent

#endif
