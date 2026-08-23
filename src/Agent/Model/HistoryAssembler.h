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

class HistoryAssembler
{
public:
    QList<ChatMessage> assemble(const QList<AgentEvent> &events, bool includeTools) const;
    QJsonArray toOpenAIMessages(const QList<ChatMessage> &messages, bool includeTools) const;
};

} // namespace SigilAgent

#endif
