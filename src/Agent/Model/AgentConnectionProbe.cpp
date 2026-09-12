/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/Model/AgentConnectionProbe.h"

#include <QElapsedTimer>

namespace SigilAgent
{

namespace
{

class ProbeSink : public ModelStreamSink
{
public:
    explicit ProbeSink(const std::atomic_bool *cancelled) : m_cancelled(cancelled) {}

    void onReasoningDelta(const QString &) override {}
    void onContentDelta(const QString &) override {}
    void onToolCallsUpdated(const QList<ToolCall> &) override {}
    bool isCancelled() const override
    {
        return m_cancelled && m_cancelled->load(std::memory_order_relaxed);
    }

private:
    const std::atomic_bool *m_cancelled = nullptr;
};

} // namespace

AgentConnectionProbeResult probeAgentConnection(
    const OpenAIProviderConfig &config, int timeout_ms,
    const std::atomic_bool *cancelled)
{
    AgentConnectionProbeResult result;
    result.model = config.model.trimmed();

    OpenAIProviderConfig probe_config = config;
    probe_config.thinking = false;
    OpenAICompatibleProvider provider(probe_config);
    ModelRequest request;
    request.model = result.model;
    request.thinking = false;
    request.reasoningEffort.clear();
    request.tools = QJsonArray();
    request.maxOutputTokens = 8;
    request.timeoutMs = qBound(100, timeout_ms, 30000);
    ChatMessage message;
    message.role = QStringLiteral("user");
    message.content = QStringLiteral("Reply with exactly OK.");
    request.messages.append(message);

    ProbeSink sink(cancelled);
    QElapsedTimer timer;
    timer.start();
    const ModelTurn turn = provider.stream(request, sink);
    result.durationMs = timer.elapsed();
    result.error = turn.error;
    result.finishReason = turn.finishReason;
    const QJsonArray traces = provider.debugTraces();
    if (!traces.isEmpty()) {
        result.httpStatus = traces.at(traces.size() - 1).toObject()
            .value(QStringLiteral("status")).toInt();
    }
    result.ok = result.error.isEmpty()
        && result.httpStatus >= 200 && result.httpStatus < 300;
    if (!result.ok && result.error.isEmpty()) {
        result.error = result.httpStatus > 0
            ? QStringLiteral("HTTP %1").arg(result.httpStatus)
            : QStringLiteral("The provider returned no successful HTTP response");
    }
    return result;
}

} // namespace SigilAgent
