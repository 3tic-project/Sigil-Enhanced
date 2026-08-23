/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/UI/AgentDock.h"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

namespace SigilAgent
{

AgentDock::AgentDock(QWidget *parent) :
    QDockWidget(tr("Agent"), parent)
{
    setObjectName(QStringLiteral("agentDock"));
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);

    QWidget *root = new QWidget(this);
    auto *root_layout = new QVBoxLayout(root);
    root_layout->setContentsMargins(6, 6, 6, 6);
    root_layout->setSpacing(6);

    auto *header = new QWidget(root);
    header->setObjectName(QStringLiteral("agentHeader"));
    auto *header_layout = new QHBoxLayout(header);
    header_layout->setContentsMargins(0, 0, 0, 0);

    m_modeCombo = new QComboBox(header);
    m_modeCombo->setObjectName(QStringLiteral("agentModeCombo"));
    m_modeCombo->addItem(tr("Ask"), QStringLiteral("ask"));
    m_modeCombo->addItem(tr("Plan"), QStringLiteral("plan"));
    m_modeCombo->addItem(tr("Edit"), QStringLiteral("edit"));

    m_modelEdit = new QLineEdit(header);
    m_modelEdit->setObjectName(QStringLiteral("agentModelEdit"));
    m_modelEdit->setPlaceholderText(tr("model"));
    m_modelEdit->setText(QStringLiteral("deepseek-v4-flash"));

    m_stopButton = new QPushButton(tr("Stop"), header);
    m_stopButton->setObjectName(QStringLiteral("agentStopButton"));
    m_newSessionButton = new QPushButton(tr("New Session"), header);
    m_newSessionButton->setObjectName(QStringLiteral("agentNewSessionButton"));

    header_layout->addWidget(m_modeCombo);
    header_layout->addWidget(m_modelEdit, 1);
    header_layout->addWidget(m_stopButton);
    header_layout->addWidget(m_newSessionButton);

    m_contextScope = new QLabel(tr("Context: book structure + sampled fragments"), root);
    m_contextScope->setObjectName(QStringLiteral("agentContextScope"));
    m_contextScope->setWordWrap(true);
    m_runState = new QLabel(tr("Idle"), root);
    m_runState->setObjectName(QStringLiteral("agentRunState"));

    m_transcript = new QScrollArea(root);
    m_transcript->setObjectName(QStringLiteral("agentTranscript"));
    m_transcript->setWidgetResizable(true);
    m_transcriptContents = new QWidget(m_transcript);
    m_transcriptContents->setObjectName(QStringLiteral("agentTranscriptContents"));
    m_transcriptLayout = new QVBoxLayout(m_transcriptContents);
    m_transcriptLayout->setContentsMargins(0, 0, 0, 0);
    m_transcriptLayout->addStretch(1);
    m_transcript->setWidget(m_transcriptContents);

    m_handles = new QLineEdit(root);
    m_handles->setObjectName(QStringLiteral("agentContextHandles"));
    m_handles->setPlaceholderText(tr("Optional handles: book path, resource id, or selection"));

    m_composer = new QPlainTextEdit(root);
    m_composer->setObjectName(QStringLiteral("agentComposer"));
    m_composer->setPlaceholderText(tr("Ask about this book, plan a change, or describe an edit…"));
    m_composer->setFixedHeight(90);

    m_sendButton = new QPushButton(tr("Send"), root);
    m_sendButton->setObjectName(QStringLiteral("agentSendButton"));

    root_layout->addWidget(header);
    root_layout->addWidget(m_contextScope);
    root_layout->addWidget(m_runState);
    root_layout->addWidget(m_transcript, 1);
    root_layout->addWidget(m_handles);
    root_layout->addWidget(m_composer);
    root_layout->addWidget(m_sendButton, 0, Qt::AlignRight);
    setWidget(root);

    connect(m_sendButton, &QPushButton::clicked, this, &AgentDock::onSend);
    connect(m_stopButton, &QPushButton::clicked, this, &AgentDock::stopRequested);
    connect(m_newSessionButton, &QPushButton::clicked, this, &AgentDock::newSessionRequested);
    connect(m_modeCombo, &QComboBox::currentIndexChanged, this, &AgentDock::onModeChanged);
}

