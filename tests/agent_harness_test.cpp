#include <cstdlib>
#include <iostream>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "Agent/Core/AgentCancellation.h"
#include "Agent/Core/AgentController.h"
#include "Agent/Core/AgentRunner.h"
#include "Agent/Core/AgentSession.h"
#include "Agent/Execution/MemoryBookWorkspace.h"
#include "Agent/Model/HistoryAssembler.h"
#include "Agent/Model/MockModelProvider.h"
#include "Agent/Security/PermissionPolicy.h"
#include "Agent/Tools/BookTools.h"
#include "Agent/Tools/DivParagraphTools.h"
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

    MemoryBookWorkspace selection_book;
    MemoryResource selection_resource;
    selection_resource.id = QStringLiteral("scope:chapter");
    selection_resource.bookPath = QStringLiteral("OEBPS/Text/selection.xhtml");
    selection_resource.kind = QStringLiteral("xhtml");
    selection_resource.mediaType = QStringLiteral("application/xhtml+xml");
    const QString ruby_selection =
        QStringLiteral("<p>本文<ruby>漢<rt>かん</rt></ruby></p>");
    selection_resource.text = QString(5000, QLatin1Char('x'))
        + ruby_selection + QString(1200, QLatin1Char('y'));
    selection_book.addResource(selection_resource);
    selection_book.setSpine(QStringList());
    const int selection_start = selection_resource.text.indexOf(ruby_selection);
    const int selection_end = selection_start + ruby_selection.size();
    const QString selection_context = PromptAssembler().contextBlock(
        &selection_book,
        QStringList {
            QStringLiteral("book"),
            QStringLiteral("scope:chapter:%1-%2").arg(selection_start).arg(selection_end)
        });
    Require(selection_context.contains(
                QStringLiteral("selection scope:chapter [%1,%2) UTF-16")
                    .arg(selection_start).arg(selection_end))
                && selection_context.contains(ruby_selection),
            "attached selection must include the exact UTF-16 range and Ruby source");
    Require(!selection_context.contains(QString(401, QLatin1Char('x'))),
            "selection context must not read the resource from offset zero");
    const QString bounded_selection_context = PromptAssembler().contextBlock(
        &selection_book,
        QStringList { QStringLiteral("scope:chapter:0-%1")
                          .arg(selection_resource.text.size()) });
    Require(bounded_selection_context.contains(
                QStringLiteral("selection truncated after 4096 UTF-16 code units"))
                && !bounded_selection_context.contains(QStringLiteral("Resources:\n"))
                && bounded_selection_context.size() < 12000,
            "large attached selections must be bounded and report truncation");

    MemoryBookWorkspace selected_files_book;
    QStringList selected_file_handles;
    for (int i = 0; i < 65; ++i) {
        MemoryResource resource;
        resource.id = QStringLiteral("selected-%1").arg(i);
        resource.bookPath = QStringLiteral("OEBPS/Text/selected-%1.xhtml").arg(i);
        resource.kind = QStringLiteral("xhtml");
        resource.mediaType = QStringLiteral("application/xhtml+xml");
        resource.text = QStringLiteral("<p>selected file %1</p>").arg(i);
        selected_files_book.addResource(resource);
        selected_file_handles.append(resource.id);
    }
    const QString selected_files_context = PromptAssembler().contextBlock(
        &selected_files_book, selected_file_handles);
    Require(selected_files_context.count(QStringLiteral("- resource selected-")) == 60
                && selected_files_context.contains(
                    QStringLiteral("5 additional selected resource(s) omitted"))
                && !selected_files_context.contains(QStringLiteral("Resources:\n")),
            "selected-files automatic context must be bounded, explicit, and file-scoped");

    MemoryBookWorkspace book = MemoryBookWorkspace::samplePhysicsBook();
    ToolRegistry registry;
    registerBookTools(&registry, &book);
    IAgentTool *python_tool = registry.find(QStringLiteral("python.run"));
    Require(python_tool != nullptr, "python.run must be registered");
    PermissionPolicy policy;
    Require(policy.evaluate(AgentMode::Ask, python_tool->descriptor()) == PermissionAction::Deny,
            "Ask must deny python.run");
    Require(policy.evaluate(AgentMode::Plan, python_tool->descriptor()) == PermissionAction::Deny,
            "Plan must deny python.run (applies immediately, no preview)");
    Require(policy.evaluate(AgentMode::Edit, python_tool->descriptor()) == PermissionAction::Ask,
            "Edit must ask before python.run");
    Require(policy.evaluate(AgentMode::Auto, python_tool->descriptor()) == PermissionAction::Allow,
            "Auto must allow python.run");
    AgentSession session;
    AgentCancellation cancellation;
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
    Require(session.eventsOf(AgentEventType::ModelRequestCompleted).size()
                == provider.requestCount(),
            "every successful model request must publish a completion event");
    const QList<AgentEvent> started_requests =
        session.eventsOf(AgentEventType::ModelRequestStarted);
    const QList<AgentEvent> completed_requests =
        session.eventsOf(AgentEventType::ModelRequestCompleted);
    Require(started_requests.size() == completed_requests.size(),
            "every completed model request must have a start event");
    for (int i = 0; i < started_requests.size(); ++i) {
        const QJsonObject started = started_requests.at(i).payload;
        const QJsonObject completed = completed_requests.at(i).payload;
        Require(!started.value(QStringLiteral("request_id")).toString().isEmpty()
                    && started.value(QStringLiteral("request_id")).toString()
                        == completed.value(QStringLiteral("request_id")).toString()
                    && started.value(QStringLiteral("session_id")).toString() == session.id()
                    && started.value(QStringLiteral("book_session_id")).toString()
                        == book.bookSessionId()
                    && started.value(QStringLiteral("book_revision")).toInteger() >= 1
                    && started.value(QStringLiteral("mode")).toString()
                        == QStringLiteral("ask")
                    && completed.value(QStringLiteral("duration_ms")).toInteger() >= 0,
                "request lifecycle events must retain identity, target, mode, and elapsed time");
    }
    Require(!hasEvent(session, AgentEventType::ModelRequestFailed),
            "successful model requests must not publish failure events");

    MemoryBookWorkspace failed_book = MemoryBookWorkspace::samplePhysicsBook();
    ToolRegistry failed_registry;
    registerBookTools(&failed_registry, &failed_book);
    AgentSession failed_session;
    AgentCancellation failed_cancellation;
    MockModelProvider failed_provider;
    ModelTurn failed_turn;
    failed_turn.error = QStringLiteral("HTTP 401: invalid credentials");
    failed_provider.addTurn(failed_turn);
    AgentRunner failed_runner(&failed_session, &failed_provider, &failed_registry, &failed_book,
                              &policy, &approve, &failed_cancellation);
    failed_runner.setModel(QStringLiteral("mock"));
    const AgentRunResult failed_result = failed_runner.runTurn(QStringLiteral("fail safely"));
    Require(failed_result.state == AgentRunState::Failed,
            "provider errors must fail the Agent turn");
    Require(hasEvent(failed_session, AgentEventType::ModelRequestFailed)
                && !hasEvent(failed_session, AgentEventType::ModelRequestCompleted),
            "failed model requests must publish failure without a false completion");
    const QJsonObject failed_request =
        failed_session.eventsOf(AgentEventType::ModelRequestFailed).constLast().payload;
    Require(failed_request.value(QStringLiteral("step")).toInt() == 1
                && !failed_request.value(QStringLiteral("request_id")).toString().isEmpty()
                && failed_request.value(QStringLiteral("model")).toString()
                    == QStringLiteral("mock")
                && failed_request.value(QStringLiteral("duration_ms")).toInteger() >= 0
                && failed_request.value(QStringLiteral("message")).toString()
                    .contains(QStringLiteral("401")),
            "model failure events must carry the request identity and readable error");

    MemoryBookWorkspace request_cancel_book = MemoryBookWorkspace::samplePhysicsBook();
    ToolRegistry request_cancel_registry;
    registerBookTools(&request_cancel_registry, &request_cancel_book);
    AgentSession request_cancel_session;
    AgentCancellation request_cancel_token;
    MockModelProvider request_cancel_provider;
    request_cancel_provider.setScript(
        [&request_cancel_token](const ModelRequest &) {
            request_cancel_token.request(AgentCancellationReason::UserStop);
            ModelTurn turn;
            turn.content = QStringLiteral("must be discarded");
            return turn;
        });
    AgentRunner request_cancel_runner(
        &request_cancel_session, &request_cancel_provider, &request_cancel_registry,
        &request_cancel_book, &policy, &approve, &request_cancel_token);
    request_cancel_runner.setModel(QStringLiteral("mock"));
    const AgentRunResult request_cancel_result = request_cancel_runner.runTurn(
        QStringLiteral("cancel during provider request"));
    const QList<AgentEvent> cancelled_requests =
        request_cancel_session.eventsOf(AgentEventType::ModelRequestCancelled);
    Require(request_cancel_result.state == AgentRunState::Cancelled
                && cancelled_requests.size() == 1
                && !cancelled_requests.first().payload
                        .value(QStringLiteral("request_id")).toString().isEmpty()
                && cancelled_requests.first().payload
                        .value(QStringLiteral("duration_ms")).toInteger() >= 0
                && !hasEvent(request_cancel_session, AgentEventType::ModelRequestFailed),
            "cancelled provider requests must publish a timed cancellation outcome");

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
                "{\"resource_id\":\"ch1\",\"expected_text\":\"<title>Heat</title>\",\"text\":\"<title>X</title>\",\"expected_revision\":1}");
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
                "{\"resource_id\":\"ch1\",\"expected_text\":\"<title>Heat</title>\",\"text\":\"<title>X</title>\",\"expected_revision\":1}");
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

    MemoryBookWorkspace paragraph_book;
    MemoryResource paragraph_css;
    paragraph_css.id = QStringLiteral("paragraph-css");
    paragraph_css.bookPath = QStringLiteral("OEBPS/Styles/paragraph.css");
    paragraph_css.kind = QStringLiteral("css");
    paragraph_css.mediaType = QStringLiteral("text/css");
    paragraph_css.text = QStringLiteral("div, p { margin: 0; padding: 0; }");
    paragraph_css.revision = 1;
    paragraph_book.addResource(paragraph_css);
    MemoryResource paragraph_xhtml;
    paragraph_xhtml.id = QStringLiteral("paragraph-page");
    paragraph_xhtml.bookPath = QStringLiteral("OEBPS/Text/paragraph.xhtml");
    paragraph_xhtml.kind = QStringLiteral("xhtml");
    paragraph_xhtml.mediaType = QStringLiteral("application/xhtml+xml");
    paragraph_xhtml.text = QStringLiteral(
        "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head>"
        "<link rel=\"stylesheet\" href=\"../Styles/paragraph.css\"/></head>"
        "<body><div><div>第一段。</div><div>第二段。</div>"
        "<div>第三段。</div><div>第四段。</div><div>第五段。</div>"
        "<div>第六段。</div><div>第七段。</div><div>第八段。</div>"
        "<div>第九段。</div><div>第十段。</div><div>第十一段。</div>"
        "<div>第十二段。</div>"
        "</div></body></html>");
    paragraph_xhtml.revision = 1;
    paragraph_book.addResource(paragraph_xhtml);
    const QString paragraph_original = paragraph_xhtml.text;
    ToolRegistry paragraph_registry;
    registerBookTools(&paragraph_registry, &paragraph_book);
    registerDivParagraphTools(&paragraph_registry, &paragraph_book);
    const ToolResult paragraph_analysis = paragraph_registry.find(
        QStringLiteral("paragraphs.analyze"))->execute(QJsonObject {
            { QStringLiteral("resource_ids"), QJsonArray { paragraph_xhtml.id } }
        });
    Require(paragraph_analysis.ok, "paragraph approval fixture analysis failed");
    const ToolResult paragraph_plan = paragraph_registry.find(
        QStringLiteral("paragraphs.plan"))->execute(QJsonObject {
            { QStringLiteral("analysis_id"),
              paragraph_analysis.data.value(QStringLiteral("analysis_id")) }
        });
    Require(paragraph_plan.ok, "paragraph approval fixture plan failed");
    const QJsonObject paragraph_apply_arguments {
        { QStringLiteral("plan_id"), paragraph_plan.data.value(QStringLiteral("plan_id")) },
        { QStringLiteral("plan_digest"), paragraph_plan.data.value(QStringLiteral("plan_digest")) },
        { QStringLiteral("expected_book_revision"),
          paragraph_plan.data.value(QStringLiteral("book_revision")) }
    };
    AgentSession paragraph_session;
    AutoApprovalGate paragraph_gate(false);
    MockModelProvider paragraph_provider;
    paragraph_provider.setScript([paragraph_apply_arguments](const ModelRequest &request) {
        bool already_requested = false;
        for (const ChatMessage &message : request.messages) {
            if (message.role != QLatin1String("assistant")) continue;
            for (const ToolCall &call : message.toolCalls) {
                if (call.id == QLatin1String("paragraph_apply")) {
                    already_requested = true;
                }
            }
        }
        ModelTurn turn;
        if (!already_requested) {
            ToolCall call;
            call.id = QStringLiteral("paragraph_apply");
            call.name = QStringLiteral("paragraphs_apply");
            call.argumentsJson = QString::fromUtf8(
                QJsonDocument(paragraph_apply_arguments).toJson(QJsonDocument::Compact));
            turn.toolCalls.append(call);
        } else {
            turn.content = QStringLiteral("The reviewed paragraph plan was not staged.");
        }
        return turn;
    });
    AgentCancellation paragraph_cancel;
    AgentRunner paragraph_runner(
        &paragraph_session, &paragraph_provider, &paragraph_registry, &paragraph_book,
        &ask_policy, &paragraph_gate, &paragraph_cancel);
    paragraph_runner.setMode(AgentMode::Edit);
    const AgentRunResult paragraph_result = paragraph_runner.runTurn(
        QStringLiteral("apply the reviewed paragraph plan"));
    Require(paragraph_result.state == AgentRunState::Completed,
            "denied paragraph staging turn should complete");
    Require(paragraph_gate.requestCount() == 1
                && paragraph_gate.lastName() == QStringLiteral("paragraphs.apply")
                && paragraph_gate.lastImpact().contains(
                    paragraph_plan.data.value(QStringLiteral("plan_id")).toString())
                && paragraph_gate.lastImpact().contains(
                    paragraph_plan.data.value(QStringLiteral("plan_digest")).toString()),
            "paragraph staging must reach the approval gate with the reviewed binding");
    Require(!paragraph_book.hasOpenTransaction()
                && paragraph_book.resourceText(paragraph_xhtml.id) == paragraph_original,
            "denied paragraph staging must not open a transaction or change the Book");

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
                "{\"resource_id\":\"ch1\",\"expected_text\":\"<title>Heat</title>\",\"text\":\"<title>HELLO</title>\",\"expected_revision\":1}");
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
    Require(edit_book.resourceText(QStringLiteral("ch1")).contains(QStringLiteral("<title>HELLO</title>")),
            "approved commit must change the live book");
    Require(hasEvent(edit_session, AgentEventType::TransactionCommitted),
            "commit must be labeled as applied in the session");
    const QJsonObject commit_status =
        edit_session.eventsOf(AgentEventType::TransactionCommitted).constLast().payload;
    Require(commit_status.value(QStringLiteral("applied_to_book")).toBool(),
            "commit event must distinguish applying to the current book");
    Require(commit_status.value(QStringLiteral("save_status")).toString()
                == QStringLiteral("not_saved"),
            "commit event must say the EPUB file has not been saved");
    Require(commit_status.value(QStringLiteral("full_epubcheck")).toObject()
                .value(QStringLiteral("status")).toString() == QStringLiteral("not_run"),
            "commit event must not imply full EPUBCheck was run");
    const QJsonObject commit_recovery =
        commit_status.value(QStringLiteral("recovery")).toObject();
    Require(commit_recovery.value(QStringLiteral("task_restore_point")).toString()
                == QStringLiteral("available")
                && !commit_recovery.value(QStringLiteral("checkpoint_id")).toString().isEmpty()
                && commit_recovery.value(QStringLiteral("affected_resources")).toArray().size() == 1,
            "text-only commits must publish a guarded task restore point");
    Require(commit_status.value(QStringLiteral("applied_changes")).toInt() > 0
                && commit_status.value(QStringLiteral("book_revision")).toInteger()
                    == static_cast<qint64>(edit_book.revision()),
            "commit status must preserve workspace counts and revision");
    const BookOpResult edit_restored = edit_book.restoreTaskRestorePoint(
        commit_recovery.value(QStringLiteral("checkpoint_id")).toString());
    Require(edit_restored.ok && edit_restored.applied
                && edit_book.resourceText(QStringLiteral("ch1")).contains(
                    QStringLiteral("<title>Heat</title>")),
            "the published task restore point must restore the committed text");

    MemoryBookWorkspace auto_book = MemoryBookWorkspace::samplePhysicsBook();
    ToolRegistry auto_registry;
    registerBookTools(&auto_registry, &auto_book);
    AgentSession auto_session;
    AutoApprovalGate auto_gate(false);
    MockModelProvider auto_provider;
    auto_provider.setScript([&](const ModelRequest &request) {
        bool began = false;
        bool copied = false;
        bool committed = false;
        for (const ChatMessage &message : request.messages) {
            if (message.role != QLatin1String("tool")) continue;
            if (message.content.contains(QStringLiteral("base_book_revision"))) began = true;
            if (message.content.contains(QStringLiteral("staging_id"))
                || message.content.contains(QStringLiteral("book_path"))) copied = true;
            if (message.content.contains(QStringLiteral("applied_changes"))) committed = true;
        }
        ModelTurn turn;
        if (!began) {
            ToolCall call;
            call.id = QStringLiteral("auto-begin");
            call.name = QStringLiteral("transaction.begin");
            call.argumentsJson = QStringLiteral("{}");
            turn.toolCalls.append(call);
            return turn;
        }
        if (!copied) {
            ToolCall call;
            call.id = QStringLiteral("auto-copy");
            call.name = QStringLiteral("resource.copy");
            call.argumentsJson = QStringLiteral("{\"resource_id\":\"ch1\"}");
            turn.toolCalls.append(call);
            return turn;
        }
        if (!committed) {
            ToolCall call;
            call.id = QStringLiteral("auto-commit");
            call.name = QStringLiteral("transaction.commit");
            call.argumentsJson = QStringLiteral("{\"expected_revision\":%1}")
                                     .arg(auto_book.revision());
            turn.toolCalls.append(call);
            return turn;
        }
        turn.content = QStringLiteral("Copied chapter 1.");
        return turn;
    });
    AgentCancellation auto_cancel;
    AgentRunner auto_runner(&auto_session, &auto_provider, &auto_registry, &auto_book,
                            &ask_policy, &auto_gate, &auto_cancel);
    auto_runner.setMode(AgentMode::Auto);
    const AgentRunResult auto_result = auto_runner.runTurn(QStringLiteral("copy chapter 1"));
    Require(auto_result.state == AgentRunState::Completed, "Auto copy loop failed");
    Require(auto_gate.requestCount() == 0, "Auto mode must not ask for approval");
    Require(!hasEvent(auto_session, AgentEventType::ToolApprovalRequested),
            "Auto mode must not emit approval cards");
    Require(hasEvent(auto_session, AgentEventType::TransactionCommitted),
            "Auto copy must commit");
    Require(auto_book.spine().size() == 3, "Auto copy must add a spine item");
    const QJsonObject auto_recovery = auto_session
        .eventsOf(AgentEventType::TransactionCommitted).constLast().payload
        .value(QStringLiteral("recovery")).toObject();
    Require(auto_recovery.value(QStringLiteral("task_restore_point")).toString()
                == QStringLiteral("unavailable")
                && auto_recovery.value(QStringLiteral("reason")).toString()
                    == QStringLiteral("structural_changes"),
            "structural commits must not advertise a text-only restore point");

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
                "{\"resource_id\":\"ch1\",\"expected_text\":\"<title>Heat</title>\",\"text\":\"<title>HELLO</title>\",\"expected_revision\":1}");
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
    const QJsonObject preview_status =
        plan_session.eventsOf(AgentEventType::TransactionPreviewed).constLast().payload;
    Require(!preview_status.value(QStringLiteral("applied_to_book")).toBool()
                && preview_status.value(QStringLiteral("save_status")).toString()
                    == QStringLiteral("not_applied"),
            "preview event must say the live book is unchanged");
    Require(preview_status.value(QStringLiteral("full_epubcheck")).toObject()
                .value(QStringLiteral("status")).toString() == QStringLiteral("not_run"),
            "preview event must not imply full EPUBCheck was run");
    Require(!hasEvent(plan_session, AgentEventType::TransactionCommitted),
            "Plan must not commit");
    Require(plan_book.resourceText(QStringLiteral("ch1")) == plan_original,
            "Plan staging plus denied commit must leave the live book unchanged");
    Require(plan_commit_denied,
            "Plan commit must come back as a tool-role PERMISSION_DENIED");
    Require(plan_result.finalText.contains(QStringLiteral("did not commit")),
            "final Plan text must reflect the tool-role deny, not a canned skip");

    MemoryBookWorkspace rebound_book = MemoryBookWorkspace::samplePhysicsBook();
    const QString original_book_session = rebound_book.bookSessionId();
    Require(!original_book_session.isEmpty()
                && rebound_book.summary().value(QStringLiteral("book_session_id")).toString()
                    == original_book_session,
            "workspace summaries must expose a stable, non-empty book session id");
    ToolRegistry rebound_registry;
    registerBookTools(&rebound_registry, &rebound_book);
    AgentSession rebound_session;
    MockModelProvider rebound_provider;
    rebound_provider.setScript([&rebound_book](const ModelRequest &) {
        rebound_book.resetBookSession();
        ModelTurn turn;
        ToolCall call;
        call.id = QStringLiteral("old-book-call");
        call.name = QStringLiteral("transaction.begin");
        call.argumentsJson = QStringLiteral("{}");
        turn.toolCalls.append(call);
        return turn;
    });
    AgentCancellation rebound_token;
    AutoApprovalGate rebound_gate(true);
    AgentRunner rebound_runner(&rebound_session, &rebound_provider, &rebound_registry,
                               &rebound_book, &ask_policy, &rebound_gate, &rebound_token);
    rebound_runner.setMode(AgentMode::Edit);
    const AgentRunResult rebound_result = rebound_runner.runTurn(
        QStringLiteral("edit the book that was open when this run began"));
    Require(rebound_result.state == AgentRunState::Failed
                && rebound_result.error.contains(QStringLiteral("open book changed")),
            "a model response must fail closed when the workspace is rebound");
    Require(rebound_book.bookSessionId() != original_book_session,
            "resetBookSession must issue a new opaque target identity");
    Require(hasEvent(rebound_session, AgentEventType::BookTargetChanged)
                && !hasEvent(rebound_session, AgentEventType::ToolRequested)
                && !rebound_book.hasOpenTransaction(),
            "an old response must be rejected before any tool targets the replacement book");
    const QJsonObject rebound_context =
        rebound_session.eventsOf(AgentEventType::ContextAttached).constLast().payload;
    const QJsonObject rebound_error =
        rebound_session.eventsOf(AgentEventType::BookTargetChanged).constLast().payload;
    Require(rebound_context.value(QStringLiteral("book_session_id")).toString()
                == original_book_session
                && rebound_error.value(QStringLiteral("expected_book_session_id")).toString()
                    == original_book_session
                && rebound_error.value(QStringLiteral("actual_book_session_id")).toString()
                    == rebound_book.bookSessionId(),
            "context and target-change diagnostics must identify expected and actual books");

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
            "{\"resource_id\":\"ch1\",\"expected_text\":\"<title>Heat</title>\",\"text\":\"<title>X</title>\",\"expected_revision\":1}");
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
    Require(hasEvent(cancel_session, AgentEventType::TransactionRolledBack),
            "cancelling staged work must emit a rollback event");
    const QJsonObject rollback_status =
        cancel_session.eventsOf(AgentEventType::TransactionRolledBack).constLast().payload;
    Require(rollback_status.value(QStringLiteral("live_book_unchanged")).toBool()
                && !rollback_status.value(QStringLiteral("applied_to_book")).toBool(),
            "rollback event must say the staged work never reached the live book");
    const QJsonObject cancellation_status =
        cancel_session.eventsOf(AgentEventType::SessionCancelled).constLast().payload;
    Require(cancellation_status.value(QStringLiteral("reason")).toString()
                == QStringLiteral("user_stop")
                && cancellation_status.value(QStringLiteral("book_session_id")).toString()
                    == cancel_book.bookSessionId(),
            "cancellation events must retain their explicit reason and bound book identity");
    AgentCancellation first_reason_wins;
    first_reason_wins.request(AgentCancellationReason::BookChanged);
    first_reason_wins.request(AgentCancellationReason::WindowClosing);
    Require(first_reason_wins.reason() == AgentCancellationReason::BookChanged,
            "the first cancellation cause must survive later stop requests");

    MemoryBookWorkspace controller_book = MemoryBookWorkspace::samplePhysicsBook();
    AgentController controller;
    Require(controller.setWorkspace(&controller_book),
            "an idle controller must accept its workspace");
    controller.setMode(AgentMode::Auto);
    auto in_flight_provider = std::make_unique<MockModelProvider>();
    bool provider_replacement_rejected = false;
    bool workspace_replacement_rejected = false;
    bool recursive_send_rejected = false;
    MemoryBookWorkspace replacement_workspace = MemoryBookWorkspace::samplePhysicsBook();
    in_flight_provider->setScript(
        [&controller, &provider_replacement_rejected, &workspace_replacement_rejected,
         &recursive_send_rejected, &replacement_workspace](const ModelRequest &) {
            const AgentRunResult nested = controller.send(
                QStringLiteral("this recursive turn must not start"), QStringList());
            recursive_send_rejected = nested.state == AgentRunState::Failed
                && nested.error.contains(QStringLiteral("already in progress"));
            workspace_replacement_rejected = !controller.setWorkspace(&replacement_workspace);
            controller.newSession();
            provider_replacement_rejected = !controller.setProvider(
                std::make_unique<MockModelProvider>());
            ModelTurn turn;
            turn.content = QStringLiteral("This response belongs to the old session.");
            return turn;
        });
    Require(controller.setProvider(std::move(in_flight_provider)),
            "an idle controller must accept provider configuration");
    const QString controller_session_before = controller.session()->id();
    const AgentRunResult controller_result = controller.send(
        QStringLiteral("start a run, then reset from its nested event loop"), QStringList());
    Require(controller_result.state == AgentRunState::Cancelled
                && provider_replacement_rejected
                && workspace_replacement_rejected
                && recursive_send_rejected
                && controller.workspace() == &controller_book,
            "active provider/workspace replacement and recursive send must be rejected before reset");
    Require(controller.session()->id() != controller_session_before
                && controller.session()->events().size() == 1
                && controller.session()->events().first().type == AgentEventType::SessionCreated
                && !controller.isRunning()
                && !controller.cancellation()->isCancelled(),
            "New Session requested in-flight must clear only after the Runner returns");
    Require(controller.runner() && controller.runner()->mode() == AgentMode::Auto,
            "deferred session reset must retain controller mode when rebuilding the Runner");
    Require(controller.setProvider(std::make_unique<MockModelProvider>()),
            "provider replacement must resume after the in-flight Runner has returned");
    return EXIT_SUCCESS;
}
