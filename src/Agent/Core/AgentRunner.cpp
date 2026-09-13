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

QString reviewablePlanKind(const QString &tool_name)
{
    if (tool_name == QLatin1String("paragraphs.plan")) {
        return QStringLiteral("paragraph_normalization");
    }
    if (tool_name == QLatin1String("toc.plan_transform")) {
        return QStringLiteral("toc_hierarchy");
    }
    return QString();
}

QJsonObject planStatus(const ToolCall &call, const ToolResult &result)
{
    const QString kind = reviewablePlanKind(call.name);
    if (kind.isEmpty() || !result.ok || result.applied || !result.previewOnly
        || result.data.value(QStringLiteral("plan_id")).toString().isEmpty()
        || result.data.value(QStringLiteral("plan_digest")).toString().isEmpty()) {
        return QJsonObject();
    }
    QJsonObject payload = result.data;
    payload.insert(QStringLiteral("tool_call_id"), call.id);
    payload.insert(QStringLiteral("name"), call.name);
    payload.insert(QStringLiteral("plan_kind"), kind);
    payload.insert(QStringLiteral("review_status"), QStringLiteral("ready"));
    payload.insert(QStringLiteral("applied_to_book"), false);
    return payload;
}

QStringList taskRestoreResources(const QJsonObject &preview, QString *unavailable_reason)
{
    const bool structural = preview.value(QStringLiteral("metadata_changed")).toBool()
        || preview.value(QStringLiteral("spine_changed")).toBool()
        || preview.value(QStringLiteral("toc_changed")).toBool()
        || !preview.value(QStringLiteral("removed")).toArray().isEmpty();
    QStringList resources;
    bool has_structural_change = structural;
    for (const QJsonValue &value : preview.value(QStringLiteral("changes")).toArray()) {
        const QJsonObject change = value.toObject();
        if (change.value(QStringLiteral("added")).toBool()
            || change.value(QStringLiteral("renamed")).toBool()) {
            has_structural_change = true;
        } else if (change.value(QStringLiteral("changed")).toBool()) {
            const QString id = change.value(QStringLiteral("resource_id")).toString();
            if (!id.isEmpty() && !resources.contains(id)) resources.append(id);
        }
    }
    if (has_structural_change) {
        if (unavailable_reason) *unavailable_reason = QStringLiteral("structural_changes");
        return QStringList();
    }
    if (resources.isEmpty() && unavailable_reason) {
        *unavailable_reason = QStringLiteral("no_text_changes");
    }
    return resources;
}

QJsonObject unavailableTaskRecovery(const QString &reason)
{
    return QJsonObject {
        { QStringLiteral("sigil_undo"), QStringLiteral("where_available") },
        { QStringLiteral("task_restore_point"), QStringLiteral("unavailable") },
        { QStringLiteral("reason"), reason }
    };
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

void AgentRunner::setTokenUsage(bool enabled)
{
    m_tokenUsage = enabled;
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
        QJsonObject payload {
            { QStringLiteral("state"), runStateName(state) }
        };
        if (m_runTimingActive) {
            payload.insert(QStringLiteral("run_id"), m_runId);
            payload.insert(QStringLiteral("book_session_id"), m_runBookSessionId);
            payload.insert(QStringLiteral("usage_requested"), m_runUsageRequested);
            const bool terminal = state == AgentRunState::Completed
                || state == AgentRunState::Cancelled
                || state == AgentRunState::Failed;
            if (terminal) {
                payload.insert(QStringLiteral("duration_ms"), m_runTimer.elapsed());
                payload.insert(QStringLiteral("model_steps"), m_runModelSteps);
                payload.insert(QStringLiteral("tool_calls"), m_runToolCalls);
                payload.insert(QStringLiteral("usage_summary"), runUsageSummary());
                m_runTimingActive = false;
            }
        }
        m_session->append(AgentEventType::RunStateChanged, payload);
    }
}

void AgentRunner::accumulateRunUsage(const ModelUsage &usage)
{
    if (!usage.isReported()) return;
    ++m_runUsageReportedRequests;
    const auto add_count = [](qint64 value, qint64 *total, int *requests) {
        if (value < 0) return;
        if (*requests == 0) *total = 0;
        *total += value;
        ++(*requests);
    };
    add_count(usage.inputTokens, &m_runUsage.inputTokens, &m_runInputUsageRequests);
    add_count(usage.outputTokens, &m_runUsage.outputTokens, &m_runOutputUsageRequests);
    add_count(usage.totalTokens, &m_runUsage.totalTokens, &m_runTotalUsageRequests);
    add_count(usage.cachedInputTokens, &m_runUsage.cachedInputTokens,
              &m_runCachedUsageRequests);
    add_count(usage.reasoningTokens, &m_runUsage.reasoningTokens,
              &m_runReasoningUsageRequests);
}

