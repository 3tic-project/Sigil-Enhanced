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
    SigilAgent::AgentEvent provider_started;
    provider_started.type = SigilAgent::AgentEventType::ModelRequestStarted;
    dock.appendEvent(provider_started);
    Require(provider_status->text().contains(QStringLiteral("Contacting provider"))
                && provider_status->property("requestState").toString()
                    == QStringLiteral("requesting"),
            "a real model request must move provider status to requesting");
    SigilAgent::AgentEvent provider_completed;
    provider_completed.type = SigilAgent::AgentEventType::ModelRequestCompleted;
    dock.appendEvent(provider_completed);
    Require(provider_status->text().contains(QStringLiteral("Last request succeeded"))
                && provider_status->property("requestState").toString()
                    == QStringLiteral("succeeded"),
            "only a completed model request may report success");
    dock.appendEvent(provider_started);
    SigilAgent::AgentEvent provider_failed;
    provider_failed.type = SigilAgent::AgentEventType::ModelRequestFailed;
    provider_failed.payload = QJsonObject {
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
    SigilAgent::AgentEvent provider_cancelled;
    provider_cancelled.type = SigilAgent::AgentEventType::SessionCancelled;
    dock.appendEvent(provider_cancelled);
    Require(provider_status->text().contains(QStringLiteral("Last request cancelled"))
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
            { QStringLiteral("task_restore_point"),
              QStringLiteral("not_created_by_commit") }
        } }
    };
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
                && applied_body->text().contains(QStringLiteral("did not create a task-wide restore point")),
            "applied card must state validation and recovery boundaries");

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
    QObject::connect(&dock, &SigilAgent::AgentDock::sendRequested,
                     [&](const QString &text, const QStringList &) {
                         seen = text;
                         Require(composer->toPlainText().isEmpty(),
                                 "composer must already be empty when sendRequested fires");
                     });
    auto *send = dock.findChild<QPushButton *>(QStringLiteral("agentSendButton"));
    Require(send && send->isEnabled(), "Send must be enabled while idle");
    send->click();
    application.processEvents();
    Require(seen == QStringLiteral("hello from enter"), "Send must emit the composer text");
    Require(composer->toPlainText().isEmpty(), "composer must stay empty after Send");
    return EXIT_SUCCESS;
}
