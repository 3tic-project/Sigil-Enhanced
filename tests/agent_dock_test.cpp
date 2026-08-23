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
    Require(mode && mode->count() == 3, "mode combo must offer Ask/Plan/Edit");
    Require(mode->itemData(0).toString() == QStringLiteral("ask")
                && mode->itemData(1).toString() == QStringLiteral("plan")
                && mode->itemData(2).toString() == QStringLiteral("edit"),
            "mode combo values must be ask/plan/edit");
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
    return EXIT_SUCCESS;
}
