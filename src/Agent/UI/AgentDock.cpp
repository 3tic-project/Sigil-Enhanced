/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/UI/AgentDock.h"

#include <QComboBox>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
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
    root_layout->setContentsMargins(8, 8, 8, 8);
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
    m_modeCombo->setToolTip(tr("Ask is read-only. Plan can stage a preview. Edit can commit after approval."));

    m_modelEdit = new QLineEdit(header);
    m_modelEdit->setObjectName(QStringLiteral("agentModelEdit"));
    m_modelEdit->setPlaceholderText(tr("model"));
    m_modelEdit->setText(QStringLiteral("deepseek-v4-flash"));

    m_stopButton = new QPushButton(tr("Stop"), header);
    m_stopButton->setObjectName(QStringLiteral("agentStopButton"));
    m_stopButton->setEnabled(false);
    m_newSessionButton = new QPushButton(tr("New Session"), header);
    m_newSessionButton->setObjectName(QStringLiteral("agentNewSessionButton"));

    header_layout->addWidget(m_modeCombo);
    header_layout->addWidget(m_modelEdit, 1);
    header_layout->addWidget(m_stopButton);
    header_layout->addWidget(m_newSessionButton);

    auto *status = new QWidget(root);
    auto *status_layout = new QHBoxLayout(status);
    status_layout->setContentsMargins(0, 0, 0, 0);
    m_runState = new QLabel(tr("Idle"), status);
    m_runState->setObjectName(QStringLiteral("agentRunState"));
    m_contextScope = new QLabel(status);
    m_contextScope->setObjectName(QStringLiteral("agentContextScope"));
    m_contextScope->setWordWrap(true);
    status_layout->addWidget(m_runState);
    status_layout->addWidget(m_contextScope, 1);

    auto *chips = new QWidget(root);
    chips->setObjectName(QStringLiteral("agentContextChips"));
    auto *chips_layout = new QHBoxLayout(chips);
    chips_layout->setContentsMargins(0, 0, 0, 0);
    m_chipBook = new QToolButton(chips);
    m_chipBook->setObjectName(QStringLiteral("agentChipBook"));
    m_chipBook->setCheckable(true);
    m_chipBook->setChecked(true);
    m_chipBook->setText(tr("Book"));
    m_chipFile = new QToolButton(chips);
    m_chipFile->setObjectName(QStringLiteral("agentChipFile"));
    m_chipFile->setCheckable(true);
    m_chipFile->setChecked(true);
    m_chipFile->setText(tr("File"));
    m_chipFile->setEnabled(false);
    m_chipSelection = new QToolButton(chips);
    m_chipSelection->setObjectName(QStringLiteral("agentChipSelection"));
    m_chipSelection->setCheckable(true);
    m_chipSelection->setChecked(false);
    m_chipSelection->setText(tr("Selection"));
    m_chipSelection->setEnabled(false);
    chips_layout->addWidget(m_chipBook);
    chips_layout->addWidget(m_chipFile);
    chips_layout->addWidget(m_chipSelection);
    chips_layout->addStretch(1);

    m_transcript = new QScrollArea(root);
    m_transcript->setObjectName(QStringLiteral("agentTranscript"));
    m_transcript->setWidgetResizable(true);
    m_transcriptContents = new QWidget(m_transcript);
    m_transcriptContents->setObjectName(QStringLiteral("agentTranscriptContents"));
    m_transcriptLayout = new QVBoxLayout(m_transcriptContents);
    m_transcriptLayout->setContentsMargins(0, 0, 0, 0);
    m_transcriptLayout->setSpacing(8);
    m_transcriptLayout->addStretch(1);
    m_transcript->setWidget(m_transcriptContents);

    m_composer = new QPlainTextEdit(root);
    m_composer->setObjectName(QStringLiteral("agentComposer"));
    m_composer->setPlaceholderText(tr("Ask about this book, plan a change, or describe an edit…"));
    m_composer->setFixedHeight(96);
    m_composer->installEventFilter(this);

    m_composerHint = new QLabel(tr("Enter to send · Shift+Enter for a new line · Stop cancels the in-flight request"), root);
    m_composerHint->setObjectName(QStringLiteral("agentComposerHint"));
    m_composerHint->setStyleSheet(QStringLiteral("color: palette(mid);"));

    m_sendButton = new QPushButton(tr("Send"), root);
    m_sendButton->setObjectName(QStringLiteral("agentSendButton"));

    root_layout->addWidget(header);
    root_layout->addWidget(status);
    root_layout->addWidget(chips);
    root_layout->addWidget(m_transcript, 1);
    root_layout->addWidget(m_composer);
    root_layout->addWidget(m_composerHint);
    root_layout->addWidget(m_sendButton, 0, Qt::AlignRight);
    setWidget(root);
    refreshScopeLabel();

    connect(m_sendButton, &QPushButton::clicked, this, &AgentDock::onSend);
    connect(m_stopButton, &QPushButton::clicked, this, &AgentDock::stopRequested);
    connect(m_newSessionButton, &QPushButton::clicked, this, &AgentDock::newSessionRequested);
    connect(m_modeCombo, &QComboBox::currentIndexChanged, this, &AgentDock::onModeChanged);
    connect(m_chipBook, &QToolButton::toggled, this, [this](bool) { refreshScopeLabel(); });
    connect(m_chipFile, &QToolButton::toggled, this, [this](bool) { refreshScopeLabel(); });
    connect(m_chipSelection, &QToolButton::toggled, this, [this](bool) { refreshScopeLabel(); });
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

