#include <cstdlib>
#include <iostream>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "Agent/Core/AgentCancellation.h"
#include "Agent/Core/AgentRunner.h"
#include "Agent/Core/AgentSession.h"
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

bool hasEvent(const SigilAgent::AgentSession &session, SigilAgent::AgentEventType type)
{
    return !session.eventsOf(type).isEmpty();
}

} // namespace

int main()
{
    using namespace SigilAgent;

    MemoryBookWorkspace book = MemoryBookWorkspace::samplePhysicsBook();
    ToolRegistry registry;
    registerBookTools(&registry, &book);
    AgentSession session;
    AgentCancellation cancellation;
    PermissionPolicy policy;
    AutoApprovalGate approve(true);
    MockModelProvider provider;
    provider.setScript([](const ModelRequest &request) {
        bool has_summary = false;
        for (const ChatMessage &message : request.messages) {
            if (message.role == QLatin1String("tool")
                && message.content.contains(QStringLiteral("Junior Physics"))) {
                has_summary = true;
            }
        }
        ModelTurn turn;
        if (!has_summary) {
            ToolCall call;
            call.id = QStringLiteral("call_summary");
            call.name = QStringLiteral("book.summary");
            call.argumentsJson = QStringLiteral("{}");
            turn.reasoning = QStringLiteral("Need the book map.");
            turn.toolCalls.append(call);
            return turn;
        }
        QString title;
        for (const ChatMessage &message : request.messages) {
            if (message.role != QLatin1String("tool")) continue;
            QJsonParseError error;
            const QJsonDocument document = QJsonDocument::fromJson(message.content.toUtf8(), &error);
            if (error.error == QJsonParseError::NoError) {
                title = document.object().value(QStringLiteral("title")).toString();
                if (title.isEmpty()) {
                    title = document.object().value(QStringLiteral("data")).toObject()
                                .value(QStringLiteral("title")).toString();
                }
            }
        }
        turn.content = QStringLiteral("Book title is %1").arg(title);
        return turn;
    });

    AgentRunner runner(&session, &provider, &registry, &book, &policy, &approve, &cancellation);
    runner.setMode(AgentMode::Ask);
    runner.setModel(QStringLiteral("mock"));
    const AgentRunResult result = runner.runTurn(QStringLiteral("summarize this book"));
    Require(result.state == AgentRunState::Completed, "mock loop did not complete");
    Require(result.toolNames.contains(QStringLiteral("book.summary")),
            "mock loop must invoke the shipped book.summary tool");
    Require(result.finalText.contains(QStringLiteral("Junior Physics")),
            "final assistant text must be derived from the real tool result");
    Require(hasEvent(session, AgentEventType::UserMessage), "session must record the user message");
    Require(hasEvent(session, AgentEventType::AssistantMessage), "session must record assistant text");
    Require(hasEvent(session, AgentEventType::ToolCompleted), "session must record the tool result");

    HistoryAssembler assembler;
    const QJsonArray replay = assembler.toOpenAIMessages(assembler.assemble(session.events(), true), true);
    bool replayed_reasoning = false;
    for (const QJsonValue &value : replay) {
        if (value.toObject().value(QStringLiteral("reasoning_content")).toString().contains(QStringLiteral("book map"))) {
            replayed_reasoning = true;
        }
    }
    Require(replayed_reasoning, "session history with tools must replay reasoning_content");

    MemoryBookWorkspace ask_book = MemoryBookWorkspace::samplePhysicsBook();
    const QString original = ask_book.resourceText(QStringLiteral("ch1"));
    ToolRegistry ask_registry;
    registerBookTools(&ask_registry, &ask_book);
    AgentSession ask_session;
    AutoApprovalGate ask_gate(true);
    MockModelProvider ask_provider;
    bool ask_saw_tool_role = false;
    QString ask_tool_blob;
    ask_provider.setScript([&](const ModelRequest &request) {
        bool already = false;
        for (const ChatMessage &message : request.messages) {
            if (message.role == QLatin1String("tool")
                && message.toolCallId == QLatin1String("call_patch")) {
                ask_saw_tool_role = true;
                ask_tool_blob = message.content;
            }
            if (message.role == QLatin1String("assistant")) {
                for (const ToolCall &call : message.toolCalls) {
                    if (call.id == QLatin1String("call_patch")) already = true;
                }
            }
        }
        ModelTurn turn;
        if (!already) {
            ToolCall call;
            call.id = QStringLiteral("call_patch");
            call.name = QStringLiteral("resource.patch_fragment");
            call.argumentsJson = QStringLiteral(
                "{\"resource_id\":\"ch1\",\"start\":0,\"end\":1,\"text\":\"X\",\"expected_revision\":1}");
            turn.toolCalls.append(call);
            return turn;
        }
        turn.content = QStringLiteral("Ask mode cannot edit.");
        return turn;
    });
    AgentCancellation ask_cancel;
    PermissionPolicy ask_policy;
    AgentRunner ask_runner(&ask_session, &ask_provider, &ask_registry, &ask_book,
                           &ask_policy, &ask_gate, &ask_cancel);
    ask_runner.setMode(AgentMode::Ask);
    const AgentRunResult ask_result = ask_runner.runTurn(QStringLiteral("rewrite chapter 1"));
    Require(ask_result.state == AgentRunState::Completed, "ask-mode turn should still complete");
    Require(hasEvent(ask_session, AgentEventType::ToolRejected),
            "Ask mode must emit a rejection for a mutating tool");
    Require(!hasEvent(ask_session, AgentEventType::ToolStarted),
            "denied tools must never start execution");
    Require(ask_book.resourceText(QStringLiteral("ch1")) == original,
            "Ask mode must not mutate the live book");
    Require(ask_gate.requestCount() == 0, "Ask deny must not go through the approval gate");
    Require(ask_saw_tool_role,
            "next model request after Ask-deny must include a tool-role message");
    Require(ask_tool_blob.contains(QStringLiteral("PERMISSION_DENIED")),
            "tool-role deny payload must carry PERMISSION_DENIED");
    Require(ask_provider.lastRequest().messages.size() > 0,
            "Ask-deny follow-up must inspect the real next ModelRequest");

    MemoryBookWorkspace deny_book = MemoryBookWorkspace::samplePhysicsBook();
    const QString deny_original = deny_book.resourceText(QStringLiteral("ch1"));
    ToolRegistry deny_registry;
    registerBookTools(&deny_registry, &deny_book);
    AgentSession deny_session;
    AutoApprovalGate deny_gate(false);
    MockModelProvider deny_provider;
    bool deny_saw_tool_role = false;
    QString deny_tool_blob;
    deny_provider.setScript([&](const ModelRequest &request) {
        bool already = false;
        for (const ChatMessage &message : request.messages) {
            if (message.role == QLatin1String("tool")
                && message.toolCallId == QLatin1String("call_patch2")) {
                deny_saw_tool_role = true;
                deny_tool_blob = message.content;
            }
            if (message.role == QLatin1String("assistant")) {
                for (const ToolCall &call : message.toolCalls) {
                    if (call.id == QLatin1String("call_patch2")) already = true;
                }
            }
        }
        ModelTurn turn;
        if (!already) {
            ToolCall call;
            call.id = QStringLiteral("call_patch2");
            call.name = QStringLiteral("resource.patch_fragment");
            call.argumentsJson = QStringLiteral(
                "{\"resource_id\":\"ch1\",\"start\":0,\"end\":1,\"text\":\"X\",\"expected_revision\":1}");
            turn.toolCalls.append(call);
            return turn;
        }
        turn.content = QStringLiteral("User denied the edit.");
        return turn;
    });
    AgentCancellation deny_cancel;
    AgentRunner deny_runner(&deny_session, &deny_provider, &deny_registry, &deny_book,
                            &ask_policy, &deny_gate, &deny_cancel);
    deny_runner.setMode(AgentMode::Edit);
    deny_runner.runTurn(QStringLiteral("patch it"));
    Require(hasEvent(deny_session, AgentEventType::ToolApprovalRequested),
            "Edit + reversible tool must emit an approval event");
    Require(hasEvent(deny_session, AgentEventType::ToolRejected),
            "deny must be recorded");
    Require(!hasEvent(deny_session, AgentEventType::ToolStarted),
            "deny must never execute the tool");
    Require(deny_book.resourceText(QStringLiteral("ch1")) == deny_original,
            "denied edit must leave the live book unchanged");
    Require(deny_gate.requestCount() == 1 && !deny_gate.lastImpact().isEmpty(),
            "approval request must include a human-readable impact");
    Require(deny_saw_tool_role,
            "next model request after user-deny must include a tool-role message");
    Require(deny_tool_blob.contains(QStringLiteral("PERMISSION_DENIED")),
            "user-deny tool payload must carry PERMISSION_DENIED");

    MemoryBookWorkspace edit_book = MemoryBookWorkspace::samplePhysicsBook();
    ToolRegistry edit_registry;
    registerBookTools(&edit_registry, &edit_book);
    AgentSession edit_session;
    AutoApprovalGate edit_gate(true);
    MockModelProvider edit_provider;
    edit_provider.setScript([&edit_book](const ModelRequest &request) {
        bool began = false;
        bool patched = false;
        bool committed = false;
        for (const ChatMessage &message : request.messages) {
            if (message.role != QLatin1String("tool")) continue;
            if (message.content.contains(QStringLiteral("transaction_id"))
                && message.content.contains(QStringLiteral("base_book_revision"))) began = true;
            if (message.content.contains(QStringLiteral("staged"))) patched = true;
            if (message.content.contains(QStringLiteral("applied_changes"))) committed = true;
        }
        ModelTurn turn;
        if (!began) {
            ToolCall call;
            call.id = QStringLiteral("begin");
            call.name = QStringLiteral("transaction.begin");
            call.argumentsJson = QStringLiteral("{}");
            turn.toolCalls.append(call);
            return turn;
        }
        if (!patched) {
            ToolCall call;
            call.id = QStringLiteral("patch");
            call.name = QStringLiteral("resource.patch_fragment");
            call.argumentsJson = QStringLiteral(
                "{\"resource_id\":\"ch1\",\"start\":0,\"end\":5,\"text\":\"HELLO\",\"expected_revision\":1}");
            turn.toolCalls.append(call);
            return turn;
        }
        if (!committed) {
            ToolCall call;
            call.id = QStringLiteral("commit");
            call.name = QStringLiteral("transaction.commit");
            call.argumentsJson = QStringLiteral("{\"expected_revision\":%1}")
                                     .arg(edit_book.revision());
            turn.toolCalls.append(call);
            return turn;
        }
        turn.content = QStringLiteral("Patched chapter 1.");
        return turn;
    });
    AgentCancellation edit_cancel;
    AgentRunner edit_runner(&edit_session, &edit_provider, &edit_registry, &edit_book,
                            &ask_policy, &edit_gate, &edit_cancel);
    edit_runner.setMode(AgentMode::Edit);
    const AgentRunResult edit_result = edit_runner.runTurn(QStringLiteral("patch chapter 1"));
    Require(edit_result.state == AgentRunState::Completed, "approved edit loop failed");
    Require(hasEvent(edit_session, AgentEventType::ToolApprovalRequested),
            "ask-mode permission must emit approval before execute");
    Require(hasEvent(edit_session, AgentEventType::ToolApproved),
            "approve must be recorded");
    Require(hasEvent(edit_session, AgentEventType::ToolStarted),
            "approve must execute the tool");
    Require(edit_book.resourceText(QStringLiteral("ch1")).startsWith(QStringLiteral("HELLO")),
            "approved commit must change the live book");
    Require(hasEvent(edit_session, AgentEventType::TransactionCommitted),
            "commit must be labeled as applied in the session");

    MemoryBookWorkspace plan_book = MemoryBookWorkspace::samplePhysicsBook();
    const QString plan_original = plan_book.resourceText(QStringLiteral("ch1"));
    ToolRegistry plan_registry;
    registerBookTools(&plan_registry, &plan_book);
    AgentSession plan_session;
    AutoApprovalGate plan_gate(true);
    MockModelProvider plan_provider;
    bool plan_commit_denied = false;
    plan_provider.setScript([&](const ModelRequest &request) {
        bool began = false;
        bool patched = false;
        bool previewed = false;
        bool saw_commit = false;
        for (const ChatMessage &message : request.messages) {
            if (message.role != QLatin1String("tool")) continue;
            if (message.toolCallId == QLatin1String("plan-commit")) {
                saw_commit = true;
                plan_commit_denied = message.content.contains(QStringLiteral("PERMISSION_DENIED"));
            }
            if (message.content.contains(QStringLiteral("base_book_revision"))) began = true;
            if (message.content.contains(QStringLiteral("staged"))) patched = true;
            if (message.content.contains(QStringLiteral("live_book_revision"))) previewed = true;
        }
        ModelTurn turn;
        if (!began) {
            ToolCall call;
            call.id = QStringLiteral("plan-begin");
            call.name = QStringLiteral("transaction.begin");
            call.argumentsJson = QStringLiteral("{}");
            turn.toolCalls.append(call);
            return turn;
        }
        if (!patched) {
            ToolCall call;
            call.id = QStringLiteral("plan-patch");
            call.name = QStringLiteral("resource.patch_fragment");
            call.argumentsJson = QStringLiteral(
                "{\"resource_id\":\"ch1\",\"start\":0,\"end\":5,\"text\":\"HELLO\",\"expected_revision\":1}");
            turn.toolCalls.append(call);
            return turn;
        }
        if (!previewed) {
            ToolCall call;
            call.id = QStringLiteral("plan-preview");
            call.name = QStringLiteral("transaction.preview");
            call.argumentsJson = QStringLiteral("{}");
            turn.toolCalls.append(call);
            return turn;
        }
        if (!saw_commit) {
            ToolCall call;
            call.id = QStringLiteral("plan-commit");
            call.name = QStringLiteral("transaction.commit");
            call.argumentsJson = QStringLiteral("{\"expected_revision\":%1}")
                                     .arg(plan_book.revision());
            turn.toolCalls.append(call);
            return turn;
        }
        turn.content = QStringLiteral("Plan staged a preview and did not commit.");
        return turn;
    });
    AgentCancellation plan_cancel;
    AgentRunner plan_runner(&plan_session, &plan_provider, &plan_registry, &plan_book,
                            &ask_policy, &plan_gate, &plan_cancel);
    plan_runner.setMode(AgentMode::Plan);
    const AgentRunResult plan_result = plan_runner.runTurn(QStringLiteral("draft a heading patch"));
    Require(plan_result.state == AgentRunState::Completed, "Plan turn must complete");
    Require(hasEvent(plan_session, AgentEventType::TransactionPreviewed),
            "Plan must be able to preview staged work");
    Require(!hasEvent(plan_session, AgentEventType::TransactionCommitted),
            "Plan must not commit");
    Require(plan_book.resourceText(QStringLiteral("ch1")) == plan_original,
            "Plan staging plus denied commit must leave the live book unchanged");
    Require(plan_commit_denied,
            "Plan commit must come back as a tool-role PERMISSION_DENIED");
    Require(plan_result.finalText.contains(QStringLiteral("did not commit")),
            "final Plan text must reflect the tool-role deny, not a canned skip");

    MemoryBookWorkspace cancel_book = MemoryBookWorkspace::samplePhysicsBook();
    ToolRegistry cancel_registry;
    registerBookTools(&cancel_registry, &cancel_book);
    AgentSession cancel_session;
    class CancelOnApproval : public IApprovalGate
    {
    public:
        CancelOnApproval(AgentCancellation *cancellation, IBookWorkspace *workspace) :
            m_cancellation(cancellation), m_workspace(workspace) {}
        bool waitForApproval(const QString &, const QString &, const QJsonObject &, const QString &) override
        {
            Require(m_workspace->hasOpenTransaction(),
                    "transaction should still be open when approval is pending");
            m_cancellation->request();
            return false;
        }
    private:
        AgentCancellation *m_cancellation;
        IBookWorkspace *m_workspace;
    };
    AgentCancellation cancel_token;
    CancelOnApproval cancel_gate(&cancel_token, &cancel_book);
    MockModelProvider cancel_provider;
    cancel_provider.addTurn([]() {
        ModelTurn turn;
        ToolCall begin;
        begin.id = QStringLiteral("b");
        begin.name = QStringLiteral("transaction.begin");
        begin.argumentsJson = QStringLiteral("{}");
        ToolCall patch;
        patch.id = QStringLiteral("p");
        patch.name = QStringLiteral("resource.patch_fragment");
        patch.argumentsJson = QStringLiteral(
            "{\"resource_id\":\"ch1\",\"start\":0,\"end\":1,\"text\":\"X\",\"expected_revision\":1}");
        turn.toolCalls << begin << patch;
        return turn;
    }());
    AgentRunner cancel_runner(&cancel_session, &cancel_provider, &cancel_registry, &cancel_book,
                              &ask_policy, &cancel_gate, &cancel_token);
    cancel_runner.setMode(AgentMode::Edit);
    const AgentRunResult cancel_result = cancel_runner.runTurn(QStringLiteral("edit then stop"));
    Require(cancel_result.state == AgentRunState::Cancelled, "stop during tool/approval must cancel");
    Require(!cancel_book.hasOpenTransaction(),
            "cancel during tool/model step must not leave an active staged transaction");
    Require(cancel_book.resourceText(QStringLiteral("ch1")).startsWith(QStringLiteral("<?xml")),
            "cancelled uncommitted work must not change live text");
    return EXIT_SUCCESS;
}
