#include <cstdlib>
#include <iostream>

#include <QApplication>
#include <QComboBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
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
                && usage_details->property("totalTokens").toLongLong() == 155,
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
    application.processEvents();

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

    dock.resetTranscript();
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
                     [&approved](const QString &id, bool ok) {
                         approved = ok && id == QStringLiteral("call-1");
                     });
    approve->click();
    application.processEvents();
    Require(approved, "Approve must emit approvalResponded");
    Require(!approve->isEnabled() && !deny->isEnabled(),
            "Approve/Deny must disable after a decision");
    Require(approve->text().contains(QStringLiteral("Approved")),
            "Approve must show it was accepted");

    dock.resetTranscript();
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
                && applied_body->text().contains(QStringLiteral("Book revision: 17")),
            "applied card must show save state, count, and book revision");
    Require(applied_body->text().contains(QStringLiteral("Full EPUBCheck: not run"))
                && applied_body->text().contains(QStringLiteral("Undo where available"))
                && applied_body->text().contains(QStringLiteral("2 text resource(s)"))
                && applied_body->text().contains(QStringLiteral("conflict check")),
            "applied card must state validation and recovery boundaries");
    auto *restore_button = dock.findChild<QPushButton *>(
        QStringLiteral("agentTaskRestoreButton-restore-17"));
    Require(restore_button && restore_button->isEnabled(),
            "a guarded text commit must offer task restoration");
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
