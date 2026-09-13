/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/Persistence/AgentSessionExport.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QRegularExpression>

namespace SigilAgent
{

namespace
{

QString isoTime(qint64 ms)
{
    if (ms <= 0) return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    return QDateTime::fromMSecsSinceEpoch(ms, Qt::UTC).toString(Qt::ISODate);
}

void flushAssistant(QStringList *lines, QString *thinking, QString *answer)
{
    if (!lines) return;
    if (thinking && !thinking->isEmpty()) {
        lines->append(QStringLiteral("## Thinking"));
        lines->append(*thinking);
        lines->append(QString());
        thinking->clear();
    }
    if (answer && !answer->isEmpty()) {
        lines->append(QStringLiteral("## Answer"));
        lines->append(*answer);
        lines->append(QString());
        answer->clear();
    }
}

void appendIndented(QStringList *lines, const QString &text)
{
    if (!lines) return;
    const QStringList source_lines = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    for (const QString &line : source_lines) {
        lines->append(QStringLiteral("    ") + line);
    }
}

void appendPlanReview(QStringList *lines, const QJsonObject &payload)
{
    if (!lines) return;
    const QString kind = payload.value(QStringLiteral("plan_kind")).toString();
    lines->append(kind == QLatin1String("paragraph_normalization")
                      ? QStringLiteral("## Plan review: Paragraph normalization")
                      : QStringLiteral("## Plan review: TOC hierarchy"));
    lines->append(QStringLiteral(
        "The live book is unchanged. Review this plan before approving its apply step."));
    if (kind == QLatin1String("paragraph_normalization")) {
        const QJsonObject summary = payload.value(QStringLiteral("summary")).toObject();
        lines->append(QStringLiteral(
            "- Files ready: %1; conversions: %2; protected items: %3")
                          .arg(summary.value(QStringLiteral("ready_files")).toInt())
                          .arg(summary.value(QStringLiteral("conversion_count")).toInt())
                          .arg(summary.value(QStringLiteral("protected_count")).toInt()));
        for (const QJsonValue &value : payload.value(QStringLiteral("changes")).toArray()) {
            const QJsonObject change = value.toObject();
            const QString path = change.value(QStringLiteral("book_path")).toString();
            lines->append(QStringLiteral("### %1").arg(
                path.isEmpty() ? change.value(QStringLiteral("resource_id")).toString() : path));
            lines->append(QStringLiteral("- Conversions: %1; protected items: %2")
                              .arg(change.value(QStringLiteral("conversion_count")).toInt())
                              .arg(change.value(QStringLiteral("protected_count")).toInt()));
            const QJsonObject diff = change.value(QStringLiteral("source_diff")).toObject();
            lines->append(QStringLiteral("Before excerpt:"));
            appendIndented(lines, diff.value(QStringLiteral("before")).toString());
            lines->append(QStringLiteral("After excerpt:"));
            appendIndented(lines, diff.value(QStringLiteral("after")).toString());
        }
    } else {
        lines->append(QStringLiteral("- Affected nodes: %1; adopted siblings: %2")
                          .arg(payload.value(QStringLiteral("affected_count")).toInt())
                          .arg(payload.value(QStringLiteral("adopted_count")).toInt()));
        for (const QJsonValue &value : payload.value(QStringLiteral("changes")).toArray()) {
            const QJsonObject change = value.toObject();
            lines->append(QStringLiteral("- %1 (%2): depth %3 → %4; parent %5 → %6")
                              .arg(change.value(QStringLiteral("label")).toString())
                              .arg(change.value(QStringLiteral("target")).toString())
                              .arg(change.value(QStringLiteral("from_depth")).toInt())
                              .arg(change.value(QStringLiteral("to_depth")).toInt())
                              .arg(change.value(QStringLiteral("from_parent_id")).toInteger())
                              .arg(change.value(QStringLiteral("to_parent_id")).toInteger()));
        }
        if (payload.value(QStringLiteral("changes_truncated")).toBool()) {
            lines->append(QStringLiteral(
                "- Additional TOC changes were omitted from this bounded review."));
        }
    }
    lines->append(QStringLiteral("- Local validation: %1")
                      .arg(payload.value(QStringLiteral("local_validation")).toString()));
    const QString epubcheck = payload.value(QStringLiteral("full_epubcheck")).toObject()
                                  .value(QStringLiteral("status")).toString();
    lines->append(epubcheck.isEmpty() || epubcheck == QLatin1String("not_run")
                      ? QStringLiteral("- Full EPUBCheck: not run.")
                      : QStringLiteral("- Full EPUBCheck: %1").arg(epubcheck));
    lines->append(QString());
}

} // namespace

QString redactSecrets(QString text, const QStringList &secrets)
{
    for (const QString &secret : secrets) {
        if (secret.size() >= 8) {
            text.replace(secret, QStringLiteral("[redacted]"));
        }
    }
    static const QRegularExpression key_re(QStringLiteral("sk-[A-Za-z0-9_-]{8,}"));
    text.replace(key_re, QStringLiteral("[redacted]"));
    static const QRegularExpression bearer_re(QStringLiteral("Bearer\\s+\\S+"),
                                              QRegularExpression::CaseInsensitiveOption);
    text.replace(bearer_re, QStringLiteral("Bearer [redacted]"));
    return text;
}

QJsonValue redactJsonValue(const QJsonValue &value, const QStringList &secrets)
{
    if (value.isString()) {
        return redactSecrets(value.toString(), secrets);
    }
    if (value.isObject()) {
        QJsonObject object;
        const QJsonObject source = value.toObject();
        for (auto it = source.begin(); it != source.end(); ++it) {
            const QString key = it.key().toLower();
            if (key.contains(QLatin1String("api_key"))
                || key == QLatin1String("authorization")
                || key == QLatin1String("token")) {
                object.insert(it.key(), QStringLiteral("[redacted]"));
                continue;
            }
            object.insert(it.key(), redactJsonValue(it.value(), secrets));
        }
        return object;
    }
    if (value.isArray()) {
        QJsonArray array;
        for (const QJsonValue &item : value.toArray()) {
            array.append(redactJsonValue(item, secrets));
        }
        return array;
    }
    return value;
}

QString exportConversationMarkdown(const AgentSession &session, const SessionExportContext &context)
{
    QStringList lines;
    lines.append(QStringLiteral("# Native Agent conversation"));
    lines.append(QString());
    lines.append(QStringLiteral("- Session: %1").arg(context.sessionId.isEmpty()
                                                         ? session.id()
                                                         : context.sessionId));
    lines.append(QStringLiteral("- Exported: %1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
    if (!context.provider.isEmpty()) {
        lines.append(QStringLiteral("- Provider: %1").arg(context.provider));
    }
    if (!context.model.isEmpty()) {
        lines.append(QStringLiteral("- Model: %1").arg(context.model));
    }
    if (!context.mode.isEmpty()) {
        lines.append(QStringLiteral("- Mode: %1").arg(context.mode));
    }
    lines.append(QString());

    QString thinking;
    QString answer;
    for (const AgentEvent &event : session.events()) {
        switch (event.type) {
            case AgentEventType::UserMessage:
                flushAssistant(&lines, &thinking, &answer);
                lines.append(QStringLiteral("## You"));
                lines.append(redactSecrets(event.payload.value(QStringLiteral("text")).toString(),
                                           context.secrets));
                lines.append(QString());
                break;
            case AgentEventType::AssistantDelta: {
                const QString kind = event.payload.value(QStringLiteral("kind")).toString();
                const QString text = event.payload.value(QStringLiteral("text")).toString();
                if (kind == QLatin1String("reasoning")) thinking += text;
                else answer += text;
                break;
            }
            case AgentEventType::AssistantMessage: {
                const QString stored_thinking = event.payload.value(QStringLiteral("reasoning_content")).toString();
                const QString stored_answer = event.payload.value(QStringLiteral("content")).toString();
                if (thinking.isEmpty()) thinking = stored_thinking;
                if (answer.isEmpty()) answer = stored_answer;
                flushAssistant(&lines, &thinking, &answer);
                break;
            }
            case AgentEventType::ToolRequested:
            case AgentEventType::ToolStarted:
            case AgentEventType::ToolCompleted:
            case AgentEventType::ToolFailed:
            case AgentEventType::ToolRejected: {
                const QString name = event.payload.value(QStringLiteral("name")).toString();
                const bool summarized_by_plan_event =
                    event.type == AgentEventType::ToolCompleted
                    && (name == QLatin1String("paragraphs.plan")
                        || name == QLatin1String("toc.plan_transform"));
                if (summarized_by_plan_event) break;
                flushAssistant(&lines, &thinking, &answer);
                QString heading = QStringLiteral("## Tool: %1").arg(name);
                if (event.type == AgentEventType::ToolFailed) {
                    heading = QStringLiteral("## Tool failed: %1").arg(name);
                } else if (event.type == AgentEventType::ToolRejected) {
                    heading = QStringLiteral("## Tool denied: %1").arg(name);
                }
                lines.append(heading);
                if (event.payload.contains(QStringLiteral("arguments"))) {
                    const QByteArray json = QJsonDocument(event.payload.value(QStringLiteral("arguments")).toObject())
                                                .toJson(QJsonDocument::Indented);
                    lines.append(QStringLiteral("Arguments:"));
                    lines.append(QStringLiteral("```json"));
                    lines.append(QString::fromUtf8(json).trimmed());
                    lines.append(QStringLiteral("```"));
                }
                if (event.payload.contains(QStringLiteral("message"))) {
                    lines.append(redactSecrets(event.payload.value(QStringLiteral("message")).toString(),
                                               context.secrets));
                } else if (event.payload.contains(QStringLiteral("data"))) {
                    const QByteArray json = QJsonDocument(event.payload.value(QStringLiteral("data")).toObject())
                                                .toJson(QJsonDocument::Indented);
                    lines.append(QStringLiteral("```json"));
                    lines.append(redactSecrets(QString::fromUtf8(json).trimmed(), context.secrets));
                    lines.append(QStringLiteral("```"));
                } else if (event.payload.contains(QStringLiteral("reason"))) {
                    lines.append(event.payload.value(QStringLiteral("reason")).toString());
                }
                lines.append(QString());
                break;
            }
            case AgentEventType::Error:
                flushAssistant(&lines, &thinking, &answer);
                lines.append(QStringLiteral("## Error"));
                lines.append(redactSecrets(event.payload.value(QStringLiteral("message")).toString(),
                                           context.secrets));
                lines.append(QString());
                break;
            case AgentEventType::SessionCancelled:
                flushAssistant(&lines, &thinking, &answer);
                lines.append(QStringLiteral("## Stopped"));
                lines.append(QStringLiteral("Run stopped."));
                lines.append(QString());
                break;
            case AgentEventType::PlanCreated:
                flushAssistant(&lines, &thinking, &answer);
                appendPlanReview(&lines, event.payload);
                break;
            case AgentEventType::TransactionPreviewed:
                flushAssistant(&lines, &thinking, &answer);
                lines.append(QStringLiteral("## Preview"));
                lines.append(QStringLiteral("Staged only. The live book was unchanged."));
                lines.append(QString());
                break;
            case AgentEventType::TransactionCommitted: {
                flushAssistant(&lines, &thinking, &answer);
                lines.append(QStringLiteral("## Applied"));
                lines.append(QStringLiteral("Applied to the current book. The EPUB file has not been saved."));
                if (event.payload.value(QStringLiteral("applied_changes")).isDouble()) {
                    lines.append(QStringLiteral("Applied changes: %1")
                                     .arg(event.payload.value(QStringLiteral("applied_changes")).toInt()));
                }
                if (event.payload.value(QStringLiteral("book_revision")).isDouble()) {
                    lines.append(QStringLiteral("Book revision: %1")
                                     .arg(event.payload.value(QStringLiteral("book_revision")).toInteger()));
                }
                const QString epubcheck_status =
                    event.payload.value(QStringLiteral("full_epubcheck")).toObject()
                        .value(QStringLiteral("status")).toString();
                lines.append(epubcheck_status.isEmpty() || epubcheck_status == QLatin1String("not_run")
                                 ? QStringLiteral("Full EPUBCheck: not run.")
                                 : QStringLiteral("Full EPUBCheck: %1").arg(epubcheck_status));
                lines.append(QStringLiteral("Recovery: use Sigil Undo where available."));
                const QJsonObject recovery =
                    event.payload.value(QStringLiteral("recovery")).toObject();
                const QString restore_status = recovery
                    .value(QStringLiteral("task_restore_point")).toString();
                if (restore_status == QLatin1String("available")) {
                    lines.append(QStringLiteral(
                        "A conflict-checked task restore point is available for %1 text resource(s).")
                                     .arg(recovery.value(QStringLiteral("affected_resources"))
                                              .toArray().size()));
                } else if (restore_status == QLatin1String("unavailable")
                           && recovery.value(QStringLiteral("reason")).toString()
                               == QLatin1String("structural_changes")) {
                    lines.append(QStringLiteral(
                        "A task restore point was not created because this commit changed book structure."));
                } else if (restore_status == QLatin1String("unavailable")) {
                    lines.append(QStringLiteral(
                        "A task restore point could not be created for this commit."));
                } else if (restore_status == QLatin1String("not_created_by_commit")) {
                    lines.append(QStringLiteral("This commit did not create a task-wide restore point."));
                }
                lines.append(QString());
                break;
            }
            case AgentEventType::TaskRestoreCompleted:
                flushAssistant(&lines, &thinking, &answer);
                lines.append(QStringLiteral("## Task restored"));
                lines.append(QStringLiteral("Restored %1 text resource(s). Later unrelated edits were preserved.")
                                 .arg(event.payload.value(QStringLiteral("affected_resources"))
                                          .toArray().size()));
                lines.append(QString());
                break;
            case AgentEventType::TaskRestoreFailed:
                flushAssistant(&lines, &thinking, &answer);
                lines.append(QStringLiteral("## Restore blocked"));
                lines.append(event.payload.value(QStringLiteral("code")).toString()
                                     == QLatin1String("TASK_RESTORE_CONFLICT")
                                 ? QStringLiteral("Affected resources changed after the task. No book content was changed.")
                                 : event.payload.value(QStringLiteral("message")).toString());
                lines.append(QString());
                break;
            case AgentEventType::TransactionRolledBack:
                flushAssistant(&lines, &thinking, &answer);
                lines.append(event.payload.value(QStringLiteral("rolled_back")).toBool()
                                 ? QStringLiteral("## Staged changes discarded")
                                 : QStringLiteral("## No staged changes"));
                lines.append(event.payload.value(QStringLiteral("rolled_back")).toBool()
                                 ? QStringLiteral("The staged transaction was discarded. The live book was not changed by this transaction.")
                                 : QStringLiteral("There was no staged transaction to discard. The live book was not changed."));
                lines.append(QString());
                break;
            default:
                break;
        }
    }
    flushAssistant(&lines, &thinking, &answer);
    return redactSecrets(lines.join(QLatin1Char('\n')), context.secrets);
}

QByteArray exportDebugJson(const AgentSession &session, const SessionExportContext &context)
{
    QJsonArray events;
    QJsonObject counts;
    int omitted_deltas = 0;
    for (const AgentEvent &event : session.events()) {
        const QString type = eventTypeName(event.type);
        counts.insert(type, counts.value(type).toInt() + 1);
        if (event.type == AgentEventType::AssistantDelta) {
            ++omitted_deltas;
            continue;
        }
        events.append(QJsonObject {
            { QStringLiteral("id"), event.id },
            { QStringLiteral("type"), type },
            { QStringLiteral("timestamp"), isoTime(event.timestampMs) },
            { QStringLiteral("timestamp_ms"), event.timestampMs },
            { QStringLiteral("payload"), redactJsonValue(event.payload, context.secrets) }
        });
    }

    QJsonObject provider {
        { QStringLiteral("kind"), context.provider },
        { QStringLiteral("chat_url"), redactSecrets(context.chatUrl, context.secrets) },
        { QStringLiteral("models_url"), redactSecrets(context.modelsUrl, context.secrets) },
        { QStringLiteral("model"), context.model },
        { QStringLiteral("thinking"), context.thinking },
        { QStringLiteral("reasoning_effort"), context.reasoningEffort },
        { QStringLiteral("api_key_present"), context.apiKeyPresent }
    };

    QJsonObject root {
        { QStringLiteral("format"), QStringLiteral("sigil-native-agent-debug-v1") },
        { QStringLiteral("exported_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate) },
        { QStringLiteral("session_id"), context.sessionId.isEmpty() ? session.id() : context.sessionId },
        { QStringLiteral("mode"), context.mode },
        { QStringLiteral("run_state"), context.runState },
        { QStringLiteral("provider"), provider },
        { QStringLiteral("http_traces"), redactJsonValue(context.httpTraces, context.secrets) },
        { QStringLiteral("event_counts"), counts },
        { QStringLiteral("assistant_delta_omitted"), omitted_deltas },
        { QStringLiteral("events"), events }
    };
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

} // namespace SigilAgent
