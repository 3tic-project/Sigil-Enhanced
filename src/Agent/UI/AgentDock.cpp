/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/UI/AgentDock.h"

#include <QAction>
#include <QButtonGroup>
#include <QComboBox>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>
#include <QVariant>
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
    m_modeCombo->addItem(tr("Auto"), QStringLiteral("auto"));
    m_modeCombo->setToolTip(tr("Ask is read-only. Plan can stage a preview. Edit can commit after approval. Auto commits without asking."));

    auto *export_button = new QToolButton(header);
    export_button->setObjectName(QStringLiteral("agentExportButton"));
    export_button->setText(tr("Export"));
    export_button->setPopupMode(QToolButton::InstantPopup);
    auto *export_menu = new QMenu(export_button);
    QAction *export_conversation = export_menu->addAction(tr("Conversation…"));
    export_conversation->setObjectName(QStringLiteral("agentExportConversationAction"));
    QAction *export_debug = export_menu->addAction(tr("Debug log…"));
    export_debug->setObjectName(QStringLiteral("agentExportDebugAction"));
    export_button->setMenu(export_menu);

    m_stopButton = new QPushButton(tr("Stop"), header);
    m_stopButton->setObjectName(QStringLiteral("agentStopButton"));
    m_stopButton->setEnabled(false);
    m_newSessionButton = new QPushButton(tr("New Session"), header);
    m_newSessionButton->setObjectName(QStringLiteral("agentNewSessionButton"));

    header_layout->addWidget(m_modeCombo);
    header_layout->addStretch(1);
    header_layout->addWidget(export_button);
    header_layout->addWidget(m_stopButton);
    header_layout->addWidget(m_newSessionButton);

    auto *status = new QWidget(root);
    auto *status_layout = new QHBoxLayout(status);
    status_layout->setContentsMargins(0, 0, 0, 0);
    m_runState = new QLabel(tr("Idle"), status);
    m_runState->setObjectName(QStringLiteral("agentRunState"));
    m_modelLabel = new QLabel(status);
    m_modelLabel->setObjectName(QStringLiteral("agentModelLabel"));
    m_modelLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_modelLabel->setToolTip(tr("Model is chosen in Preferences → Native Agent"));
    m_contextScope = new QLabel(status);
    m_contextScope->setObjectName(QStringLiteral("agentContextScope"));
    m_contextScope->setWordWrap(true);
    status_layout->addWidget(m_runState);
    status_layout->addWidget(m_modelLabel);
    status_layout->addWidget(m_contextScope, 1);

    m_providerStatus = new QLabel(root);
    m_providerStatus->setObjectName(QStringLiteral("agentProviderStatus"));
    m_providerStatus->setWordWrap(true);
    m_providerStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_bookStatus = new QLabel(root);
    m_bookStatus->setObjectName(QStringLiteral("agentBookStatus"));
    m_bookStatus->setWordWrap(true);
    m_bookStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_bookStatus->setToolTip(
        tr("Agent plans and tool calls are bound to this open book."));

    auto *chips = new QWidget(root);
    chips->setObjectName(QStringLiteral("agentContextChips"));
    auto *chips_layout = new QHBoxLayout(chips);
    chips_layout->setContentsMargins(0, 0, 0, 0);
    m_scopeGroup = new QButtonGroup(chips);
    m_scopeGroup->setExclusive(true);
    m_chipBook = new QToolButton(chips);
    m_chipBook->setObjectName(QStringLiteral("agentChipBook"));
    m_chipBook->setCheckable(true);
    m_chipBook->setText(tr("Whole book"));
    m_chipFile = new QToolButton(chips);
    m_chipFile->setObjectName(QStringLiteral("agentChipFile"));
    m_chipFile->setCheckable(true);
    m_chipFile->setText(tr("File"));
    m_chipFile->setEnabled(false);
    m_chipSelectedFiles = new QToolButton(chips);
    m_chipSelectedFiles->setObjectName(QStringLiteral("agentChipSelectedFiles"));
    m_chipSelectedFiles->setCheckable(true);
    m_chipSelectedFiles->setText(tr("Selected files"));
    m_chipSelectedFiles->setEnabled(false);
    m_chipSelection = new QToolButton(chips);
    m_chipSelection->setObjectName(QStringLiteral("agentChipSelection"));
    m_chipSelection->setCheckable(true);
    m_chipSelection->setChecked(false);
    m_chipSelection->setText(tr("Selection"));
    m_chipSelection->setEnabled(false);
    m_scopeGroup->addButton(m_chipBook);
    m_scopeGroup->addButton(m_chipFile);
    m_scopeGroup->addButton(m_chipSelectedFiles);
    m_scopeGroup->addButton(m_chipSelection);
    chips_layout->addWidget(m_chipBook);
    chips_layout->addWidget(m_chipFile);
    chips_layout->addWidget(m_chipSelectedFiles);
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
    root_layout->addWidget(m_providerStatus);
    root_layout->addWidget(m_bookStatus);
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
    connect(export_conversation, &QAction::triggered, this, &AgentDock::exportConversationRequested);
    connect(export_debug, &QAction::triggered, this, &AgentDock::exportDebugLogRequested);
    connect(m_modeCombo, &QComboBox::currentIndexChanged, this, &AgentDock::onModeChanged);
    for (QToolButton *scope : {m_chipBook, m_chipFile, m_chipSelectedFiles, m_chipSelection}) {
        connect(scope, &QToolButton::clicked, this, [this]() {
            m_scopeChoiceExplicit = true;
            refreshScopeLabel();
        });
    }
    chooseDefaultScope();
    refreshProviderStatus();
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
    if (!m_modelLabel) return;
    m_modelLabel->setText(model.isEmpty()
                              ? tr("No model (set in Preferences)")
                              : model);
    m_modelLabel->setToolTip(tr("Model is chosen in Preferences → Native Agent"));
}

