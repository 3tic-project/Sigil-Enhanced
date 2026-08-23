/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/Core/AgentRunner.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

#include "Agent/Tools/BookTools.h"

namespace SigilAgent
{

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
    if (m_workspace && m_workspace->hasOpenTransaction()) {
        const BookOpResult rolled = m_workspace->rollbackTransaction();
        if (m_session) {
            m_session->append(AgentEventType::TransactionRolledBack, rolled.data);
        }
    }
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
            m_session->append(AgentEventType::TransactionPreviewed, result.data);
        } else if (call.name == QLatin1String("transaction.commit") && result.applied) {
            m_session->append(AgentEventType::TransactionCommitted, result.data);
        } else if (call.name == QLatin1String("transaction.rollback")) {
            m_session->append(AgentEventType::TransactionRolledBack, result.data);
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
    if (!tool) {
        const ToolResult missing = ToolResult::failure(QStringLiteral("UNKNOWN_TOOL"),
                                                       QStringLiteral("Unknown tool %1").arg(call.name));
        publishToolOutcome(call, missing);
        return missing;
    }
    const AgentToolDescriptor descriptor = tool->descriptor();
    const QJsonObject arguments = parseArguments(call.argumentsJson);
    const PermissionAction permission = m_policy
        ? m_policy->evaluate(m_mode, descriptor) : PermissionAction::Deny;

    m_session->append(AgentEventType::ToolRequested, QJsonObject {
        { QStringLiteral("tool_call_id"), call.id },
        { QStringLiteral("name"), call.name },
        { QStringLiteral("arguments"), arguments },
        { QStringLiteral("risk"), toolRiskName(descriptor.risk) }
    });

    if (permission == PermissionAction::Deny) {
        const QString reason = m_policy->denyReason(m_mode, descriptor);
        m_session->append(AgentEventType::ToolRejected, QJsonObject {
            { QStringLiteral("tool_call_id"), call.id },
            { QStringLiteral("name"), call.name },
            { QStringLiteral("reason"), reason }
        });
        const ToolResult denied = ToolResult::denied(reason);
        publishToolOutcome(call, denied);
        return denied;
    }

    if (permission == PermissionAction::Ask) {
        const QString impact = humanReadableImpact(call.name, arguments);
        setState(AgentRunState::AwaitingApproval);
        m_session->append(AgentEventType::ToolApprovalRequested, QJsonObject {
            { QStringLiteral("tool_call_id"), call.id },
            { QStringLiteral("name"), call.name },
            { QStringLiteral("impact"), impact },
            { QStringLiteral("arguments"), arguments }
        });
        const bool approved = m_gate && m_gate->waitForApproval(call.id, call.name, arguments, impact);
        if (m_cancellation && m_cancellation->isCancelled()) {
            const ToolResult cancelled = ToolResult::cancelled();
            publishToolOutcome(call, cancelled);
            return cancelled;
        }
        if (!approved) {
            m_session->append(AgentEventType::ToolRejected, QJsonObject {
                { QStringLiteral("tool_call_id"), call.id },
                { QStringLiteral("name"), call.name },
                { QStringLiteral("reason"), QStringLiteral("User denied the tool") }
            });
            const ToolResult denied = ToolResult::denied(QStringLiteral("User denied the tool"));
            publishToolOutcome(call, denied);
            return denied;
        }
        m_session->append(AgentEventType::ToolApproved, QJsonObject {
            { QStringLiteral("tool_call_id"), call.id },
            { QStringLiteral("name"), call.name }
        });
        setState(AgentRunState::ExecutingTools);
    }

    if (m_cancellation && m_cancellation->isCancelled()) {
        const ToolResult cancelled = ToolResult::cancelled();
        publishToolOutcome(call, cancelled);
        return cancelled;
    }

    setState(AgentRunState::ExecutingTools);
    m_session->append(AgentEventType::ToolStarted, QJsonObject {
        { QStringLiteral("tool_call_id"), call.id },
        { QStringLiteral("name"), call.name }
    });
    ToolResult result = tool->execute(arguments);
    publishToolOutcome(call, result);
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

    setState(AgentRunState::PreparingContext);
    m_session->append(AgentEventType::UserMessage, QJsonObject {
        { QStringLiteral("text"), user_text },
        { QStringLiteral("handles"), QJsonArray::fromStringList(handles) }
    });
    if (m_workspace) {
        m_session->append(AgentEventType::ContextAttached, QJsonObject {
            { QStringLiteral("scope"), QStringLiteral("book structure + sampled fragments") },
            { QStringLiteral("book_revision"), static_cast<qint64>(m_workspace->revision()) }
        });
    }

    int steps = 0;
    while (steps < m_maxSteps) {
        ++steps;
        if (m_cancellation && m_cancellation->isCancelled()) {
            rollbackOpenWork();
            setState(AgentRunState::Cancelled);
            m_session->append(AgentEventType::SessionCancelled, QJsonObject {
                { QStringLiteral("reason"), QStringLiteral("stop") }
            });
            result.state = AgentRunState::Cancelled;
            return result;
        }

        const ModelRequest request = m_prompts.build(
            *m_session, m_workspace, *m_tools, m_mode, m_model, m_thinking, m_effort, handles);
        m_session->append(AgentEventType::ModelRequestStarted, QJsonObject {
            { QStringLiteral("step"), steps },
            { QStringLiteral("model"), request.model },
            { QStringLiteral("thinking"), request.thinking },
            { QStringLiteral("tools"), request.tools.size() }
        });
        setState(AgentRunState::RequestingModel);

        SessionSink sink(m_session, m_cancellation);
        setState(AgentRunState::StreamingResponse);
        ModelTurn turn = m_provider->stream(request, sink);
        if (m_cancellation && m_cancellation->isCancelled()) {
            rollbackOpenWork();
            setState(AgentRunState::Cancelled);
            m_session->append(AgentEventType::SessionCancelled, QJsonObject {
                { QStringLiteral("reason"), QStringLiteral("stop") }
            });
            result.state = AgentRunState::Cancelled;
            return result;
        }
        if (!turn.error.isEmpty()) {
            setState(AgentRunState::Failed);
            m_session->append(AgentEventType::Error, QJsonObject {
                { QStringLiteral("message"), turn.error }
            });
            result.state = AgentRunState::Failed;
            result.error = turn.error;
            rollbackOpenWork();
            return result;
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
            result.toolNames.append(call.name);
            if (m_cancellation && m_cancellation->isCancelled()) {
                publishToolOutcome(call, ToolResult::cancelled());
                continue;
            }
            executeTool(call);
        }
        if (m_cancellation && m_cancellation->isCancelled()) {
            rollbackOpenWork();
            setState(AgentRunState::Cancelled);
            m_session->append(AgentEventType::SessionCancelled, QJsonObject {
                { QStringLiteral("reason"), QStringLiteral("stop") }
            });
            result.state = AgentRunState::Cancelled;
            return result;
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