void AgentDock::setBookContext(const QString &title, quint64 revision)
{
    m_bookTitle = title;
    m_bookRevision = revision;
    m_chipBook->setText(title.isEmpty()
                            ? tr("Book")
                            : tr("Book · %1 (rev %2)").arg(title).arg(revision));
    refreshScopeLabel();
}

void AgentDock::setCurrentFile(const QString &book_path, const QString &resource_id)
{
    m_filePath = book_path;
    m_fileId = resource_id;
    const bool have = !book_path.isEmpty() || !resource_id.isEmpty();
    const bool was_enabled = m_chipFile->isEnabled();
    m_chipFile->setEnabled(have);
    if (have && !was_enabled) m_chipFile->setChecked(true);
    if (!have) m_chipFile->setChecked(false);
    m_chipFile->setText(have ? tr("File · %1").arg(book_path.isEmpty() ? resource_id : book_path)
                             : tr("File"));
    refreshScopeLabel();
}

void AgentDock::setSelection(const QString &resource_id, int start, int end, const QString &snippet)
{
    m_selectionId = resource_id;
    m_selectionStart = start;
    m_selectionEnd = end;
    m_selectionSnippet = snippet;
    const bool have = !resource_id.isEmpty() && end > start;
    const bool was_enabled = m_chipSelection->isEnabled();
    m_chipSelection->setEnabled(have);
    if (have && !was_enabled) m_chipSelection->setChecked(true);
    if (!have) m_chipSelection->setChecked(false);
    m_chipSelection->setText(have ? tr("Selection · %1–%2").arg(start).arg(end) : tr("Selection"));
    refreshScopeLabel();
}

void AgentDock::refreshScopeLabel()
{
    QStringList parts;
    if (m_chipBook->isChecked()) parts.append(tr("book structure"));
    if (m_chipFile->isEnabled() && m_chipFile->isChecked()) parts.append(tr("current file"));
    if (m_chipSelection->isEnabled() && m_chipSelection->isChecked()) parts.append(tr("selection"));
    if (parts.isEmpty()) parts.append(tr("book structure + sampled fragments"));
    setContextScope(parts.join(QStringLiteral(" + ")));
}

void AgentDock::resetTranscript()
{
    QLayoutItem *item = nullptr;
    while ((item = m_transcriptLayout->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }
    m_transcriptLayout->addStretch(1);
    m_approvalCards.clear();
    m_currentThinking = nullptr;
    m_currentAnswer = nullptr;
    m_turn = 0;
}

QString AgentDock::composerText() const
{
    return m_composer->toPlainText().trimmed();
}

QStringList AgentDock::contextHandles() const
{
    QStringList handles;
    if (m_chipBook->isChecked()) handles.append(QStringLiteral("book"));
    if (m_chipFile->isEnabled() && m_chipFile->isChecked()) {
        handles.append(m_fileId.isEmpty() ? m_filePath : m_fileId);
    }
    if (m_chipSelection->isEnabled() && m_chipSelection->isChecked()) {
        handles.append(QStringLiteral("%1:%2-%3")
                           .arg(m_selectionId)
                           .arg(m_selectionStart)
                           .arg(m_selectionEnd));
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

bool AgentDock::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_composer && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
            && !(key->modifiers() & Qt::ShiftModifier)) {
            onSend();
            return true;
        }
    }
    return QDockWidget::eventFilter(watched, event);
}