void AgentDock::setProviderConfiguration(const AgentProviderReadiness &readiness)
{
    m_providerReadiness = readiness;
    m_providerRequestState = ProviderRequestState::Configured;
    m_providerFailure.clear();
    setModelName(readiness.model);
    refreshProviderStatus();
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
    m_newSessionButton->setEnabled(!running);
    m_modeCombo->setEnabled(!running);
}

void AgentDock::setBookContext(const QString &title,
                               const QString &file_name,
                               int resource_count,
                               bool modified,
                               quint64 revision,
                               const QString &book_session_id)
{
    m_bookTitle = title;
    m_bookFileName = file_name;
    m_bookResourceCount = qMax(0, resource_count);
    m_bookModified = modified;
    m_bookRevision = revision;
    m_bookSessionId = book_session_id;
    m_bookStatus->setProperty("bookTitle", title);
    m_bookStatus->setProperty("bookFileName", file_name);
    m_bookStatus->setProperty("resourceCount", m_bookResourceCount);
    m_bookStatus->setProperty("modified", modified);
    m_bookStatus->setProperty("agentRevision", QVariant::fromValue<qulonglong>(revision));
    m_bookStatus->setProperty("bookSessionId", book_session_id);
    QString identity;
    if (!file_name.trimmed().isEmpty() && !title.trimmed().isEmpty()) {
        identity = tr("%1 — %2").arg(file_name.trimmed(), title.trimmed());
    } else if (!file_name.trimmed().isEmpty()) {
        identity = file_name.trimmed();
    } else if (!title.trimmed().isEmpty()) {
        identity = title.trimmed();
    } else {
        identity = tr("Untitled book");
    }
    const QString resources = m_bookResourceCount == 1
        ? tr("%1 resource").arg(m_bookResourceCount)
        : tr("%1 resources").arg(m_bookResourceCount);
    const QString save_state = modified ? tr("Unsaved changes") : tr("Saved");
    m_bookStatus->setText(
        tr("Current book: %1 · %2 · %3 · Book session %4 · Agent rev %5")
            .arg(identity, resources, save_state)
            .arg(book_session_id.left(8))
            .arg(revision));
    m_bookStatus->setAccessibleName(m_bookStatus->text());
    m_chipBook->setText(tr("Whole book"));
    m_chipBook->setToolTip(tr("Attach the complete resource map for %1").arg(identity));
    refreshScopeLabel();
}

