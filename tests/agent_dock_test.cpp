#include <cstdlib>
#include <iostream>

#include <QApplication>
#include <QComboBox>
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
    Require(mode && mode->count() == 4, "mode combo must offer Ask/Plan/Edit/Auto");
    Require(mode->itemData(0).toString() == QStringLiteral("ask")
                && mode->itemData(1).toString() == QStringLiteral("plan")
                && mode->itemData(2).toString() == QStringLiteral("edit")
                && mode->itemData(3).toString() == QStringLiteral("auto"),
            "mode combo values must be ask/plan/edit/auto");
    Require(stop && stop->text().contains(QStringLiteral("Stop")), "Stop control is missing");
    Require(fresh && fresh->text().contains(QStringLiteral("New Session")),
            "New Session control is missing");
    Require(composer, "composer is missing");
    Require(transcript, "transcript surface is missing");
    Require(dock.findChild<QToolButton *>(QStringLiteral("agentChipBook")),
            "book context chip is missing");
    Require(dock.findChild<QToolButton *>(QStringLiteral("agentChipFile")),
            "file context chip is missing");
    Require(dock.findChild<QToolButton *>(QStringLiteral("agentChipSelection")),
            "selection context chip is missing");

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
