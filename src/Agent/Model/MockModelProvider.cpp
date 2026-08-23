/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/Model/MockModelProvider.h"

namespace SigilAgent
{

void MockModelProvider::setScript(Script script)
{
    m_script = std::move(script);
}

void MockModelProvider::addTurn(const ModelTurn &turn)
{
    m_turns.append(turn);
}

int MockModelProvider::requestCount() const
{
    return m_requestCount;
}

ModelRequest MockModelProvider::lastRequest() const
{
    return m_lastRequest;
}

ModelCapabilities MockModelProvider::capabilities() const
{
    ModelCapabilities caps;
    return caps;
}

ModelTurn MockModelProvider::stream(const ModelRequest &request, ModelStreamSink &sink)
{
    m_lastRequest = request;
    ++m_requestCount;
    ModelTurn turn;
    if (m_script) {
        turn = m_script(request);
    } else if (m_index < m_turns.size()) {
        turn = m_turns.at(m_index);
        ++m_index;
    } else {
        turn.content = QStringLiteral("No scripted model turn remains.");
        turn.finishReason = QStringLiteral("stop");
    }

    if (sink.isCancelled()) {
        turn.error = QStringLiteral("cancelled");
        turn.finishReason = QStringLiteral("cancelled");
        return turn;
    }
    if (!turn.reasoning.isEmpty()) sink.onReasoningDelta(turn.reasoning);
    if (!turn.content.isEmpty()) sink.onContentDelta(turn.content);
    if (!turn.toolCalls.isEmpty()) sink.onToolCallsUpdated(turn.toolCalls);
    if (turn.finishReason.isEmpty()) {
        turn.finishReason = turn.toolCalls.isEmpty()
            ? QStringLiteral("stop") : QStringLiteral("tool_calls");
    }
    return turn;
}

} // namespace SigilAgent