void AgentDock::setCurrentFile(const QString &book_path, const QString &resource_id)
{
    m_filePath = book_path;
    m_fileId = resource_id;
    const bool have = !book_path.isEmpty() || !resource_id.isEmpty();
    m_chipFile->setEnabled(have);
    m_chipFile->setText(have ? tr("File · %1").arg(book_path.isEmpty() ? resource_id : book_path)
                             : tr("File"));
    chooseDefaultScope();
}

void AgentDock::setSelectedFiles(const QStringList &book_paths,
                                 const QStringList &resource_ids)
{
    m_selectedFilePaths.clear();
    m_selectedFileIds.clear();
    const int count = qMin(book_paths.size(), resource_ids.size());
    for (int i = 0; i < count; ++i) {
        const QString id = resource_ids.at(i).trimmed();
        if (id.isEmpty() || m_selectedFileIds.contains(id)) continue;
        m_selectedFileIds.append(id);
        m_selectedFilePaths.append(book_paths.at(i));
    }
    m_chipSelectedFiles->setProperty("resourceIds", m_selectedFileIds);
    m_chipSelectedFiles->setProperty("bookPaths", m_selectedFilePaths);
    const int selected = m_selectedFileIds.size();
    m_chipSelectedFiles->setEnabled(selected > 0);
    if (selected == 1) {
        const QString label = m_selectedFilePaths.first().isEmpty()
            ? m_selectedFileIds.first() : m_selectedFilePaths.first();
        m_chipSelectedFiles->setText(tr("Selected file · %1").arg(label));
    } else {
        m_chipSelectedFiles->setText(selected > 0
            ? tr("Selected files · %1").arg(selected)
            : tr("Selected files"));
    }
    m_chipSelectedFiles->setToolTip(selected > 0
        ? tr("Attach %1 file(s) selected in Book Browser").arg(selected)
        : tr("Select one or more files in Book Browser"));
    chooseDefaultScope();
}

void AgentDock::setSelection(const QString &resource_id, int start, int end, const QString &snippet)
{
    m_selectionId = resource_id;
    m_selectionStart = start;
    m_selectionEnd = end;
    m_selectionSnippet = snippet;
    m_chipSelection->setProperty("resourceId", resource_id);
    m_chipSelection->setProperty("selectionStart", start);
    m_chipSelection->setProperty("selectionEnd", end);
    const bool have = !resource_id.isEmpty() && end > start;
    m_chipSelection->setEnabled(have);
    m_chipSelection->setText(have ? tr("Selection · %1–%2").arg(start).arg(end) : tr("Selection"));
    chooseDefaultScope();
}

void AgentDock::chooseDefaultScope()
{
    const bool explicit_scope_is_valid = m_scopeChoiceExplicit
        && ((m_chipSelection->isChecked() && m_chipSelection->isEnabled())
            || (m_chipFile->isChecked() && m_chipFile->isEnabled())
            || (m_chipSelectedFiles->isChecked() && m_chipSelectedFiles->isEnabled())
            || m_chipBook->isChecked());
    if (explicit_scope_is_valid) {
        refreshScopeLabel();
        return;
    }
    m_scopeChoiceExplicit = false;
    if (m_chipSelection->isEnabled()) {
        m_chipSelection->setChecked(true);
    } else if (m_chipFile->isEnabled()) {
        m_chipFile->setChecked(true);
    } else if (m_chipSelectedFiles->isEnabled()) {
        m_chipSelectedFiles->setChecked(true);
    } else {
        m_chipBook->setChecked(true);
    }
    refreshScopeLabel();
}

