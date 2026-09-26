#include <cstdlib>
#include <iostream>

#include <QJsonArray>
#include <QJsonDocument>

#include "Agent/Core/AgentCancellation.h"
#include "Agent/Core/AgentRunner.h"
#include "Agent/Core/AgentSession.h"
#include "Agent/Core/PromptAssembler.h"
#include "Agent/Execution/MemoryBookWorkspace.h"
#include "Agent/Model/HistoryAssembler.h"
#include "Agent/Model/MockModelProvider.h"
#include "Agent/Security/PermissionPolicy.h"
#include "Agent/Tools/BookTools.h"
#include "Agent/Tools/ToolRegistry.h"

namespace
{

void Require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

QString wire(const QList<SigilAgent::ChatMessage> &messages)
{
    SigilAgent::HistoryAssembler assembler;
    return QString::fromUtf8(QJsonDocument(assembler.toOpenAIMessages(messages, true))
                                 .toJson(QJsonDocument::Compact));
}

QString messagePrefix(const QList<SigilAgent::ChatMessage> &messages, int count)
{
    QList<SigilAgent::ChatMessage> prefix;
    for (int i = 0; i < count && i < messages.size(); ++i) prefix.append(messages.at(i));
    return wire(prefix);
}

} // namespace

int main()
{
    using namespace SigilAgent;

    Require(!PromptAssembler().systemPrompt(AgentMode::Auto).contains(
                QStringLiteral("more tool call")),
            "the system prompt must not carry a per-step remaining tool count");
    Require(PromptAssembler().systemPrompt(AgentMode::Auto, 512).contains(
                QStringLiteral("at most 512 tool calls in total"))
                && !PromptAssembler().systemPrompt(AgentMode::Auto, 510).contains(
                    QStringLiteral("at most 512")),
            "a run-level tool ceiling is part of the stable system prompt");

    MemoryBookWorkspace book = MemoryBookWorkspace::samplePhysicsBook();
    ToolRegistry registry;
    registerBookTools(&registry, &book);
    PermissionPolicy policy;
    AgentSession session;
    session.append(AgentEventType::UserMessage, QJsonObject {
        { QStringLiteral("text"), QStringLiteral("排版这本书") }
    });
    PromptAssembler prompts;
    const ModelRequest first = prompts.build(
        session, &book, registry, AgentMode::Auto, QStringLiteral("deepseek-flash"),
        true, QStringLiteral("high"), QStringList(), 0, &policy, 512, 512);
    const ModelRequest second = prompts.build(
        session, &book, registry, AgentMode::Auto, QStringLiteral("deepseek-flash"),
        true, QStringLiteral("high"), QStringList(), 0, &policy, 510, 512);
    Require(first.messages.size() >= 3 && second.messages.size() == first.messages.size(),
            "each request ends with a run tail");
    Require(first.messages.first().content == second.messages.first().content,
            "changing the remaining tool count must not change the system prompt");
    Require(QJsonDocument(first.tools).toJson(QJsonDocument::Compact)
                == QJsonDocument(second.tools).toJson(QJsonDocument::Compact),
            "the tool schema must be identical across steps of one run");
    Require(first.messages.at(1).content == second.messages.at(1).content,
            "the book map must stay identical while the book is unchanged");
    Require(messagePrefix(first.messages, first.messages.size() - 1)
                == messagePrefix(second.messages, second.messages.size() - 1),
            "only the run tail may differ when the remaining tool count changes");
    Require(first.messages.last().content.contains(QStringLiteral("Remaining tool calls: 512"))
                && second.messages.last().content.contains(
                    QStringLiteral("Remaining tool calls: 510"))
                && !first.messages.first().content.contains(
                    QStringLiteral("Remaining tool calls")),
            "the remaining tool count lives only in the run tail");

    book.setMetadata(QJsonObject {
        { QStringLiteral("title"), QStringLiteral("Changed Title") },
        { QStringLiteral("language"), QStringLiteral("zh-CN") }
    });
    const ModelRequest retitled = prompts.build(
        session, &book, registry, AgentMode::Auto, QStringLiteral("deepseek-flash"),
        true, QStringLiteral("high"), QStringList(), 0, &policy, 510, 512);
    Require(retitled.messages.at(1).content != first.messages.at(1).content
                && retitled.messages.at(1).content.contains(QStringLiteral("Changed Title")),
            "an unpinned book map follows live metadata");
    book.setMetadata(QJsonObject {
        { QStringLiteral("title"), QStringLiteral("Junior Physics") },
        { QStringLiteral("language"), QStringLiteral("zh-CN") },
        { QStringLiteral("creator"), QStringLiteral("rescueme") }
    });

    ToolCall read_call;
    read_call.id = QStringLiteral("read-1");
    read_call.name = QStringLiteral("resource.read_fragment");
    read_call.argumentsJson = QStringLiteral("{\"resource_id\":\"ch1\"}");
    session.append(AgentEventType::AssistantMessage, QJsonObject {
        { QStringLiteral("content"), QString() },
        { QStringLiteral("reasoning_content"), QStringLiteral("Read the chapter first.") },
        { QStringLiteral("tool_calls"), QJsonArray { toolCallToJson(read_call) } }
    });
    session.append(AgentEventType::ToolCompleted, QJsonObject {
        { QStringLiteral("tool_call_id"), read_call.id },
        { QStringLiteral("name"), read_call.name },
        { QStringLiteral("result"), QString(20000, QLatin1Char('Q')) }
    });
    const ModelRequest grown = prompts.build(
        session, &book, registry, AgentMode::Auto, QStringLiteral("deepseek-flash"),
        true, QStringLiteral("high"), QStringList(), 0, &policy, 509, 512);
    Require(grown.messages.first().content == first.messages.first().content,
            "growing the tool history must not rewrite the system prompt");
    Require(messagePrefix(grown.messages, first.messages.size() - 1)
                == messagePrefix(first.messages, first.messages.size() - 1),
            "the next request must extend the previous request ahead of the run tail");
    bool replayed_reasoning = false;
    bool saw_truncation = false;
    for (const ChatMessage &message : grown.messages) {
        if (message.reasoningContent.contains(QStringLiteral("Read the chapter first."))) {
            replayed_reasoning = true;
        }
        if (message.content.contains(QStringLiteral("tool result truncated from"))) {
            saw_truncation = true;
            Require(message.content.toUtf8().size() <= MODEL_TOOL_RESULT_BYTES,
                    "a projected tool result must stay inside the byte cap");
            Require(message.content.contains(QStringLiteral("read-1"))
                        && message.content.contains(QString(200, QLatin1Char('Q'))),
                    "truncation must keep the tool identity and both ends of the payload");
        }
    }
    Require(replayed_reasoning, "tool-turn reasoning_content must still be replayed");
    Require(saw_truncation, "oversized tool results must be truncated on the way to the model");
    const QString stored = session.eventsOf(AgentEventType::ToolCompleted)
                               .first().payload.value(QStringLiteral("result")).toString();
    Require(stored.size() == 20000,
            "truncation must not rewrite the session log");
    const ModelRequest grown_again = prompts.build(
        session, &book, registry, AgentMode::Auto, QStringLiteral("deepseek-flash"),
        true, QStringLiteral("high"), QStringList(), 0, &policy, 509, 512);
    Require(wire(grown.messages) == wire(grown_again.messages),
            "projecting the same events twice must be byte-identical");

    AgentSession long_session;
    long_session.append(AgentEventType::UserMessage, QJsonObject {
        { QStringLiteral("text"), QStringLiteral("Keep going") }
    });
    for (int i = 0; i < 6; ++i) {
        ToolCall call;
        call.id = QStringLiteral("fold_%1").arg(i);
        call.name = QStringLiteral("book.summary");
        call.argumentsJson = QStringLiteral("{}");
        long_session.append(AgentEventType::AssistantMessage, QJsonObject {
            { QStringLiteral("content"), QString() },
            { QStringLiteral("tool_calls"), QJsonArray { toolCallToJson(call) } }
        });
        long_session.append(AgentEventType::ToolCompleted, QJsonObject {
            { QStringLiteral("tool_call_id"), call.id },
            { QStringLiteral("name"), call.name },
            { QStringLiteral("result"), QString(1800, QLatin1Char('a' + i)) }
        });
    }
    HistoryAssembler assembler;
    const QList<ChatMessage> unfolded = assembler.assemble(long_session.events(), true);
    bool saw_fold_0 = false;
    for (const ChatMessage &message : unfolded) {
        for (const ToolCall &call : message.toolCalls) {
            if (call.id == QLatin1String("fold_0")) saw_fold_0 = true;
        }
    }
    Require(saw_fold_0, "history below the checkpoint is append-only");
    const HistoryCheckpoint checkpoint = assembler.planCheckpoint(
        long_session.events(), true, 2500);
    Require(checkpoint.installed, "a long tool run must produce one checkpoint");
    const QList<ChatMessage> folded = assembler.assemble(
        long_session.events(), true, 0, nullptr, 0, &checkpoint);
    QString frozen_summary;
    bool saw_kept = false;
    bool saw_dropped = false;
    for (const ChatMessage &message : folded) {
        if (message.content.contains(QStringLiteral("<compaction-summary>"))) {
            frozen_summary = message.content;
        }
        for (const ToolCall &call : message.toolCalls) {
            if (call.id == QLatin1String("fold_5")) saw_kept = true;
            if (call.id == QLatin1String("fold_0")) saw_dropped = true;
        }
    }
    Require(!frozen_summary.isEmpty() && saw_kept && !saw_dropped,
            "the checkpoint keeps the newest tool round and folds the oldest");

    ToolCall extra;
    extra.id = QStringLiteral("fold_extra");
    extra.name = QStringLiteral("book.summary");
    extra.argumentsJson = QStringLiteral("{}");
    long_session.append(AgentEventType::AssistantMessage, QJsonObject {
        { QStringLiteral("content"), QString() },
        { QStringLiteral("tool_calls"), QJsonArray { toolCallToJson(extra) } }
    });
    long_session.append(AgentEventType::ToolCompleted, QJsonObject {
        { QStringLiteral("tool_call_id"), extra.id },
        { QStringLiteral("name"), extra.name },
        { QStringLiteral("result"), QString(1800, QLatin1Char('z')) }
    });
    const QList<ChatMessage> extended = assembler.assemble(
        long_session.events(), true, 0, nullptr, 0, &checkpoint);
    bool summary_unchanged = false;
    bool saw_extra = false;
    for (const ChatMessage &message : extended) {
        if (message.content == frozen_summary) summary_unchanged = true;
        for (const ToolCall &call : message.toolCalls) {
            if (call.id == QLatin1String("fold_extra")) saw_extra = true;
            if (call.id == QLatin1String("fold_0")) saw_dropped = true;
        }
    }
    Require(summary_unchanged && saw_extra && !saw_dropped,
            "new tool rounds append after a frozen checkpoint");
    const HistoryCheckpoint replanned = assembler.planCheckpoint(
        long_session.events(), true, 2500);
    Require(replanned.installed && replanned.summary != frozen_summary,
            "a later checkpoint replacement is explicit and gets a new summary");

    MemoryBookWorkspace pinned_book = MemoryBookWorkspace::samplePhysicsBook();
    ToolRegistry pinned_registry;
    registerBookTools(&pinned_registry, &pinned_book);
    AgentSession pinned_session;
    AgentCancellation cancellation;
    MockModelProvider provider;
    QList<ModelRequest> requests;
    provider.setScript([&requests](const ModelRequest &request) {
        requests.append(request);
        ModelTurn turn;
        bool patched = false;
        for (const ChatMessage &message : request.messages) {
            if (message.role == QLatin1String("tool")) patched = true;
        }
        if (!patched) {
            ToolCall call;
            call.id = QStringLiteral("patch-1");
            call.name = QStringLiteral("resource.patch_fragment");
            call.argumentsJson = QStringLiteral(
                "{\"resource_id\":\"ch1\",\"expected_text\":\"<title>Heat</title>\","
                "\"text\":\"<title>X</title>\",\"expected_revision\":1}");
            turn.toolCalls.append(call);
            return turn;
        }
        turn.content = QStringLiteral("patched");
        return turn;
    });
    AutoApprovalGate gate(true);
    AgentRunner runner(&pinned_session, &provider, &pinned_registry, &pinned_book,
                       &policy, &gate, &cancellation);
    runner.setMode(AgentMode::Auto);
    runner.setModel(QStringLiteral("mock"));
    const AgentRunResult run = runner.runTurn(QStringLiteral("改标题"));
    Require(run.state == AgentRunState::Completed, "pinned run should complete");
    Require(requests.size() == 2, "the patch run should take two model requests");
    Require(requests.at(0).messages.first().content
                == requests.at(1).messages.first().content,
            "the runner must reuse the pinned system prompt");
    Require(QJsonDocument(requests.at(0).tools).toJson(QJsonDocument::Compact)
                == QJsonDocument(requests.at(1).tools).toJson(QJsonDocument::Compact),
            "the runner must reuse the pinned tool schema");
    Require(requests.at(0).messages.at(1).content
                == requests.at(1).messages.at(1).content,
            "the runner must reuse the pinned book map");
    Require(requests.at(1).messages.at(1).content.contains(QStringLiteral("<title>Heat</title>")),
            "the pinned book map must keep the chapter sample from the start of the run");
    const ModelRequest unpinned = prompts.build(
        pinned_session, &pinned_book, pinned_registry, AgentMode::Auto,
        QStringLiteral("mock"), true, QStringLiteral("medium"), QStringList(),
        0, &policy, 0, DEFAULT_MAX_TOOL_CALLS);
    Require(unpinned.messages.at(1).content != requests.at(1).messages.at(1).content,
            "without the pin, the staged edit would change the book map");
    Require(requests.at(0).historyContext.value(QStringLiteral("prefix_reused")).toBool()
                == false
                && requests.at(1).historyContext.value(QStringLiteral("prefix_reused")).toBool()
                && requests.at(0).historyContext.value(QStringLiteral("prefix_sha256"))
                    == requests.at(1).historyContext.value(QStringLiteral("prefix_sha256")),
            "the second request must report a reused prefix fingerprint");
    Require(messagePrefix(requests.at(1).messages, requests.at(0).messages.size() - 1)
                == messagePrefix(requests.at(0).messages, requests.at(0).messages.size() - 1),
            "the second request must extend the first request ahead of the run tail");
    return EXIT_SUCCESS;
}
