/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/Core/AgentRunner.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QUuid>

#include "Agent/Tools/BookTools.h"

namespace SigilAgent
{

namespace
{

QJsonObject withEpubcheckStatus(QJsonObject payload)
{
    if (!payload.contains(QStringLiteral("full_epubcheck"))) {
        payload.insert(QStringLiteral("full_epubcheck"), QJsonObject {
            { QStringLiteral("status"), QStringLiteral("not_run") },
            { QStringLiteral("message"),
              QStringLiteral("Transaction handling does not run full EPUBCheck.") }
        });
    }
    return payload;
}

QJsonObject previewStatus(QJsonObject payload)
{
    payload.insert(QStringLiteral("applied_to_book"), false);
    payload.insert(QStringLiteral("save_status"), QStringLiteral("not_applied"));
    return withEpubcheckStatus(payload);
}

QJsonObject commitStatus(QJsonObject payload)
{
    payload.insert(QStringLiteral("applied_to_book"), true);
    payload.insert(QStringLiteral("save_status"), QStringLiteral("not_saved"));
    if (!payload.contains(QStringLiteral("recovery"))) {
        payload.insert(QStringLiteral("recovery"), QJsonObject {
            { QStringLiteral("sigil_undo"), QStringLiteral("where_available") },
            { QStringLiteral("task_restore_point"),
              QStringLiteral("not_created_by_commit") }
        });
    }
    return withEpubcheckStatus(payload);
}

QJsonObject rollbackStatus(QJsonObject payload)
{
    payload.insert(QStringLiteral("applied_to_book"), false);
    payload.insert(QStringLiteral("save_status"), QStringLiteral("not_applied"));
    payload.insert(QStringLiteral("live_book_unchanged"), true);
    return payload;
}

} // namespace

AgentRunner::SessionSink::SessionSink(AgentSession *session, AgentCancellation *cancellation) :
    m_session(session),
    m_cancellation(cancellation)
{
}

void AgentRunner::SessionSink::onReasoningDelta(const QString &text)
{
    if (!m_session || text.isEmpty()) return;
    m_session->append(AgentEventType::AssistantDelta, QJsonObject {
        { QStringLiteral("kind"), QStringLiteral("reasoning") },
        { QStringLiteral("text"), text }
    });
}

void AgentRunner::SessionSink::onContentDelta(const QString &text)
{
    if (!m_session || text.isEmpty()) return;
    m_session->append(AgentEventType::AssistantDelta, QJsonObject {
        { QStringLiteral("kind"), QStringLiteral("content") },
        { QStringLiteral("text"), text }
    });
}

void AgentRunner::SessionSink::onToolCallsUpdated(const QList<ToolCall> &)
{
}

bool AgentRunner::SessionSink::isCancelled() const
{
    return m_cancellation && m_cancellation->isCancelled();
}

AgentRunner::AgentRunner(AgentSession *session,
                         IModelProvider *provider,
                         ToolRegistry *tools,
                         IBookWorkspace *workspace,
                         PermissionPolicy *policy,
                         IApprovalGate *gate,
                         AgentCancellation *cancellation) :
    m_session(session),
    m_provider(provider),
    m_tools(tools),
    m_workspace(workspace),
    m_policy(policy),
    m_gate(gate),
    m_cancellation(cancellation)
{
}

void AgentRunner::setMode(AgentMode mode)
{
    m_mode = mode;
}

AgentMode AgentRunner::mode() const
{
    return m_mode;
}

void AgentRunner::setModel(const QString &model)
{
    m_model = model;
}

void AgentRunner::setThinking(bool enabled, const QString &effort)
{
    m_thinking = enabled;
    m_effort = effort;
}

void AgentRunner::setMaxSteps(int steps)
{
    m_maxSteps = qMax(1, steps);
}

AgentRunState AgentRunner::state() const
{
    return m_state;
}

void AgentRunner::setState(AgentRunState state)
{
    m_state = state;
    if (m_session) {
        m_session->append(AgentEventType::RunStateChanged, QJsonObject {
            { QStringLiteral("state"), runStateName(state) }
        });
    }
}

QJsonObject AgentRunner::parseArguments(const QString &json) const
{
    if (json.trimmed().isEmpty()) return QJsonObject();
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return QJsonObject();
    }
    return document.object();
}