void AgentDock::refreshScopeLabel()
{
    QStringList parts;
    if (m_chipBook->isChecked()) parts.append(tr("whole book"));
    if (m_chipFile->isEnabled() && m_chipFile->isChecked()) parts.append(tr("current file"));
    if (m_chipSelectedFiles->isEnabled() && m_chipSelectedFiles->isChecked()) {
        parts.append(m_selectedFileIds.size() == 1
            ? tr("one selected file")
            : tr("%1 selected files").arg(m_selectedFileIds.size()));
    }
    if (m_chipSelection->isEnabled() && m_chipSelection->isChecked()) parts.append(tr("selection"));
    setContextScope(parts.join(QStringLiteral(" + ")));
}

QString AgentDock::providerFailureSummary(const QString &message) const
{
    if (message.contains(QStringLiteral("base URL is not configured"), Qt::CaseInsensitive)) {
        return tr("Endpoint is not configured");
    }
    if (message.contains(QStringLiteral("API key is not configured"), Qt::CaseInsensitive)) {
        return tr("API key is not configured");
    }
    if (message.contains(QStringLiteral("Model is not configured"), Qt::CaseInsensitive)) {
        return tr("Model is not configured");
    }

    const QRegularExpressionMatch http =
        QRegularExpression(QStringLiteral("^HTTP\\s+(\\d{3})"),
                           QRegularExpression::CaseInsensitiveOption).match(message.trimmed());
    if (http.hasMatch()) {
        const int status = http.captured(1).toInt();
        if (status == 401) return tr("Authentication failed (HTTP 401)");
        if (status == 403) return tr("Access denied (HTTP 403)");
        if (status == 404) return tr("Endpoint or model not found (HTTP 404)");
        if (status == 408) return tr("Provider request timed out (HTTP 408)");
        if (status == 429) return tr("Provider rate limit reached (HTTP 429)");
        if (status >= 500) return tr("Provider unavailable (HTTP %1)").arg(status);
        return tr("Provider returned HTTP %1").arg(status);
    }
    if (message.contains(QStringLiteral("timed out"), Qt::CaseInsensitive)
        || message.contains(QStringLiteral("host not found"), Qt::CaseInsensitive)
        || message.contains(QStringLiteral("connection refused"), Qt::CaseInsensitive)
        || message.contains(QStringLiteral("network"), Qt::CaseInsensitive)
        || message.contains(QStringLiteral("SSL"), Qt::CaseInsensitive)
        || message.contains(QStringLiteral("TLS"), Qt::CaseInsensitive)) {
        return tr("Network connection failed");
    }
    return tr("Request failed; see the Error card");
}

void AgentDock::refreshProviderStatus()
{
    if (!m_providerStatus) return;
    QStringList identity;
    if (!m_providerReadiness.displayName.isEmpty()) {
        identity.append(m_providerReadiness.displayName);
    }
    if (!m_providerReadiness.model.isEmpty()) {
        identity.append(m_providerReadiness.model);
    }
    if (!m_providerReadiness.endpointHost.isEmpty()) {
        identity.append(m_providerReadiness.endpointHost);
    }

    QString state;
    QString state_name;
    switch (m_providerReadiness.issue) {
        case AgentProviderSetupIssue::Endpoint:
            state = tr("Setup required: endpoint");
            state_name = QStringLiteral("setup_required");
            break;
        case AgentProviderSetupIssue::ApiKey:
            state = tr("Setup required: API key");
            state_name = QStringLiteral("setup_required");
            break;
        case AgentProviderSetupIssue::Model:
            state = tr("Setup required: model");
            state_name = QStringLiteral("setup_required");
            break;
        case AgentProviderSetupIssue::None:
            switch (m_providerRequestState) {
                case ProviderRequestState::Configured:
                    state = tr("Configured · not tested");
                    state_name = QStringLiteral("configured");
                    break;
                case ProviderRequestState::Requesting:
                    state = tr("Contacting provider…");
                    state_name = QStringLiteral("requesting");
                    break;
                case ProviderRequestState::Succeeded:
                    state = tr("Last request succeeded");
                    state_name = QStringLiteral("succeeded");
                    break;
                case ProviderRequestState::Failed:
                    state = tr("Last request failed: %1").arg(m_providerFailure);
                    state_name = QStringLiteral("failed");
                    break;
                case ProviderRequestState::Cancelled:
                    state = tr("Last request cancelled");
                    state_name = QStringLiteral("cancelled");
                    break;
            }
            break;
    }

    const QString details = identity.join(QStringLiteral(" · "));
    m_providerStatus->setText(details.isEmpty()
                                  ? state
                                  : tr("Provider: %1 · %2").arg(details, state));
    m_providerStatus->setProperty("providerName", m_providerReadiness.displayName);
    m_providerStatus->setProperty("model", m_providerReadiness.model);
    m_providerStatus->setProperty("endpointHost", m_providerReadiness.endpointHost);
    m_providerStatus->setProperty("requestState", state_name);
    m_providerStatus->setAccessibleName(m_providerStatus->text());
    m_providerStatus->setToolTip(m_providerReadiness.isConfigured()
        ? tr("Configured means the required settings are present. Connectivity is verified only by a real request.")
        : tr("Configure the provider in Preferences → Native Agent."));
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
    m_step = 0;
}