QJsonObject AgentRunner::runUsageSummary() const
{
    QJsonObject summary {
        { QStringLiteral("request_count"), m_runModelSteps },
        { QStringLiteral("reported_request_count"), m_runUsageReportedRequests },
        { QStringLiteral("missing_request_count"),
          qMax(0, m_runModelSteps - m_runUsageReportedRequests) },
        { QStringLiteral("all_requests_reported"),
          m_runModelSteps > 0 && m_runUsageReportedRequests == m_runModelSteps }
    };
    const auto insert_count = [&summary](const QString &token_key,
                                         const QString &coverage_key,
                                         qint64 value,
                                         int requests) {
        if (requests <= 0 || value < 0) return;
        summary.insert(token_key, value);
        summary.insert(coverage_key, requests);
    };
    insert_count(QStringLiteral("input_tokens"),
                 QStringLiteral("input_request_count"),
                 m_runUsage.inputTokens, m_runInputUsageRequests);
    insert_count(QStringLiteral("output_tokens"),
                 QStringLiteral("output_request_count"),
                 m_runUsage.outputTokens, m_runOutputUsageRequests);
    insert_count(QStringLiteral("total_tokens"),
                 QStringLiteral("total_request_count"),
                 m_runUsage.totalTokens, m_runTotalUsageRequests);
    insert_count(QStringLiteral("cached_input_tokens"),
                 QStringLiteral("cached_input_request_count"),
                 m_runUsage.cachedInputTokens, m_runCachedUsageRequests);
    insert_count(QStringLiteral("reasoning_tokens"),
                 QStringLiteral("reasoning_request_count"),
                 m_runUsage.reasoningTokens, m_runReasoningUsageRequests);
    return summary;
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
    setState(AgentRunState::Cancelled);
    return result;
}