AgentMode AgentDock::mode() const
{
    return modeFromName(m_modeCombo->currentData().toString());
}

void AgentDock::setMode(AgentMode mode)
{
    const QString name = modeName(mode);
    const int index = m_modeCombo->findData(name);
    if (index >= 0) m_modeCombo->setCurrentIndex(index);
}

void AgentDock::setContextScope(const QString &scope)
{
    m_contextScope->setText(tr("Context: %1").arg(scope));
}

void AgentDock::setModelName(const QString &model)
{
    m_modelEdit->setText(model);
}

void AgentDock::setRunState(AgentRunState state)
{
    m_runState->setText(runStateName(state));
    const bool running = state != AgentRunState::Idle
        && state != AgentRunState::Completed
        && state != AgentRunState::Cancelled
        && state != AgentRunState::Failed;
    m_sendButton->setEnabled(!running);
    m_stopButton->setEnabled(running || state == AgentRunState::AwaitingApproval);
}

void AgentDock::resetTranscript()
{
    QLayoutItem *item = nullptr;
    while ((item = m_transcriptLayout->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    m_transcriptLayout->addStretch(1);
    m_approvalCards.clear();
}

QString AgentDock::composerText() const
{
    return m_composer->toPlainText().trimmed();
}

QStringList AgentDock::contextHandles() const
{
    const QString raw = m_handles->text().trimmed();
    if (raw.isEmpty()) return {};
    QStringList handles;
    for (const QString &part : raw.split(QRegularExpression(QStringLiteral("[,\\s]+")), Qt::SkipEmptyParts)) {
        handles.append(part);
    }
    return handles;
}

void AgentDock::onSend()
{
    const QString text = composerText();
    if (text.isEmpty()) return;
    emit sendRequested(text, contextHandles());
    m_composer->clear();
}

void AgentDock::onModeChanged()
{
    emit modeChanged(mode());
}

QWidget *AgentDock::makeCard(const QString &object_name,
                             const QString &title,
                             const QString &body,
                             bool collapsed)
{
    auto *frame = new QFrame(m_transcriptContents);
    frame->setObjectName(object_name);
    frame->setFrameShape(QFrame::StyledPanel);
    auto *layout = new QVBoxLayout(frame);
    auto *toggle = new QToolButton(frame);
    toggle->setObjectName(object_name + QStringLiteral("Title"));
    toggle->setText(title);
    toggle->setToolButtonStyle(Qt::ToolButtonTextOnly);
    toggle->setCheckable(true);
    toggle->setChecked(!collapsed);
    auto *body_label = new QLabel(body, frame);
    body_label->setObjectName(object_name + QStringLiteral("Body"));
    body_label->setWordWrap(true);
    body_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    body_label->setVisible(!collapsed);
    layout->addWidget(toggle);
    layout->addWidget(body_label);
    connect(toggle, &QToolButton::toggled, body_label, &QLabel::setVisible);
    return frame;
}

QWidget *AgentDock::findCard(const QString &object_name) const
{
    return m_transcriptContents->findChild<QWidget *>(object_name);
}

void AgentDock::appendCard(QWidget *card)
{
    m_transcriptLayout->insertWidget(m_transcriptLayout->count() - 1, card);
}

void AgentDock::setThinkingText(const QString &text, bool append)
{
    QWidget *card = findCard(QStringLiteral("agentThinkingCard"));
    if (!card) {
        card = makeCard(QStringLiteral("agentThinkingCard"), tr("Thinking"), text, true);
        appendCard(card);
        return;
    }
    auto *body = card->findChild<QLabel *>(QStringLiteral("agentThinkingCardBody"));
    if (!body) return;
    body->setText(append ? body->text() + text : text);
}

void AgentDock::setAnswerText(const QString &text, bool append)
{
    QWidget *card = findCard(QStringLiteral("agentAnswerCard"));
    if (!card) {
        card = makeCard(QStringLiteral("agentAnswerCard"), tr("Answer"), text, false);
        appendCard(card);
        return;
    }
    auto *body = card->findChild<QLabel *>(QStringLiteral("agentAnswerCardBody"));
    if (!body) return;
    body->setText(append ? body->text() + text : text);
}

void AgentDock::appendEvent(const AgentEvent &event)
{
    switch (event.type) {
        case AgentEventType::UserMessage:
            appendCard(makeCard(QStringLiteral("agentUserCard"),
                                tr("You"),
                                event.payload.value(QStringLiteral("text")).toString(),
                                false));
            break;
        case AgentEventType::AssistantDelta:
            if (event.payload.value(QStringLiteral("kind")).toString() == QLatin1String("reasoning")) {
                setThinkingText(event.payload.value(QStringLiteral("text")).toString(), true);
            } else {
                setAnswerText(event.payload.value(QStringLiteral("text")).toString(), true);
            }
            break;
        case AgentEventType::AssistantMessage: {
            const QString reasoning = event.payload.value(QStringLiteral("reasoning_content")).toString();
            const QString content = event.payload.value(QStringLiteral("content")).toString();
            if (!reasoning.isEmpty() && !findCard(QStringLiteral("agentThinkingCard"))) {
                setThinkingText(reasoning, false);
            }
            if (!content.isEmpty()) setAnswerText(content, false);
            break;
        }
        case AgentEventType::ToolRequested:
        case AgentEventType::ToolStarted:
        case AgentEventType::ToolCompleted:
        case AgentEventType::ToolFailed: {
            const QString name = event.payload.value(QStringLiteral("name")).toString();
            const QString title = event.type == AgentEventType::ToolFailed
                ? tr("Tool failed: %1").arg(name) : tr("Tool: %1").arg(name);
            QString body = name;
            if (event.payload.contains(QStringLiteral("message"))) {
                body = event.payload.value(QStringLiteral("message")).toString();
            } else if (event.payload.value(QStringLiteral("applied")).toBool()) {
                body = tr("Applied to the book (undoable).");
            }
            appendCard(makeCard(QStringLiteral("agentToolCard"), title, body, true));
            break;
        }
        case AgentEventType::ToolApprovalRequested: {
            const QString id = event.payload.value(QStringLiteral("tool_call_id")).toString();
            const QString name = event.payload.value(QStringLiteral("name")).toString();
            const QString impact = event.payload.value(QStringLiteral("impact")).toString();
            auto *frame = new QFrame(m_transcriptContents);
            frame->setObjectName(QStringLiteral("agentApprovalCard"));
            auto *layout = new QVBoxLayout(frame);
            layout->addWidget(new QLabel(tr("Approve %1?").arg(name), frame));
            auto *impact_label = new QLabel(impact, frame);
            impact_label->setObjectName(QStringLiteral("agentApprovalImpact"));
            impact_label->setWordWrap(true);
            layout->addWidget(impact_label);
            auto *buttons = new QHBoxLayout();
            auto *approve = new QPushButton(tr("Approve"), frame);
            auto *deny = new QPushButton(tr("Deny"), frame);
            buttons->addWidget(approve);
            buttons->addWidget(deny);
            layout->addLayout(buttons);
            connect(approve, &QPushButton::clicked, this, [this, id]() {
                emit approvalResponded(id, true);
            });
            connect(deny, &QPushButton::clicked, this, [this, id]() {
                emit approvalResponded(id, false);
            });
            appendCard(frame);
            m_approvalCards.insert(id, frame);
            break;
        }
        case AgentEventType::TransactionPreviewed:
            appendCard(makeCard(QStringLiteral("agentPreviewCard"),
                                tr("Preview"),
                                tr("Staged changes are not on the live book yet."),
                                false));
            break;
        case AgentEventType::TransactionCommitted:
            appendCard(makeCard(QStringLiteral("agentAppliedCard"),
                                tr("Applied"),
                                tr("Committed to the book. Use Undo to reverse this step."),
                                false));
            break;
        case AgentEventType::Error:
            appendCard(makeCard(QStringLiteral("agentErrorCard"),
                                tr("Error"),
                                event.payload.value(QStringLiteral("message")).toString(),
                                false));
            break;
        case AgentEventType::SessionCancelled:
            appendCard(makeCard(QStringLiteral("agentErrorCard"),
                                tr("Stopped"),
                                tr("Run stopped. Uncommitted staged work was rolled back."),
                                false));
            break;
        default:
            break;
    }
}

} // namespace SigilAgent