QString AgentDock::composerText() const
{
    return m_composer->toPlainText().trimmed();
}

QStringList AgentDock::contextHandles() const
{
    QStringList handles;
    if (m_chipBook->isChecked()) handles.append(QStringLiteral("book"));
    else if (m_chipFile->isEnabled() && m_chipFile->isChecked()) {
        handles.append(m_fileId.isEmpty() ? m_filePath : m_fileId);
    } else if (m_chipSelectedFiles->isEnabled() && m_chipSelectedFiles->isChecked()) {
        handles.append(m_selectedFileIds);
    } else if (m_chipSelection->isEnabled() && m_chipSelection->isChecked()) {
        handles.append(QStringLiteral("%1:%2-%3")
                           .arg(m_selectionId)
                           .arg(m_selectionStart)
                           .arg(m_selectionEnd));
    }
    return handles;
}

void AgentDock::onSend()
{
    if (m_sendButton && !m_sendButton->isEnabled()) return;
    const QString text = composerText();
    if (text.isEmpty()) return;
    m_composer->clear();
    if (m_sendButton) m_sendButton->setEnabled(false);
    emit sendRequested(text, contextHandles());
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
    const int turn = qMax(1, m_turn);
    const int step = qMax(1, m_step);
    if (turn == 1 && step == 1) return QStringLiteral("agentThinkingCard");
    if (step == 1) return QStringLiteral("agentThinkingCard%1").arg(turn);
    return QStringLiteral("agentThinkingCard%1-%2").arg(turn).arg(step);
}

QString AgentDock::answerCardName() const
{
    const int turn = qMax(1, m_turn);
    const int step = qMax(1, m_step);
    if (turn == 1 && step == 1) return QStringLiteral("agentAnswerCard");
    if (step == 1) return QStringLiteral("agentAnswerCard%1").arg(turn);
    return QStringLiteral("agentAnswerCard%1-%2").arg(turn).arg(step);
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
    m_step = 0;
    m_currentThinking = nullptr;
    m_currentAnswer = nullptr;
}

void AgentDock::beginModelStep()
{
    if (m_currentAnswer || m_currentThinking || m_step >= 1) {
        m_step = qMax(2, m_step + 1);
        m_currentThinking = nullptr;
        m_currentAnswer = nullptr;
        return;
    }
    m_step = 1;
}