AgentRunResult AgentRunner::failBookTargetChanged(const QString &stage)
{
    const QString actual = m_workspace ? m_workspace->bookSessionId() : QString();
    const QString message = QStringLiteral(
        "The open book changed during this Agent run. The old response was not applied to the new book.");
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
    setState(AgentRunState::Failed);
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
        const QJsonObject plan_status = planStatus(call, result);
        if (!plan_status.isEmpty()) {
            m_session->append(AgentEventType::PlanCreated, plan_status);
        }
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
    QString task_restore_id;
    QJsonObject task_recovery;
    if (local.name == QLatin1String("transaction.commit") && m_workspace) {
        const BookOpResult preview = m_workspace->previewTransaction();
        if (!preview.ok) {
            task_recovery = unavailableTaskRecovery(QStringLiteral("preview_failed"));
        } else {
            QString reason;
            const QStringList resources = taskRestoreResources(preview.data, &reason);
            if (resources.isEmpty()) {
                task_recovery = unavailableTaskRecovery(reason);
            } else {
                const QString label = QStringLiteral("Agent transaction %1")
                    .arg(preview.data.value(QStringLiteral("transaction_id")).toString());
                const BookOpResult created =
                    m_workspace->createTaskRestorePoint(label, resources);
                if (created.ok) {
                    task_restore_id = created.data
                        .value(QStringLiteral("checkpoint_id")).toString();
                } else {
                    task_recovery = unavailableTaskRecovery(
                        QStringLiteral("checkpoint_create_failed"));
                    task_recovery.insert(QStringLiteral("code"), created.code);
                }
            }
        }
    }

    ToolResult result = tool->execute(arguments);
    if (local.name == QLatin1String("transaction.commit") && m_workspace) {
        if (result.ok && result.applied && !task_restore_id.isEmpty()) {
            const BookOpResult sealed = m_workspace->sealTaskRestorePoint(task_restore_id);
            if (sealed.ok) {
                task_recovery = QJsonObject {
                    { QStringLiteral("sigil_undo"), QStringLiteral("where_available") },
                    { QStringLiteral("task_restore_point"), QStringLiteral("available") },
                    { QStringLiteral("checkpoint_id"), task_restore_id },
                    { QStringLiteral("book_session_id"), m_runBookSessionId },
                    { QStringLiteral("affected_resources"),
                      sealed.data.value(QStringLiteral("affected_resources")) },
                    { QStringLiteral("conflict_guard"),
                      QStringLiteral("post_commit_text_and_path") }
                };
            } else {
                m_workspace->discardTaskRestorePoint(task_restore_id);
                task_recovery = unavailableTaskRecovery(QStringLiteral("checkpoint_seal_failed"));
                task_recovery.insert(QStringLiteral("code"), sealed.code);
            }
        } else if (!task_restore_id.isEmpty()) {
            m_workspace->discardTaskRestorePoint(task_restore_id);
        }
        if (result.ok && result.applied) {
            if (task_recovery.isEmpty()) {
                task_recovery = unavailableTaskRecovery(QStringLiteral("not_created_by_commit"));
            }
            result.data.insert(QStringLiteral("recovery"), task_recovery);
        }
    }
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
    m_runId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_runModelSteps = 0;
    m_runToolCalls = 0;
    m_runUsage = ModelUsage();
    m_runUsageReportedRequests = 0;
    m_runInputUsageRequests = 0;
    m_runOutputUsageRequests = 0;
    m_runTotalUsageRequests = 0;
    m_runCachedUsageRequests = 0;
    m_runReasoningUsageRequests = 0;
    m_runUsageRequested = m_tokenUsage;
    m_runTimer.start();
    m_runTimingActive = true;

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

        ModelRequest request = m_prompts.build(
            *m_session, m_workspace, *m_tools, m_mode, m_model, m_thinking, m_effort, handles);
        request.includeUsage = m_runUsageRequested;
        ++m_runModelSteps;
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
            { QStringLiteral("usage_requested"), request.includeUsage },
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
            QJsonObject cancelled_payload {
                { QStringLiteral("request_id"), request_id },
                { QStringLiteral("step"), steps },
                { QStringLiteral("model"), request.model },
                { QStringLiteral("duration_ms"), duration_ms }
            };
            if (turn.timing.isReported()) {
                cancelled_payload.insert(
                    QStringLiteral("response_timing"),
                    modelResponseTimingToJson(turn.timing));
            }
            m_session->append(AgentEventType::ModelRequestCancelled, cancelled_payload);
            return cancelRun();
        }
        if (!turn.error.isEmpty()) {
            QJsonObject failed_payload {
                { QStringLiteral("request_id"), request_id },
                { QStringLiteral("step"), steps },
                { QStringLiteral("model"), request.model },
                { QStringLiteral("duration_ms"), duration_ms },
                { QStringLiteral("message"), turn.error }
            };
            if (turn.timing.isReported()) {
                failed_payload.insert(
                    QStringLiteral("response_timing"),
                    modelResponseTimingToJson(turn.timing));
            }
            m_session->append(AgentEventType::ModelRequestFailed, failed_payload);
            m_session->append(AgentEventType::Error, QJsonObject {
                { QStringLiteral("message"), turn.error }
            });
            result.state = AgentRunState::Failed;
            result.error = turn.error;
            rollbackOpenWork();
            setState(AgentRunState::Failed);
            return result;
        }

        accumulateRunUsage(turn.usage);

        QJsonObject completed_payload {
            { QStringLiteral("request_id"), request_id },
            { QStringLiteral("step"), steps },
            { QStringLiteral("model"), request.model },
            { QStringLiteral("duration_ms"), duration_ms },
            { QStringLiteral("finish_reason"), turn.finishReason },
            { QStringLiteral("tool_calls"), turn.toolCalls.size() }
        };
        if (turn.usage.isReported()) {
            completed_payload.insert(QStringLiteral("usage"), modelUsageToJson(turn.usage));
        }
        if (turn.timing.isReported()) {
            completed_payload.insert(
                QStringLiteral("response_timing"),
                modelResponseTimingToJson(turn.timing));
        }
        m_session->append(AgentEventType::ModelRequestCompleted, completed_payload);
        if (!bookTargetMatchesRun()) {
            return failBookTargetChanged(QStringLiteral("after_model_response"));
        }
        QJsonArray tool_calls_json;
        m_runToolCalls += turn.toolCalls.size();
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
    result.state = AgentRunState::Failed;
    result.error = QStringLiteral("Exceeded max agent steps");
    m_session->append(AgentEventType::Error, QJsonObject {
        { QStringLiteral("message"), result.error }
    });
    setState(AgentRunState::Failed);
    return result;
}

} // namespace SigilAgent
