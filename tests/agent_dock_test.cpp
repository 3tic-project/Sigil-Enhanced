#include <cstdlib>
#include <iostream>

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>

#include "Agent/UI/AgentDock.h"

namespace
{

void Require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void ProcessEventsFor(int milliseconds)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

} // namespace

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    QApplication application(argc, argv);
    SigilAgent::AgentDock dock;
    dock.setSessionId(QStringLiteral("session-full-id"));
    dock.show();
    application.processEvents();

    auto *mode = dock.findChild<QComboBox *>(QStringLiteral("agentModeCombo"));
    auto *stop = dock.findChild<QPushButton *>(QStringLiteral("agentStopButton"));
    auto *fresh = dock.findChild<QPushButton *>(QStringLiteral("agentNewSessionButton"));
    auto *composer = dock.findChild<QPlainTextEdit *>(QStringLiteral("agentComposer"));
    auto *transcript = dock.findChild<QScrollArea *>(QStringLiteral("agentTranscript"));
    Require(!dock.findChild<QLineEdit *>(QStringLiteral("agentModelEdit")),
            "Agent dock must not let the user type a model name");
    Require(dock.findChild<QLabel *>(QStringLiteral("agentModelLabel")),
            "Agent dock must show the model chosen in Preferences");
    Require(dock.findChild<QToolButton *>(QStringLiteral("agentExportButton")),
            "Agent dock must offer export");
    dock.setModelName(QStringLiteral("deepseek-chat"));
    auto *model_label = dock.findChild<QLabel *>(QStringLiteral("agentModelLabel"));
    Require(model_label && model_label->text().contains(QStringLiteral("deepseek-chat")),
            "dock model label must show the settings model");
    auto *provider_status = dock.findChild<QLabel *>(QStringLiteral("agentProviderStatus"));
    Require(provider_status, "dock must expose provider setup and request status");
    SigilAgent::AgentProviderReadiness setup_required;
    setup_required.kind = SigilAgent::AgentProviderKind::DeepSeek;
    setup_required.displayName = QStringLiteral("DeepSeek");
    setup_required.model = QStringLiteral("deepseek-chat");
    setup_required.endpointHost = QStringLiteral("api.deepseek.com");
    setup_required.issue = SigilAgent::AgentProviderSetupIssue::ApiKey;
    dock.setProviderConfiguration(setup_required);
    Require(provider_status->text().contains(QStringLiteral("Setup required: API key"))
                && provider_status->property("requestState").toString()
                    == QStringLiteral("setup_required"),
            "missing provider settings must be explicit without claiming connectivity");

    SigilAgent::AgentProviderReadiness configured = setup_required;
    configured.issue = SigilAgent::AgentProviderSetupIssue::None;
    dock.setProviderConfiguration(configured);
    Require(provider_status->text().contains(QStringLiteral("DeepSeek"))
                && provider_status->text().contains(QStringLiteral("deepseek-chat"))
                && provider_status->text().contains(QStringLiteral("api.deepseek.com"))
                && provider_status->text().contains(QStringLiteral("Configured · not tested"))
                && provider_status->property("requestState").toString()
                    == QStringLiteral("configured"),
            "complete settings must say configured but not tested");
    configured.verifiedAtMs = 1700000000000;
    dock.setProviderConfiguration(configured);
    Require(provider_status->text().contains(QStringLiteral("Chat tested successfully"))
                && provider_status->property("requestState").toString()
                    == QStringLiteral("verified")
                && provider_status->property("connectionVerifiedAtMs").toLongLong()
                    == 1700000000000,
            "a matching saved probe must be shown as historical verification");
    SigilAgent::AgentEvent run_started;
    run_started.type = SigilAgent::AgentEventType::RunStateChanged;
    run_started.timestampMs = 1699999999900;
    run_started.payload = QJsonObject {
        { QStringLiteral("run_id"), QStringLiteral("run-full-id") },
        { QStringLiteral("state"), QStringLiteral("preparing_context") },
        { QStringLiteral("book_session_id"), QStringLiteral("request-book-id") },
        { QStringLiteral("usage_requested"), true }
    };
    dock.appendEvent(run_started);
    auto *run_details =
        dock.findChild<QLabel *>(QStringLiteral("agentTechnicalDetails"));
    Require(run_details
                && run_details->text().contains(QStringLiteral("Whole run: in progress"))
                && run_details->text().contains(
                    QStringLiteral("Run token usage: awaiting completed requests"))
                && run_details->property("runId").toString()
                    == QStringLiteral("run-full-id")
                && run_details->property("runDurationMs").toLongLong() == -1,
            "a preparing run must expose its identity without inventing a final duration");
    SigilAgent::AgentEvent provider_started;
    provider_started.type = SigilAgent::AgentEventType::ModelRequestStarted;
    provider_started.payload = QJsonObject {
        { QStringLiteral("request_id"), QStringLiteral("request-full-id") },
        { QStringLiteral("session_id"), QStringLiteral("session-full-id") },
        { QStringLiteral("book_session_id"), QStringLiteral("request-book-id") },
        { QStringLiteral("book_revision"), 6 },
        { QStringLiteral("step"), 1 },
        { QStringLiteral("model"), QStringLiteral("deepseek-chat") },
        { QStringLiteral("mode"), QStringLiteral("ask") },
        { QStringLiteral("usage_requested"), true },
        { QStringLiteral("context_handles"), QJsonArray {
              QStringLiteral("chapter-1:12-34"), QStringLiteral("book-css") } }
    };
    dock.appendEvent(provider_started);
    Require(provider_status->text().contains(QStringLiteral("Contacting provider"))
                && provider_status->property("requestState").toString()
                    == QStringLiteral("requesting"),
            "a real model request must move provider status to requesting");
    SigilAgent::AgentEvent provider_completed;
    provider_completed.type = SigilAgent::AgentEventType::ModelRequestCompleted;
    provider_completed.timestampMs = 1700000000000;
    provider_completed.payload = QJsonObject {
        { QStringLiteral("request_id"), QStringLiteral("request-full-id") },
        { QStringLiteral("step"), 1 },
        { QStringLiteral("model"), QStringLiteral("deepseek-chat") },
        { QStringLiteral("duration_ms"), 27 },
        { QStringLiteral("response_timing"), QJsonObject {
              { QStringLiteral("first_byte_ms"), 9 },
              { QStringLiteral("first_model_event_ms"), 14 } } },
        { QStringLiteral("usage"), QJsonObject {
              { QStringLiteral("input_tokens"), 120 },
              { QStringLiteral("output_tokens"), 35 },
              { QStringLiteral("total_tokens"), 155 },
              { QStringLiteral("cached_input_tokens"), 80 },
              { QStringLiteral("reasoning_tokens"), 12 } } }
    };
    dock.appendEvent(provider_completed);
    Require(provider_status->text().contains(QStringLiteral("Last request succeeded"))
                && provider_status->text().contains(QStringLiteral("27 ms"))
                && provider_status->property("requestState").toString()
                    == QStringLiteral("succeeded")
                && provider_status->property("requestId").toString()
                    == QStringLiteral("request-full-id")
                && provider_status->property("durationMs").toLongLong() == 27,
            "only a completed model request may report success with measured timing");
    auto *usage_details =
        dock.findChild<QLabel *>(QStringLiteral("agentTechnicalDetails"));
    Require(usage_details
                && usage_details->text().contains(
                    QStringLiteral("Token usage: input 120 · output 35 · total 155"))
                && usage_details->text().contains(
                    QStringLiteral("cached input 80 · reasoning 12"))
                && usage_details->property("usageRequested").toBool()
                && usage_details->property("usageReported").toBool()
                && usage_details->property("totalTokens").toLongLong() == 155
                && usage_details->text().contains(
                    QStringLiteral("Response latency: first byte 9 ms · first model event 14 ms"))
                && usage_details->property("firstByteMs").toLongLong() == 9
                && usage_details->property("firstModelEventMs").toLongLong() == 14,
            "technical details must expose exact provider-reported token usage");
    SigilAgent::AgentEvent run_completed;
    run_completed.type = SigilAgent::AgentEventType::RunStateChanged;
    run_completed.timestampMs = 1700000000100;
    run_completed.payload = QJsonObject {
        { QStringLiteral("run_id"), QStringLiteral("run-full-id") },
        { QStringLiteral("state"), QStringLiteral("completed") },
        { QStringLiteral("book_session_id"), QStringLiteral("request-book-id") },
        { QStringLiteral("duration_ms"), 91 },
        { QStringLiteral("model_steps"), 2 },
        { QStringLiteral("tool_calls"), 1 },
        { QStringLiteral("usage_requested"), true },
        { QStringLiteral("usage_summary"), QJsonObject {
              { QStringLiteral("request_count"), 2 },
              { QStringLiteral("reported_request_count"), 2 },
              { QStringLiteral("missing_request_count"), 0 },
              { QStringLiteral("all_requests_reported"), true },
              { QStringLiteral("input_tokens"), 250 },
              { QStringLiteral("input_request_count"), 2 },
              { QStringLiteral("output_tokens"), 30 },
              { QStringLiteral("output_request_count"), 2 },
              { QStringLiteral("total_tokens"), 280 },
              { QStringLiteral("total_request_count"), 2 },
              { QStringLiteral("cached_input_tokens"), 160 },
              { QStringLiteral("cached_input_request_count"), 2 },
              { QStringLiteral("reasoning_tokens"), 10 },
              { QStringLiteral("reasoning_request_count"), 2 } } }
    };
    dock.appendEvent(run_completed);
    Require(run_details->text().contains(
                QStringLiteral("Whole run: 91 ms · model requests 2 · tool calls 1"))
                && run_details->text().contains(
                    QStringLiteral("Run token usage: input 250 · output 30 · total 280"))
                && run_details->text().contains(
                    QStringLiteral("Run usage details: cached input 160 · reasoning 10"))
                && run_details->property("runStatus").toString()
                    == QStringLiteral("completed")
                && run_details->property("runDurationMs").toLongLong() == 91
                && run_details->property("runModelSteps").toInt() == 2
                && run_details->property("runToolCalls").toInt() == 1
                && run_details->property("runUsageComplete").toBool()
                && run_details->property("runUsageRequestCount").toInt() == 2
                && run_details->property("runTotalTokens").toLongLong() == 280,
            "terminal technical details must distinguish whole-run timing and counts");

    SigilAgent::AgentEvent legacy_run_started = run_started;
    legacy_run_started.payload.insert(QStringLiteral("run_id"),
                                      QStringLiteral("legacy-run-id"));
    legacy_run_started.payload.remove(QStringLiteral("usage_requested"));
    dock.appendEvent(legacy_run_started);
    Require(run_details->text().contains(QStringLiteral("Run token usage: not requested"))
                && !run_details->property("runUsageRequested").toBool(),
            "a new run without usage metadata must not inherit the prior run setting");

    SigilAgent::AgentEvent partial_run_started = run_started;
    partial_run_started.payload.insert(QStringLiteral("run_id"),
                                       QStringLiteral("partial-run-id"));
    dock.appendEvent(partial_run_started);
    SigilAgent::AgentEvent partial_run_completed = run_completed;
    partial_run_completed.payload.insert(QStringLiteral("run_id"),
                                         QStringLiteral("partial-run-id"));
    partial_run_completed.payload.insert(QStringLiteral("usage_summary"), QJsonObject {
        { QStringLiteral("request_count"), 2 },
        { QStringLiteral("reported_request_count"), 1 },
        { QStringLiteral("missing_request_count"), 1 },
        { QStringLiteral("all_requests_reported"), false },
        { QStringLiteral("input_tokens"), 40 },
        { QStringLiteral("input_request_count"), 1 },
        { QStringLiteral("output_tokens"), 6 },
        { QStringLiteral("output_request_count"), 1 },
        { QStringLiteral("total_tokens"), 46 },
        { QStringLiteral("total_request_count"), 1 }
    });
    dock.appendEvent(partial_run_completed);
    Require(run_details->text().contains(
                QStringLiteral("Run token usage (1 of 2 requests reported): input 40 · output 6 · total 46"))
                && !run_details->property("runUsageComplete").toBool()
                && run_details->property("runUsageReportedRequests").toInt() == 1,
            "partial run usage must disclose request coverage beside exact known sums");

    SigilAgent::AgentEvent no_usage_completed = provider_completed;
    no_usage_completed.payload.remove(QStringLiteral("usage"));
    dock.appendEvent(provider_started);
    dock.appendEvent(no_usage_completed);
    Require(usage_details->text().contains(
                QStringLiteral("Token usage: not reported by provider"))
                && usage_details->property("usageRequested").toBool()
                && !usage_details->property("usageReported").toBool()
                && usage_details->property("totalTokens").toLongLong() == -1,
            "missing provider usage must stay unavailable instead of becoming zero");

    SigilAgent::AgentEvent usage_disabled_started = provider_started;
    usage_disabled_started.payload.insert(QStringLiteral("usage_requested"), false);
    dock.appendEvent(usage_disabled_started);
    dock.appendEvent(no_usage_completed);
    Require(usage_details->text().contains(QStringLiteral("Token usage: not requested"))
                && !usage_details->property("usageRequested").toBool(),
            "technical details must distinguish a disabled usage request");
    dock.appendEvent(provider_started);
    SigilAgent::AgentEvent provider_failed;
    provider_failed.type = SigilAgent::AgentEventType::ModelRequestFailed;
    provider_failed.timestampMs = 1700000001000;
    provider_failed.payload = QJsonObject {
        { QStringLiteral("request_id"), QStringLiteral("request-full-id") },
        { QStringLiteral("duration_ms"), 31 },
        { QStringLiteral("message"),
          QStringLiteral("HTTP 401: rejected sk-private-provider-key") }
    };
    dock.appendEvent(provider_failed);
    Require(provider_status->text().contains(QStringLiteral("Authentication failed (HTTP 401)"))
                && !provider_status->text().contains(QStringLiteral("sk-private-provider-key"))
                && provider_status->property("requestState").toString()
                    == QStringLiteral("failed"),
            "provider failures must use a readable summary without echoing response details");
    dock.appendEvent(provider_started);
    SigilAgent::AgentEvent request_cancelled;
    request_cancelled.type = SigilAgent::AgentEventType::ModelRequestCancelled;
    request_cancelled.timestampMs = 1700000002000;
    request_cancelled.payload = QJsonObject {
        { QStringLiteral("request_id"), QStringLiteral("request-full-id") },
        { QStringLiteral("step"), 1 },
        { QStringLiteral("model"), QStringLiteral("deepseek-chat") },
        { QStringLiteral("duration_ms"), 44 }
    };
    dock.appendEvent(request_cancelled);
    SigilAgent::AgentEvent provider_cancelled;
    provider_cancelled.type = SigilAgent::AgentEventType::SessionCancelled;
    dock.appendEvent(provider_cancelled);
    Require(provider_status->text().contains(QStringLiteral("Last request cancelled"))
                && provider_status->text().contains(QStringLiteral("44 ms"))
                && provider_status->property("requestState").toString()
                    == QStringLiteral("cancelled"),
            "cancelled requests must not leave provider status stuck on contacting");
    dock.resetTranscript();
    dock.setBookContext(QStringLiteral("Junior Physics"),
                        QStringLiteral("physics.epub"), 42, true, 7,
                        QStringLiteral("12345678-abcd"));
    auto *book_status = dock.findChild<QLabel *>(QStringLiteral("agentBookStatus"));
    Require(book_status && book_status->text().contains(QStringLiteral("physics.epub"))
                && book_status->text().contains(QStringLiteral("Junior Physics"))
                && book_status->text().contains(QStringLiteral("42 resources"))
                && book_status->text().contains(QStringLiteral("Unsaved changes"))
                && book_status->text().contains(QStringLiteral("Book session 12345678"))
                && book_status->property("bookSessionId").toString()
                    == QStringLiteral("12345678-abcd")
                && book_status->text().contains(QStringLiteral("Agent rev 7")),
            "book status must identify the file, title, resources, session, save state, and revision");
    dock.setBookContext(QStringLiteral("Junior Physics"),
                        QStringLiteral("physics.epub"), 42, false, 7,
                        QStringLiteral("12345678-abcd"));
    Require(book_status->text().contains(QStringLiteral("Saved"))
                && !book_status->text().contains(QStringLiteral("Unsaved changes")),
            "book status must update after saving");
    auto *technical_toggle = dock.findChild<QToolButton *>(
        QStringLiteral("agentTechnicalDetailsToggle"));
    auto *technical = dock.findChild<QLabel *>(QStringLiteral("agentTechnicalDetails"));
    Require(technical_toggle && technical && !technical->isVisible(),
            "technical request details must exist and start collapsed");
    technical_toggle->click();
    Require(technical->isVisible()
                && technical->text().contains(QStringLiteral("session-full-id"))
                && technical->text().contains(QStringLiteral("request-full-id"))
                && technical->text().contains(QStringLiteral("request-book-id"))
                && technical->text().contains(QStringLiteral("chapter-1:12-34"))
                && technical->property("requestBookRevision").toLongLong() == 6
                && technical->property("requestHandles").toStringList()
                    == QStringList { QStringLiteral("chapter-1:12-34"),
                                     QStringLiteral("book-css") },
            "expanded details must expose full immutable request identity and scope");
    technical_toggle->click();
    Require(!technical->isVisible(), "technical details must collapse again");
    Require(mode && mode->count() == 4, "mode combo must offer Ask/Plan/Edit/Auto");
    Require(mode->itemData(0).toString() == QStringLiteral("ask")
                && mode->itemData(1).toString() == QStringLiteral("plan")
                && mode->itemData(2).toString() == QStringLiteral("edit")
                && mode->itemData(3).toString() == QStringLiteral("auto"),
            "mode combo values must be ask/plan/edit/auto");
    Require(stop && stop->text().contains(QStringLiteral("Stop")), "Stop control is missing");
    Require(fresh && fresh->text().contains(QStringLiteral("New Session")),
            "New Session control is missing");
    dock.setRunState(SigilAgent::AgentRunState::StreamingResponse);
    Require(!mode->isEnabled() && !fresh->isEnabled() && stop->isEnabled(),
            "mode and New Session must be locked while a run owns the Runner stack");
    dock.setRunState(SigilAgent::AgentRunState::Completed);
    Require(mode->isEnabled() && fresh->isEnabled() && !stop->isEnabled(),
            "terminal run state must restore mode and New Session controls");
    dock.resetTranscript();
    SigilAgent::AgentEvent target_changed;
    target_changed.type = SigilAgent::AgentEventType::BookTargetChanged;
    dock.appendEvent(target_changed);
    Require(dock.findChild<QWidget *>(QStringLiteral("agentBookTargetChangedCard")),
            "a defensive book-session mismatch must have a dedicated visible result card");
    dock.resetTranscript();
    SigilAgent::AgentEvent book_cancelled;
    book_cancelled.type = SigilAgent::AgentEventType::SessionCancelled;
    book_cancelled.payload = QJsonObject {
        { QStringLiteral("reason"), QStringLiteral("book_changed") }
    };
    dock.appendEvent(book_cancelled);
    Require(dock.findChild<QWidget *>(QStringLiteral("agentBookChangedCard")),
            "switching books must explain why the prior run stopped");
    dock.resetTranscript();
    Require(composer, "composer is missing");
    Require(transcript, "transcript surface is missing");
    auto *whole_book_chip =
        dock.findChild<QToolButton *>(QStringLiteral("agentChipBook"));
    auto *file_chip =
        dock.findChild<QToolButton *>(QStringLiteral("agentChipFile"));
    auto *selected_files_chip =
        dock.findChild<QToolButton *>(QStringLiteral("agentChipSelectedFiles"));
    Require(whole_book_chip,
            "whole-book context chip is missing");
    Require(file_chip,
            "file context chip is missing");
    Require(selected_files_chip,
            "Book Browser selected-files context chip is missing");
    Require(dock.findChild<QToolButton *>(QStringLiteral("agentChipSelection")),
            "selection context chip is missing");
    auto *selection_chip =
        dock.findChild<QToolButton *>(QStringLiteral("agentChipSelection"));
    dock.setCurrentFile(QStringLiteral("OEBPS/Text/ch1.xhtml"), QStringLiteral("chapter-1"));
    Require(file_chip->isChecked()
                && dock.contextHandles() == QStringList { QStringLiteral("chapter-1") },
            "current file must be the default scope when there is no editor selection");
    dock.setSelection(QStringLiteral("chapter-1"), 12, 34,
                      QStringLiteral("<ruby>字<rt>じ</rt></ruby>"));
    Require(selection_chip && selection_chip->isEnabled() && selection_chip->isChecked()
                && selection_chip->text().contains(QStringLiteral("12–34")),
            "a live editor selection must enable and select the context chip");
    Require(selection_chip->property("resourceId").toString() == QStringLiteral("chapter-1")
                && selection_chip->property("selectionStart").toInt() == 12
                && selection_chip->property("selectionEnd").toInt() == 34,
            "selection chip must expose its exact resource and UTF-16 range");
    Require(dock.contextHandles().contains(QStringLiteral("chapter-1:12-34")),
            "selected context must emit a bounded resource range handle");
    dock.setSelection(QStringLiteral("chapter-1"), 34, 34, QString());
    Require(!selection_chip->isEnabled() && file_chip->isChecked()
                && dock.contextHandles() == QStringList { QStringLiteral("chapter-1") }
                && !dock.contextHandles().contains(QStringLiteral("chapter-1:34-34")),
            "a collapsed cursor must fall back to current file without a fake selection");

    dock.setSelectedFiles(
        QStringList { QStringLiteral("OEBPS/Text/ch1.xhtml"),
                      QStringLiteral("OEBPS/Styles/book.css"),
                      QStringLiteral("OEBPS/Styles/book.css") },
        QStringList { QStringLiteral("chapter-1"), QStringLiteral("book-css"),
                      QStringLiteral("book-css") });
    Require(selected_files_chip->isEnabled()
                && selected_files_chip->property("resourceIds").toStringList()
                    == QStringList { QStringLiteral("chapter-1"), QStringLiteral("book-css") },
            "selected-files scope must preserve Book Browser order and remove duplicates");
    selected_files_chip->click();
    Require(selected_files_chip->isChecked()
                && !file_chip->isChecked() && !whole_book_chip->isChecked()
                && dock.contextHandles()
                    == QStringList { QStringLiteral("chapter-1"), QStringLiteral("book-css") },
            "selected files must be an explicit exclusive scope");
    dock.setSelection(QStringLiteral("chapter-1"), 4, 10, QStringLiteral("source"));
    Require(selected_files_chip->isChecked(),
            "a manually selected valid scope must survive editor selection changes");
    whole_book_chip->click();
    Require(whole_book_chip->isChecked()
                && dock.contextHandles() == QStringList { QStringLiteral("book") },
            "whole book must be explicit and must not also attach file scopes");

    SigilAgent::AgentEvent thinking;
    thinking.type = SigilAgent::AgentEventType::AssistantDelta;
    thinking.payload = QJsonObject {
        { QStringLiteral("kind"), QStringLiteral("reasoning") },
        { QStringLiteral("text"), QStringLiteral("I should inspect the spine.") }
    };
    dock.appendEvent(thinking);

    SigilAgent::AgentEvent answer;
    answer.type = SigilAgent::AgentEventType::AssistantDelta;
    answer.payload = QJsonObject {
        { QStringLiteral("kind"), QStringLiteral("content") },
        { QStringLiteral("text"), QStringLiteral("This book has two chapters.") }
    };
    dock.appendEvent(answer);
    ProcessEventsFor(100);

    QWidget *thinking_card = dock.findChild<QWidget *>(QStringLiteral("agentThinkingCard"));
    QWidget *answer_card = dock.findChild<QWidget *>(QStringLiteral("agentAnswerCard"));
    Require(thinking_card && answer_card, "thinking and answer cards must both exist");
    Require(thinking_card != answer_card, "thinking must be a distinct card from the answer");
    auto *thinking_body = thinking_card->findChild<QLabel *>(QStringLiteral("agentThinkingCardBody"));
    auto *answer_body = answer_card->findChild<QLabel *>(QStringLiteral("agentAnswerCardBody"));
    Require(thinking_body && thinking_body->text().contains(QStringLiteral("spine")),
            "thinking card must show reasoning text");
    Require(answer_body && answer_body->text().contains(QStringLiteral("two chapters")),
            "answer card must show the user-visible content");
    Require(!thinking_body->isVisible(), "thinking card must start collapsed");
    Require(answer_body->isVisible(), "answer card must be visible");
    Require(transcript->property("streamFlushIntervalMs").toInt() == 33
                && transcript->property("streamRenderBatches").toULongLong() == 1,
            "reasoning and answer deltas in one frame must share one render batch");

    dock.resetTranscript();
    SigilAgent::AgentEvent tiny_delta;
    tiny_delta.type = SigilAgent::AgentEventType::AssistantDelta;
    tiny_delta.payload = QJsonObject {
        { QStringLiteral("kind"), QStringLiteral("content") },
        { QStringLiteral("text"), QStringLiteral("x") }
    };
    constexpr int streamed_chunks = 2000;
    for (int i = 0; i < streamed_chunks; ++i) dock.appendEvent(tiny_delta);
    Require(!dock.findChild<QWidget *>(QStringLiteral("agentAnswerCard"))
                && transcript->property("streamRenderBatches").toULongLong() == 0,
            "a burst of stream chunks must not trigger per-chunk card rendering");
    SigilAgent::AgentEvent complete_answer;
    complete_answer.type = SigilAgent::AgentEventType::AssistantMessage;
    complete_answer.payload = QJsonObject {
        { QStringLiteral("content"), QString(streamed_chunks, QLatin1Char('x')) }
    };
    dock.appendEvent(complete_answer);
    auto *coalesced_body = dock.findChild<QLabel *>(QStringLiteral("agentAnswerCardBody"));
    Require(coalesced_body && coalesced_body->text().size() == streamed_chunks
                && coalesced_body->text() == QString(streamed_chunks, QLatin1Char('x'))
                && transcript->property("streamRenderBatches").toULongLong() == 1,
            "a terminal event must synchronously flush every queued stream character once");

    dock.resetTranscript();
    dock.appendEvent(tiny_delta);
    dock.resetTranscript();
    ProcessEventsFor(100);
    Require(!dock.findChild<QWidget *>(QStringLiteral("agentAnswerCard"))
                && transcript->property("streamRenderBatches").toULongLong() == 0,
            "transcript reset must cancel a pending stream flush from the old session");

    SigilAgent::AgentEvent first_user;
    first_user.type = SigilAgent::AgentEventType::UserMessage;
    first_user.payload = QJsonObject { { QStringLiteral("text"), QStringLiteral("one") } };
    dock.appendEvent(first_user);
    SigilAgent::AgentEvent first_answer;
    first_answer.type = SigilAgent::AgentEventType::AssistantDelta;
    first_answer.payload = QJsonObject {
        { QStringLiteral("kind"), QStringLiteral("content") },
        { QStringLiteral("text"), QStringLiteral("first answer") }
    };
    dock.appendEvent(first_answer);
    SigilAgent::AgentEvent second_user;
    second_user.type = SigilAgent::AgentEventType::UserMessage;
    second_user.payload = QJsonObject { { QStringLiteral("text"), QStringLiteral("two") } };
    dock.appendEvent(second_user);
    SigilAgent::AgentEvent second_answer;
    second_answer.type = SigilAgent::AgentEventType::AssistantDelta;
    second_answer.payload = QJsonObject {
        { QStringLiteral("kind"), QStringLiteral("content") },
        { QStringLiteral("text"), QStringLiteral("second answer") }
    };
    dock.appendEvent(second_answer);
    SigilAgent::AgentEvent final_answer;
    final_answer.type = SigilAgent::AgentEventType::AssistantMessage;
    final_answer.payload = QJsonObject {
        { QStringLiteral("content"), QStringLiteral("second answer") }
    };
    dock.appendEvent(final_answer);
    application.processEvents();

    QWidget *answer1 = dock.findChild<QWidget *>(QStringLiteral("agentAnswerCard"));
    QWidget *answer2 = dock.findChild<QWidget *>(QStringLiteral("agentAnswerCard2"));
    Require(answer1 && answer2 && answer1 != answer2,
            "each user turn must keep its own answer card");
    auto *body1 = answer1->findChild<QLabel *>(QStringLiteral("agentAnswerCardBody"));
    auto *body2 = answer2->findChild<QLabel *>(QStringLiteral("agentAnswerCard2Body"));
    Require(body1 && body1->text().contains(QStringLiteral("first answer")),
            "the first answer card must not be overwritten by a later turn");
    Require(body2 && body2->text().contains(QStringLiteral("second answer")),
            "the second turn must render on a new answer card");

    dock.resetTranscript();
    SigilAgent::AgentEvent user;
    user.type = SigilAgent::AgentEventType::UserMessage;
    user.payload = QJsonObject { { QStringLiteral("text"), QStringLiteral("fix title") } };
    dock.appendEvent(user);
    SigilAgent::AgentEvent step1;
    step1.type = SigilAgent::AgentEventType::ModelRequestStarted;
    dock.appendEvent(step1);
    SigilAgent::AgentEvent first_content;
    first_content.type = SigilAgent::AgentEventType::AssistantMessage;
    first_content.payload = QJsonObject {
        { QStringLiteral("content"), QStringLiteral("I'll patch the title.") }
    };
    dock.appendEvent(first_content);
    SigilAgent::AgentEvent step2;
    step2.type = SigilAgent::AgentEventType::ModelRequestStarted;
    dock.appendEvent(step2);
    SigilAgent::AgentEvent second_content;
    second_content.type = SigilAgent::AgentEventType::AssistantMessage;
    second_content.payload = QJsonObject {
        { QStringLiteral("content"), QStringLiteral("长度保持不变（216→216），符合预期。提交修复：") }
    };
    dock.appendEvent(second_content);
    application.processEvents();
    auto *step1_body = dock.findChild<QLabel *>(QStringLiteral("agentAnswerCardBody"));
    auto *step2_card = dock.findChild<QWidget *>(QStringLiteral("agentAnswerCard1-2"));
    auto *step2_body = step2_card
        ? step2_card->findChild<QLabel *>(QStringLiteral("agentAnswerCard1-2Body"))
        : nullptr;
    Require(step1_body && step1_body->text().contains(QStringLiteral("I'll patch the title.")),
            "later model steps must not overwrite the earlier answer card");
    Require(step2_body && step2_body->text().contains(QStringLiteral("提交修复")),
            "each model step must keep its own answer card");

    SigilAgent::AgentEvent approval;
    approval.type = SigilAgent::AgentEventType::ToolApprovalRequested;
    approval.payload = QJsonObject {
        { QStringLiteral("tool_call_id"), QStringLiteral("call-1") },
        { QStringLiteral("name"), QStringLiteral("transaction.commit") },
        { QStringLiteral("impact"), QStringLiteral("Commit staged edits") }
    };
    dock.appendEvent(approval);
    application.processEvents();
    auto *approve = dock.findChild<QPushButton *>(QStringLiteral("agentApproveButton-call-1"));
    auto *deny = dock.findChild<QPushButton *>(QStringLiteral("agentDenyButton-call-1"));
    Require(approve && deny && approve->isEnabled() && deny->isEnabled(),
            "approval buttons must start enabled");
    bool approved = false;
    QObject::connect(&dock, &SigilAgent::AgentDock::approvalResponded,
                     [&approved](const QString &id, bool ok,
                                 const QJsonObject &overrides) {
                         if (id != QLatin1String("call-1")) return;
                         Require(overrides.isEmpty(),
                                 "ordinary approval must not add argument overrides");
                         approved = ok;
                     });
    approve->click();
    application.processEvents();
    Require(approved, "Approve must emit approvalResponded");
    Require(!approve->isEnabled() && !deny->isEnabled(),
            "Approve/Deny must disable after a decision");
    Require(approve->text().contains(QStringLiteral("Approved")),
            "Approve must show it was accepted");

    SigilAgent::AgentEvent paragraph_plan;
    paragraph_plan.type = SigilAgent::AgentEventType::PlanCreated;
    paragraph_plan.payload = QJsonObject {
        { QStringLiteral("tool_call_id"), QStringLiteral("plan-call") },
        { QStringLiteral("name"), QStringLiteral("paragraphs.plan") },
        { QStringLiteral("plan_kind"), QStringLiteral("paragraph_normalization") },
        { QStringLiteral("plan_id"), QStringLiteral("paragraph-plan-id") },
        { QStringLiteral("plan_digest"), QStringLiteral("paragraph-plan-digest") },
        { QStringLiteral("book_session_id"), QStringLiteral("12345678-abcd") },
        { QStringLiteral("book_revision"), 7 },
        { QStringLiteral("applied_to_book"), false },
        { QStringLiteral("summary"), QJsonObject {
            { QStringLiteral("ready_files"), 2 },
            { QStringLiteral("review_only_files"), 0 },
            { QStringLiteral("skipped_files"), 1 },
            { QStringLiteral("error_files"), 0 },
            { QStringLiteral("conversion_count"), 20 },
            { QStringLiteral("protected_count"), 3 }
        } },
        { QStringLiteral("changes_css"), false },
        { QStringLiteral("changes_opf"), false },
        { QStringLiteral("adds_resources"), false },
        { QStringLiteral("changes"), QJsonArray {
            QJsonObject {
                { QStringLiteral("resource_id"), QStringLiteral("chapter-1") },
                { QStringLiteral("book_path"), QStringLiteral("Text/chapter-1.xhtml") },
                { QStringLiteral("conversion_count"), 12 },
                { QStringLiteral("protected_count"), 2 },
                { QStringLiteral("source_diff"), QJsonObject {
                    { QStringLiteral("before"), QStringLiteral("<div>Original paragraph</div>") },
                    { QStringLiteral("after"), QStringLiteral("<p>Original paragraph</p>") },
                    { QStringLiteral("prefix_truncated"), true },
                    { QStringLiteral("suffix_truncated"), false }
                } }
            },
            QJsonObject {
                { QStringLiteral("resource_id"), QStringLiteral("chapter-2") },
                { QStringLiteral("book_path"), QStringLiteral("Text/chapter-2.xhtml") },
                { QStringLiteral("conversion_count"), 8 },
                { QStringLiteral("protected_count"), 1 },
                { QStringLiteral("source_diff"), QJsonObject {
                    { QStringLiteral("before"), QStringLiteral("<div>Second paragraph</div>") },
                    { QStringLiteral("after"), QStringLiteral("<p>Second paragraph</p>") }
                } }
            }
        } },
        { QStringLiteral("operation_groups_independent"), true },
        { QStringLiteral("operation_groups"), QJsonArray {
            QJsonObject {
                { QStringLiteral("group_id"), QStringLiteral("chapter-1") },
                { QStringLiteral("label"), QStringLiteral("Text/chapter-1.xhtml") },
                { QStringLiteral("resource_ids"), QJsonArray {
                    QStringLiteral("chapter-1") } },
                { QStringLiteral("conversion_count"), 12 },
                { QStringLiteral("protected_count"), 2 },
                { QStringLiteral("independently_applicable"), true }
            },
            QJsonObject {
                { QStringLiteral("group_id"), QStringLiteral("chapter-2") },
                { QStringLiteral("label"), QStringLiteral("Text/chapter-2.xhtml") },
                { QStringLiteral("resource_ids"), QJsonArray {
                    QStringLiteral("chapter-2") } },
                { QStringLiteral("conversion_count"), 8 },
                { QStringLiteral("protected_count"), 1 },
                { QStringLiteral("independently_applicable"), true }
            }
        } },
        { QStringLiteral("local_validation"), QStringLiteral("passed") },
        { QStringLiteral("full_epubcheck"), QJsonObject {
            { QStringLiteral("status"), QStringLiteral("not_run") }
        } }
    };
    int opened_plan_resources = 0;
    QString opened_plan_path;
    QObject::connect(&dock, &SigilAgent::AgentDock::openPlanResourceRequested,
                     [&opened_plan_resources, &opened_plan_path](
                         const QString &path, const QString &book_session_id) {
        if (book_session_id == QLatin1String("12345678-abcd")) {
            ++opened_plan_resources;
            opened_plan_path = path;
        }
    });
    dock.appendEvent(paragraph_plan);
    application.processEvents();
    auto *paragraph_plan_card = dock.findChild<QWidget *>(
        QStringLiteral("agentPlanReviewCard-plan-call"));
    auto *paragraph_plan_body = paragraph_plan_card
        ? paragraph_plan_card->findChild<QLabel *>(
              QStringLiteral("agentPlanReviewCard-plan-callBody"))
        : nullptr;
    auto *open_paragraph = dock.findChild<QPushButton *>(
        QStringLiteral("agentPlanOpenResourceButton-plan-call-0"));
    auto *compare_paragraph = dock.findChild<QPushButton *>(
        QStringLiteral("agentPlanCompareButton-plan-call-0"));
    Require(paragraph_plan_card && paragraph_plan_body && paragraph_plan_body->isVisible()
                && paragraph_plan_body->textFormat() == Qt::PlainText
                && paragraph_plan_card->property("planId").toString()
                    == QStringLiteral("paragraph-plan-id")
                && paragraph_plan_card->property("planDigest").toString()
                    == QStringLiteral("paragraph-plan-digest")
                && paragraph_plan_card->property("bookRevision").toLongLong() == 7,
            "paragraph plan card must expose a visible, plain-text reviewed binding");
    Require(paragraph_plan_body->text().contains(QStringLiteral("12 conversion(s)"))
                && paragraph_plan_body->text().contains(QStringLiteral("2 protected item(s)"))
                && paragraph_plan_body->text().contains(
                    QStringLiteral("<div>Original paragraph</div>"))
                && paragraph_plan_body->text().contains(
                    QStringLiteral("<p>Original paragraph</p>"))
                && paragraph_plan_body->text().contains(QStringLiteral("XHTML only"))
                && paragraph_plan_body->text().contains(
                    QStringLiteral("Full EPUBCheck: not run")),
            "paragraph review must show scope, bounded source excerpts, and validation limits");
    Require(open_paragraph && open_paragraph->isEnabled()
                && open_paragraph->property("bookPath").toString()
                    == QStringLiteral("Text/chapter-1.xhtml"),
            "paragraph review must offer a book-bound resource navigation action");
    Require(compare_paragraph && compare_paragraph->isEnabled()
                && compare_paragraph->property("bookPath").toString()
                    == QStringLiteral("Text/chapter-1.xhtml")
                && compare_paragraph->property("bookSessionId").toString()
                    == QStringLiteral("12345678-abcd")
                && compare_paragraph->accessibleName().contains(
                    QStringLiteral("Text/chapter-1.xhtml")),
            "paragraph review must offer a book-bound side-by-side comparison");
    compare_paragraph->click();
    application.processEvents();
    QPointer<QDialog> paragraph_comparison = dock.findChild<QDialog *>(
        QStringLiteral("agentPlanComparisonDialog-plan-call-0"));
    auto *paragraph_before = paragraph_comparison
        ? paragraph_comparison->findChild<QPlainTextEdit *>(
              QStringLiteral("agentPlanComparisonBefore"))
        : nullptr;
    auto *paragraph_after = paragraph_comparison
        ? paragraph_comparison->findChild<QPlainTextEdit *>(
              QStringLiteral("agentPlanComparisonAfter"))
        : nullptr;
    Require(paragraph_comparison && paragraph_comparison->isVisible()
                && paragraph_comparison->windowTitle().contains(
                    QStringLiteral("Text/chapter-1.xhtml"))
                && paragraph_comparison->property("planId").toString()
                    == QStringLiteral("paragraph-plan-id")
                && paragraph_comparison->property("planDigest").toString()
                    == QStringLiteral("paragraph-plan-digest")
                && paragraph_comparison->property("bookRevision").toLongLong() == 7
                && paragraph_comparison->property("bookSessionId").toString()
                    == QStringLiteral("12345678-abcd"),
            "paragraph comparison must preserve the reviewed plan and book binding");
    Require(paragraph_before && paragraph_after
                && paragraph_before->isReadOnly() && paragraph_after->isReadOnly()
                && paragraph_before->toPlainText()
                    == QStringLiteral("<div>Original paragraph</div>")
                && paragraph_after->toPlainText()
                    == QStringLiteral("<p>Original paragraph</p>")
                && paragraph_comparison->property("displayTruncated").toBool(),
            "paragraph comparison must render literal bounded source in distinct panes");
    compare_paragraph->click();
    Require(dock.findChildren<QDialog *>(
                QStringLiteral("agentPlanComparisonDialog-plan-call-0")).size() == 1,
            "reopening one plan comparison must reuse its existing dialog");

    SigilAgent::AgentEvent matching_plan_approval;
    matching_plan_approval.type = SigilAgent::AgentEventType::ToolApprovalRequested;
    matching_plan_approval.payload = QJsonObject {
        { QStringLiteral("tool_call_id"), QStringLiteral("matching-plan-apply") },
        { QStringLiteral("name"), QStringLiteral("paragraphs.apply") },
        { QStringLiteral("impact"), QStringLiteral("Stage reviewed paragraph plan") },
        { QStringLiteral("arguments"), QJsonObject {
            { QStringLiteral("plan_id"), QStringLiteral("paragraph-plan-id") },
            { QStringLiteral("plan_digest"), QStringLiteral("paragraph-plan-digest") },
            { QStringLiteral("expected_book_revision"), 7 }
        } }
    };
    dock.appendEvent(matching_plan_approval);
    application.processEvents();
    auto *matching_approval_card = dock.findChild<QWidget *>(
        QStringLiteral("agentApprovalCard-matching-plan-apply"));
    auto *matching_approve = dock.findChild<QPushButton *>(
        QStringLiteral("agentApproveButton-matching-plan-apply"));
    auto *matching_binding = matching_approval_card
        ? matching_approval_card->findChild<QLabel *>(
              QStringLiteral("agentApprovalPlanBinding"))
        : nullptr;
    auto *group_list = matching_approval_card
        ? matching_approval_card->findChild<QListWidget *>(
              QStringLiteral("agentPlanGroupList-matching-plan-apply"))
        : nullptr;
    auto *select_all_groups = matching_approval_card
        ? matching_approval_card->findChild<QPushButton *>(
              QStringLiteral("agentPlanGroupsSelectAll-matching-plan-apply"))
        : nullptr;
    auto *clear_groups = matching_approval_card
        ? matching_approval_card->findChild<QPushButton *>(
              QStringLiteral("agentPlanGroupsClear-matching-plan-apply"))
        : nullptr;
    Require(matching_approve && matching_approve->isEnabled()
                && matching_approval_card->property("planReviewRequired").toBool()
                && matching_approval_card->property("reviewedPlanMatched").toBool()
                && matching_binding
                && matching_binding->text().contains(QStringLiteral("2 of 2")),
            "native apply approval must enable only for its displayed plan binding");
    Require(group_list && group_list->count() == 2
                && group_list->property("totalGroups").toInt() == 2
                && group_list->item(0)->checkState() == Qt::Checked
                && group_list->item(1)->checkState() == Qt::Checked
                && select_all_groups && clear_groups,
            "paragraph approval must expose every independent XHTML group as checked");
    clear_groups->click();
    Require(!matching_approve->isEnabled()
                && matching_binding->text().contains(
                    QStringLiteral("select at least one")),
            "clearing every paragraph group must visibly block approval");
    select_all_groups->click();
    Require(matching_approve->isEnabled()
                && matching_binding->text().contains(QStringLiteral("2 of 2")),
            "Select all must restore the complete reviewed group selection");
    group_list->item(0)->setCheckState(Qt::Unchecked);
    Require(matching_approve->isEnabled()
                && matching_binding->text().contains(QStringLiteral("1 of 2"))
                && matching_approve->property("selectedResourceIds").toStringList()
                    == QStringList { QStringLiteral("chapter-2") },
            "an independent plan group must be removable without invalidating the other group");
    QString approved_group_call;
    QJsonObject approved_group_overrides;
    QObject::connect(&dock, &SigilAgent::AgentDock::approvalResponded,
                     [&approved_group_call, &approved_group_overrides](
                         const QString &id, bool ok,
                         const QJsonObject &overrides) {
        if (id == QLatin1String("matching-plan-apply") && ok) {
            approved_group_call = id;
            approved_group_overrides = overrides;
        }
    });
    matching_approve->click();
    Require(approved_group_call == QStringLiteral("matching-plan-apply")
                && approved_group_overrides.value(
                    QStringLiteral("selected_resource_ids")).toArray()
                    == QJsonArray { QStringLiteral("chapter-2") }
                && !group_list->isEnabled()
                && !select_all_groups->isEnabled()
                && !clear_groups->isEnabled(),
            "approval must freeze and emit exactly the checked paragraph groups");

    SigilAgent::AgentEvent mismatched_plan_approval = matching_plan_approval;
    mismatched_plan_approval.payload.insert(
        QStringLiteral("tool_call_id"), QStringLiteral("mismatched-plan-apply"));
    QJsonObject mismatched_arguments =
        mismatched_plan_approval.payload.value(QStringLiteral("arguments")).toObject();
    mismatched_arguments.insert(QStringLiteral("plan_digest"), QStringLiteral("other-digest"));
    mismatched_plan_approval.payload.insert(QStringLiteral("arguments"), mismatched_arguments);
    dock.appendEvent(mismatched_plan_approval);
    application.processEvents();
    auto *mismatched_approval_card = dock.findChild<QWidget *>(
        QStringLiteral("agentApprovalCard-mismatched-plan-apply"));
    auto *mismatched_approve = dock.findChild<QPushButton *>(
        QStringLiteral("agentApproveButton-mismatched-plan-apply"));
    auto *mismatched_deny = dock.findChild<QPushButton *>(
        QStringLiteral("agentDenyButton-mismatched-plan-apply"));
    auto *mismatched_binding = mismatched_approval_card
        ? mismatched_approval_card->findChild<QLabel *>(
              QStringLiteral("agentApprovalPlanBinding"))
        : nullptr;
    Require(mismatched_approve && !mismatched_approve->isEnabled()
                && mismatched_deny && mismatched_deny->isEnabled()
                && !mismatched_approval_card->property("reviewedPlanMatched").toBool()
                && mismatched_binding
                && mismatched_binding->text().contains(QStringLiteral("blocked")),
            "an unreviewed digest must be visible and blocked while remaining deniable");

    SigilAgent::AgentEvent malformed_group_plan = paragraph_plan;
    malformed_group_plan.payload.insert(
        QStringLiteral("tool_call_id"), QStringLiteral("malformed-group-plan"));
    malformed_group_plan.payload.insert(
        QStringLiteral("plan_id"), QStringLiteral("malformed-group-plan-id"));
    malformed_group_plan.payload.insert(
        QStringLiteral("plan_digest"), QStringLiteral("malformed-group-plan-digest"));
    QJsonObject duplicate_group = paragraph_plan.payload.value(
        QStringLiteral("operation_groups")).toArray().first().toObject();
    malformed_group_plan.payload.insert(
        QStringLiteral("operation_groups"),
        QJsonArray { duplicate_group, duplicate_group });
    dock.appendEvent(malformed_group_plan);
    SigilAgent::AgentEvent malformed_group_approval = matching_plan_approval;
    malformed_group_approval.payload.insert(
        QStringLiteral("tool_call_id"), QStringLiteral("malformed-group-apply"));
    QJsonObject malformed_group_arguments = malformed_group_approval.payload.value(
        QStringLiteral("arguments")).toObject();
    malformed_group_arguments.insert(
        QStringLiteral("plan_id"), QStringLiteral("malformed-group-plan-id"));
    malformed_group_arguments.insert(
        QStringLiteral("plan_digest"),
        QStringLiteral("malformed-group-plan-digest"));
    malformed_group_approval.payload.insert(
        QStringLiteral("arguments"), malformed_group_arguments);
    dock.appendEvent(malformed_group_approval);
    application.processEvents();
    auto *malformed_group_card = dock.findChild<QWidget *>(
        QStringLiteral("agentApprovalCard-malformed-group-apply"));
    auto *malformed_group_approve = dock.findChild<QPushButton *>(
        QStringLiteral("agentApproveButton-malformed-group-apply"));
    auto *malformed_group_deny = dock.findChild<QPushButton *>(
        QStringLiteral("agentDenyButton-malformed-group-apply"));
    Require(malformed_group_card && malformed_group_approve
                && !malformed_group_approve->isEnabled()
                && malformed_group_deny && malformed_group_deny->isEnabled()
                && !malformed_group_card->property(
                    "reviewedPlanMatched").toBool(),
            "duplicate reviewed operation groups must fail closed while remaining deniable");

    open_paragraph->click();
    Require(opened_plan_resources == 1
                && opened_plan_path == QStringLiteral("Text/chapter-1.xhtml"),
            "plan navigation must emit the reviewed resource and book session");
    dock.setBookContext(QStringLiteral("Another Book"), QStringLiteral("other.epub"),
                        3, false, 1, QStringLiteral("other-book-session"));
    Require(!open_paragraph->isEnabled() && !compare_paragraph->isEnabled(),
            "plan navigation and comparison must disable as soon as the open book changes");
    Require(!paragraph_comparison || !paragraph_comparison->isVisible(),
            "an open comparison must close when the dock binds another book");
    open_paragraph->click();
    Require(opened_plan_resources == 1,
            "a stale plan card must not navigate after the open book changes");
    dock.setBookContext(QStringLiteral("Junior Physics"),
                        QStringLiteral("physics.epub"), 42, false, 7,
                        QStringLiteral("12345678-abcd"));
    Require(open_paragraph->isEnabled() && compare_paragraph->isEnabled(),
            "returning to the plan-bound book session must restore review actions");

    SigilAgent::AgentEvent toc_plan;
    toc_plan.type = SigilAgent::AgentEventType::PlanCreated;
    toc_plan.payload = QJsonObject {
        { QStringLiteral("tool_call_id"), QStringLiteral("toc-plan-call") },
        { QStringLiteral("name"), QStringLiteral("toc.plan_transform") },
        { QStringLiteral("plan_kind"), QStringLiteral("toc_hierarchy") },
        { QStringLiteral("plan_id"), QStringLiteral("toc-plan-id") },
        { QStringLiteral("plan_digest"), QStringLiteral("toc-plan-digest") },
        { QStringLiteral("book_session_id"), QStringLiteral("12345678-abcd") },
        { QStringLiteral("book_revision"), 7 },
        { QStringLiteral("affected_count"), 2 },
        { QStringLiteral("adopted_count"), 1 },
        { QStringLiteral("preorder_preserved"), true },
        { QStringLiteral("changes_xhtml_headings"), false },
        { QStringLiteral("changes"), QJsonArray { QJsonObject {
            { QStringLiteral("label"), QStringLiteral("Chapter C") },
            { QStringLiteral("target"), QStringLiteral("Text/c.xhtml#one") },
            { QStringLiteral("from_depth"), 2 },
            { QStringLiteral("to_depth"), 1 },
            { QStringLiteral("from_parent_id"), 1 },
            { QStringLiteral("to_parent_id"), 0 }
        } } },
        { QStringLiteral("changes_truncated"), true },
        { QStringLiteral("local_validation"), QStringLiteral("passed") },
        { QStringLiteral("full_epubcheck"), QJsonObject {
            { QStringLiteral("status"), QStringLiteral("not_run") }
        } }
    };
    dock.appendEvent(toc_plan);
    application.processEvents();
    auto *toc_plan_card = dock.findChild<QWidget *>(
        QStringLiteral("agentPlanReviewCard-toc-plan-call"));
    auto *toc_plan_body = toc_plan_card
        ? toc_plan_card->findChild<QLabel *>(
              QStringLiteral("agentPlanReviewCard-toc-plan-callBody"))
        : nullptr;
    auto *open_toc = dock.findChild<QPushButton *>(
        QStringLiteral("agentPlanOpenResourceButton-toc-plan-call-0"));
    auto *compare_toc = dock.findChild<QPushButton *>(
        QStringLiteral("agentPlanCompareButton-toc-plan-call-toc"));
    Require(toc_plan_body && toc_plan_body->text().contains(
                QStringLiteral("2 affected node(s)"))
                && toc_plan_body->text().contains(QStringLiteral("Chapter C"))
                && toc_plan_body->text().contains(QStringLiteral("Depth: 2 → 1"))
                && toc_plan_body->text().contains(QStringLiteral("Preorder preserved: Yes"))
                && toc_plan_body->text().contains(QStringLiteral("omitted")),
            "TOC review must show bounded reparenting and structural invariants");
    Require(open_toc && open_toc->property("bookPath").toString()
                    == QStringLiteral("Text/c.xhtml"),
            "TOC review navigation must strip the target fragment");
    Require(compare_toc && compare_toc->isEnabled()
                && compare_toc->accessibleName().contains(
                    QStringLiteral("TOC hierarchy")),
            "TOC review must offer a book-bound hierarchy comparison");
    compare_toc->click();
    application.processEvents();
    QPointer<QDialog> toc_comparison = dock.findChild<QDialog *>(
        QStringLiteral("agentPlanComparisonDialog-toc-plan-call-toc"));
    auto *toc_before = toc_comparison
        ? toc_comparison->findChild<QPlainTextEdit *>(
              QStringLiteral("agentPlanComparisonBefore"))
        : nullptr;
    auto *toc_after = toc_comparison
        ? toc_comparison->findChild<QPlainTextEdit *>(
              QStringLiteral("agentPlanComparisonAfter"))
        : nullptr;
    Require(toc_comparison && toc_comparison->isVisible()
                && toc_comparison->property("planId").toString()
                    == QStringLiteral("toc-plan-id")
                && toc_comparison->property("comparisonSubject").toString()
                    == QStringLiteral("TOC hierarchy")
                && toc_comparison->property("displayTruncated").toBool(),
            "TOC comparison must preserve the reviewed plan and truncation boundary");
    Require(toc_before && toc_after
                && toc_before->toPlainText().contains(
                    QStringLiteral("Depth: 2 · Parent: 1"))
                && toc_after->toPlainText().contains(
                    QStringLiteral("Depth: 1 · Parent: 0")),
            "TOC comparison must place old and new hierarchy values in separate panes");
    SigilAgent::AgentEvent toc_approval;
    toc_approval.type = SigilAgent::AgentEventType::ToolApprovalRequested;
    toc_approval.payload = QJsonObject {
        { QStringLiteral("tool_call_id"), QStringLiteral("toc-plan-apply") },
        { QStringLiteral("name"), QStringLiteral("toc.apply_transform") },
        { QStringLiteral("impact"), QStringLiteral("Stage reviewed TOC plan") },
        { QStringLiteral("arguments"), QJsonObject {
            { QStringLiteral("plan_id"), QStringLiteral("toc-plan-id") },
            { QStringLiteral("plan_digest"), QStringLiteral("toc-plan-digest") },
            { QStringLiteral("expected_book_revision"), 7 }
        } }
    };
    dock.appendEvent(toc_approval);
    application.processEvents();
    auto *toc_approval_card = dock.findChild<QWidget *>(
        QStringLiteral("agentApprovalCard-toc-plan-apply"));
    auto *toc_approve = dock.findChild<QPushButton *>(
        QStringLiteral("agentApproveButton-toc-plan-apply"));
    auto *toc_dependency = toc_approval_card
        ? toc_approval_card->findChild<QLabel *>(
              QStringLiteral("agentApprovalPlanDependency"))
        : nullptr;
    Require(toc_approve && toc_approve->isEnabled() && toc_dependency
                && toc_dependency->text().contains(QStringLiteral("dependent group"))
                && !toc_approval_card->findChild<QListWidget *>(),
            "dependent TOC hierarchy changes must remain one visible inseparable group");

    dock.resetTranscript();
    application.processEvents();
    Require(!toc_comparison || !toc_comparison->isVisible(),
            "resetting the transcript must close detached plan comparisons");
    SigilAgent::AgentEvent preview;
    preview.type = SigilAgent::AgentEventType::TransactionPreviewed;
    preview.payload = QJsonObject {
        { QStringLiteral("changes"), QJsonArray {
            QJsonObject {
                { QStringLiteral("resource_id"), QStringLiteral("chapter-1") },
                { QStringLiteral("original_length"), 80 },
                { QStringLiteral("staged_length"), 96 },
                { QStringLiteral("changed"), true }
            },
            QJsonObject {
                { QStringLiteral("resource_id"), QStringLiteral("staged-cover") },
                { QStringLiteral("book_path"), QStringLiteral("Images/cover.jpg") },
                { QStringLiteral("added"), true }
            },
            QJsonObject {
                { QStringLiteral("resource_id"), QStringLiteral("chapter-2") },
                { QStringLiteral("from"), QStringLiteral("Text/ch2.xhtml") },
                { QStringLiteral("book_path"), QStringLiteral("Text/chapter-2.xhtml") },
                { QStringLiteral("renamed"), true }
            }
        } },
        { QStringLiteral("metadata_changed"), true },
        { QStringLiteral("spine_changed"), true },
        { QStringLiteral("toc_changed"), true },
        { QStringLiteral("removed"), QJsonArray { QStringLiteral("old-style") } },
        { QStringLiteral("applied_to_book"), false }
    };
    dock.appendEvent(preview);
    application.processEvents();
    auto *preview_card = dock.findChild<QWidget *>(QStringLiteral("agentPreviewCard"));
    auto *preview_body = preview_card
        ? preview_card->findChild<QLabel *>(QStringLiteral("agentPreviewCardBody"))
        : nullptr;
    Require(preview_body && preview_body->isVisible(), "preview status card must be visible");
    Require(preview_body->text().contains(QStringLiteral("live book is unchanged"))
                && preview_body->text().contains(QStringLiteral("Text: chapter-1 (80 → 96)")),
            "preview card must distinguish staging from a live text edit");
    Require(preview_body->text().contains(QStringLiteral("Added: Images/cover.jpg"))
                && preview_body->text().contains(QStringLiteral("Renamed: Text/ch2.xhtml → Text/chapter-2.xhtml"))
                && preview_body->text().contains(QStringLiteral("Removed: old-style")),
            "preview card must describe structural resource changes");
    Require(preview_body->text().contains(QStringLiteral("Metadata changes"))
                && preview_body->text().contains(QStringLiteral("Reading order changes"))
                && preview_body->text().contains(QStringLiteral("TOC hierarchy changes")),
            "preview card must expose metadata, spine, and TOC changes");

    SigilAgent::AgentEvent committed;
    committed.type = SigilAgent::AgentEventType::TransactionCommitted;
    committed.payload = QJsonObject {
        { QStringLiteral("applied_to_book"), true },
        { QStringLiteral("save_status"), QStringLiteral("not_saved") },
        { QStringLiteral("applied_changes"), 4 },
        { QStringLiteral("book_revision"), 17 },
        { QStringLiteral("resource_outcomes"), QJsonObject {
            { QStringLiteral("scope_available"), true },
            { QStringLiteral("all_or_nothing"), true },
            { QStringLiteral("status"), QStringLiteral("all_applied") },
            { QStringLiteral("transaction_state"), QStringLiteral("committed") },
            { QStringLiteral("resource_count"), 2 },
            { QStringLiteral("successful_resource_count"), 2 },
            { QStringLiteral("failed_resource_count"), 0 },
            { QStringLiteral("structural_operation_count"), 1 },
            { QStringLiteral("successful_structural_operation_count"), 1 },
            { QStringLiteral("failed_structural_operation_count"), 0 }
        } },
        { QStringLiteral("full_epubcheck"), QJsonObject {
            { QStringLiteral("status"), QStringLiteral("not_run") }
        } },
        { QStringLiteral("recovery"), QJsonObject {
            { QStringLiteral("sigil_undo"), QStringLiteral("where_available") },
            { QStringLiteral("task_restore_point"), QStringLiteral("available") },
            { QStringLiteral("checkpoint_id"), QStringLiteral("restore-17") },
            { QStringLiteral("book_session_id"), QStringLiteral("12345678-abcd") },
            { QStringLiteral("affected_resources"), QJsonArray {
                QStringLiteral("chapter-1"), QStringLiteral("chapter-2") } }
        } }
    };
    dock.setRunState(SigilAgent::AgentRunState::Completed);
    dock.appendEvent(committed);
    application.processEvents();
    auto *applied_card = dock.findChild<QWidget *>(QStringLiteral("agentAppliedCard"));
    auto *applied_body = applied_card
        ? applied_card->findChild<QLabel *>(QStringLiteral("agentAppliedCardBody"))
        : nullptr;
    Require(applied_body && applied_body->isVisible(), "applied status card must be visible");
    Require(applied_body->text().contains(QStringLiteral("EPUB file has not been saved"))
                && applied_body->text().contains(QStringLiteral("Applied changes: 4"))
                && applied_body->text().contains(QStringLiteral("Book revision: 17"))
                && applied_body->text().contains(
                    QStringLiteral("Resources: 2 succeeded · 0 failed"))
                && applied_body->text().contains(
                    QStringLiteral("Structural operations: 1 succeeded · 0 failed"))
                && applied_body->text().contains(
                    QStringLiteral("all staged targets were applied"))
                && applied_card->property("successfulResourceCount").toInt() == 2
                && applied_card->property("failedResourceCount").toInt() == 0
                && applied_card->property("transactionState").toString()
                    == QStringLiteral("committed"),
            "applied card must show save state and exact atomic resource outcomes");
    Require(applied_body->text().contains(QStringLiteral("Full EPUBCheck: not run"))
                && applied_body->text().contains(QStringLiteral("Undo where available"))
                && applied_body->text().contains(QStringLiteral("2 text resource(s)"))
                && applied_body->text().contains(QStringLiteral("conflict check")),
            "applied card must state validation and recovery boundaries");
    auto *restore_button = dock.findChild<QPushButton *>(
        QStringLiteral("agentTaskRestoreButton-restore-17"));
    Require(restore_button && restore_button->isEnabled(),
            "a guarded text commit must offer task restoration");

    SigilAgent::AgentEvent rolled_back_commit;
    rolled_back_commit.type = SigilAgent::AgentEventType::ToolFailed;
    rolled_back_commit.payload = QJsonObject {
        { QStringLiteral("tool_call_id"), QStringLiteral("commit-rolled-back") },
        { QStringLiteral("name"), QStringLiteral("transaction.commit") },
        { QStringLiteral("code"), QStringLiteral("TRANSACTION_ROLLED_BACK") },
        { QStringLiteral("message"), QStringLiteral("<unsafe> write failed") },
        { QStringLiteral("data"), QJsonObject {
            { QStringLiteral("resource_outcomes"), QJsonObject {
                { QStringLiteral("scope_available"), true },
                { QStringLiteral("all_or_nothing"), true },
                { QStringLiteral("status"), QStringLiteral("not_applied") },
                { QStringLiteral("transaction_state"), QStringLiteral("rolled_back") },
                { QStringLiteral("resource_count"), 2 },
                { QStringLiteral("successful_resource_count"), 0 },
                { QStringLiteral("failed_resource_count"), 2 },
                { QStringLiteral("structural_operation_count"), 1 },
                { QStringLiteral("successful_structural_operation_count"), 0 },
                { QStringLiteral("failed_structural_operation_count"), 1 }
            } }
        } }
    };
    dock.appendEvent(rolled_back_commit);
    application.processEvents();
    auto *rolled_back_card = dock.findChild<QWidget *>(
        QStringLiteral("agentToolCard-commit-rolled-back"));
    auto *rolled_back_title = rolled_back_card
        ? rolled_back_card->findChild<QToolButton *>(
              QStringLiteral("agentToolCard-commit-rolled-backTitle"))
        : nullptr;
    auto *rolled_back_body = rolled_back_card
        ? rolled_back_card->findChild<QLabel *>(
              QStringLiteral("agentToolCard-commit-rolled-backBody"))
        : nullptr;
    Require(rolled_back_title && rolled_back_title->text().contains(
                QStringLiteral("Apply failed"))
                && rolled_back_body && rolled_back_body->isVisible()
                && rolled_back_body->textFormat() == Qt::PlainText
                && rolled_back_body->text().contains(
                    QStringLiteral("Not applied to the current book"))
                && rolled_back_body->text().contains(
                    QStringLiteral("Resources: 0 succeeded · 2 failed"))
                && rolled_back_body->text().contains(
                    QStringLiteral("Structural operations: 0 succeeded · 1 failed"))
                && rolled_back_body->text().contains(
                    QStringLiteral("no partial book changes remain"))
                && rolled_back_body->text().contains(
                    QStringLiteral("Full EPUBCheck: not run"))
                && rolled_back_body->text().contains(
                    QStringLiteral("<unsafe> write failed"))
                && rolled_back_card->property("successfulResourceCount").toInt() == 0
                && rolled_back_card->property("failedResourceCount").toInt() == 2
                && rolled_back_card->property("transactionState").toString()
                    == QStringLiteral("rolled_back"),
            "rolled-back commit failure must visibly report atomic resource outcomes as plain text");

    SigilAgent::AgentEvent retained_commit = rolled_back_commit;
    retained_commit.payload.insert(
        QStringLiteral("tool_call_id"), QStringLiteral("commit-retained"));
    retained_commit.payload.insert(
        QStringLiteral("code"), QStringLiteral("BOOK_REVISION_CONFLICT"));
    QJsonObject retained_data = retained_commit.payload.value(
        QStringLiteral("data")).toObject();
    QJsonObject retained_outcomes = retained_data.value(
        QStringLiteral("resource_outcomes")).toObject();
    retained_outcomes.insert(
        QStringLiteral("transaction_state"), QStringLiteral("staged"));
    retained_data.insert(QStringLiteral("resource_outcomes"), retained_outcomes);
    retained_commit.payload.insert(QStringLiteral("data"), retained_data);
    dock.appendEvent(retained_commit);
    application.processEvents();
    auto *retained_card = dock.findChild<QWidget *>(
        QStringLiteral("agentToolCard-commit-retained"));
    auto *retained_body = retained_card
        ? retained_card->findChild<QLabel *>(
              QStringLiteral("agentToolCard-commit-retainedBody"))
        : nullptr;
    Require(retained_body && retained_body->isVisible()
                && retained_body->text().contains(
                    QStringLiteral("remains available for review, retry, or rollback"))
                && retained_card->property("transactionState").toString()
                    == QStringLiteral("staged"),
            "revision conflict result must say that its staged transaction remains available");
    QString requested_restore;
    QString requested_restore_book;
    QObject::connect(&dock, &SigilAgent::AgentDock::taskRestoreRequested,
                     [&requested_restore, &requested_restore_book](
                         const QString &checkpoint_id, const QString &book_session_id) {
        requested_restore = checkpoint_id;
        requested_restore_book = book_session_id;
    });
    restore_button->click();
    Require(requested_restore == QStringLiteral("restore-17")
                && requested_restore_book == QStringLiteral("12345678-abcd")
                && !restore_button->isEnabled(),
            "Restore this task must emit the checkpoint and frozen book session once");

    SigilAgent::AgentEvent restore_conflict;
    restore_conflict.type = SigilAgent::AgentEventType::TaskRestoreFailed;
    restore_conflict.payload = QJsonObject {
        { QStringLiteral("checkpoint_id"), QStringLiteral("restore-17") },
        { QStringLiteral("code"), QStringLiteral("TASK_RESTORE_CONFLICT") },
        { QStringLiteral("conflicts"), QJsonArray {
            QJsonObject {
                { QStringLiteral("resource_id"), QStringLiteral("chapter-2") },
                { QStringLiteral("reason"), QStringLiteral("content_changed") }
            } } }
    };
    dock.appendEvent(restore_conflict);
    application.processEvents();
    auto *conflict_card = dock.findChild<QWidget *>(
        QStringLiteral("agentTaskRestoreFailedCard-restore-17"));
    auto *conflict_body = conflict_card
        ? conflict_card->findChild<QLabel *>(
              QStringLiteral("agentTaskRestoreFailedCard-restore-17Body"))
        : nullptr;
    Require(conflict_body && conflict_body->text().contains(QStringLiteral("No book content was changed"))
                && restore_button->isEnabled(),
            "a restore conflict must be visible, non-mutating, and retryable after manual resolution");

    SigilAgent::AgentEvent restored;
    restored.type = SigilAgent::AgentEventType::TaskRestoreCompleted;
    restored.payload = QJsonObject {
        { QStringLiteral("checkpoint_id"), QStringLiteral("restore-17") },
        { QStringLiteral("affected_resources"), QJsonArray {
            QStringLiteral("chapter-1"), QStringLiteral("chapter-2") } },
        { QStringLiteral("book_revision"), 18 }
    };
    dock.appendEvent(restored);
    application.processEvents();
    Require(!restore_button->isEnabled()
                && restore_button->text().contains(QStringLiteral("Restored")),
            "a completed task restore must permanently settle its action");

    SigilAgent::AgentEvent rolled_back;
    rolled_back.type = SigilAgent::AgentEventType::TransactionRolledBack;
    rolled_back.payload = QJsonObject {
        { QStringLiteral("rolled_back"), true },
        { QStringLiteral("live_book_unchanged"), true }
    };
    dock.appendEvent(rolled_back);
    application.processEvents();
    auto *rollback_card = dock.findChild<QWidget *>(QStringLiteral("agentRollbackCard"));
    auto *rollback_body = rollback_card
        ? rollback_card->findChild<QLabel *>(QStringLiteral("agentRollbackCardBody"))
        : nullptr;
    Require(rollback_body && rollback_body->isVisible()
                && rollback_body->text().contains(QStringLiteral("live book was not changed")),
            "rollback must render a visible live-book status card");

    composer->setPlainText(QStringLiteral("hello from enter"));
    QString seen;
    QStringList seen_handles;
    int send_count = 0;
    QObject::connect(&dock, &SigilAgent::AgentDock::sendRequested,
                     [&](const QString &text, const QStringList &handles) {
                         ++send_count;
                         seen = text;
                         seen_handles = handles;
                         Require(composer->toPlainText().isEmpty(),
                                 "composer must already be empty when sendRequested fires");
                     });
    auto *send = dock.findChild<QPushButton *>(QStringLiteral("agentSendButton"));
    Require(send && send->isEnabled(), "Send must be enabled while idle");
    send->click();
    application.processEvents();
    Require(send_count == 1 && seen == QStringLiteral("hello from enter")
                && seen_handles == QStringList { QStringLiteral("book") },
            "Send must emit the composer text and exact scope snapshot");
    Require(composer->toPlainText().isEmpty(), "composer must stay empty after Send");

    auto *retry = dock.findChild<QPushButton *>(QStringLiteral("agentRetryButton"));
    Require(retry && !retry->isEnabled(),
            "Retry must remain unavailable until the submitted provider request fails");
    dock.setRunState(SigilAgent::AgentRunState::StreamingResponse);
    dock.appendEvent(provider_started);
    dock.appendEvent(provider_failed);
    dock.setRunState(SigilAgent::AgentRunState::Failed);
    Require(retry->isEnabled()
                && retry->property("bookSessionId").toString()
                    == QStringLiteral("12345678-abcd")
                && retry->property("contextHandles").toStringList()
                    == QStringList { QStringLiteral("book") },
            "a provider failure must enable retry for the exact submitted book and scope");
    retry->click();
    application.processEvents();
    Require(send_count == 2 && seen == QStringLiteral("hello from enter")
                && seen_handles == QStringList { QStringLiteral("book") }
                && !retry->isEnabled(),
            "Retry must resend the frozen text and handles once, then disable itself");

    dock.setRunState(SigilAgent::AgentRunState::StreamingResponse);
    dock.appendEvent(provider_started);
    dock.appendEvent(provider_failed);
    dock.setRunState(SigilAgent::AgentRunState::Failed);
    Require(retry->isEnabled(), "a repeated provider failure must offer another retry");
    dock.setBookContext(QStringLiteral("Another Book"), QStringLiteral("other.epub"),
                        3, false, 1, QStringLiteral("different-book-session"));
    Require(!retry->isEnabled()
                && retry->toolTip().contains(QStringLiteral("open book changed")),
            "retry must fail closed after the window binds a different book session");
    dock.setBookContext(QStringLiteral("Junior Physics"), QStringLiteral("physics.epub"),
                        42, false, 7, QStringLiteral("12345678-abcd"));
    dock.appendEvent(provider_started);
    SigilAgent::AgentEvent late_failure = provider_failed;
    late_failure.payload.insert(QStringLiteral("step"), 2);
    dock.appendEvent(late_failure);
    dock.setRunState(SigilAgent::AgentRunState::Failed);
    Require(!retry->isEnabled()
                && retry->toolTip().contains(QStringLiteral("already executed tools")),
            "a later model-step failure must not offer a retry that could duplicate tool effects");
    dock.setSessionId(QStringLiteral("new-session-id"));
    Require(!retry->isEnabled()
                && retry->property("contextHandles").toStringList().isEmpty(),
            "New Session must discard all retry payload and scope state");
    return EXIT_SUCCESS;
}