void AgentDock::settleApproval(const QString &toolCallId, bool approved)
{
    QWidget *card = m_approvalCards.value(toolCallId);
    if (!card) return;
    if (auto *approve = card->findChild<QPushButton *>(
            QStringLiteral("agentApproveButton-%1").arg(toolCallId))) {
        approve->setEnabled(false);
        if (approved) approve->setText(tr("Approved"));
    }
    if (auto *deny = card->findChild<QPushButton *>(
            QStringLiteral("agentDenyButton-%1").arg(toolCallId))) {
        deny->setEnabled(false);
        if (!approved) deny->setText(tr("Denied"));
    }
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
    lines.append(tr("The live book is unchanged; these changes are staged only."));
    bool described_change = false;
    for (const QJsonValue &value : changes) {
        const QJsonObject change = value.toObject();
        if (change.value(QStringLiteral("added")).toBool()) {
            lines.append(tr("• Added: %1")
                             .arg(change.value(QStringLiteral("book_path")).toString()));
            described_change = true;
            continue;
        }
        if (change.value(QStringLiteral("renamed")).toBool()) {
            lines.append(tr("• Renamed: %1 → %2")
                             .arg(change.value(QStringLiteral("from")).toString(),
                                  change.value(QStringLiteral("book_path")).toString()));
            described_change = true;
            continue;
        }
        const bool has_text_lengths = change.contains(QStringLiteral("original_length"))
            && change.contains(QStringLiteral("staged_length"));
        const bool changed = !change.contains(QStringLiteral("changed"))
            || change.value(QStringLiteral("changed")).toBool();
        if (has_text_lengths && changed) {
            lines.append(tr("• Text: %1 (%2 → %3)")
                             .arg(change.value(QStringLiteral("resource_id")).toString())
                             .arg(change.value(QStringLiteral("original_length")).toInt())
                             .arg(change.value(QStringLiteral("staged_length")).toInt()));
            described_change = true;
        }
    }
    if (payload.value(QStringLiteral("metadata_changed")).toBool()) {
        lines.append(tr("• Metadata changes"));
        described_change = true;
    }
    if (payload.value(QStringLiteral("spine_changed")).toBool()) {
        lines.append(tr("• Reading order changes"));
        described_change = true;
    }
    if (payload.value(QStringLiteral("toc_changed")).toBool()) {
        lines.append(tr("• TOC hierarchy changes"));
        described_change = true;
    }
    for (const QJsonValue &value : payload.value(QStringLiteral("removed")).toArray()) {
        lines.append(tr("• Removed: %1").arg(value.toString()));
        described_change = true;
    }
    if (!described_change) {
        lines.append(tr("No staged differences were reported."));
    }
    return lines.join(QLatin1Char('\n'));
}

