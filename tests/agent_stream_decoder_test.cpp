#include <cstdlib>
#include <iostream>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "Agent/AgentTypes.h"
#include "Agent/Core/AgentSession.h"
#include "Agent/Model/HistoryAssembler.h"
#include "Agent/Model/OpenAICompatibleProvider.h"
#include "Agent/Model/StreamingJsonDecoder.h"

namespace
{

void Require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

QByteArray sseLine(const QJsonObject &delta, const QString &finish = QString())
{
    QJsonObject choice { { QStringLiteral("index"), 0 }, { QStringLiteral("delta"), delta } };
    if (!finish.isEmpty()) choice.insert(QStringLiteral("finish_reason"), finish);
    QJsonObject payload {
        { QStringLiteral("id"), QStringLiteral("chatcmpl-test") },
        { QStringLiteral("choices"), QJsonArray { choice } }
    };
    return QByteArray("data: ") + QJsonDocument(payload).toJson(QJsonDocument::Compact) + '\n';
}

} // namespace

int main()
{
    using namespace SigilAgent;
    StreamingJsonDecoder decoder;

    QByteArray stream;
    stream += sseLine(QJsonObject { { QStringLiteral("reasoning"), QStringLiteral("Inspect the book first.") } });
    stream += sseLine(QJsonObject { { QStringLiteral("reasoning_content"), QStringLiteral(" Then list fonts.") } });
    stream += sseLine(QJsonObject { { QStringLiteral("content"), QStringLiteral("I'll inspect structure.") } });
    stream += sseLine(QJsonObject {
        { QStringLiteral("tool_calls"), QJsonArray { QJsonObject {
            { QStringLiteral("index"), 0 },
            { QStringLiteral("id"), QStringLiteral("call_summary") },
            { QStringLiteral("type"), QStringLiteral("function") },
            { QStringLiteral("function"), QJsonObject {
                { QStringLiteral("name"), QStringLiteral("book.summary") },
                { QStringLiteral("arguments"), QStringLiteral("{") }
            } }
        } } }
    });
    stream += sseLine(QJsonObject {
        { QStringLiteral("tool_calls"), QJsonArray { QJsonObject {
            { QStringLiteral("index"), 0 },
            { QStringLiteral("function"), QJsonObject {
                { QStringLiteral("arguments"), QStringLiteral("}") }
            } }
        } } }
    }, QStringLiteral("tool_calls"));
    stream += QByteArray(
        "data: {\"choices\":[],\"usage\":{\"prompt_tokens\":120,"
        "\"completion_tokens\":35,\"total_tokens\":155,"
        "\"prompt_tokens_details\":{\"cached_tokens\":80},"
        "\"completion_tokens_details\":{\"reasoning_tokens\":12}}}\n");
    stream += QByteArray("data: [DONE]\n");

    // Split mid-line to prove the decoder buffers.
    decoder.feed(stream.left(40));
    decoder.feed(stream.mid(40));
    const QList<StreamDelta> deltas = decoder.takeDeltas();
    bool saw_reasoning = false;
    bool saw_content = false;
    bool saw_tools = false;
    for (const StreamDelta &delta : deltas) {
        if (!delta.reasoning.isEmpty()) saw_reasoning = true;
        if (!delta.content.isEmpty()) saw_content = true;
        if (!delta.toolCalls.isEmpty()) saw_tools = true;
    }
    Require(saw_reasoning && saw_content && saw_tools,
            "mixed reasoning_content / content / tool_calls deltas must all be observed");

    const ModelTurn turn = decoder.finish();
    Require(turn.reasoning == QStringLiteral("Inspect the book first. Then list fonts."),
            "reasoning_content fragments must concatenate separately from content");
    Require(turn.content == QStringLiteral("I'll inspect structure."),
            "content must not include reasoning_content");
    Require(turn.toolCalls.size() == 1 && turn.toolCalls.first().name == QStringLiteral("book.summary"),
            "tool call name must be reconstructed");
    Require(turn.toolCalls.first().argumentsJson == QStringLiteral("{}"),
            "tool call argument fragments must concatenate");
    Require(turn.finishReason == QStringLiteral("tool_calls"),
            "finish_reason tool_calls must be preserved");
    Require(turn.usage.inputTokens == 120
                && turn.usage.outputTokens == 35
                && turn.usage.totalTokens == 155
                && turn.usage.cachedInputTokens == 80
                && turn.usage.reasoningTokens == 12,
            "an empty-choices final chunk must retain provider-reported token usage");
    const QJsonObject usage_json = modelUsageToJson(turn.usage);
    Require(usage_json.value(QStringLiteral("input_tokens")).toInteger() == 120
                && usage_json.value(QStringLiteral("reasoning_tokens")).toInteger() == 12,
            "reported usage must serialize to the provider-neutral event schema");

    ModelResponseTiming timing;
    timing.firstByteMs = 14;
    timing.firstEventMs = 37;
    const ModelResponseTiming timing_round_trip =
        modelResponseTimingFromJson(modelResponseTimingToJson(timing));
    Require(timing_round_trip.firstByteMs == 14
                && timing_round_trip.firstEventMs == 37,
            "response timing must round-trip through the lifecycle event schema");

    StreamingJsonDecoder alias_decoder;
    alias_decoder.feed(QByteArray(
        "data: {\"usage\":{\"input_tokens\":7,\"output_tokens\":3},"
        "\"choices\":[]}\n\ndata: [DONE]\n\n"));
    const ModelTurn alias_turn = alias_decoder.finish();
    Require(alias_turn.usage.inputTokens == 7
                && alias_turn.usage.outputTokens == 3
                && alias_turn.usage.totalTokens == 10,
            "input/output token aliases must parse and yield an exact total when omitted");

    StreamingJsonDecoder deepseek_usage_decoder;
    deepseek_usage_decoder.feed(QByteArray(
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}],"
        "\"usage\":{\"prompt_tokens\":21,\"completion_tokens\":9,"
        "\"total_tokens\":30,\"prompt_cache_hit_tokens\":13,"
        "\"completion_tokens_details\":{\"reasoning_tokens\":5}}}\n\n"
        "data: [DONE]\n\n"));
    const ModelTurn deepseek_usage_turn = deepseek_usage_decoder.finish();
    Require(deepseek_usage_turn.usage.cachedInputTokens == 13
                && deepseek_usage_turn.usage.reasoningTokens == 5,
            "DeepSeek top-level cache hits and nested reasoning usage must be retained");

    AgentSession session;
    QJsonArray calls { toolCallToJson(turn.toolCalls.first()) };
    session.append(AgentEventType::UserMessage, QJsonObject { { QStringLiteral("text"), QStringLiteral("summarize") } });
    session.append(AgentEventType::AssistantMessage, QJsonObject {
        { QStringLiteral("content"), turn.content },
        { QStringLiteral("reasoning_content"), turn.reasoning },
        { QStringLiteral("tool_calls"), calls }
    });
    session.append(AgentEventType::ToolCompleted, QJsonObject {
        { QStringLiteral("tool_call_id"), QStringLiteral("call_summary") },
        { QStringLiteral("id"), QStringLiteral("call_summary") },
        { QStringLiteral("result"), QJsonObject { { QStringLiteral("title"), QStringLiteral("Junior Physics") } } }
    });

    HistoryAssembler assembler;
    const QList<ChatMessage> with_tools_msgs = assembler.assemble(session.events(), true);
    const QJsonArray with_tools = assembler.toOpenAIMessages(with_tools_msgs, true);
    const QJsonArray without_tools = assembler.toOpenAIMessages(with_tools_msgs, false);

    bool assistant_has_reasoning = false;
    bool assistant_omits_reasoning = true;
    for (const QJsonValue &value : with_tools) {
        const QJsonObject object = value.toObject();
        if (object.value(QStringLiteral("role")).toString() != QLatin1String("assistant")) continue;
        Require(object.contains(QStringLiteral("reasoning_content")),
                "tools present → assistant reasoning_content must be replayed");
        Require(object.value(QStringLiteral("reasoning_content")).toString() == turn.reasoning,
                "replayed reasoning_content must match the original CoT");
        Require(object.value(QStringLiteral("content")).toString() == turn.content,
                "replayed content must stay the user-visible answer, not CoT");
        assistant_has_reasoning = true;
    }
    Require(assistant_has_reasoning, "history with tools must include an assistant message");

    for (const QJsonValue &value : without_tools) {
        const QJsonObject object = value.toObject();
        if (object.value(QStringLiteral("role")).toString() != QLatin1String("assistant")) continue;
        if (object.contains(QStringLiteral("reasoning_content"))) assistant_omits_reasoning = false;
    }
    Require(assistant_omits_reasoning,
            "tools absent → prior CoT may be omitted and must not be required");

    ModelRequest request;
    request.model = QStringLiteral("deepseek-v4-flash");
    request.messages = with_tools_msgs;
    request.tools = QJsonArray { QJsonObject { { QStringLiteral("type"), QStringLiteral("function") } } };
    request.thinking = true;
    request.reasoningEffort = QStringLiteral("medium");
    const QJsonObject body = OpenAICompatibleProvider::buildChatBody(request);
    Require(body.value(QStringLiteral("thinking")).toObject().value(QStringLiteral("type")).toString()
                == QStringLiteral("enabled"),
            "chat body must send thinking enabled");
    Require(body.value(QStringLiteral("reasoning_effort")).toString() == QStringLiteral("medium"),
            "chat body must send reasoning_effort");
    Require(!QJsonDocument(body).toJson().contains("sk-"),
            "request body must not contain an API key");
    const QJsonArray messages = body.value(QStringLiteral("messages")).toArray();
    bool replayed = false;
    for (const QJsonValue &value : messages) {
        const QJsonObject object = value.toObject();
        if (object.contains(QStringLiteral("reasoning_content"))) replayed = true;
    }
    Require(replayed, "when tools are present the chat body must replay reasoning_content");

    request.tools = QJsonArray();
    const QJsonObject body_no_tools = OpenAICompatibleProvider::buildChatBody(request);
    bool required = false;
    for (const QJsonValue &value : body_no_tools.value(QStringLiteral("messages")).toArray()) {
        if (value.toObject().contains(QStringLiteral("reasoning_content"))) required = true;
    }
    Require(!required, "when tools are absent CoT is omitted from the next request");
    return EXIT_SUCCESS;
}