void AgentRunner::rollbackOpenWork()
{
    if (bookTargetMatchesRun() && m_workspace && m_workspace->hasOpenTransaction()) {
        const BookOpResult rolled = m_workspace->rollbackTransaction();
        if (m_session) {
            m_session->append(AgentEventType::TransactionRolledBack,
                              rollbackStatus(rolled.data));
        }
    }
}

bool AgentRunner::bookTargetMatchesRun() const
{
    return !m_workspace || m_workspace->bookSessionId() == m_runBookSessionId;
}

AgentRunResult AgentRunner::cancelRun()
{
    rollbackOpenWork();
    setState(AgentRunState::Cancelled);
    const AgentCancellationReason reason = m_cancellation
        ? m_cancellation->reason() : AgentCancellationReason::UserStop;
    if (m_session) {
        m_session->append(AgentEventType::SessionCancelled, QJsonObject {
            { QStringLiteral("reason"), cancellationReasonName(reason) },
            { QStringLiteral("book_session_id"), m_runBookSessionId }
        });
    }
    AgentRunResult result;
    result.state = AgentRunState::Cancelled;
    return result;
}

AgentRunResult AgentRunner::failBookTargetChanged(const QString &stage)
{
    const QString actual = m_workspace ? m_workspace->bookSessionId() : QString();
    const QString message = QStringLiteral(
        "The open book changed during this Agent run. The old response was not applied to the new book.");
    setState(AgentRunState::Failed);
    if (m_session) {
        const QJsonObject payload {
            { QStringLiteral("code"), QStringLiteral("BOOK_TARGET_CHANGED") },
            { QStringLiteral("message"), message },
            { QStringLiteral("stage"), stage },
            { QStringLiteral("expected_book_session_id"), m_runBookSessionId },
            { QStringLiteral("actual_book_session_id"), actual }
        };
        m_session->append(AgentEventType::BookTargetChanged, payload);
        m_session->append(AgentEventType::Error, payload);
    }
    AgentRunResult result;
    result.state = AgentRunState::Failed;
    result.error = message;
    return result;
}

void AgentRunner::publishToolOutcome(const ToolCall &call, const ToolResult &result)
{
    if (!m_session) return;
    QJsonObject payload = result.toJson();
    payload.insert(QStringLiteral("tool_call_id"), call.id);
    payload.insert(QStringLiteral("name"), call.name);
    payload.insert(QStringLiteral("id"), call.id);
    if (result.ok) {
        m_session->append(AgentEventType::ToolCompleted, payload);
        if (call.name == QLatin1String("transaction.preview")) {
            m_session->append(AgentEventType::TransactionPreviewed,
                              previewStatus(result.data));
        } else if (call.name == QLatin1String("transaction.commit") && result.applied) {
            m_session->append(AgentEventType::TransactionCommitted,
                              commitStatus(result.data));
        } else if (call.name == QLatin1String("transaction.rollback")) {
            m_session->append(AgentEventType::TransactionRolledBack,
                              rollbackStatus(result.data));
        } else if (call.name == QLatin1String("checkpoint.create")) {
            m_session->append(AgentEventType::CheckpointCreated, result.data);
        }
    } else {
        m_session->append(AgentEventType::ToolFailed, payload);
    }
    if (m_workspace) {
        m_session->append(AgentEventType::BookRevisionObserved, QJsonObject {
            { QStringLiteral("book_revision"), static_cast<qint64>(m_workspace->revision()) }
        });
    }
}