QString AgentDock::appliedBody(const QJsonObject &payload) const
{
    QStringList lines;
    lines.append(tr("Applied to the current book. The EPUB file has not been saved."));
    if (payload.value(QStringLiteral("applied_changes")).isDouble()) {
        lines.append(tr("Applied changes: %1")
                         .arg(payload.value(QStringLiteral("applied_changes")).toInt()));
    }
    if (payload.value(QStringLiteral("book_revision")).isDouble()) {
        lines.append(tr("Book revision: %1")
                         .arg(payload.value(QStringLiteral("book_revision")).toInteger()));
    }
    const QString epubcheck_status =
        payload.value(QStringLiteral("full_epubcheck")).toObject()
            .value(QStringLiteral("status")).toString();
    if (epubcheck_status.isEmpty() || epubcheck_status == QLatin1String("not_run")) {
        lines.append(tr("Full EPUBCheck: not run."));
    } else {
        lines.append(tr("Full EPUBCheck: %1").arg(epubcheck_status));
    }
    lines.append(tr("Recovery: use Sigil Undo where available."));
    if (payload.value(QStringLiteral("recovery")).toObject()
            .value(QStringLiteral("task_restore_point")).toString()
        == QLatin1String("not_created_by_commit")) {
        lines.append(tr("This commit did not create a task-wide restore point."));
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
        case AgentEventType::ModelRequestStarted:
            beginModelStep();
            m_providerRequestState = ProviderRequestState::Requesting;
            m_providerFailure.clear();
            refreshProviderStatus();
            break;
        case AgentEventType::ModelRequestCompleted:
            m_providerRequestState = ProviderRequestState::Succeeded;
            m_providerFailure.clear();
            refreshProviderStatus();
            break;
        case AgentEventType::ModelRequestFailed:
            m_providerRequestState = ProviderRequestState::Failed;
            m_providerFailure = providerFailureSummary(
                event.payload.value(QStringLiteral("message")).toString());
            refreshProviderStatus();
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
            frame->setObjectName(QStringLiteral("agentApprovalCard-%1").arg(id));
            auto *layout = new QVBoxLayout(frame);
            layout->addWidget(new QLabel(tr("Approve %1?").arg(name), frame));
            auto *impact_label = new QLabel(impact, frame);
            impact_label->setObjectName(QStringLiteral("agentApprovalImpact"));
            impact_label->setWordWrap(true);
            layout->addWidget(impact_label);
            auto *buttons = new QHBoxLayout();
            auto *approve = new QPushButton(tr("Approve"), frame);
            approve->setObjectName(QStringLiteral("agentApproveButton-%1").arg(id));
            auto *deny = new QPushButton(tr("Deny"), frame);
            deny->setObjectName(QStringLiteral("agentDenyButton-%1").arg(id));
            buttons->addWidget(approve);
            buttons->addWidget(deny);
            layout->addLayout(buttons);
            connect(approve, &QPushButton::clicked, this, [this, id]() {
                settleApproval(id, true);
                emit approvalResponded(id, true);
            });
            connect(deny, &QPushButton::clicked, this, [this, id]() {
                settleApproval(id, false);
                emit approvalResponded(id, false);
            });
            appendCard(frame);
            m_approvalCards.insert(id, frame);
            break;
        }
        case AgentEventType::ToolApproved:
            settleApproval(event.payload.value(QStringLiteral("tool_call_id")).toString(), true);
            break;
        case AgentEventType::ToolRejected:
            settleApproval(event.payload.value(QStringLiteral("tool_call_id")).toString(), false);
            break;
        case AgentEventType::TransactionPreviewed:
            appendCard(makeCard(QStringLiteral("agentPreviewCard"),
                                tr("Preview"),
                                previewBody(event.payload),
                                false));
            break;
        case AgentEventType::TransactionCommitted:
            appendCard(makeCard(QStringLiteral("agentAppliedCard"),
                                tr("Applied"),
                                appliedBody(event.payload),
                                false));
            break;
        case AgentEventType::TransactionRolledBack: {
            const bool rolled_back = event.payload.value(QStringLiteral("rolled_back")).toBool();
            appendCard(makeCard(QStringLiteral("agentRollbackCard"),
                                rolled_back ? tr("Staged changes discarded")
                                            : tr("No staged changes"),
                                rolled_back
                                    ? tr("The staged transaction was discarded. The live book was not changed by this transaction.")
                                    : tr("There was no staged transaction to discard. The live book was not changed."),
                                false));
            break;
        }
        case AgentEventType::Error:
            appendCard(makeCard(QStringLiteral("agentErrorCard"),
                                tr("Error"),
                                event.payload.value(QStringLiteral("message")).toString(),
                                false));
            break;
        case AgentEventType::SessionCancelled:
            if (m_providerRequestState == ProviderRequestState::Requesting) {
                m_providerRequestState = ProviderRequestState::Cancelled;
                m_providerFailure.clear();
                refreshProviderStatus();
            }
            if (event.payload.value(QStringLiteral("reason")).toString()
                == QLatin1String("book_changed")) {
                appendCard(makeCard(QStringLiteral("agentBookChangedCard"),
                                    tr("Book changed"),
                                    tr("The run stopped because this window switched to another book. No old response was applied to the new book."),
                                    false));
            } else if (event.payload.value(QStringLiteral("reason")).toString()
                       == QLatin1String("window_closing")) {
                appendCard(makeCard(QStringLiteral("agentClosingCard"),
                                    tr("Closing"),
                                    tr("The run stopped before this window closed."),
                                    false));
            } else {
                appendCard(makeCard(QStringLiteral("agentErrorCard"),
                                    tr("Stopped"),
                                    tr("Run stopped. Uncommitted staged work was rolled back."),
                                    false));
            }
            break;
        case AgentEventType::BookTargetChanged:
            appendCard(makeCard(QStringLiteral("agentBookTargetChangedCard"),
                                tr("Book target changed"),
                                tr("The open book changed during this run. The old response was blocked before it could be applied to the new book."),
                                false));
            break;
        default:
            break;
    }
}

} // namespace SigilAgent
