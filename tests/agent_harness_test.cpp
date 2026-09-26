#include <cstdlib>
#include <iostream>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QThread>

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

QStringList toolSchemaNames(const QJsonArray &schemas)
{
    QStringList names;
    for (const QJsonValue &value : schemas) {
        names.append(value.toObject()
                         .value(QStringLiteral("function")).toObject()
                         .value(QStringLiteral("name")).toString());
    }
    return names;
}

} // namespace

int main()
{
    using namespace SigilAgent;

    const QString edit_system_prompt = PromptAssembler().systemPrompt(AgentMode::Edit);
    Require(edit_system_prompt.contains(QStringLiteral("resource_outcomes"))
                && edit_system_prompt.contains(QStringLiteral("transaction_state"))
                && edit_system_prompt.contains(
                    QStringLiteral("never infer resource success from applied_changes"))
                && edit_system_prompt.contains(
                    QStringLiteral("When has_more=true, use next_offset"))
                && edit_system_prompt.contains(QStringLiteral("font.inventory"))
                && edit_system_prompt.contains(QStringLiteral("book.validate"))
                && edit_system_prompt.contains(QStringLiteral("book.check"))
                && edit_system_prompt.contains(QStringLiteral("manuscript.parse"))
                && edit_system_prompt.contains(QStringLiteral("checkpoint.list"))
                && edit_system_prompt.contains(QStringLiteral("session.tasks"))
                && edit_system_prompt.contains(QStringLiteral("keyless session.recall"))
                && edit_system_prompt.contains(QStringLiteral("toc.inspect_hierarchy"))
                && edit_system_prompt.contains(
                    QStringLiteral("same analysis_id"))
                && edit_system_prompt.contains(
                    QStringLiteral("review_next_offset"))
                && edit_system_prompt.contains(
                    QStringLiteral("review_complete=true"))
                && edit_system_prompt.contains(QStringLiteral("preview_digest")),
            "system prompt must ground commit summaries and paginated inventory traversal");
    Require(edit_system_prompt.contains(QStringLiteral("eligible source paragraphs"))
                && edit_system_prompt.contains(QStringLiteral("report partial work"))
                && edit_system_prompt.contains(QStringLiteral("run book.check")),
            "bilingual tasks must require coverage and validation before a completion claim");
    Require(edit_system_prompt.contains(QStringLiteral("1-based source line written as L<number>"))
                && edit_system_prompt.contains(QStringLiteral("a table spanning files must name the path in every row"))
                && edit_system_prompt.contains(QStringLiteral("Never invent paths, lines, or offsets"))
                && edit_system_prompt.contains(QStringLiteral("Do not write sigil-agent:// links")),
            "system prompt must require verified path and source-line citations the dock can link");

    MemoryBookWorkspace bounded_summary_book;
    bounded_summary_book.setMetadata(QJsonObject {
        { QStringLiteral("title"),
          QString(700, QLatin1Char('t')) + QStringLiteral("TITLE-CONTEXT-TAIL") },
        { QStringLiteral("language"),
          QString(200, QLatin1Char('l')) + QStringLiteral("LANGUAGE-CONTEXT-TAIL") }
    });
    bounded_summary_book.setEpubVersion(
        QString(100, QLatin1Char('v')) + QStringLiteral("VERSION-CONTEXT-TAIL"));
    const QString bounded_summary_context = PromptAssembler().contextBlock(
        &bounded_summary_book, QStringList());
    Require(bounded_summary_context.contains(
                QStringLiteral("\"title_length\":718"))
                && bounded_summary_context.contains(
                    QStringLiteral("\"title_truncated\":true"))
                && bounded_summary_context.contains(
                    QStringLiteral("\"language_length\":221"))
                && bounded_summary_context.contains(
                    QStringLiteral("\"language_truncated\":true"))
                && bounded_summary_context.contains(
                    QStringLiteral("\"epub_version_length\":120"))
                && bounded_summary_context.contains(
                    QStringLiteral("\"epub_version_truncated\":true"))
                && !bounded_summary_context.contains(
                    QStringLiteral("TITLE-CONTEXT-TAIL"))
                && !bounded_summary_context.contains(
                    QStringLiteral("LANGUAGE-CONTEXT-TAIL"))
                && !bounded_summary_context.contains(
                    QStringLiteral("VERSION-CONTEXT-TAIL"))
                && bounded_summary_context.size() < 1500,
            "automatic book identity context must bound long summary strings");

    MemoryBookWorkspace long_label_book;
    MemoryResource long_label_resource;
    long_label_resource.id = QString(700, QLatin1Char('i'))
        + QStringLiteral("RESOURCE-ID-TAIL");
    long_label_resource.bookPath = QStringLiteral("OEBPS/Text/")
        + QString(700, QLatin1Char('p')) + QStringLiteral("PATH-TAIL.xhtml");
    long_label_resource.kind = QString(100, QLatin1Char('k'))
        + QStringLiteral("KIND-TAIL");
    long_label_resource.mediaType = QStringLiteral("application/xhtml+xml");
    long_label_resource.text = QStringLiteral("<p>bounded label body</p>");
    long_label_book.addResource(long_label_resource);
    long_label_book.setSpine(QStringList { long_label_resource.id });
    const QString bounded_label_context = PromptAssembler().contextBlock(
        &long_label_book, QStringList());
    Require(bounded_label_context.contains(QStringLiteral("truncated from"))
                && bounded_label_context.contains(
                    QStringLiteral("<p>bounded label body</p>"))
                && !bounded_label_context.contains(QStringLiteral("RESOURCE-ID-TAIL"))
                && !bounded_label_context.contains(QStringLiteral("PATH-TAIL.xhtml"))
                && !bounded_label_context.contains(QStringLiteral("KIND-TAIL"))
                && bounded_label_context.size() < 1800,
            "automatic book map must bound resource path, kind, and sample-id labels");
    const QString bounded_label_selection = PromptAssembler().contextBlock(
        &long_label_book,
        QStringList { QStringLiteral("%1:0-%2")
                          .arg(long_label_resource.id)
                          .arg(long_label_resource.text.size()) });
    Require(bounded_label_selection.contains(QStringLiteral("truncated from"))
                && bounded_label_selection.contains(long_label_resource.text)
                && !bounded_label_selection.contains(
                    QStringLiteral("RESOURCE-ID-TAIL"))
                && bounded_label_selection.size() < 1800,
            "attached selection headers must bound IDs without changing exact reads");

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
    QStringList many_selection_handles {
        QStringLiteral("book"), QStringLiteral("book")
    };
    for (int index = 0; index < 12; ++index) {
        many_selection_handles.append(QStringLiteral("scope:chapter:0-5000"));
    }
    const QString bounded_many_selections_context = PromptAssembler().contextBlock(
        &selection_book, many_selection_handles);
    Require(bounded_many_selections_context.count(
                QStringLiteral("- selection scope:chapter")) == 8
                && bounded_many_selections_context.contains(
                    QStringLiteral("4 additional selection(s) omitted"))
                && bounded_many_selections_context.count(
                    QStringLiteral("- book (structure shown above)")) == 1
                && bounded_many_selections_context.size() < 50000,
            "automatic context must cap selection excerpts and deduplicate book handles");

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

    AgentSession bounded_context_session;
    for (int index = 0; index < 25; ++index) {
        const QString note = index == 24
            ? QString(700, QLatin1Char('n')) + QStringLiteral("TASK-NOTE-TAIL")
            : QStringLiteral("task-note-[%1]").arg(index);
        Require(!bounded_context_session.addTask(
                    QStringLiteral("session-task-[%1]").arg(index),
                    note).isEmpty(),
                "session context task fixture must fit storage bounds");
    }
    for (int index = 0; index < 20; ++index) {
        const QString value = index == 19
            ? QString(700, QLatin1Char('m')) + QStringLiteral("MEMORY-VALUE-TAIL")
            : QStringLiteral("memory-value-[%1]").arg(index);
        Require(bounded_context_session.remember(
                    QStringLiteral("memory-key-[%1]").arg(index),
                    value),
                "session context memory fixture must fit storage bounds");
    }
    const QString bounded_session_context = PromptAssembler().contextBlock(
        &selected_files_book, QStringList(), &bounded_context_session);
    Require(bounded_session_context.count(QStringLiteral("session-task-[")) == 8
                && !bounded_session_context.contains(QStringLiteral("session-task-[0]"))
                && bounded_session_context.contains(QStringLiteral("session-task-[24]"))
                && bounded_session_context.contains(
                    QStringLiteral("17 earlier task(s) omitted"))
                && bounded_session_context.contains(QStringLiteral("\"note_truncated\":true"))
                && !bounded_session_context.contains(QStringLiteral("TASK-NOTE-TAIL"))
                && bounded_session_context.count(QStringLiteral("memory-value-[")) == 7
                && !bounded_session_context.contains(QStringLiteral("memory-value-[0]"))
                && bounded_session_context.contains(
                    QStringLiteral("12 earlier memory note(s) omitted"))
                && bounded_session_context.contains(
                    QStringLiteral("Truncated memory value sizes"))
                && bounded_session_context.contains(QStringLiteral("memory-key-[19]"))
                && !bounded_session_context.contains(QStringLiteral("MEMORY-VALUE-TAIL")),
            "automatic session context must retain only recent bounded task and memory windows");

    MemoryBookWorkspace book = MemoryBookWorkspace::samplePhysicsBook();
    ToolRegistry registry;
    registerBookTools(&registry, &book);
    IAgentTool *python_tool = registry.find(QStringLiteral("python.run"));
    Require(python_tool != nullptr, "python.run must be registered");
    IAgentTool *python_inspect = registry.find(QStringLiteral("python.inspect"));
    Require(python_inspect != nullptr, "python.inspect must be registered");
    IAgentTool *proof_audit = registry.find(QStringLiteral("proof.audit"));
    IAgentTool *proof_configure = registry.find(QStringLiteral("proof.configure"));
    IAgentTool *proof_decide = registry.find(QStringLiteral("proof.decide"));
    IAgentTool *proof_apply = registry.find(QStringLiteral("proof.apply"));
    Require(proof_audit && proof_configure && proof_decide && proof_apply,
            "proofreading tools must be registered");
    PermissionPolicy policy;
    Require(policy.evaluate(AgentMode::Ask, proof_audit->descriptor()) == PermissionAction::Allow
                && policy.evaluate(AgentMode::Ask, proof_configure->descriptor()) == PermissionAction::Deny
                && policy.evaluate(AgentMode::Ask, proof_decide->descriptor()) == PermissionAction::Deny
                && policy.evaluate(AgentMode::Plan, proof_configure->descriptor()) == PermissionAction::Deny
                && policy.evaluate(AgentMode::Plan, proof_decide->descriptor()) == PermissionAction::Deny
                && policy.evaluate(AgentMode::Auto, proof_apply->descriptor()) == PermissionAction::Deny
                && policy.evaluate(AgentMode::Auto, proof_configure->descriptor()) == PermissionAction::Deny
                && policy.evaluate(AgentMode::Auto, proof_decide->descriptor()) == PermissionAction::Deny
                && policy.evaluate(AgentMode::Edit, proof_configure->descriptor()) == PermissionAction::Ask
                && policy.evaluate(AgentMode::Edit, proof_decide->descriptor()) == PermissionAction::Ask
                && policy.evaluate(AgentMode::Edit, proof_apply->descriptor()) == PermissionAction::Ask,
            "proof scans are read-only and proof application requires reviewed Edit approval");
    Require(policy.evaluate(AgentMode::Ask, python_inspect->descriptor()) == PermissionAction::Allow
                && policy.evaluate(AgentMode::Plan, python_inspect->descriptor())
                    == PermissionAction::Allow,
            "Read-only Python inspection must be available in Ask and Plan");
    Require(policy.evaluate(AgentMode::Ask, python_tool->descriptor()) == PermissionAction::Deny,
            "Ask must deny python.run");
    Require(policy.evaluate(AgentMode::Plan, python_tool->descriptor()) == PermissionAction::Deny,
            "Plan must deny python.run (applies immediately, no preview)");
    Require(policy.evaluate(AgentMode::Edit, python_tool->descriptor()) == PermissionAction::Ask,
            "Edit must ask before python.run");
    Require(policy.evaluate(AgentMode::Auto, python_tool->descriptor()) == PermissionAction::Allow,
            "Auto must allow python.run");
    AgentSession catalog_session;
    catalog_session.append(AgentEventType::UserMessage, QJsonObject {
        { QStringLiteral("text"), QStringLiteral("inspect or edit this book") }
    });
    PromptAssembler prompt_assembler;
    const ModelRequest ask_catalog = prompt_assembler.build(
        catalog_session, &book, registry, AgentMode::Ask,
        QStringLiteral("mock"), false, QString(), QStringList(),
        DEFAULT_PREVIOUS_TURN_HISTORY_BUDGET_BYTES, &policy);
    const ModelRequest plan_catalog = prompt_assembler.build(
        catalog_session, &book, registry, AgentMode::Plan,
        QStringLiteral("mock"), false, QString(), QStringList(),
        DEFAULT_PREVIOUS_TURN_HISTORY_BUDGET_BYTES, &policy);
    const ModelRequest edit_catalog = prompt_assembler.build(
        catalog_session, &book, registry, AgentMode::Edit,
        QStringLiteral("mock"), false, QString(), QStringList(),
        DEFAULT_PREVIOUS_TURN_HISTORY_BUDGET_BYTES, &policy);
    const ModelRequest auto_catalog = prompt_assembler.build(
        catalog_session, &book, registry, AgentMode::Auto,
        QStringLiteral("mock"), false, QString(), QStringList(),
        DEFAULT_PREVIOUS_TURN_HISTORY_BUDGET_BYTES, &policy);
    const QStringList ask_tools = toolSchemaNames(ask_catalog.tools);
    const QStringList plan_tools = toolSchemaNames(plan_catalog.tools);
    const QStringList edit_tools = toolSchemaNames(edit_catalog.tools);
    Require(ask_tools.contains(QStringLiteral("book_summary"))
                && ask_tools.contains(QStringLiteral("transaction_preview"))
                && !ask_tools.contains(QStringLiteral("resource_patch_fragment"))
                && !ask_tools.contains(QStringLiteral("transaction_commit"))
                && !ask_tools.contains(QStringLiteral("python_run"))
                && !ask_tools.contains(QStringLiteral("proof_configure"))
                && !ask_tools.contains(QStringLiteral("proof_decide")),
            "Ask schema catalog must hide every book-mutating tool");
    Require(plan_tools.contains(QStringLiteral("resource_patch_fragment"))
                && plan_tools.contains(QStringLiteral("transaction_preview"))
                && !plan_tools.contains(QStringLiteral("transaction_commit"))
                && !plan_tools.contains(QStringLiteral("checkpoint_create"))
                && !plan_tools.contains(QStringLiteral("python_run"))
                && !plan_tools.contains(QStringLiteral("proof_configure"))
                && !plan_tools.contains(QStringLiteral("proof_decide")),
            "Plan schema catalog must retain staging but hide apply-only tools");
    Require(edit_tools.contains(QStringLiteral("transaction_commit"))
                && edit_tools.contains(QStringLiteral("checkpoint_create"))
                && edit_tools.contains(QStringLiteral("python_run"))
                && edit_tools.size() == registry.descriptors().size(),
            "Edit schema catalog must retain the full compatible tool surface");
    const QJsonObject ask_tool_context = ask_catalog.toolContext;
    Require(ask_tool_context.value(QStringLiteral("policy_applied")).toBool()
                && ask_tool_context.value(QStringLiteral("mode")).toString()
                    == QStringLiteral("ask")
                && ask_tool_context.value(QStringLiteral("total_tool_count")).toInt()
                    == registry.descriptors().size()
                && ask_tool_context.value(QStringLiteral("exposed_tool_count")).toInt()
                    == ask_catalog.tools.size()
                && ask_tool_context.value(QStringLiteral("hidden_tool_count")).toInt()
                    == registry.descriptors().size() - ask_catalog.tools.size()
                && ask_tool_context.value(QStringLiteral("saved_schema_bytes")).toInt() > 0,
            "mode-filtered requests must publish exact tool catalog savings");
    Require(edit_catalog.toolContext.value(
                QStringLiteral("hidden_tool_count")).toInt() == 0
                && edit_catalog.toolContext.value(
                    QStringLiteral("saved_schema_bytes")).toInt() == 0
                && auto_catalog.tools.size() + 3 == edit_catalog.tools.size()
                && auto_catalog.toolContext.value(
                    QStringLiteral("hidden_tool_count")).toInt() == 3
                && auto_catalog.toolContext.value(
                    QStringLiteral("saved_schema_bytes")).toInt() > 0,
            "Auto catalog must hide proofreading configuration, decisions and application");
    AgentSession session;
    AgentCancellation cancellation;
    AutoApprovalGate approve(true);
    MockModelProvider provider;
    provider.setScript([](const ModelRequest &request) {
        QThread::msleep(8);
        bool has_summary = false;
        for (const ChatMessage &message : request.messages) {
            if (message.role == QLatin1String("tool")
                && message.content.contains(QStringLiteral("Junior Physics"))) {
                has_summary = true;
            }
        }
        ModelTurn turn;
        turn.usage.inputTokens = has_summary ? 150 : 100;
        turn.usage.outputTokens = has_summary ? 20 : 10;
        turn.usage.totalTokens = turn.usage.inputTokens + turn.usage.outputTokens;
        turn.usage.cachedInputTokens = has_summary ? 100 : 60;
        turn.usage.reasoningTokens = has_summary ? 4 : 6;
        turn.timing.firstByteMs = 2;
        turn.timing.firstEventMs = 5;
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
    Require(!hasEvent(session, AgentEventType::PlanCreated),
            "ordinary read tools must not be presented as reviewable plans");
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
        const QJsonObject history = started.value(
            QStringLiteral("history_context")).toObject();
        const QJsonObject tool_context = started.value(
            QStringLiteral("tool_context")).toObject();
        Require(!started.value(QStringLiteral("request_id")).toString().isEmpty()
                    && started.value(QStringLiteral("request_id")).toString()
                        == completed.value(QStringLiteral("request_id")).toString()
                    && started.value(QStringLiteral("session_id")).toString() == session.id()
                    && started.value(QStringLiteral("book_session_id")).toString()
                        == book.bookSessionId()
                    && started.value(QStringLiteral("book_revision")).toInteger() >= 1
                    && started.value(QStringLiteral("mode")).toString()
                        == QStringLiteral("ask")
                    && started.value(QStringLiteral("usage_requested")).toBool()
                    && !history.value(QStringLiteral("limit_enabled")).toBool()
                    && history.value(QStringLiteral("budget_bytes")).toInt() == 0
                    && history.value(QStringLiteral("prefix_reused")).toBool() == (i > 0)
                    && history.value(QStringLiteral("prefix_sha256")).toString().size() == 64
                    && history.value(
                        QStringLiteral("included_turn_count")).toInt() == 1
                    && history.value(
                        QStringLiteral("omitted_turn_count")).toInt() == 0
                    && history.value(
                        QStringLiteral("current_turn_bytes")).toInteger() > 0
                    && tool_context.value(QStringLiteral("mode")).toString()
                        == QStringLiteral("ask")
                    && tool_context.value(
                        QStringLiteral("exposed_tool_count")).toInt()
                        < tool_context.value(
                            QStringLiteral("total_tool_count")).toInt()
                    && tool_context.value(
                        QStringLiteral("hidden_tool_count")).toInt() > 0
                    && started.value(QStringLiteral("max_tool_calls")).toInt()
                        == DEFAULT_MAX_TOOL_CALLS
                    && started.value(QStringLiteral("used_tool_calls")).toInt() == i
                    && started.value(QStringLiteral("remaining_tool_calls")).toInt()
                        == DEFAULT_MAX_TOOL_CALLS - i
                    && completed.value(QStringLiteral("duration_ms")).toInteger() >= 0,
                "request lifecycle events must retain identity, target, history, mode tools, tool-call budget, and elapsed time");
        const QJsonObject usage = completed.value(QStringLiteral("usage")).toObject();
        Require(usage.value(QStringLiteral("input_tokens")).toInteger() > 0
                    && usage.value(QStringLiteral("output_tokens")).toInteger() > 0
                    && usage.value(QStringLiteral("total_tokens")).toInteger()
                        == usage.value(QStringLiteral("input_tokens")).toInteger()
                            + usage.value(QStringLiteral("output_tokens")).toInteger(),
                "completed requests must retain exact provider-reported token usage");
        const QJsonObject timing =
            completed.value(QStringLiteral("response_timing")).toObject();
        Require(timing.value(QStringLiteral("first_byte_ms")).toInteger() == 2
                    && timing.value(QStringLiteral("first_model_event_ms")).toInteger() == 5
                    && completed.value(QStringLiteral("duration_ms")).toInteger()
                        >= timing.value(QStringLiteral("first_model_event_ms")).toInteger(),
                "completed requests must retain measured first-byte and first-event latency");
    }
    const QList<AgentEvent> successful_run_states =
        session.eventsOf(AgentEventType::RunStateChanged);
    const QJsonObject run_started = successful_run_states.constFirst().payload;
    const QJsonObject run_completed = successful_run_states.constLast().payload;
    Require(run_started.value(QStringLiteral("state")).toString()
                    == QStringLiteral("preparing_context")
                && !run_started.value(QStringLiteral("run_id")).toString().isEmpty()
                && run_started.value(QStringLiteral("run_id")).toString()
                    == run_completed.value(QStringLiteral("run_id")).toString()
                && run_completed.value(QStringLiteral("state")).toString()
                    == QStringLiteral("completed")
                && run_started.value(QStringLiteral("max_model_steps")).toInt()
                    == DEFAULT_MAX_MODEL_STEPS
                && run_completed.value(QStringLiteral("max_model_steps")).toInt()
                    == DEFAULT_MAX_MODEL_STEPS
                && run_started.value(QStringLiteral("max_tool_calls")).toInt()
                    == DEFAULT_MAX_TOOL_CALLS
                && run_completed.value(QStringLiteral("max_tool_calls")).toInt()
                    == DEFAULT_MAX_TOOL_CALLS
                && run_completed.value(QStringLiteral("duration_ms")).toInteger() >= 0
                && run_completed.value(QStringLiteral("model_steps")).toInt()
                    == provider.requestCount()
                && run_completed.value(QStringLiteral("tool_calls")).toInt() == 1,
            "one run id must bind preparation through a timed multi-step terminal event");
    const QJsonObject successful_usage =
        run_completed.value(QStringLiteral("usage_summary")).toObject();
    Require(run_completed.value(QStringLiteral("usage_requested")).toBool()
                && successful_usage.value(QStringLiteral("request_count")).toInt() == 2
                && successful_usage.value(QStringLiteral("reported_request_count")).toInt() == 2
                && successful_usage.value(QStringLiteral("missing_request_count")).toInt() == 0
                && successful_usage.value(QStringLiteral("all_requests_reported")).toBool()
                && successful_usage.value(QStringLiteral("input_tokens")).toInteger() == 250
                && successful_usage.value(QStringLiteral("output_tokens")).toInteger() == 30
                && successful_usage.value(QStringLiteral("total_tokens")).toInteger() == 280
                && successful_usage.value(QStringLiteral("cached_input_tokens")).toInteger() == 160
                && successful_usage.value(QStringLiteral("reasoning_tokens")).toInteger() == 10
                && successful_usage.value(QStringLiteral("total_request_count")).toInt() == 2,
            "a completed multi-step run must sum exact usage with per-field coverage");
    Require(!hasEvent(session, AgentEventType::ModelRequestFailed),
            "successful model requests must not publish failure events");

    MemoryBookWorkspace step_limit_book = MemoryBookWorkspace::samplePhysicsBook();
    ToolRegistry step_limit_registry;
    registerBookTools(&step_limit_registry, &step_limit_book);
    AgentSession step_limit_session;
    AgentCancellation step_limit_cancel;
    PermissionPolicy step_limit_policy;
    AutoApprovalGate step_limit_gate(true);
    MockModelProvider step_limit_provider;
    int step_limit_calls = 0;
    step_limit_provider.setScript([&step_limit_calls](const ModelRequest &) {
        ++step_limit_calls;
        ToolCall call;
        call.id = QStringLiteral("step-limit-%1").arg(step_limit_calls);
        call.name = step_limit_calls == 1
            ? QStringLiteral("transaction.begin")
            : QStringLiteral("book.summary");
        call.argumentsJson = step_limit_calls == 1
            ? QStringLiteral("{\"label\":\"step limit rollback\"}")
            : QStringLiteral("{}");
        ModelTurn turn;
        turn.toolCalls.append(call);
        return turn;
    });
    AgentRunner step_limit_runner(
        &step_limit_session, &step_limit_provider, &step_limit_registry,
        &step_limit_book, &step_limit_policy, &step_limit_gate,
        &step_limit_cancel);
    step_limit_runner.setMode(AgentMode::Plan);
    step_limit_runner.setMaxSteps(2);
    const AgentRunResult step_limit_result = step_limit_runner.runTurn(
        QStringLiteral("keep calling tools forever"));
    const QJsonObject step_limit_error = step_limit_session
        .eventsOf(AgentEventType::Error).constLast().payload;
    const QJsonObject step_limit_terminal = step_limit_session
        .eventsOf(AgentEventType::RunStateChanged).constLast().payload;
    Require(step_limit_result.state == AgentRunState::Failed
                && step_limit_calls == 2
                && step_limit_result.error.contains(QStringLiteral("2 model steps"))
                && !step_limit_book.hasOpenTransaction()
                && hasEvent(step_limit_session,
                            AgentEventType::TransactionRolledBack)
                && step_limit_error.value(QStringLiteral("code")).toString()
                    == QStringLiteral("MAX_MODEL_STEPS_EXCEEDED")
                && step_limit_error.value(QStringLiteral("model_steps")).toInt() == 2
                && step_limit_error.value(QStringLiteral("max_model_steps")).toInt() == 2
                && step_limit_terminal.value(QStringLiteral("state")).toString()
                    == QStringLiteral("failed")
                && step_limit_terminal.value(QStringLiteral("model_steps")).toInt() == 2
                && step_limit_terminal.value(QStringLiteral("max_model_steps")).toInt() == 2,
            "the configured model-step limit must fail with audit data and roll back staged work");

    MemoryBookWorkspace tool_limit_book = MemoryBookWorkspace::samplePhysicsBook();
    ToolRegistry tool_limit_registry;
    registerBookTools(&tool_limit_registry, &tool_limit_book);
    AgentSession tool_limit_session;
    AgentCancellation tool_limit_cancel;
    PermissionPolicy tool_limit_policy;
    AutoApprovalGate tool_limit_gate(true);
    MockModelProvider tool_limit_provider;
    int tool_limit_requests = 0;
    tool_limit_provider.setScript([&tool_limit_requests](const ModelRequest &) {
        ++tool_limit_requests;
        ModelTurn turn;
        if (tool_limit_requests == 1) {
            ToolCall begin;
            begin.id = QStringLiteral("tool-limit-begin");
            begin.name = QStringLiteral("transaction.begin");
            begin.argumentsJson = QStringLiteral("{\"label\":\"tool limit rollback\"}");
            turn.toolCalls.append(begin);
        } else {
            for (int i = 0; i < 2; ++i) {
                ToolCall call;
                call.id = QStringLiteral("tool-limit-rejected-%1").arg(i);
                call.name = QStringLiteral("book.summary");
                call.argumentsJson = QStringLiteral("{}");
                turn.toolCalls.append(call);
            }
        }
        return turn;
    });
    AgentRunner tool_limit_runner(
        &tool_limit_session, &tool_limit_provider, &tool_limit_registry,
        &tool_limit_book, &tool_limit_policy, &tool_limit_gate,
        &tool_limit_cancel);
    tool_limit_runner.setMode(AgentMode::Plan);
    tool_limit_runner.setMaxToolCalls(2);
    const AgentRunResult tool_limit_result = tool_limit_runner.runTurn(
        QStringLiteral("request an oversized tool batch"));
    const QJsonObject tool_limit_error = tool_limit_session
        .eventsOf(AgentEventType::Error).constLast().payload;
    const QJsonObject tool_limit_terminal = tool_limit_session
        .eventsOf(AgentEventType::RunStateChanged).constLast().payload;
    const QJsonObject tool_limit_second_request = tool_limit_session
        .eventsOf(AgentEventType::ModelRequestStarted).constLast().payload;
    Require(tool_limit_result.state == AgentRunState::Failed
                && tool_limit_requests == 2
                && tool_limit_result.error.contains(
                    QStringLiteral("maximum of 2 tool calls"))
                && !tool_limit_book.hasOpenTransaction()
                && hasEvent(tool_limit_session,
                            AgentEventType::TransactionRolledBack)
                && tool_limit_session.eventsOf(
                    AgentEventType::ToolStarted).size() == 1
                && tool_limit_session.eventsOf(
                    AgentEventType::AssistantMessage).size() == 1
                && tool_limit_error.value(QStringLiteral("code")).toString()
                    == QStringLiteral("MAX_TOOL_CALLS_EXCEEDED")
                && tool_limit_error.value(QStringLiteral("tool_calls")).toInt() == 1
                && tool_limit_error.value(
                    QStringLiteral("requested_tool_calls")).toInt() == 2
                && tool_limit_error.value(
                    QStringLiteral("remaining_tool_calls")).toInt() == 1
                && tool_limit_error.value(QStringLiteral("max_tool_calls")).toInt() == 2
                && tool_limit_second_request.value(
                    QStringLiteral("used_tool_calls")).toInt() == 1
                && tool_limit_second_request.value(
                    QStringLiteral("remaining_tool_calls")).toInt() == 1
                && tool_limit_provider.lastRequest().messages.constFirst().content
                    .contains(QStringLiteral("at most 2 tool calls in total"))
                && !tool_limit_provider.lastRequest().messages.constFirst().content
                    .contains(QStringLiteral("Remaining tool calls"))
                && tool_limit_provider.lastRequest().messages.constLast().content
                    .contains(QStringLiteral("Remaining tool calls: 1"))
                && tool_limit_terminal.value(QStringLiteral("state")).toString()
                    == QStringLiteral("failed")
                && tool_limit_terminal.value(QStringLiteral("tool_calls")).toInt() == 1
                && tool_limit_terminal.value(
                    QStringLiteral("max_tool_calls")).toInt() == 2,
            "an oversized tool-call batch must execute nothing from that batch, audit the limit, and roll back staged work");

    MemoryBookWorkspace failed_book = MemoryBookWorkspace::samplePhysicsBook();
    ToolRegistry failed_registry;
    registerBookTools(&failed_registry, &failed_book);
    AgentSession failed_session;
    AgentCancellation failed_cancellation;
    MockModelProvider failed_provider;
    ModelTurn failed_turn;
    failed_turn.error = QStringLiteral("HTTP 401: invalid credentials");
    failed_turn.timing.firstByteMs = 0;
    failed_provider.addTurn(failed_turn);
    AgentRunner failed_runner(&failed_session, &failed_provider, &failed_registry, &failed_book,
                              &policy, &approve, &failed_cancellation);
    failed_runner.setModel(QStringLiteral("mock"));
    failed_runner.setTokenUsage(false);
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
    Require(failed_request.value(QStringLiteral("response_timing")).toObject()
                    .value(QStringLiteral("first_byte_ms")).toInteger() == 0
                && !failed_request.value(QStringLiteral("response_timing")).toObject()
                        .contains(QStringLiteral("first_model_event_ms")),
            "provider failures must retain an observed response byte without inventing a model event");
    Require(!failed_provider.lastRequest().includeUsage
                && !failed_session.eventsOf(AgentEventType::ModelRequestStarted).constLast()
                        .payload.value(QStringLiteral("usage_requested")).toBool(),
            "disabling token usage must reach both the model request and lifecycle event");
    const QJsonObject failed_run =
        failed_session.eventsOf(AgentEventType::RunStateChanged).constLast().payload;
    Require(failed_run.value(QStringLiteral("state")).toString()
                    == QStringLiteral("failed")
                && failed_run.value(QStringLiteral("duration_ms")).toInteger() >= 0
                && failed_run.value(QStringLiteral("model_steps")).toInt() == 1,
            "provider failures must publish one timed whole-run terminal state");
    Require(!failed_run.value(QStringLiteral("usage_requested")).toBool()
                && failed_run.value(QStringLiteral("usage_summary")).toObject()
                       .value(QStringLiteral("reported_request_count")).toInt() == 0,
            "a run with usage disabled must not claim missing provider reports");

    {
        MemoryBookWorkspace oversized_book = MemoryBookWorkspace::samplePhysicsBook();
        ToolRegistry oversized_registry;
        registerBookTools(&oversized_registry, &oversized_book);
        AgentSession oversized_session;
        MockModelProvider oversized_provider;
        AutoApprovalGate oversized_gate(true);
        AgentCancellation oversized_cancel;
        AgentRunner oversized_runner(&oversized_session, &oversized_provider,
                                     &oversized_registry, &oversized_book,
                                     &policy, &oversized_gate, &oversized_cancel);
        const AgentRunResult oversized_result = oversized_runner.runTurn(
            QString(500000, QLatin1Char('x')));
        const QJsonObject oversized_failure = oversized_session.eventsOf(
            AgentEventType::ModelRequestFailed).constLast().payload;
        Require(oversized_result.state == AgentRunState::Failed
                    && oversized_provider.requestCount() == 0
                    && oversized_failure.value(QStringLiteral("code")).toString()
                        == QStringLiteral("CONTEXT_BUDGET_EXCEEDED")
                    && oversized_failure.value(
                        QStringLiteral("request_payload_bytes_estimate")).toInteger() > 0,
                "an oversized request must fail locally with metrics before calling the provider");
    }

    MemoryBookWorkspace partial_book = MemoryBookWorkspace::samplePhysicsBook();
    ToolRegistry partial_registry;
    registerBookTools(&partial_registry, &partial_book);
    AgentSession partial_session;
    AgentCancellation partial_cancellation;
    MockModelProvider partial_provider;
    ModelTurn partial_tool_turn;
    ToolCall partial_call;
    partial_call.id = QStringLiteral("partial-summary");
    partial_call.name = QStringLiteral("book.summary");
    partial_call.argumentsJson = QStringLiteral("{}");
    partial_tool_turn.toolCalls.append(partial_call);
    partial_tool_turn.usage.inputTokens = 40;
    partial_tool_turn.usage.outputTokens = 6;
    partial_tool_turn.usage.totalTokens = 46;
    partial_provider.addTurn(partial_tool_turn);
    ModelTurn partial_failure;
    partial_failure.error = QStringLiteral("HTTP 503: retry later");
    partial_provider.addTurn(partial_failure);
    AgentRunner partial_runner(
        &partial_session, &partial_provider, &partial_registry, &partial_book,
        &policy, &approve, &partial_cancellation);
    partial_runner.setModel(QStringLiteral("mock"));
    const AgentRunResult partial_result =
        partial_runner.runTurn(QStringLiteral("summarize, then fail"));
    const QJsonObject partial_run =
        partial_session.eventsOf(AgentEventType::RunStateChanged).constLast().payload;
    const QJsonObject partial_usage =
        partial_run.value(QStringLiteral("usage_summary")).toObject();
    Require(partial_result.state == AgentRunState::Failed
                && partial_usage.value(QStringLiteral("request_count")).toInt() == 2
                && partial_usage.value(QStringLiteral("reported_request_count")).toInt() == 1
                && partial_usage.value(QStringLiteral("missing_request_count")).toInt() == 1
                && !partial_usage.value(QStringLiteral("all_requests_reported")).toBool()
                && partial_usage.value(QStringLiteral("total_tokens")).toInteger() == 46
                && partial_usage.value(QStringLiteral("total_request_count")).toInt() == 1,
            "partial run usage must expose exact sums and request coverage without claiming completeness");

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
            turn.timing.firstByteMs = 0;
            turn.timing.firstEventMs = 0;
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
                && cancelled_requests.first().payload
                        .value(QStringLiteral("response_timing")).toObject()
                        .value(QStringLiteral("first_model_event_ms")).toInteger() == 0
                && !hasEvent(request_cancel_session, AgentEventType::ModelRequestFailed),
            "cancelled provider requests must publish a timed cancellation outcome");
    const QJsonObject cancelled_run = request_cancel_session
        .eventsOf(AgentEventType::RunStateChanged).constLast().payload;
    Require(cancelled_run.value(QStringLiteral("state")).toString()
                    == QStringLiteral("cancelled")
                && cancelled_run.value(QStringLiteral("duration_ms")).toInteger() >= 0
                && cancelled_run.value(QStringLiteral("model_steps")).toInt() == 1,
            "cancelled turns must publish a timed whole-run terminal state after cleanup");
    Require(cancelled_run.value(QStringLiteral("usage_requested")).toBool()
                && cancelled_run.value(QStringLiteral("usage_summary")).toObject()
                       .value(QStringLiteral("missing_request_count")).toInt() == 1,
            "a cancelled in-flight request must remain missing from the run usage summary");

    HistoryAssembler assembler;
    const QJsonArray replay = assembler.toOpenAIMessages(assembler.assemble(session.events(), true), true);
    bool replayed_reasoning = false;
    for (const QJsonValue &value : replay) {
        if (value.toObject().value(QStringLiteral("reasoning_content")).toString().contains(QStringLiteral("book map"))) {
            replayed_reasoning = true;
        }
    }
    Require(replayed_reasoning, "session history with tools must replay reasoning_content");

    AgentSession long_turn_session;
    long_turn_session.append(AgentEventType::UserMessage,
                             QJsonObject { { QStringLiteral("text"), QStringLiteral("Translate this book") } });
    for (int i = 0; i < 300; ++i) {
        ToolCall call;
        call.id = QStringLiteral("long_call_%1").arg(i);
        call.name = QStringLiteral("resource.read_fragment");
        call.argumentsJson = QStringLiteral("{\"resource_id\":\"ch1\"}");
        long_turn_session.append(AgentEventType::AssistantMessage, QJsonObject {
            { QStringLiteral("content"), QString() },
            { QStringLiteral("tool_calls"), QJsonArray { toolCallToJson(call) } }
        });
        long_turn_session.append(AgentEventType::ToolCompleted, QJsonObject {
            { QStringLiteral("tool_call_id"), call.id },
            { QStringLiteral("name"), call.name },
            { QStringLiteral("result"), QString(2500, QLatin1Char('x')) }
        });
        if (i == 4) {
            long_turn_session.append(AgentEventType::TransactionCommitted, QJsonObject {
                { QStringLiteral("book_revision"), 7 },
                { QStringLiteral("resource_outcomes"), QJsonObject {
                    { QStringLiteral("resource_ids"), QJsonArray { QStringLiteral("ch1") } }
                } },
                { QStringLiteral("recovery"), QJsonObject {
                    { QStringLiteral("checkpoint_id"), QStringLiteral("restore-7") }
                } }
            });
        }
    }
    const HistoryCheckpoint checkpoint = assembler.planCheckpoint(
        long_turn_session.events(), true, DEFAULT_CHECKPOINT_TAIL_BYTES);
    HistoryAssemblyStats compact_stats;
    const QList<ChatMessage> compact_messages = assembler.assemble(
        long_turn_session.events(), true, 0, &compact_stats, 0, &checkpoint);
    const QJsonArray compact_wire = assembler.toOpenAIMessages(compact_messages, true);
    const QJsonArray compact_again = assembler.toOpenAIMessages(
        assembler.assemble(long_turn_session.events(), true, 0, nullptr, 0, &checkpoint),
        true);
    Require(checkpoint.installed
                && compact_stats.checkpointInstalled
                && compact_stats.omittedCurrentTurnMessages > 0
                && compact_wire == compact_again
                && compact_messages.first().content == QStringLiteral("Translate this book")
                && compact_messages.at(1).content.contains(
                    QStringLiteral("<compaction-summary>"))
                && compact_messages.at(1).content.contains(QStringLiteral("ch1"))
                && compact_messages.at(1).content.contains(QStringLiteral("restore-7")),
            "a frozen checkpoint must keep the user request and a stable summary");
    QSet<QString> visible_call_ids;
    for (const ChatMessage &message : compact_messages) {
        if (message.role == QLatin1String("assistant")) {
            for (const ToolCall &call : message.toolCalls) visible_call_ids.insert(call.id);
        } else if (message.role == QLatin1String("tool")) {
            Require(visible_call_ids.contains(message.toolCallId),
                    "compacted tool results must keep their assistant tool calls");
        }
    }
    Require(visible_call_ids.contains(QStringLiteral("long_call_299"))
                && !visible_call_ids.contains(QStringLiteral("long_call_0")),
            "current-turn compaction must retain recent complete tool exchanges");

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
    Require(!toolSchemaNames(ask_provider.lastRequest().tools).contains(
                QStringLiteral("resource_patch_fragment")),
            "a provider returning an unadvertised mutation must still be denied at execution");

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
    MemoryResource paragraph_xhtml_2 = paragraph_xhtml;
    paragraph_xhtml_2.id = QStringLiteral("paragraph-page-2");
    paragraph_xhtml_2.bookPath = QStringLiteral("OEBPS/Text/paragraph-2.xhtml");
    paragraph_book.addResource(paragraph_xhtml_2);
    const QString paragraph_original = paragraph_xhtml.text;
    const QString paragraph_original_2 = paragraph_xhtml_2.text;
    ToolRegistry paragraph_registry;
    registerBookTools(&paragraph_registry, &paragraph_book);
    registerDivParagraphTools(&paragraph_registry, &paragraph_book);
    const ToolResult paragraph_analysis = paragraph_registry.find(
        QStringLiteral("paragraphs.analyze"))->execute(QJsonObject {
            { QStringLiteral("resource_ids"), QJsonArray {
                paragraph_xhtml.id, paragraph_xhtml_2.id } }
        });
    Require(paragraph_analysis.ok, "paragraph approval fixture analysis failed");
    const ToolResult paragraph_plan = paragraph_registry.find(
        QStringLiteral("paragraphs.plan"))->execute(QJsonObject {
            { QStringLiteral("analysis_id"),
              paragraph_analysis.data.value(QStringLiteral("analysis_id")) }
        });
    Require(paragraph_plan.ok, "paragraph approval fixture plan failed");
    AgentSession plan_event_session;
    AutoApprovalGate plan_event_gate(true);
    MockModelProvider plan_event_provider;
    const QJsonObject plan_event_arguments {
        { QStringLiteral("analysis_id"),
          paragraph_analysis.data.value(QStringLiteral("analysis_id")) }
    };
    plan_event_provider.setScript([plan_event_arguments](const ModelRequest &request) {
        bool plan_created = false;
        for (const ChatMessage &message : request.messages) {
            if (message.role == QLatin1String("tool")
                && message.content.contains(QStringLiteral("plan_id"))) {
                plan_created = true;
            }
        }
        ModelTurn turn;
        if (!plan_created) {
            ToolCall call;
            call.id = QStringLiteral("paragraph_plan_review");
            call.name = QStringLiteral("paragraphs.plan");
            call.argumentsJson = QString::fromUtf8(
                QJsonDocument(plan_event_arguments).toJson(QJsonDocument::Compact));
            turn.toolCalls.append(call);
        } else {
            turn.content = QStringLiteral("The paragraph plan is ready for review.");
        }
        return turn;
    });
    AgentCancellation plan_event_cancel;
    AgentRunner plan_event_runner(
        &plan_event_session, &plan_event_provider, &paragraph_registry, &paragraph_book,
        &ask_policy, &plan_event_gate, &plan_event_cancel);
    plan_event_runner.setMode(AgentMode::Plan);
    const AgentRunResult plan_event_result = plan_event_runner.runTurn(
        QStringLiteral("create a reviewable paragraph plan"));
    Require(plan_event_result.state == AgentRunState::Completed,
            "reviewable paragraph plan runner failed");
    const QList<AgentEvent> plan_events =
        plan_event_session.eventsOf(AgentEventType::PlanCreated);
    Require(plan_events.size() == 1,
            "a successful native planning tool must publish one plan-created event");
    const QJsonObject plan_event = plan_events.constFirst().payload;
    Require(plan_event.value(QStringLiteral("tool_call_id")).toString()
                    == QStringLiteral("paragraph_plan_review")
                && plan_event.value(QStringLiteral("name")).toString()
                    == QStringLiteral("paragraphs.plan")
                && plan_event.value(QStringLiteral("plan_kind")).toString()
                    == QStringLiteral("paragraph_normalization")
                && plan_event.value(QStringLiteral("review_status")).toString()
                    == QStringLiteral("ready")
                && plan_event.value(QStringLiteral("book_session_id")).toString()
                    == paragraph_book.bookSessionId()
                && !plan_event.value(QStringLiteral("run_id")).toString().isEmpty()
                && !plan_event.value(QStringLiteral("applied_to_book")).toBool()
                && plan_event.value(QStringLiteral("plan_id")).toString()
                    == paragraph_plan.data.value(QStringLiteral("plan_id")).toString()
                && plan_event.value(QStringLiteral("plan_digest")).toString()
                    == paragraph_plan.data.value(QStringLiteral("plan_digest")).toString()
                && plan_event.value(
                    QStringLiteral("operation_groups_independent")).toBool()
                && plan_event.value(QStringLiteral("operation_groups"))
                    .toArray().size() == 2
                && plan_event.value(QStringLiteral("total_count")).toInt() == 2
                && plan_event.value(
                    QStringLiteral("reviewed_count")).toInt() == 2
                && plan_event.value(
                    QStringLiteral("review_complete")).toBool()
                && !plan_event.value(QStringLiteral("changes")).toArray().isEmpty(),
            "plan-created event must preserve the binding, operation groups, and bounded changes");
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

    class SelectParagraphGroupGate : public IApprovalGate
    {
    public:
        ApprovalDecision waitForApproval(const QString &, const QString &,
                                         const QJsonObject &,
                                         const QString &) override
        {
            return ApprovalDecision {
                true,
                QJsonObject {
                    { QStringLiteral("selected_resource_ids"),
                      QJsonArray { QStringLiteral("paragraph-page-2") } },
                    { QStringLiteral("plan_digest"), QStringLiteral("must-be-ignored") }
                }
            };
        }
    } selected_group_gate;
    AgentSession selected_group_session;
    MockModelProvider selected_group_provider;
    selected_group_provider.setScript(
        [paragraph_apply_arguments](const ModelRequest &request) {
        bool already_requested = false;
        for (const ChatMessage &message : request.messages) {
            if (message.role != QLatin1String("assistant")) continue;
            for (const ToolCall &call : message.toolCalls) {
                if (call.id == QLatin1String("selected_paragraph_apply")) {
                    already_requested = true;
                }
            }
        }
        ModelTurn turn;
        if (!already_requested) {
            ToolCall call;
            call.id = QStringLiteral("selected_paragraph_apply");
            call.name = QStringLiteral("paragraphs_apply");
            call.argumentsJson = QString::fromUtf8(
                QJsonDocument(paragraph_apply_arguments).toJson(QJsonDocument::Compact));
            turn.toolCalls.append(call);
        } else {
            turn.content = QStringLiteral("The selected paragraph group was staged.");
        }
        return turn;
    });
    AgentCancellation selected_group_cancel;
    AgentRunner selected_group_runner(
        &selected_group_session, &selected_group_provider, &paragraph_registry,
        &paragraph_book, &ask_policy, &selected_group_gate, &selected_group_cancel);
    selected_group_runner.setMode(AgentMode::Edit);
    const AgentRunResult selected_group_result = selected_group_runner.runTurn(
        QStringLiteral("apply one reviewed paragraph group"));
    Require(selected_group_result.state == AgentRunState::Completed
                && paragraph_book.hasOpenTransaction(),
            "approved paragraph group should leave one reviewed transaction staged");
    const QJsonObject approved_group = selected_group_session.eventsOf(
        AgentEventType::ToolApproved).constFirst().payload;
    const QJsonObject approved_group_arguments = approved_group.value(
        QStringLiteral("arguments")).toObject();
    Require(approved_group.value(
                QStringLiteral("argument_overrides_applied")).toBool()
                && approved_group_arguments.value(
                    QStringLiteral("selected_resource_ids")).toArray()
                    == QJsonArray { QStringLiteral("paragraph-page-2") }
                && approved_group_arguments.value(
                    QStringLiteral("plan_digest")).toString()
                    == paragraph_plan.data.value(
                        QStringLiteral("plan_digest")).toString(),
            "Runner must audit the selected groups while ignoring unauthorized approval overrides");
    Require(paragraph_book.workingText(paragraph_xhtml.id) == paragraph_original
                && paragraph_book.workingText(paragraph_xhtml_2.id)
                    != paragraph_original_2,
            "approval argument overrides must stage only the explicitly selected plan group");
    Require(paragraph_book.rollbackTransaction().ok,
            "selected paragraph approval fixture must roll back its staged transaction");

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
    const QJsonObject edit_outcomes = commit_status.value(
        QStringLiteral("resource_outcomes")).toObject();
    Require(edit_outcomes.value(QStringLiteral("scope_available")).toBool()
                && edit_outcomes.value(QStringLiteral("all_or_nothing")).toBool()
                && edit_outcomes.value(QStringLiteral("status")).toString()
                    == QStringLiteral("all_applied")
                && edit_outcomes.value(QStringLiteral("transaction_state")).toString()
                    == QStringLiteral("committed")
                && edit_outcomes.value(QStringLiteral("resource_ids")).toArray()
                    == QJsonArray { QStringLiteral("ch1") }
                && edit_outcomes.value(
                    QStringLiteral("successful_resource_count")).toInt() == 1
                && edit_outcomes.value(
                    QStringLiteral("failed_resource_count")).toInt() == 0
                && edit_outcomes.value(
                    QStringLiteral("structural_operation_count")).toInt() == 0,
            "successful commit must publish exact all-or-nothing resource outcomes");
    const BookOpResult edit_restored = edit_book.restoreTaskRestorePoint(
        commit_recovery.value(QStringLiteral("checkpoint_id")).toString());
    Require(edit_restored.ok && edit_restored.applied
                && edit_book.resourceText(QStringLiteral("ch1")).contains(
                    QStringLiteral("<title>Heat</title>")),
            "the published task restore point must restore the committed text");

    {
        MemoryBookWorkspace partial_book = MemoryBookWorkspace::samplePhysicsBook();
        ToolRegistry partial_registry;
        registerBookTools(&partial_registry, &partial_book);
        AgentSession partial_session;
        AutoApprovalGate partial_gate(true);
        MockModelProvider partial_provider;
        int partial_step = 0;
        partial_provider.setScript([&](const ModelRequest &) {
            ModelTurn turn;
            if (partial_step == 3) {
                turn.error = QStringLiteral("HTTP 400: provider rejected request");
                return turn;
            }
            ToolCall call;
            call.id = QStringLiteral("partial_%1").arg(partial_step);
            if (partial_step == 0) {
                call.name = QStringLiteral("transaction.begin");
                call.argumentsJson = QStringLiteral("{}");
            } else if (partial_step == 1) {
                call.name = QStringLiteral("resource.patch_fragment");
                call.argumentsJson = QStringLiteral(
                    "{\"resource_id\":\"ch1\",\"expected_text\":\"<title>Heat</title>\",\"text\":\"<title>Partial</title>\",\"expected_resource_revision\":1}");
            } else {
                call.name = QStringLiteral("transaction.commit");
                call.argumentsJson = QStringLiteral("{\"expected_book_revision\":1}");
            }
            ++partial_step;
            turn.toolCalls.append(call);
            return turn;
        });
        AgentCancellation partial_cancel;
        AgentRunner partial_runner(&partial_session, &partial_provider,
                                   &partial_registry, &partial_book, &ask_policy,
                                   &partial_gate, &partial_cancel);
        partial_runner.setMode(AgentMode::Edit);
        const AgentRunResult partial_result = partial_runner.runTurn(
            QStringLiteral("Make a long edit"));
        const QJsonObject partial_outcome = partial_session.eventsOf(
            AgentEventType::RunStateChanged).constLast().payload
            .value(QStringLiteral("partial_outcome")).toObject();
        Require(partial_result.state == AgentRunState::Failed
                    && partial_book.resourceText(QStringLiteral("ch1")).contains(
                        QStringLiteral("<title>Partial</title>"))
                    && partial_outcome.value(QStringLiteral("status")).toString()
                        == QStringLiteral("partial_applied")
                    && partial_outcome.value(QStringLiteral("commit_count")).toInt() == 1
                    && partial_outcome.value(QStringLiteral("resource_ids")).toArray()
                        == QJsonArray { QStringLiteral("ch1") }
                    && partial_outcome.value(QStringLiteral("book_revision")).toInteger()
                        == static_cast<qint64>(partial_book.revision()),
                "provider failure after a commit must preserve a machine-readable partial outcome");
    }

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

    MemoryBookWorkspace failed_commit_book = MemoryBookWorkspace::samplePhysicsBook();
    const QString failed_commit_ch1 = failed_commit_book.resourceText(
        QStringLiteral("ch1"));
    const QString failed_commit_ch2 = failed_commit_book.resourceText(
        QStringLiteral("ch2"));
    failed_commit_book.setInjectedFailureIndex(1);
    ToolRegistry failed_commit_registry;
    registerBookTools(&failed_commit_registry, &failed_commit_book);
    AgentSession failed_commit_session;
    AutoApprovalGate failed_commit_gate(true);
    MockModelProvider failed_commit_provider;
    failed_commit_provider.setScript([&failed_commit_book](const ModelRequest &request) {
        bool began = false;
        bool patched_ch1 = false;
        bool patched_ch2 = false;
        bool metadata_staged = false;
        bool commit_failed = false;
        for (const ChatMessage &message : request.messages) {
            if (message.role != QLatin1String("tool")) continue;
            if (message.toolCallId == QLatin1String("failed-commit-begin")) {
                began = true;
            } else if (message.toolCallId == QLatin1String("failed-commit-ch1")) {
                patched_ch1 = true;
            } else if (message.toolCallId == QLatin1String("failed-commit-ch2")) {
                patched_ch2 = true;
            } else if (message.toolCallId == QLatin1String("failed-commit-metadata")) {
                metadata_staged = true;
            } else if (message.toolCallId == QLatin1String("failed-commit-apply")) {
                commit_failed = message.content.contains(
                    QStringLiteral("TRANSACTION_ROLLED_BACK"));
            }
        }
        ModelTurn turn;
        ToolCall call;
        if (!began) {
            call.id = QStringLiteral("failed-commit-begin");
            call.name = QStringLiteral("transaction.begin");
            call.argumentsJson = QStringLiteral("{}");
        } else if (!patched_ch1) {
            call.id = QStringLiteral("failed-commit-ch1");
            call.name = QStringLiteral("resource.patch_fragment");
            call.argumentsJson = QStringLiteral(
                "{\"resource_id\":\"ch1\",\"expected_text\":\"<title>Heat</title>\",\"text\":\"<title>Heat revised</title>\",\"expected_revision\":1}");
        } else if (!patched_ch2) {
            call.id = QStringLiteral("failed-commit-ch2");
            call.name = QStringLiteral("resource.patch_fragment");
            call.argumentsJson = QStringLiteral(
                "{\"resource_id\":\"ch2\",\"expected_text\":\"<title>Light</title>\",\"text\":\"<title>Light revised</title>\",\"expected_revision\":1}");
        } else if (!metadata_staged) {
            call.id = QStringLiteral("failed-commit-metadata");
            call.name = QStringLiteral("metadata.update");
            call.argumentsJson = QStringLiteral(
                "{\"patch\":{\"title\":\"Should roll back\"}}");
        } else if (!commit_failed) {
            call.id = QStringLiteral("failed-commit-apply");
            call.name = QStringLiteral("transaction.commit");
            call.argumentsJson = QStringLiteral("{\"expected_revision\":%1}")
                                     .arg(failed_commit_book.revision());
        } else {
            turn.content = QStringLiteral("The atomic commit failed and was rolled back.");
            return turn;
        }
        turn.toolCalls.append(call);
        return turn;
    });
    AgentCancellation failed_commit_cancel;
    AgentRunner failed_commit_runner(
        &failed_commit_session, &failed_commit_provider, &failed_commit_registry,
        &failed_commit_book, &ask_policy, &failed_commit_gate,
        &failed_commit_cancel);
    failed_commit_runner.setMode(AgentMode::Auto);
    const AgentRunResult failed_commit_result = failed_commit_runner.runTurn(
        QStringLiteral("apply two text edits atomically"));
    Require(failed_commit_result.state == AgentRunState::Completed
                && !failed_commit_book.hasOpenTransaction()
                && failed_commit_book.resourceText(QStringLiteral("ch1"))
                    == failed_commit_ch1
                && failed_commit_book.resourceText(QStringLiteral("ch2"))
                    == failed_commit_ch2
                && failed_commit_book.metadata().value(
                    QStringLiteral("title")).toString()
                    == QStringLiteral("Junior Physics")
                && !hasEvent(failed_commit_session,
                             AgentEventType::TransactionCommitted),
            "failed multi-resource commit must roll back without a committed event");
    const QList<AgentEvent> failed_tool_events = failed_commit_session.eventsOf(
        AgentEventType::ToolFailed);
    Require(failed_tool_events.size() == 1
                && failed_tool_events.constFirst().payload.value(
                    QStringLiteral("name")).toString()
                    == QStringLiteral("transaction.commit"),
            "failed transaction commit must publish one auditable tool failure");
    const QJsonObject failed_outcomes = failed_tool_events.constFirst().payload
        .value(QStringLiteral("data")).toObject()
        .value(QStringLiteral("resource_outcomes")).toObject();
    Require(failed_outcomes.value(QStringLiteral("scope_available")).toBool()
                && failed_outcomes.value(QStringLiteral("all_or_nothing")).toBool()
                && failed_outcomes.value(QStringLiteral("status")).toString()
                    == QStringLiteral("not_applied")
                && failed_outcomes.value(
                    QStringLiteral("transaction_state")).toString()
                    == QStringLiteral("rolled_back")
                && failed_outcomes.value(QStringLiteral("resource_ids")).toArray()
                    == QJsonArray { QStringLiteral("ch1"), QStringLiteral("ch2") }
                && failed_outcomes.value(
                    QStringLiteral("successful_resource_count")).toInt() == 0
                && failed_outcomes.value(
                    QStringLiteral("failed_resource_count")).toInt() == 2,
            "rolled-back commit must report zero successes and every attempted resource as failed");
    Require(failed_outcomes.value(
                QStringLiteral("structural_operations")).toArray()
                    == QJsonArray { QStringLiteral("metadata") }
                && failed_outcomes.value(
                    QStringLiteral("successful_structural_operation_count")).toInt() == 0
                && failed_outcomes.value(
                    QStringLiteral("failed_structural_operation_count")).toInt() == 1,
            "rolled-back commit must report structural operations separately from resources");

    MemoryBookWorkspace conflicted_commit_book =
        MemoryBookWorkspace::samplePhysicsBook();
    const QString conflicted_original = conflicted_commit_book.resourceText(
        QStringLiteral("ch1"));
    ToolRegistry conflicted_commit_registry;
    registerBookTools(&conflicted_commit_registry, &conflicted_commit_book);
    AgentSession conflicted_commit_session;
    AutoApprovalGate conflicted_commit_gate(true);
    MockModelProvider conflicted_commit_provider;
    conflicted_commit_provider.setScript(
        [&conflicted_commit_book](const ModelRequest &request) {
        bool began = false;
        bool patched = false;
        bool commit_conflicted = false;
        for (const ChatMessage &message : request.messages) {
            if (message.role != QLatin1String("tool")) continue;
            if (message.toolCallId == QLatin1String("conflict-begin")) {
                began = true;
            } else if (message.toolCallId == QLatin1String("conflict-patch")) {
                patched = true;
            } else if (message.toolCallId == QLatin1String("conflict-commit")) {
                commit_conflicted = message.content.contains(
                    QStringLiteral("BOOK_REVISION_CONFLICT"));
            }
        }
        ModelTurn turn;
        ToolCall call;
        if (!began) {
            call.id = QStringLiteral("conflict-begin");
            call.name = QStringLiteral("transaction.begin");
            call.argumentsJson = QStringLiteral("{}");
        } else if (!patched) {
            call.id = QStringLiteral("conflict-patch");
            call.name = QStringLiteral("resource.patch_fragment");
            call.argumentsJson = QStringLiteral(
                "{\"resource_id\":\"ch1\",\"expected_text\":\"<title>Heat</title>\",\"text\":\"<title>Conflict</title>\",\"expected_revision\":1}");
        } else if (!commit_conflicted) {
            call.id = QStringLiteral("conflict-commit");
            call.name = QStringLiteral("transaction.commit");
            call.argumentsJson = QStringLiteral("{\"expected_revision\":%1}")
                                     .arg(conflicted_commit_book.revision() + 1);
        } else {
            turn.content = QStringLiteral("The revision conflict left the edit staged.");
            return turn;
        }
        turn.toolCalls.append(call);
        return turn;
    });
    AgentCancellation conflicted_commit_cancel;
    AgentRunner conflicted_commit_runner(
        &conflicted_commit_session, &conflicted_commit_provider,
        &conflicted_commit_registry, &conflicted_commit_book, &ask_policy,
        &conflicted_commit_gate, &conflicted_commit_cancel);
    conflicted_commit_runner.setMode(AgentMode::Auto);
    const AgentRunResult conflicted_commit_result =
        conflicted_commit_runner.runTurn(QStringLiteral("try a stale commit"));
    const QJsonObject conflict_failure = conflicted_commit_session.eventsOf(
        AgentEventType::ToolFailed).constFirst().payload;
    const QJsonObject conflict_outcomes = conflict_failure.value(
        QStringLiteral("data")).toObject().value(
        QStringLiteral("resource_outcomes")).toObject();
    Require(conflicted_commit_result.state == AgentRunState::Completed
                && conflicted_commit_book.hasOpenTransaction()
                && conflicted_commit_book.resourceText(QStringLiteral("ch1"))
                    == conflicted_original
                && conflict_failure.value(QStringLiteral("code")).toString()
                    == QStringLiteral("BOOK_REVISION_CONFLICT")
                && conflict_outcomes.value(
                    QStringLiteral("transaction_state")).toString()
                    == QStringLiteral("staged")
                && conflict_outcomes.value(
                    QStringLiteral("successful_resource_count")).toInt() == 0
                && conflict_outcomes.value(
                    QStringLiteral("failed_resource_count")).toInt() == 1,
            "revision-conflicted commit must report zero applied resources and retain staging");
    Require(conflicted_commit_book.rollbackTransaction().ok,
            "conflicted outcome fixture must discard its retained staged transaction");

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
                    == QStringLiteral("not_applied")
                && !preview_status.value(
                    QStringLiteral("preview_digest")).toString().isEmpty()
                && preview_status.value(QStringLiteral("total_counts")).toObject()
                    .value(QStringLiteral("changes")).toInt() == 1
                && !preview_status.value(QStringLiteral("has_more")).toBool(),
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
        ApprovalDecision waitForApproval(const QString &, const QString &,
                                         const QJsonObject &,
                                         const QString &) override
        {
            Require(m_workspace->hasOpenTransaction(),
                    "transaction should still be open when approval is pending");
            m_cancellation->request();
            return ApprovalDecision();
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

    MemoryBookWorkspace controller_restore_book = MemoryBookWorkspace::samplePhysicsBook();
    const QString controller_restore_before =
        controller_restore_book.resourceText(QStringLiteral("ch1"));
    Require(controller_restore_book.beginTransaction(QStringLiteral("controller restore")).ok,
            "controller restore transaction begin failed");
    Require(controller_restore_book.patchFragment(
                QStringLiteral("ch1"), -1, -1, QStringLiteral("<title>CONTROLLER</title>"),
                controller_restore_book.resourceRevision(QStringLiteral("ch1")),
                QStringLiteral("<title>Heat</title>")).ok,
            "controller restore patch failed");
    const QString controller_restore_id = controller_restore_book.createTaskRestorePoint(
        QStringLiteral("controller restore"), QStringList { QStringLiteral("ch1") })
        .data.value(QStringLiteral("checkpoint_id")).toString();
    Require(controller_restore_book.commitTransaction(controller_restore_book.revision()).applied
                && controller_restore_book.sealTaskRestorePoint(controller_restore_id).ok,
            "controller restore point setup failed");
    AgentController restore_controller;
    Require(restore_controller.setWorkspace(&controller_restore_book),
            "restore controller workspace setup failed");
    const BookOpResult wrong_book_restore = restore_controller.restoreTask(
        controller_restore_id, QStringLiteral("another-book"));
    Require(!wrong_book_restore.ok
                && wrong_book_restore.code == QStringLiteral("BOOK_TARGET_CHANGED")
                && hasEvent(*restore_controller.session(), AgentEventType::TaskRestoreFailed)
                && controller_restore_book.resourceText(QStringLiteral("ch1"))
                    != controller_restore_before,
            "controller must reject a restore card bound to another book session");
    const BookOpResult controller_restored = restore_controller.restoreTask(
        controller_restore_id, controller_restore_book.bookSessionId());
    Require(controller_restored.ok
                && hasEvent(*restore_controller.session(), AgentEventType::TaskRestoreCompleted)
                && controller_restore_book.resourceText(QStringLiteral("ch1"))
                    == controller_restore_before,
            "controller must publish a completed event after a guarded restore");
    return EXIT_SUCCESS;
}