QString AgentDock::thinkingCardName() const
{
    return m_turn <= 1 ? QStringLiteral("agentThinkingCard")
                       : QStringLiteral("agentThinkingCard%1").arg(m_turn);
}

QString AgentDock::answerCardName() const
{
    return m_turn <= 1 ? QStringLiteral("agentAnswerCard")
                       : QStringLiteral("agentAnswerCard%1").arg(m_turn);
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
    QScrollBar *bar = m_transcript->verticalScrollBar();
    bar->setValue(bar->maximum());
}

void AgentDock::beginUserTurn()
{
    ++m_turn;
    m_currentThinking = nullptr;
    m_currentAnswer = nullptr;
}

void AgentDock::setThinkingText(const QString &text, bool append)
{
    if (!m_currentThinking) {
        m_currentThinking = makeCard(thinkingCardName(), tr("Thinking"), text, true);
        appendCard(m_currentThinking);
        return;
    }
    auto *body = m_currentThinking->findChild<QLabel *>(thinkingCardName() + QStringLiteral("Body"));
    if (!body) return;
    body->setText(append ? body->text() + text : text);
}

void AgentDock::setAnswerText(const QString &text, bool append)
{
    if (!m_currentAnswer) {
        m_currentAnswer = makeCard(answerCardName(), tr("Answer"), text, false);
        appendCard(m_currentAnswer);
        return;
    }
    auto *body = m_currentAnswer->findChild<QLabel *>(answerCardName() + QStringLiteral("Body"));
    if (!body) return;
    body->setText(append ? body->text() + text : text);
}

QString AgentDock::previewBody(const QJsonObject &payload) const
{
    const QJsonArray changes = payload.value(QStringLiteral("changes")).toArray();
    QStringList lines;
    lines.append(tr("Staged changes are not on the live book yet."));
    for (const QJsonValue &value : changes) {
        const QJsonObject change = value.toObject();
        lines.append(QStringLiteral("• %1  %2 → %3")
                         .arg(change.value(QStringLiteral("resource_id")).toString(),
                              QString::number(change.value(QStringLiteral("original_length")).toInt()),
                              QString::number(change.value(QStringLiteral("staged_length")).toInt())));
    }
    if (payload.value(QStringLiteral("metadata_changed")).toBool()) {
        lines.append(tr("• metadata staged"));
    }
    return lines.join(QLatin1Char('\n'));
}

void AgentDock::appendEvent(const AgentEvent &event)
{
    switch (event.type) {
        case AgentEventType::UserMessage:
            beginUserTurn();
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
            if (!reasoning.isEmpty() && !m_currentThinking) setThinkingText(reasoning, false);
            if (!content.isEmpty()) setAnswerText(content, false);
            break;
        }
        case AgentEventType::ToolRequested:
        case AgentEventType::ToolStarted:
        case AgentEventType::ToolCompleted:
        case AgentEventType::ToolFailed: {
            const QString id = event.payload.value(QStringLiteral("tool_call_id")).toString();
            const QString name = event.payload.value(QStringLiteral("name")).toString();
            QString title = tr("Tool: %1").arg(name);
            if (event.type == AgentEventType::ToolFailed) title = tr("Tool failed: %1").arg(name);
            else if (event.type == AgentEventType::ToolStarted) title = tr("Tool running: %1").arg(name);
            else if (event.payload.value(QStringLiteral("applied")).toBool()) {
                title = tr("Applied: %1").arg(name);
            }
            QString body = name;
            if (event.payload.contains(QStringLiteral("message"))) {
                body = event.payload.value(QStringLiteral("message")).toString();
            } else if (event.payload.contains(QStringLiteral("data"))) {
                body = QString::fromUtf8(
                    QJsonDocument(event.payload.value(QStringLiteral("data")).toObject())
                        .toJson(QJsonDocument::Compact));
                if (body.size() > 400) body = body.left(400) + QStringLiteral("…");
            }
            const QString object_name = id.isEmpty()
                ? QStringLiteral("agentToolCard")
                : QStringLiteral("agentToolCard-%1").arg(id);
            if (QWidget *existing = findCard(object_name)) {
                if (auto *title_btn = existing->findChild<QToolButton *>(object_name + QStringLiteral("Title"))) {
                    title_btn->setText(title);
                }
                if (auto *body_label = existing->findChild<QLabel *>(object_name + QStringLiteral("Body"))) {
                    body_label->setText(body);
                }
            } else {
                appendCard(makeCard(object_name, title, body, true));
            }
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
                                previewBody(event.payload),
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