ToolResult AgentRunner::executeTool(const ToolCall &call)
{
    IAgentTool *tool = m_tools ? m_tools->find(call.name) : nullptr;
    ToolCall local = call;
    if (tool) local.name = tool->descriptor().name;
    if (!tool) {
        const ToolResult missing = ToolResult::failure(QStringLiteral("UNKNOWN_TOOL"),
                                                       QStringLiteral("Unknown tool %1").arg(call.name));
        publishToolOutcome(local, missing);
        return missing;
    }
    const AgentToolDescriptor descriptor = tool->descriptor();
    const QJsonObject arguments = parseArguments(call.argumentsJson);
    const PermissionAction permission = m_policy
        ? m_policy->evaluate(m_mode, descriptor) : PermissionAction::Deny;

    m_session->append(AgentEventType::ToolRequested, QJsonObject {
        { QStringLiteral("tool_call_id"), local.id },
        { QStringLiteral("name"), local.name },
        { QStringLiteral("arguments"), arguments },
        { QStringLiteral("risk"), toolRiskName(descriptor.risk) }
    });

    if (permission == PermissionAction::Deny) {
        const QString reason = m_policy->denyReason(m_mode, descriptor);
        m_session->append(AgentEventType::ToolRejected, QJsonObject {
            { QStringLiteral("tool_call_id"), local.id },
            { QStringLiteral("name"), local.name },
            { QStringLiteral("reason"), reason }
        });
        const ToolResult denied = ToolResult::denied(reason);
        publishToolOutcome(local, denied);
        return denied;
    }

    if (permission == PermissionAction::Ask) {
        const QString impact = humanReadableImpact(local.name, arguments);
        setState(AgentRunState::AwaitingApproval);
        m_session->append(AgentEventType::ToolApprovalRequested, QJsonObject {
            { QStringLiteral("tool_call_id"), local.id },
            { QStringLiteral("name"), local.name },
            { QStringLiteral("impact"), impact },
            { QStringLiteral("arguments"), arguments }
        });
        const bool approved = m_gate && m_gate->waitForApproval(local.id, local.name, arguments, impact);
        if (m_cancellation && m_cancellation->isCancelled()) {
            const ToolResult cancelled = ToolResult::cancelled();
            publishToolOutcome(local, cancelled);
            return cancelled;
        }
        if (!approved) {
            m_session->append(AgentEventType::ToolRejected, QJsonObject {
                { QStringLiteral("tool_call_id"), local.id },
                { QStringLiteral("name"), local.name },
                { QStringLiteral("reason"), QStringLiteral("User denied the tool") }
            });
            const ToolResult denied = ToolResult::denied(QStringLiteral("User denied the tool"));
            publishToolOutcome(local, denied);
            return denied;
        }
        m_session->append(AgentEventType::ToolApproved, QJsonObject {
            { QStringLiteral("tool_call_id"), local.id },
            { QStringLiteral("name"), local.name }
        });
        setState(AgentRunState::ExecutingTools);
    }

    if (!bookTargetMatchesRun()) {
        const ToolResult changed = ToolResult::failure(
            QStringLiteral("BOOK_TARGET_CHANGED"),
            QStringLiteral("The open book changed before this tool could run."));
        publishToolOutcome(local, changed);
        return changed;
    }
    if (m_cancellation && m_cancellation->isCancelled()) {
        const ToolResult cancelled = ToolResult::cancelled();
        publishToolOutcome(local, cancelled);
        return cancelled;
    }

    setState(AgentRunState::ExecutingTools);
    m_session->append(AgentEventType::ToolStarted, QJsonObject {
        { QStringLiteral("tool_call_id"), local.id },
        { QStringLiteral("name"), local.name }
    });
    ToolResult result = tool->execute(arguments);
    publishToolOutcome(local, result);
    return result;
}

AgentRunResult AgentRunner::runTurn(const QString &user_text, const QStringList &handles)
{
    AgentRunResult result;
    if (!m_session || !m_provider || !m_tools) {
        result.state = AgentRunState::Failed;
        result.error = QStringLiteral("Agent runner is not fully wired");
        return result;
    }
    if (m_cancellation) m_cancellation->reset();
    m_runBookSessionId = m_workspace ? m_workspace->bookSessionId() : QString();

    setState(AgentRunState::PreparingContext);
    m_session->append(AgentEventType::UserMessage, QJsonObject {
        { QStringLiteral("text"), user_text },
        { QStringLiteral("handles"), QJsonArray::fromStringList(handles) }
    });
    if (m_workspace) {
        m_session->append(AgentEventType::ContextAttached, QJsonObject {
            { QStringLiteral("scope"), QStringLiteral("book structure + sampled fragments") },
            { QStringLiteral("book_session_id"), m_runBookSessionId },
            { QStringLiteral("book_revision"), static_cast<qint64>(m_workspace->revision()) }
        });
    }

    int steps = 0;
    while (steps < m_maxSteps) {
        ++steps;
        if (m_cancellation && m_cancellation->isCancelled()) {
            return cancelRun();
        }

        const ModelRequest request = m_prompts.build(
            *m_session, m_workspace, *m_tools, m_mode, m_model, m_thinking, m_effort, handles);
        const QString request_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_session->append(AgentEventType::ModelRequestStarted, QJsonObject {
            { QStringLiteral("request_id"), request_id },
            { QStringLiteral("session_id"), m_session->id() },
            { QStringLiteral("book_session_id"), m_runBookSessionId },
            { QStringLiteral("book_revision"), m_workspace
                  ? static_cast<qint64>(m_workspace->revision()) : 0 },
            { QStringLiteral("step"), steps },
            { QStringLiteral("model"), request.model },
            { QStringLiteral("mode"), modeName(m_mode) },
            { QStringLiteral("context_handles"), QJsonArray::fromStringList(handles) },
            { QStringLiteral("thinking"), request.thinking },
            { QStringLiteral("tools"), request.tools.size() }
        });
        setState(AgentRunState::RequestingModel);

        SessionSink sink(m_session, m_cancellation);
        setState(AgentRunState::StreamingResponse);
        QElapsedTimer request_timer;
        request_timer.start();
        ModelTurn turn = m_provider->stream(request, sink);
        const qint64 duration_ms = request_timer.elapsed();
        if (m_cancellation && m_cancellation->isCancelled()) {
            m_session->append(AgentEventType::ModelRequestCancelled, QJsonObject {
                { QStringLiteral("request_id"), request_id },
                { QStringLiteral("step"), steps },
                { QStringLiteral("model"), request.model },
                { QStringLiteral("duration_ms"), duration_ms }
            });
            return cancelRun();
        }
        if (!turn.error.isEmpty()) {
            setState(AgentRunState::Failed);
            m_session->append(AgentEventType::ModelRequestFailed, QJsonObject {
                { QStringLiteral("request_id"), request_id },
                { QStringLiteral("step"), steps },
                { QStringLiteral("model"), request.model },
                { QStringLiteral("duration_ms"), duration_ms },
                { QStringLiteral("message"), turn.error }
            });
            m_session->append(AgentEventType::Error, QJsonObject {
                { QStringLiteral("message"), turn.error }
            });
            result.state = AgentRunState::Failed;
            result.error = turn.error;
            rollbackOpenWork();
            return result;
        }

        m_session->append(AgentEventType::ModelRequestCompleted, QJsonObject {
            { QStringLiteral("request_id"), request_id },
            { QStringLiteral("step"), steps },
            { QStringLiteral("model"), request.model },
            { QStringLiteral("duration_ms"), duration_ms },
            { QStringLiteral("finish_reason"), turn.finishReason },
            { QStringLiteral("tool_calls"), turn.toolCalls.size() }
        });
        if (!bookTargetMatchesRun()) {
            return failBookTargetChanged(QStringLiteral("after_model_response"));
        }
        QJsonArray tool_calls_json;
        for (const ToolCall &call : turn.toolCalls) {
            tool_calls_json.append(toolCallToJson(call));
        }
        m_session->append(AgentEventType::AssistantMessage, QJsonObject {
            { QStringLiteral("content"), turn.content },
            { QStringLiteral("reasoning_content"), turn.reasoning },
            { QStringLiteral("tool_calls"), tool_calls_json },
            { QStringLiteral("is_final"), turn.toolCalls.isEmpty() }
        });
        result.finalText = turn.content;

        if (turn.toolCalls.isEmpty()) {
            setState(AgentRunState::Completed);
            result.state = AgentRunState::Completed;
            return result;
        }

        setState(AgentRunState::ExecutingTools);
        for (const ToolCall &call : turn.toolCalls) {
            if (!bookTargetMatchesRun()) {
                return failBookTargetChanged(QStringLiteral("before_tool"));
            }
            IAgentTool *resolved = m_tools->find(call.name);
            result.toolNames.append(resolved ? resolved->descriptor().name : call.name);
            if (m_cancellation && m_cancellation->isCancelled()) {
                publishToolOutcome(call, ToolResult::cancelled());
                continue;
            }
            executeTool(call);
            if (!bookTargetMatchesRun()) {
                return failBookTargetChanged(QStringLiteral("after_tool"));
            }
        }
        if (m_cancellation && m_cancellation->isCancelled()) {
            return cancelRun();
        }
    }

    rollbackOpenWork();
    setState(AgentRunState::Failed);
    result.state = AgentRunState::Failed;
    result.error = QStringLiteral("Exceeded max agent steps");
    m_session->append(AgentEventType::Error, QJsonObject {
        { QStringLiteral("message"), result.error }
    });
    return result;
}

} // namespace SigilAgent
