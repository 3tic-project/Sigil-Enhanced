/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/UI/AgentDock.h"
#include "Agent/UI/AgentPlanComparisonDialog.h"

#include <utility>

#include <QAction>
#include <QButtonGroup>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QVariant>
#include <QVBoxLayout>

namespace SigilAgent
{

namespace
{

constexpr int STREAM_FLUSH_INTERVAL_MS = 33;

QString kibText(qint64 bytes)
{
    bytes = qMax<qint64>(0, bytes);
    if (bytes % 1024 == 0) return QString::number(bytes / 1024);
    return QString::number(bytes / 1024.0, 'f', 1);
}

QStringList checkedPlanGroupResourceIds(const QListWidget *list)
{
    QStringList resource_ids;
    if (!list) return resource_ids;
    for (int row = 0; row < list->count(); ++row) {
        const QListWidgetItem *item = list->item(row);
        if (item && item->checkState() == Qt::Checked) {
            resource_ids.append(item->data(Qt::UserRole).toString());
        }
    }
    return resource_ids;
}

}

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

    auto *provider_row = new QWidget(root);
    provider_row->setObjectName(QStringLiteral("agentProviderRow"));
    auto *provider_layout = new QHBoxLayout(provider_row);
    provider_layout->setContentsMargins(0, 0, 0, 0);
    m_providerStatus = new QLabel(provider_row);
    m_providerStatus->setObjectName(QStringLiteral("agentProviderStatus"));
    m_providerStatus->setWordWrap(true);
    m_providerStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_retryButton = new QPushButton(tr("Retry"), provider_row);
    m_retryButton->setObjectName(QStringLiteral("agentRetryButton"));
    m_retryButton->setEnabled(false);
    provider_layout->addWidget(m_providerStatus, 1);
    provider_layout->addWidget(m_retryButton);

    m_bookStatus = new QLabel(root);
    m_bookStatus->setObjectName(QStringLiteral("agentBookStatus"));
    m_bookStatus->setWordWrap(true);
    m_bookStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_bookStatus->setToolTip(
        tr("Agent plans and tool calls are bound to this open book."));

    m_technicalDetailsToggle = new QToolButton(root);
    m_technicalDetailsToggle->setObjectName(QStringLiteral("agentTechnicalDetailsToggle"));
    m_technicalDetailsToggle->setText(tr("Technical details"));
    m_technicalDetailsToggle->setCheckable(true);
    m_technicalDetailsToggle->setArrowType(Qt::RightArrow);
    m_technicalDetailsToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_technicalDetailsToggle->setAccessibleDescription(
        tr("Show request identifiers, timing, token usage, run limits, history and tool budgets, target revision, and scope handles."));
    m_technicalDetails = new QLabel(root);
    m_technicalDetails->setObjectName(QStringLiteral("agentTechnicalDetails"));
    m_technicalDetails->setWordWrap(true);
    m_technicalDetails->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_technicalDetails->hide();

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
    m_streamFlushTimer = new QTimer(this);
    m_streamFlushTimer->setSingleShot(true);
    m_streamFlushTimer->setInterval(STREAM_FLUSH_INTERVAL_MS);
    m_transcript->setProperty("streamFlushIntervalMs", STREAM_FLUSH_INTERVAL_MS);
    m_transcript->setProperty("streamRenderBatches", QVariant::fromValue<qulonglong>(0));
    connect(m_streamFlushTimer, &QTimer::timeout,
            this, &AgentDock::flushAssistantDeltas);

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
    root_layout->addWidget(provider_row);
    root_layout->addWidget(m_bookStatus);
    root_layout->addWidget(m_technicalDetailsToggle);
    root_layout->addWidget(m_technicalDetails);
    root_layout->addWidget(chips);
    root_layout->addWidget(m_transcript, 1);
    root_layout->addWidget(m_composer);
    root_layout->addWidget(m_composerHint);
    root_layout->addWidget(m_sendButton, 0, Qt::AlignRight);
    setWidget(root);
    refreshScopeLabel();

    connect(m_sendButton, &QPushButton::clicked, this, &AgentDock::onSend);
    connect(m_retryButton, &QPushButton::clicked, this, [this]() {
        if (!m_retryButton->isEnabled()) return;
        m_retryAvailable = false;
        refreshRetryState();
        emit sendRequested(m_lastSubmittedText, m_lastSubmittedHandles);
    });
    connect(m_stopButton, &QPushButton::clicked, this, &AgentDock::stopRequested);
    connect(m_newSessionButton, &QPushButton::clicked, this, &AgentDock::newSessionRequested);
    connect(m_technicalDetailsToggle, &QToolButton::toggled, this, [this](bool expanded) {
        m_technicalDetailsToggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        m_technicalDetails->setVisible(expanded);
    });
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
    refreshTechnicalDetails();
    refreshRetryState();
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

void AgentDock::setSessionId(const QString &session_id)
{
    if (m_sessionId == session_id) return;
    discardAssistantDeltas();
    m_sessionId = session_id;
    m_requestId.clear();
    m_requestModel.clear();
    m_requestMode.clear();
    m_requestStatus.clear();
    m_requestBookSessionId.clear();
    m_requestHandles.clear();
    m_requestBookRevision = 0;
    m_requestStep = 0;
    m_requestDurationMs = -1;
    m_requestFinishedAtMs = 0;
    m_requestUsageRequested = false;
    m_requestUsage = ModelUsage();
    m_requestTiming = ModelResponseTiming();
    m_requestHistoryContext = QJsonObject();
    m_requestToolContext = QJsonObject();
    m_runId.clear();
    m_runStatus.clear();
    m_runDurationMs = -1;
    m_runFinishedAtMs = 0;
    m_runModelSteps = -1;
    m_runMaxModelSteps = -1;
    m_runToolCalls = -1;
    m_runUsageRequested = false;
    m_runUsageComplete = false;
    m_runUsageRequestCount = 0;
    m_runUsageReportedRequests = 0;
    m_runUsage = ModelUsage();
    m_runInputUsageRequests = 0;
    m_runOutputUsageRequests = 0;
    m_runTotalUsageRequests = 0;
    m_runCachedUsageRequests = 0;
    m_runReasoningUsageRequests = 0;
    m_lastSubmittedText.clear();
    m_lastSubmittedHandles.clear();
    m_lastSubmittedBookSessionId.clear();
    m_retryAvailable = false;
    refreshTechnicalDetails();
    refreshRetryState();
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
    refreshTechnicalDetails();
}

void AgentDock::setRunState(AgentRunState state)
{
    m_runState->setText(runStateName(state));
    const bool running = state != AgentRunState::Idle
        && state != AgentRunState::Completed
        && state != AgentRunState::Cancelled
        && state != AgentRunState::Failed;
    m_runActive = running;
    m_sendButton->setEnabled(!running);
    m_stopButton->setEnabled(running || state == AgentRunState::AwaitingApproval);
    m_newSessionButton->setEnabled(!running);
    m_modeCombo->setEnabled(!running);
    refreshRetryState();
    refreshTaskRestoreState();
}

void AgentDock::setBookContext(const QString &title,
                               const QString &file_name,
                               int resource_count,
                               bool modified,
                               quint64 revision,
                               const QString &book_session_id)
{
    const bool changed_book = !m_bookSessionId.isEmpty()
        && m_bookSessionId != book_session_id;
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
    refreshTechnicalDetails();
    refreshRetryState();
    refreshPlanNavigationState();
    if (changed_book) refreshTaskRestoreState();
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
                    if (m_providerReadiness.verifiedAtMs > 0) {
                        const QString tested = QDateTime::fromMSecsSinceEpoch(
                            m_providerReadiness.verifiedAtMs)
                            .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
                        state = tr("Chat tested successfully · %1").arg(tested);
                        state_name = QStringLiteral("verified");
                    } else {
                        state = tr("Configured · not tested");
                        state_name = QStringLiteral("configured");
                    }
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

    if (m_providerReadiness.issue == AgentProviderSetupIssue::None
        && m_providerRequestState != ProviderRequestState::Configured
        && m_providerRequestState != ProviderRequestState::Requesting
        && m_requestDurationMs >= 0 && m_requestFinishedAtMs > 0) {
        const QString finished = QDateTime::fromMSecsSinceEpoch(m_requestFinishedAtMs)
            .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        state = tr("%1 · %2 ms · %3").arg(state).arg(m_requestDurationMs).arg(finished);
    }

    const QString details = identity.join(QStringLiteral(" · "));
    m_providerStatus->setText(details.isEmpty()
                                  ? state
                                  : tr("Provider: %1 · %2").arg(details, state));
    m_providerStatus->setProperty("providerName", m_providerReadiness.displayName);
    m_providerStatus->setProperty("model", m_providerReadiness.model);
    m_providerStatus->setProperty("endpointHost", m_providerReadiness.endpointHost);
    m_providerStatus->setProperty("requestState", state_name);
    m_providerStatus->setProperty("requestId", m_requestId);
    m_providerStatus->setProperty("durationMs", m_requestDurationMs);
    m_providerStatus->setProperty("finishedAtMs", m_requestFinishedAtMs);
    m_providerStatus->setProperty("connectionVerifiedAtMs",
                                  m_providerReadiness.verifiedAtMs);
    m_providerStatus->setAccessibleName(m_providerStatus->text());
    if (!m_providerReadiness.isConfigured()) {
        m_providerStatus->setToolTip(
            tr("Configure the provider in Preferences → Native Agent."));
    } else if (m_providerReadiness.verifiedAtMs > 0) {
        m_providerStatus->setToolTip(tr("This exact saved provider configuration passed a Chat Completions test. This is a historical test, not a live connection indicator."));
    } else {
        m_providerStatus->setToolTip(tr("Configured means the required settings are present. Use Test Chat Completions in Preferences to verify them."));
    }
}

void AgentDock::captureRequestEvent(const AgentEvent &event, const QString &status)
{
    const QJsonObject payload = event.payload;
    if (status == QLatin1String("requesting")) {
        m_requestUsageRequested = payload.value(QStringLiteral("usage_requested")).toBool();
        m_requestUsage = ModelUsage();
        m_requestTiming = ModelResponseTiming();
        m_requestHistoryContext =
            payload.value(QStringLiteral("history_context")).toObject();
        m_requestToolContext =
            payload.value(QStringLiteral("tool_context")).toObject();
    } else if (payload.contains(QStringLiteral("usage_requested"))) {
        m_requestUsageRequested = payload.value(QStringLiteral("usage_requested")).toBool();
    }
    if (payload.value(QStringLiteral("usage")).isObject()) {
        m_requestUsage = modelUsageFromJson(
            payload.value(QStringLiteral("usage")).toObject());
    }
    if (payload.value(QStringLiteral("response_timing")).isObject()) {
        m_requestTiming = modelResponseTimingFromJson(
            payload.value(QStringLiteral("response_timing")).toObject());
    }
    if (payload.value(QStringLiteral("history_context")).isObject()) {
        m_requestHistoryContext =
            payload.value(QStringLiteral("history_context")).toObject();
    }
    if (payload.value(QStringLiteral("tool_context")).isObject()) {
        m_requestToolContext =
            payload.value(QStringLiteral("tool_context")).toObject();
    }
    if (payload.contains(QStringLiteral("request_id"))) {
        m_requestId = payload.value(QStringLiteral("request_id")).toString();
    }
    if (payload.contains(QStringLiteral("model"))) {
        m_requestModel = payload.value(QStringLiteral("model")).toString();
    }
    if (payload.contains(QStringLiteral("mode"))) {
        m_requestMode = payload.value(QStringLiteral("mode")).toString();
    }
    if (payload.contains(QStringLiteral("book_session_id"))) {
        m_requestBookSessionId = payload.value(QStringLiteral("book_session_id")).toString();
    }
    if (payload.contains(QStringLiteral("book_revision"))) {
        m_requestBookRevision = payload.value(QStringLiteral("book_revision")).toInteger();
    }
    if (payload.contains(QStringLiteral("context_handles"))) {
        m_requestHandles.clear();
        for (const QJsonValue &value : payload.value(QStringLiteral("context_handles")).toArray()) {
            m_requestHandles.append(value.toString());
        }
    }
    if (payload.contains(QStringLiteral("step"))) {
        m_requestStep = payload.value(QStringLiteral("step")).toInt();
    }
    if (payload.contains(QStringLiteral("duration_ms"))) {
        m_requestDurationMs = payload.value(QStringLiteral("duration_ms")).toInteger();
        m_requestFinishedAtMs = event.timestampMs;
    } else {
        m_requestDurationMs = -1;
        m_requestFinishedAtMs = 0;
    }
    m_requestStatus = status;
    refreshTechnicalDetails();
}

void AgentDock::captureRunEvent(const AgentEvent &event)
{
    const QJsonObject payload = event.payload;
    const QString run_id = payload.value(QStringLiteral("run_id")).toString();
    const QString status = payload.value(QStringLiteral("state")).toString();
    if (run_id.isEmpty()) return;
    if (run_id != m_runId || status == QLatin1String("preparing_context")) {
        m_runDurationMs = -1;
        m_runFinishedAtMs = 0;
        m_runModelSteps = -1;
        m_runMaxModelSteps = -1;
        m_runToolCalls = -1;
        m_runUsageComplete = false;
        m_runUsageRequested = false;
        m_runUsageRequestCount = 0;
        m_runUsageReportedRequests = 0;
        m_runUsage = ModelUsage();
        m_runInputUsageRequests = 0;
        m_runOutputUsageRequests = 0;
        m_runTotalUsageRequests = 0;
        m_runCachedUsageRequests = 0;
        m_runReasoningUsageRequests = 0;
    }
    m_runId = run_id;
    m_runStatus = status;
    if (payload.contains(QStringLiteral("max_model_steps"))) {
        m_runMaxModelSteps =
            payload.value(QStringLiteral("max_model_steps")).toInt();
    }
    if (payload.contains(QStringLiteral("usage_requested"))) {
        m_runUsageRequested = payload.value(QStringLiteral("usage_requested")).toBool();
    }
    if (payload.contains(QStringLiteral("duration_ms"))) {
        m_runDurationMs = payload.value(QStringLiteral("duration_ms")).toInteger();
        m_runFinishedAtMs = event.timestampMs;
        m_runModelSteps = payload.value(QStringLiteral("model_steps")).toInt();
        m_runToolCalls = payload.value(QStringLiteral("tool_calls")).toInt();
        const QJsonObject usage =
            payload.value(QStringLiteral("usage_summary")).toObject();
        m_runUsageRequestCount = usage.value(QStringLiteral("request_count")).toInt();
        m_runUsageReportedRequests =
            usage.value(QStringLiteral("reported_request_count")).toInt();
        m_runUsageComplete =
            usage.value(QStringLiteral("all_requests_reported")).toBool();
        m_runUsage = modelUsageFromJson(usage);
        m_runInputUsageRequests =
            usage.value(QStringLiteral("input_request_count")).toInt();
        m_runOutputUsageRequests =
            usage.value(QStringLiteral("output_request_count")).toInt();
        m_runTotalUsageRequests =
            usage.value(QStringLiteral("total_request_count")).toInt();
        m_runCachedUsageRequests =
            usage.value(QStringLiteral("cached_input_request_count")).toInt();
        m_runReasoningUsageRequests =
            usage.value(QStringLiteral("reasoning_request_count")).toInt();
    }
    refreshTechnicalDetails();
}

void AgentDock::refreshTechnicalDetails()
{
    if (!m_technicalDetails) return;
    QStringList lines;
    lines.append(tr("Session: %1").arg(m_sessionId.isEmpty() ? tr("Not available") : m_sessionId));
    lines.append(tr("Book session: %1").arg(
        m_bookSessionId.isEmpty() ? tr("Not available") : m_bookSessionId));
    lines.append(tr("Book revision: %1").arg(m_bookRevision));
    if (!m_runId.isEmpty()) {
        lines.append(tr("Run: %1").arg(m_runId));
        lines.append(tr("Run status: %1").arg(m_runStatus));
        if (m_runDurationMs >= 0) {
            const QString finished = QDateTime::fromMSecsSinceEpoch(m_runFinishedAtMs)
                .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
            lines.append(tr("Whole run: %1 ms · model requests %2 · tool calls %3 · Finished: %4")
                             .arg(m_runDurationMs)
                             .arg(m_runModelSteps)
                             .arg(m_runToolCalls)
                             .arg(finished));
        } else {
            lines.append(tr("Whole run: in progress"));
        }
        if (m_runMaxModelSteps > 0) {
            if (m_runDurationMs >= 0 && m_runModelSteps >= 0) {
                lines.append(tr("Model-step budget: %1/%2 used")
                                 .arg(m_runModelSteps)
                                 .arg(m_runMaxModelSteps));
            } else {
                lines.append(tr("Model-step budget: limit %1 per run")
                                 .arg(m_runMaxModelSteps));
            }
        }
        if (!m_runUsageRequested) {
            lines.append(tr("Run token usage: not requested"));
        } else if (m_runDurationMs < 0) {
            lines.append(tr("Run token usage: awaiting completed requests"));
        } else if (m_runUsageRequestCount <= 0) {
            lines.append(tr("Run token usage: no model request was sent"));
        } else if (m_runUsageReportedRequests <= 0) {
            lines.append(tr("Run token usage: not reported by provider"));
        } else {
            const QString unavailable = tr("Not reported");
            const auto complete_count = [&unavailable, this](qint64 count, int coverage) {
                return count >= 0 && coverage == m_runUsageReportedRequests
                    ? QString::number(count) : unavailable;
            };
            const QString input = complete_count(
                m_runUsage.inputTokens, m_runInputUsageRequests);
            const QString output = complete_count(
                m_runUsage.outputTokens, m_runOutputUsageRequests);
            const QString total = complete_count(
                m_runUsage.totalTokens, m_runTotalUsageRequests);
            if (m_runUsageComplete) {
                lines.append(tr("Run token usage: input %1 · output %2 · total %3")
                                 .arg(input, output, total));
            } else {
                lines.append(tr("Run token usage (%1 of %2 requests reported): input %3 · output %4 · total %5")
                                 .arg(m_runUsageReportedRequests)
                                 .arg(m_runUsageRequestCount)
                                 .arg(input, output, total));
            }
            const bool cached_complete = m_runUsage.cachedInputTokens >= 0
                && m_runCachedUsageRequests == m_runUsageReportedRequests;
            const bool reasoning_complete = m_runUsage.reasoningTokens >= 0
                && m_runReasoningUsageRequests == m_runUsageReportedRequests;
            if (cached_complete || reasoning_complete) {
                lines.append(tr("Run usage details: cached input %1 · reasoning %2")
                                 .arg(cached_complete
                                          ? QString::number(m_runUsage.cachedInputTokens)
                                          : unavailable,
                                      reasoning_complete
                                          ? QString::number(m_runUsage.reasoningTokens)
                                          : unavailable));
            }
        }
    }
    if (!m_requestId.isEmpty()) {
        lines.append(tr("Request: %1").arg(m_requestId));
        lines.append(tr("Step: %1 · Mode: %2 · Status: %3")
                         .arg(m_requestStep)
                         .arg(m_requestMode.isEmpty() ? tr("Not available") : m_requestMode,
                              m_requestStatus));
        lines.append(tr("Request target: %1 · revision %2")
                         .arg(m_requestBookSessionId, QString::number(m_requestBookRevision)));
        lines.append(tr("Model: %1").arg(m_requestModel));
        lines.append(tr("Scope handles: %1").arg(
            m_requestHandles.isEmpty() ? tr("None") : m_requestHandles.join(QStringLiteral(", "))));
        if (!m_requestToolContext.isEmpty()) {
            lines.append(tr("Request tools: %1/%2 exposed · %3 hidden by mode policy · schema %4/%5 KiB")
                             .arg(m_requestToolContext
                                      .value(QStringLiteral("exposed_tool_count")).toInt())
                             .arg(m_requestToolContext
                                      .value(QStringLiteral("total_tool_count")).toInt())
                             .arg(m_requestToolContext
                                      .value(QStringLiteral("hidden_tool_count")).toInt())
                             .arg(kibText(m_requestToolContext
                                              .value(QStringLiteral("exposed_schema_bytes"))
                                              .toInteger()),
                                  kibText(m_requestToolContext
                                              .value(QStringLiteral("unfiltered_schema_bytes"))
                                              .toInteger())));
        }
        if (!m_requestHistoryContext.isEmpty()) {
            const int included_turns = m_requestHistoryContext
                .value(QStringLiteral("included_turn_count")).toInt();
            const int total_turns = m_requestHistoryContext
                .value(QStringLiteral("total_turn_count")).toInt();
            const int omitted_turns = m_requestHistoryContext
                .value(QStringLiteral("omitted_turn_count")).toInt();
            const qint64 included_previous_bytes = m_requestHistoryContext
                .value(QStringLiteral("included_previous_turn_bytes")).toInteger();
            const qint64 current_bytes = m_requestHistoryContext
                .value(QStringLiteral("current_turn_bytes")).toInteger();
            if (m_requestHistoryContext
                    .value(QStringLiteral("limit_enabled")).toBool()) {
                const qint64 budget_bytes = m_requestHistoryContext
                    .value(QStringLiteral("budget_bytes")).toInteger();
                lines.append(tr("Request history: %1/%2 turns sent · %3 omitted · previous %4/%5 KiB · current %6 KiB (always retained)")
                                 .arg(included_turns)
                                 .arg(total_turns)
                                 .arg(omitted_turns)
                                 .arg(kibText(included_previous_bytes),
                                      kibText(budget_bytes),
                                      kibText(current_bytes)));
            } else {
                lines.append(tr("Request history: %1/%2 turns sent · unlimited previous-turn budget · previous %3 KiB · current %4 KiB (always retained)")
                                 .arg(included_turns)
                                 .arg(total_turns)
                                 .arg(kibText(included_previous_bytes),
                                      kibText(current_bytes)));
            }
        }
        if (m_requestDurationMs >= 0) {
            const QString finished = QDateTime::fromMSecsSinceEpoch(m_requestFinishedAtMs)
                .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
            lines.append(tr("Duration: %1 ms · Finished: %2")
                             .arg(m_requestDurationMs).arg(finished));
        }
        if (m_requestStatus == QLatin1String("requesting")) {
            lines.append(tr("Response latency: awaiting response"));
        } else if (m_requestTiming.isReported()) {
            const QString unobserved = tr("Not observed");
            lines.append(tr("Response latency: first byte %1 ms · first model event %2 ms")
                             .arg(m_requestTiming.firstByteMs >= 0
                                      ? QString::number(m_requestTiming.firstByteMs)
                                      : unobserved,
                                  m_requestTiming.firstEventMs >= 0
                                      ? QString::number(m_requestTiming.firstEventMs)
                                      : unobserved));
        } else {
            lines.append(tr("Response latency: not observed"));
        }
        if (m_requestUsage.isReported()) {
            const QString unavailable = tr("Not reported");
            const auto count_text = [&unavailable](qint64 count) {
                return count >= 0 ? QString::number(count) : unavailable;
            };
            lines.append(tr("Token usage: input %1 · output %2 · total %3")
                             .arg(count_text(m_requestUsage.inputTokens),
                                  count_text(m_requestUsage.outputTokens),
                                  count_text(m_requestUsage.totalTokens)));
            if (m_requestUsage.cachedInputTokens >= 0
                || m_requestUsage.reasoningTokens >= 0) {
                lines.append(tr("Usage details: cached input %1 · reasoning %2")
                                 .arg(count_text(m_requestUsage.cachedInputTokens),
                                      count_text(m_requestUsage.reasoningTokens)));
            }
        } else if (m_requestUsageRequested
                   && m_requestStatus == QLatin1String("requesting")) {
            lines.append(tr("Token usage: requested; awaiting response"));
        } else if (m_requestUsageRequested) {
            lines.append(tr("Token usage: not reported by provider"));
        } else {
            lines.append(tr("Token usage: not requested"));
        }
    } else {
        lines.append(tr("No model request in this session."));
    }
    const QString provider = m_providerReadiness.displayName.isEmpty()
        ? tr("Not available") : m_providerReadiness.displayName;
    const QString endpoint = m_providerReadiness.endpointHost.isEmpty()
        ? tr("Not available") : m_providerReadiness.endpointHost;
    lines.append(tr("Provider: %1 · Endpoint: %2").arg(provider, endpoint));
    m_technicalDetails->setText(lines.join(QLatin1Char('\n')));
    m_technicalDetails->setProperty("sessionId", m_sessionId);
    m_technicalDetails->setProperty("bookSessionId", m_bookSessionId);
    m_technicalDetails->setProperty("runId", m_runId);
    m_technicalDetails->setProperty("runStatus", m_runStatus);
    m_technicalDetails->setProperty("runDurationMs", m_runDurationMs);
    m_technicalDetails->setProperty("runFinishedAtMs", m_runFinishedAtMs);
    m_technicalDetails->setProperty("runModelSteps", m_runModelSteps);
    m_technicalDetails->setProperty("runMaxModelSteps", m_runMaxModelSteps);
    m_technicalDetails->setProperty("runToolCalls", m_runToolCalls);
    m_technicalDetails->setProperty("runUsageRequested", m_runUsageRequested);
    m_technicalDetails->setProperty("runUsageComplete", m_runUsageComplete);
    m_technicalDetails->setProperty("runUsageRequestCount", m_runUsageRequestCount);
    m_technicalDetails->setProperty("runUsageReportedRequests", m_runUsageReportedRequests);
    m_technicalDetails->setProperty("runInputTokens", m_runUsage.inputTokens);
    m_technicalDetails->setProperty("runOutputTokens", m_runUsage.outputTokens);
    m_technicalDetails->setProperty("runTotalTokens", m_runUsage.totalTokens);
    m_technicalDetails->setProperty("runCachedInputTokens", m_runUsage.cachedInputTokens);
    m_technicalDetails->setProperty("runReasoningTokens", m_runUsage.reasoningTokens);
    m_technicalDetails->setProperty("runInputUsageRequests", m_runInputUsageRequests);
    m_technicalDetails->setProperty("runOutputUsageRequests", m_runOutputUsageRequests);
    m_technicalDetails->setProperty("runTotalUsageRequests", m_runTotalUsageRequests);
    m_technicalDetails->setProperty("runCachedUsageRequests", m_runCachedUsageRequests);
    m_technicalDetails->setProperty("runReasoningUsageRequests", m_runReasoningUsageRequests);
    m_technicalDetails->setProperty("requestId", m_requestId);
    m_technicalDetails->setProperty("requestStatus", m_requestStatus);
    m_technicalDetails->setProperty("requestBookSessionId", m_requestBookSessionId);
    m_technicalDetails->setProperty("requestBookRevision", m_requestBookRevision);
    m_technicalDetails->setProperty("requestHandles", m_requestHandles);
    m_technicalDetails->setProperty("durationMs", m_requestDurationMs);
    m_technicalDetails->setProperty("finishedAtMs", m_requestFinishedAtMs);
    m_technicalDetails->setProperty("usageRequested", m_requestUsageRequested);
    m_technicalDetails->setProperty("usageReported", m_requestUsage.isReported());
    m_technicalDetails->setProperty("inputTokens", m_requestUsage.inputTokens);
    m_technicalDetails->setProperty("outputTokens", m_requestUsage.outputTokens);
    m_technicalDetails->setProperty("totalTokens", m_requestUsage.totalTokens);
    m_technicalDetails->setProperty("cachedInputTokens", m_requestUsage.cachedInputTokens);
    m_technicalDetails->setProperty("reasoningTokens", m_requestUsage.reasoningTokens);
    m_technicalDetails->setProperty("firstByteMs", m_requestTiming.firstByteMs);
    m_technicalDetails->setProperty("firstModelEventMs", m_requestTiming.firstEventMs);
    m_technicalDetails->setProperty(
        "historyBudgetBytes",
        m_requestHistoryContext.value(QStringLiteral("budget_bytes")).toInteger());
    m_technicalDetails->setProperty(
        "historyTotalTurns",
        m_requestHistoryContext.value(QStringLiteral("total_turn_count")).toInt());
    m_technicalDetails->setProperty(
        "historyIncludedTurns",
        m_requestHistoryContext.value(QStringLiteral("included_turn_count")).toInt());
    m_technicalDetails->setProperty(
        "historyOmittedTurns",
        m_requestHistoryContext.value(QStringLiteral("omitted_turn_count")).toInt());
    m_technicalDetails->setProperty(
        "historyIncludedPreviousBytes",
        m_requestHistoryContext.value(
            QStringLiteral("included_previous_turn_bytes")).toInteger());
    m_technicalDetails->setProperty(
        "historyCurrentTurnBytes",
        m_requestHistoryContext.value(QStringLiteral("current_turn_bytes")).toInteger());
    m_technicalDetails->setProperty(
        "toolTotalCount",
        m_requestToolContext.value(QStringLiteral("total_tool_count")).toInt());
    m_technicalDetails->setProperty(
        "toolExposedCount",
        m_requestToolContext.value(QStringLiteral("exposed_tool_count")).toInt());
    m_technicalDetails->setProperty(
        "toolHiddenCount",
        m_requestToolContext.value(QStringLiteral("hidden_tool_count")).toInt());
    m_technicalDetails->setProperty(
        "toolSchemaBytes",
        m_requestToolContext.value(QStringLiteral("exposed_schema_bytes")).toInteger());
    m_technicalDetails->setProperty(
        "toolUnfilteredSchemaBytes",
        m_requestToolContext.value(QStringLiteral("unfiltered_schema_bytes")).toInteger());
    m_technicalDetails->setProperty(
        "toolSavedSchemaBytes",
        m_requestToolContext.value(QStringLiteral("saved_schema_bytes")).toInteger());
    m_technicalDetails->setProperty(
        "hiddenToolNames",
        m_requestToolContext.value(QStringLiteral("hidden_tools"))
            .toArray().toVariantList());
    m_technicalDetails->setAccessibleName(m_technicalDetails->text());
}

void AgentDock::refreshRetryState()
{
    if (!m_retryButton) return;
    const bool same_book = !m_lastSubmittedBookSessionId.isEmpty()
        && m_lastSubmittedBookSessionId == m_bookSessionId;
    const bool first_request = m_requestStep == 1;
    const bool enabled = m_retryAvailable && !m_runActive && same_book && first_request
        && !m_lastSubmittedText.isEmpty();
    m_retryButton->setEnabled(enabled);
    m_retryButton->setProperty("bookSessionId", m_lastSubmittedBookSessionId);
    m_retryButton->setProperty("contextHandles", m_lastSubmittedHandles);
    if (enabled) {
        m_retryButton->setToolTip(
            tr("Resend the last prompt with the same scope handles."));
    } else if (m_retryAvailable && !same_book) {
        m_retryButton->setToolTip(
            tr("Retry is unavailable because the open book changed."));
    } else if (m_retryAvailable && !first_request) {
        m_retryButton->setToolTip(
            tr("Retry is unavailable because this turn already executed tools."));
    } else {
        m_retryButton->setToolTip(
            tr("Retry is available after a provider request fails."));
    }
}

void AgentDock::refreshTaskRestoreState()
{
    for (auto it = m_taskRestoreButtons.begin(); it != m_taskRestoreButtons.end(); ++it) {
        QPushButton *button = it.value();
        if (!button) continue;
        const bool available = button->property("restoreAvailable").toBool();
        const bool same_book = !m_bookSessionId.isEmpty()
            && button->property("bookSessionId").toString() == m_bookSessionId;
        button->setEnabled(available && same_book && !m_runActive);
        if (!same_book) {
            button->setToolTip(tr("Restore is unavailable because the open book changed."));
        } else if (available && m_runActive) {
            button->setToolTip(tr("Stop the active Agent run before restoring this task."));
        } else if (available) {
            button->setToolTip(
                tr("Restore the text resources changed by this commit. Later edits to those resources will block restoration."));
        }
    }
}

void AgentDock::refreshPlanNavigationState()
{
    const QList<QPushButton *> buttons =
        m_transcriptContents->findChildren<QPushButton *>();
    for (QPushButton *button : buttons) {
        if (!button
            || (!button->objectName().startsWith(
                    QLatin1String("agentPlanOpenResourceButton-"))
                && !button->objectName().startsWith(
                    QLatin1String("agentPlanCompareButton-")))) {
            continue;
        }
        const QString bound_session =
            button->property("bookSessionId").toString();
        button->setEnabled(!bound_session.isEmpty()
                           && bound_session == m_bookSessionId);
    }
    const QList<QDialog *> dialogs = findChildren<QDialog *>(
        QString(), Qt::FindDirectChildrenOnly);
    for (QDialog *dialog : dialogs) {
        if (!dialog || !dialog->objectName().startsWith(
                           QLatin1String("agentPlanComparisonDialog-"))) {
            continue;
        }
        if (dialog->property("bookSessionId").toString() != m_bookSessionId) {
            dialog->close();
        }
    }
}

void AgentDock::closePlanComparisons()
{
    const QList<QDialog *> dialogs = findChildren<QDialog *>(
        QString(), Qt::FindDirectChildrenOnly);
    for (QDialog *dialog : dialogs) {
        if (dialog && dialog->objectName().startsWith(
                          QLatin1String("agentPlanComparisonDialog-"))) {
            dialog->close();
        }
    }
}

void AgentDock::showPlanComparison(
    const QJsonObject &payload,
    const QString &comparison_id,
    const QString &subject,
    const AgentPlanComparisonContent &content)
{
    const QString book_session_id =
        payload.value(QStringLiteral("book_session_id")).toString();
    if (book_session_id.isEmpty() || book_session_id != m_bookSessionId) return;
    const QString object_name =
        QStringLiteral("agentPlanComparisonDialog-%1").arg(comparison_id);
    if (QDialog *existing = findChild<QDialog *>(object_name)) {
        existing->show();
        existing->raise();
        return;
    }
    auto *dialog = new AgentPlanComparisonDialog(content, this);
    dialog->setObjectName(object_name);
    dialog->setProperty("planId", payload.value(QStringLiteral("plan_id")).toString());
    dialog->setProperty(
        "planDigest", payload.value(QStringLiteral("plan_digest")).toString());
    dialog->setProperty(
        "bookRevision", payload.value(QStringLiteral("book_revision")).toInteger());
    dialog->setProperty("bookSessionId", book_session_id);
    dialog->setProperty("comparisonSubject", subject);
    dialog->show();
}

bool AgentDock::matchesReviewedPlan(const QString &tool_name,
                                    const QJsonObject &arguments) const
{
    QString expected_kind;
    if (tool_name == QLatin1String("paragraphs.apply")) {
        expected_kind = QStringLiteral("paragraph_normalization");
    } else if (tool_name == QLatin1String("toc.apply_transform")) {
        expected_kind = QStringLiteral("toc_hierarchy");
    } else {
        return true;
    }
    const QString plan_id = arguments.value(QStringLiteral("plan_id")).toString();
    if (plan_id.isEmpty() || !m_reviewedPlans.contains(plan_id)) return false;
    const QJsonObject plan = m_reviewedPlans.value(plan_id);
    return plan.value(QStringLiteral("plan_kind")).toString() == expected_kind
        && plan.value(QStringLiteral("plan_digest")).toString()
            == arguments.value(QStringLiteral("plan_digest")).toString()
        && plan.value(QStringLiteral("book_revision")).toInteger(-1)
            == arguments.value(QStringLiteral("expected_book_revision")).toInteger(-2)
        && !m_bookSessionId.isEmpty()
        && plan.value(QStringLiteral("book_session_id")).toString() == m_bookSessionId;
}

void AgentDock::resetTranscript()
{
    discardAssistantDeltas();
    closePlanComparisons();
    m_streamRenderBatches = 0;
    if (m_transcript) {
        m_transcript->setProperty(
            "streamRenderBatches", QVariant::fromValue<qulonglong>(0));
    }
    QLayoutItem *item = nullptr;
    while ((item = m_transcriptLayout->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }
    m_transcriptLayout->addStretch(1);
    m_approvalCards.clear();
    m_reviewedPlans.clear();
    m_taskRestoreButtons.clear();
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
    m_lastSubmittedText = text;
    m_lastSubmittedHandles = contextHandles();
    m_lastSubmittedBookSessionId = m_bookSessionId;
    m_retryAvailable = false;
    m_composer->clear();
    if (m_sendButton) m_sendButton->setEnabled(false);
    refreshRetryState();
    emit sendRequested(text, m_lastSubmittedHandles);
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
    if (auto *groups = card->findChild<QListWidget *>(
            QStringLiteral("agentPlanGroupList-%1").arg(toolCallId))) {
        groups->setEnabled(false);
    }
    for (const QString &name : {
             QStringLiteral("agentPlanGroupsSelectAll-%1").arg(toolCallId),
             QStringLiteral("agentPlanGroupsClear-%1").arg(toolCallId) }) {
        if (auto *button = card->findChild<QPushButton *>(name)) {
            button->setEnabled(false);
        }
    }
}

void AgentDock::queueAssistantDelta(const QString &kind, const QString &text)
{
    if (text.isEmpty()) return;
    if (kind == QLatin1String("reasoning")) {
        m_pendingThinkingText.append(text);
    } else {
        m_pendingAnswerText.append(text);
    }
    if (m_streamFlushTimer && !m_streamFlushTimer->isActive()) {
        m_streamFlushTimer->start();
    }
}

void AgentDock::flushAssistantDeltas()
{
    if (m_streamFlushTimer) m_streamFlushTimer->stop();
    if (m_pendingThinkingText.isEmpty() && m_pendingAnswerText.isEmpty()) return;
    const QString thinking = std::exchange(m_pendingThinkingText, QString());
    const QString answer = std::exchange(m_pendingAnswerText, QString());
    if (!thinking.isEmpty()) setThinkingText(thinking, true);
    if (!answer.isEmpty()) setAnswerText(answer, true);
    ++m_streamRenderBatches;
    if (m_transcript) {
        m_transcript->setProperty(
            "streamRenderBatches",
            QVariant::fromValue<qulonglong>(m_streamRenderBatches));
    }
}

void AgentDock::discardAssistantDeltas()
{
    if (m_streamFlushTimer) m_streamFlushTimer->stop();
    m_pendingThinkingText.clear();
    m_pendingAnswerText.clear();
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
    const QString updated = append ? body->text() + text : text;
    if (body->text() != updated) body->setText(updated);
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
    const QString updated = append ? body->text() + text : text;
    if (body->text() != updated) body->setText(updated);
}

QString AgentDock::planReviewBody(const QJsonObject &payload) const
{
    QStringList lines;
    lines.append(tr("The live book is unchanged. Review this plan before approving its apply step."));
    const QString kind = payload.value(QStringLiteral("plan_kind")).toString();
    if (kind == QLatin1String("paragraph_normalization")) {
        const QJsonObject summary = payload.value(QStringLiteral("summary")).toObject();
        lines.append(tr("Paragraph normalization: %1 file(s) ready · %2 conversion(s) · %3 protected item(s)")
                         .arg(summary.value(QStringLiteral("ready_files")).toInt())
                         .arg(summary.value(QStringLiteral("conversion_count")).toInt())
                         .arg(summary.value(QStringLiteral("protected_count")).toInt()));
        lines.append(tr("Other files: %1 review only · %2 skipped · %3 failed")
                         .arg(summary.value(QStringLiteral("review_only_files")).toInt())
                         .arg(summary.value(QStringLiteral("skipped_files")).toInt())
                         .arg(summary.value(QStringLiteral("error_files")).toInt()));
        const QJsonArray groups = payload.value(
            QStringLiteral("operation_groups")).toArray();
        if (payload.value(
                QStringLiteral("operation_groups_independent")).toBool()
            && !groups.isEmpty()) {
            lines.append(tr("Independent operation groups: %1 XHTML file(s). Choose groups when the apply approval appears.")
                             .arg(groups.size()));
        }
        if (!payload.value(QStringLiteral("changes_css")).toBool()
            && !payload.value(QStringLiteral("changes_opf")).toBool()
            && !payload.value(QStringLiteral("adds_resources")).toBool()) {
            lines.append(tr("Plan scope: XHTML only; no CSS, OPF, or resource additions."));
        } else {
            lines.append(tr("Plan may change CSS, OPF, or resource inventory; inspect each change."));
        }
        for (const QJsonValue &value : payload.value(QStringLiteral("changes")).toArray()) {
            const QJsonObject change = value.toObject();
            const QString path = change.value(QStringLiteral("book_path")).toString();
            lines.append(QString());
            lines.append(tr("File: %1 · %2 conversion(s) · %3 protected item(s)")
                             .arg(path.isEmpty()
                                      ? change.value(QStringLiteral("resource_id")).toString()
                                      : path)
                             .arg(change.value(QStringLiteral("conversion_count")).toInt())
                             .arg(change.value(QStringLiteral("protected_count")).toInt()));
            const QJsonObject diff = change.value(QStringLiteral("source_diff")).toObject();
            QString before = diff.value(QStringLiteral("before")).toString();
            QString after = diff.value(QStringLiteral("after")).toString();
            if (diff.value(QStringLiteral("prefix_truncated")).toBool()) {
                before.prepend(QStringLiteral("…"));
                after.prepend(QStringLiteral("…"));
            }
            if (diff.value(QStringLiteral("suffix_truncated")).toBool()) {
                before.append(QStringLiteral("…"));
                after.append(QStringLiteral("…"));
            }
            lines.append(tr("Before excerpt:"));
            lines.append(before);
            lines.append(tr("After excerpt:"));
            lines.append(after);
        }
    } else if (kind == QLatin1String("toc_hierarchy")) {
        lines.append(tr("TOC hierarchy: %1 affected node(s) · %2 adopted sibling(s)")
                         .arg(payload.value(QStringLiteral("affected_count")).toInt())
                         .arg(payload.value(QStringLiteral("adopted_count")).toInt()));
        lines.append(tr("Preorder preserved: %1 · XHTML heading levels changed: %2")
                         .arg(payload.value(QStringLiteral("preorder_preserved")).toBool()
                                  ? tr("Yes") : tr("No"),
                              payload.value(QStringLiteral("changes_xhtml_headings")).toBool()
                                  ? tr("Yes") : tr("No")));
        lines.append(tr("Operation groups: one dependent TOC hierarchy change."));
        for (const QJsonValue &value : payload.value(QStringLiteral("changes")).toArray()) {
            const QJsonObject change = value.toObject();
            lines.append(QString());
            lines.append(tr("Entry: %1 · %2")
                             .arg(change.value(QStringLiteral("label")).toString(),
                                  change.value(QStringLiteral("target")).toString()));
            lines.append(tr("Depth: %1 → %2 · Parent: %3 → %4")
                             .arg(change.value(QStringLiteral("from_depth")).toInt())
                             .arg(change.value(QStringLiteral("to_depth")).toInt())
                             .arg(change.value(QStringLiteral("from_parent_id")).toInteger())
                             .arg(change.value(QStringLiteral("to_parent_id")).toInteger()));
        }
        if (payload.value(QStringLiteral("changes_truncated")).toBool()) {
            lines.append(tr("Additional TOC changes are omitted from this bounded review."));
        }
    } else {
        lines.append(tr("This native plan is ready for review."));
    }
    lines.append(QString());
    lines.append(tr("Local validation: %1")
                     .arg(payload.value(QStringLiteral("local_validation")).toString()));
    const QString epubcheck = payload.value(QStringLiteral("full_epubcheck")).toObject()
                                  .value(QStringLiteral("status")).toString();
    lines.append(epubcheck.isEmpty() || epubcheck == QLatin1String("not_run")
                     ? tr("Full EPUBCheck: not run.")
                     : tr("Full EPUBCheck: %1").arg(epubcheck));
    return lines.join(QLatin1Char('\n'));
}

QWidget *AgentDock::makePlanReviewCard(const QJsonObject &payload)
{
    const QString call_id = payload.value(QStringLiteral("tool_call_id")).toString();
    const QString object_name = call_id.isEmpty()
        ? QStringLiteral("agentPlanReviewCard")
        : QStringLiteral("agentPlanReviewCard-%1").arg(call_id);
    const bool paragraph = payload.value(QStringLiteral("plan_kind")).toString()
        == QLatin1String("paragraph_normalization");
    QWidget *card = makeCard(object_name,
                             paragraph ? tr("Review paragraph plan")
                                       : tr("Review TOC plan"),
                             planReviewBody(payload), false);
    card->setProperty("planId", payload.value(QStringLiteral("plan_id")).toString());
    card->setProperty("planDigest", payload.value(QStringLiteral("plan_digest")).toString());
    card->setProperty("bookRevision", payload.value(QStringLiteral("book_revision")).toInteger());
    const QString book_session_id =
        payload.value(QStringLiteral("book_session_id")).toString();
    card->setProperty("bookSessionId", book_session_id);
    if (auto *body = card->findChild<QLabel *>(object_name + QStringLiteral("Body"))) {
        body->setTextFormat(Qt::PlainText);
        body->setAccessibleName(body->text());
    }

    QStringList paths;
    QList<QJsonObject> path_changes;
    for (const QJsonValue &value : payload.value(QStringLiteral("changes")).toArray()) {
        const QJsonObject change = value.toObject();
        QString path = paragraph
            ? change.value(QStringLiteral("book_path")).toString()
            : change.value(QStringLiteral("target")).toString();
        const int fragment = path.indexOf(QLatin1Char('#'));
        if (fragment >= 0) path.truncate(fragment);
        if (path.isEmpty() || path.startsWith(QLatin1Char('/'))
            || path.contains(QStringLiteral("://")) || paths.contains(path)) {
            continue;
        }
        paths.append(path);
        path_changes.append(change);
        if (paths.size() >= 8) break;
    }
    auto *layout = qobject_cast<QVBoxLayout *>(card->layout());
    for (int index = 0; layout && index < paths.size(); ++index) {
        const QString path = paths.at(index);
        const QJsonObject change = path_changes.at(index);
        auto *row = new QWidget(card);
        row->setObjectName(QStringLiteral("agentPlanResourceRow-%1-%2")
                               .arg(call_id).arg(index));
        auto *row_layout = new QHBoxLayout(row);
        row_layout->setContentsMargins(0, 0, 0, 0);
        auto *open = new QPushButton(tr("Open %1").arg(path), row);
        open->setObjectName(QStringLiteral("agentPlanOpenResourceButton-%1-%2")
                                .arg(call_id).arg(index));
        open->setProperty("bookPath", path);
        open->setProperty("bookSessionId", book_session_id);
        open->setAccessibleDescription(tr("Open this resource in Sigil for plan review."));
        open->setEnabled(!book_session_id.isEmpty() && book_session_id == m_bookSessionId);
        row_layout->addWidget(open);
        connect(open, &QPushButton::clicked, this,
                [this, open, path, book_session_id]() {
            if (book_session_id.isEmpty() || book_session_id != m_bookSessionId) {
                open->setEnabled(false);
                return;
            }
            emit openPlanResourceRequested(path, book_session_id);
        });
        const QJsonObject diff = change.value(QStringLiteral("source_diff")).toObject();
        if (paragraph && (diff.contains(QStringLiteral("before"))
                          || diff.contains(QStringLiteral("after")))) {
            auto *compare = new QPushButton(tr("Compare…"), row);
            compare->setObjectName(QStringLiteral("agentPlanCompareButton-%1-%2")
                                       .arg(call_id).arg(index));
            compare->setProperty("bookPath", path);
            compare->setProperty("bookSessionId", book_session_id);
            compare->setAccessibleName(tr("Compare %1").arg(path));
            compare->setAccessibleDescription(
                tr("Compare the reviewed before and after excerpts side by side."));
            compare->setEnabled(!book_session_id.isEmpty()
                                && book_session_id == m_bookSessionId);
            row_layout->addWidget(compare);
            connect(compare, &QPushButton::clicked, this,
                    [this, compare, payload, diff, path, call_id, index,
                     book_session_id]() {
                if (book_session_id.isEmpty()
                    || book_session_id != m_bookSessionId) {
                    compare->setEnabled(false);
                    return;
                }
                AgentPlanComparisonContent content;
                content.windowTitle = tr("Plan comparison — %1").arg(path);
                content.summary = tr(
                    "Read-only excerpt from the reviewed plan. The live book is unchanged.");
                content.beforeTitle = tr("Before");
                content.afterTitle = tr("After");
                content.beforeAccessibleName =
                    tr("Before excerpt for %1").arg(path);
                content.afterAccessibleName =
                    tr("After excerpt for %1").arg(path);
                content.beforeText = diff.value(QStringLiteral("before")).toString();
                content.afterText = diff.value(QStringLiteral("after")).toString();
                content.prefixTruncated =
                    diff.value(QStringLiteral("prefix_truncated")).toBool();
                content.suffixTruncated =
                    diff.value(QStringLiteral("suffix_truncated")).toBool();
                content.truncationNotice = tr(
                    "This comparison is a bounded excerpt; source outside the displayed region is omitted.");
                showPlanComparison(
                    payload,
                    QStringLiteral("%1-%2").arg(call_id).arg(index),
                    path, content);
            });
        }
        row_layout->addStretch(1);
        layout->addWidget(row);
    }
    if (layout && !paragraph
        && !payload.value(QStringLiteral("changes")).toArray().isEmpty()) {
        auto *compare = new QPushButton(tr("Compare hierarchy…"), card);
        compare->setObjectName(
            QStringLiteral("agentPlanCompareButton-%1-toc").arg(call_id));
        compare->setProperty("bookSessionId", book_session_id);
        compare->setAccessibleName(tr("Compare TOC hierarchy"));
        compare->setAccessibleDescription(
            tr("Compare the reviewed TOC hierarchy before and after side by side."));
        compare->setEnabled(!book_session_id.isEmpty()
                            && book_session_id == m_bookSessionId);
        layout->addWidget(compare, 0, Qt::AlignLeft);
        connect(compare, &QPushButton::clicked, this,
                [this, compare, payload, call_id, book_session_id]() {
            if (book_session_id.isEmpty() || book_session_id != m_bookSessionId) {
                compare->setEnabled(false);
                return;
            }
            QStringList before;
            QStringList after;
            for (const QJsonValue &value :
                 payload.value(QStringLiteral("changes")).toArray()) {
                const QJsonObject change = value.toObject();
                const QString entry = tr("Entry: %1 · %2")
                                          .arg(change.value(QStringLiteral("label")).toString(),
                                               change.value(QStringLiteral("target")).toString());
                before.append(entry);
                before.append(tr("Depth: %1 · Parent: %2")
                                  .arg(change.value(QStringLiteral("from_depth")).toInt())
                                  .arg(change.value(QStringLiteral("from_parent_id")).toInteger()));
                before.append(QString());
                after.append(entry);
                after.append(tr("Depth: %1 · Parent: %2")
                                 .arg(change.value(QStringLiteral("to_depth")).toInt())
                                 .arg(change.value(QStringLiteral("to_parent_id")).toInteger()));
                after.append(QString());
            }
            AgentPlanComparisonContent content;
            content.windowTitle = tr("Plan comparison — %1").arg(tr("TOC hierarchy"));
            content.summary = tr(
                "Read-only hierarchy comparison from the reviewed plan. The live book is unchanged.");
            content.beforeTitle = tr("Before");
            content.afterTitle = tr("After");
            content.beforeAccessibleName = tr("TOC hierarchy before transformation");
            content.afterAccessibleName = tr("TOC hierarchy after transformation");
            content.beforeText = before.join(QLatin1Char('\n')).trimmed();
            content.afterText = after.join(QLatin1Char('\n')).trimmed();
            content.suffixTruncated =
                payload.value(QStringLiteral("changes_truncated")).toBool();
            content.truncationNotice = tr(
                "This comparison is bounded; additional TOC changes are omitted.");
            showPlanComparison(
                payload, QStringLiteral("%1-toc").arg(call_id),
                tr("TOC hierarchy"), content);
        });
    }
    return card;
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
    const QString outcomes = resourceOutcomesBody(
        payload.value(QStringLiteral("resource_outcomes")).toObject());
    if (!outcomes.isEmpty()) lines.append(outcomes);
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
    const QJsonObject recovery = payload.value(QStringLiteral("recovery")).toObject();
    const QString restore_status =
        recovery.value(QStringLiteral("task_restore_point")).toString();
    if (restore_status == QLatin1String("available")) {
        const int count = recovery.value(QStringLiteral("affected_resources")).toArray().size();
        lines.append(tr("Task restore point: %1 text resource(s), protected by a post-commit conflict check.")
                         .arg(count));
    } else if (restore_status == QLatin1String("unavailable")
               && recovery.value(QStringLiteral("reason")).toString()
                   == QLatin1String("structural_changes")) {
        lines.append(tr("A task restore point was not created because this commit changed book structure."));
    } else if (restore_status == QLatin1String("unavailable")) {
        lines.append(tr("A task restore point could not be created for this commit."));
    } else if (restore_status == QLatin1String("not_created_by_commit")) {
        lines.append(tr("This commit did not create a task-wide restore point."));
    }
    return lines.join(QLatin1Char('\n'));
}

QString AgentDock::resourceOutcomesBody(const QJsonObject &outcomes) const
{
    if (outcomes.isEmpty()) return QString();
    if (!outcomes.value(QStringLiteral("scope_available")).toBool()) {
        return tr("Resource result unavailable because the commit scope could not be inspected.");
    }

    QStringList lines;
    lines.append(tr("Resources: %1 succeeded · %2 failed.")
                     .arg(outcomes.value(
                         QStringLiteral("successful_resource_count")).toInt())
                     .arg(outcomes.value(
                         QStringLiteral("failed_resource_count")).toInt()));
    const int structural_count = outcomes.value(
        QStringLiteral("structural_operation_count")).toInt();
    if (structural_count > 0) {
        lines.append(tr("Structural operations: %1 succeeded · %2 failed.")
                         .arg(outcomes.value(
                             QStringLiteral(
                                 "successful_structural_operation_count")).toInt())
                         .arg(outcomes.value(
                             QStringLiteral(
                                 "failed_structural_operation_count")).toInt()));
    }
    if (outcomes.value(QStringLiteral("all_or_nothing")).toBool()) {
        lines.append(outcomes.value(QStringLiteral("status")).toString()
                         == QLatin1String("all_applied")
                     ? tr("Atomic result: all staged targets were applied.")
                     : tr("Atomic result: no staged target was applied."));
    }
    return lines.join(QLatin1Char('\n'));
}

QString AgentDock::failedCommitBody(const QJsonObject &payload) const
{
    QStringList lines;
    lines.append(tr("Not applied to the current book."));
    const QJsonObject data = payload.value(QStringLiteral("data")).toObject();
    const QJsonObject outcomes = data.value(
        QStringLiteral("resource_outcomes")).toObject();
    const QString outcome_body = resourceOutcomesBody(outcomes);
    if (!outcome_body.isEmpty()) lines.append(outcome_body);

    const QString transaction_state = outcomes.value(
        QStringLiteral("transaction_state")).toString();
    if (transaction_state == QLatin1String("staged")) {
        lines.append(tr("The staged transaction remains available for review, retry, or rollback."));
    } else if (transaction_state == QLatin1String("rolled_back")) {
        lines.append(tr("The staged transaction was rolled back; no partial book changes remain."));
    } else if (transaction_state == QLatin1String("not_open")) {
        lines.append(tr("No staged transaction remains."));
    }
    lines.append(tr("Full EPUBCheck: not run."));
    const QString code = payload.value(QStringLiteral("code")).toString();
    const QString message = payload.value(QStringLiteral("message")).toString();
    if (!code.isEmpty() && !message.isEmpty()) {
        lines.append(tr("Failure: %1 — %2").arg(code, message));
    } else if (!message.isEmpty()) {
        lines.append(tr("Failure: %1").arg(message));
    }
    return lines.join(QLatin1Char('\n'));
}

void AgentDock::appendEvent(const AgentEvent &event)
{
    if (event.type != AgentEventType::AssistantDelta) flushAssistantDeltas();
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
            m_retryAvailable = false;
            captureRequestEvent(event, QStringLiteral("requesting"));
            refreshProviderStatus();
            refreshRetryState();
            break;
        case AgentEventType::ModelRequestCompleted:
            m_providerRequestState = ProviderRequestState::Succeeded;
            m_providerFailure.clear();
            m_retryAvailable = false;
            captureRequestEvent(event, QStringLiteral("succeeded"));
            refreshProviderStatus();
            refreshRetryState();
            break;
        case AgentEventType::ModelRequestFailed:
            m_providerRequestState = ProviderRequestState::Failed;
            m_providerFailure = providerFailureSummary(
                event.payload.value(QStringLiteral("message")).toString());
            m_retryAvailable = true;
            captureRequestEvent(event, QStringLiteral("failed"));
            refreshProviderStatus();
            refreshRetryState();
            break;
        case AgentEventType::ModelRequestCancelled:
            m_providerRequestState = ProviderRequestState::Cancelled;
            m_providerFailure.clear();
            m_retryAvailable = false;
            captureRequestEvent(event, QStringLiteral("cancelled"));
            refreshProviderStatus();
            refreshRetryState();
            break;
        case AgentEventType::AssistantDelta:
            queueAssistantDelta(
                event.payload.value(QStringLiteral("kind")).toString(),
                event.payload.value(QStringLiteral("text")).toString());
            break;
        case AgentEventType::AssistantMessage: {
            const QString reasoning = event.payload.value(QStringLiteral("reasoning_content")).toString();
            const QString content = event.payload.value(QStringLiteral("content")).toString();
            if (!reasoning.isEmpty()) setThinkingText(reasoning, false);
            if (!content.isEmpty()) setAnswerText(content, false);
            break;
        }
        case AgentEventType::ToolRequested:
        case AgentEventType::ToolStarted:
        case AgentEventType::ToolCompleted:
        case AgentEventType::ToolFailed: {
            const QString id = event.payload.value(QStringLiteral("tool_call_id")).toString();
            const QString name = event.payload.value(QStringLiteral("name")).toString();
            const bool commit_failure = event.type == AgentEventType::ToolFailed
                && name == QLatin1String("transaction.commit");
            QString title = tr("Tool: %1").arg(name);
            if (commit_failure) title = tr("Apply failed");
            else if (event.type == AgentEventType::ToolFailed) title = tr("Tool failed: %1").arg(name);
            else if (event.type == AgentEventType::ToolStarted) title = tr("Tool running: %1").arg(name);
            else if (event.payload.value(QStringLiteral("applied")).toBool()) {
                title = tr("Applied: %1").arg(name);
            }
            QString body = name;
            if (commit_failure) {
                body = failedCommitBody(event.payload);
            } else if (event.payload.contains(QStringLiteral("message"))) {
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
            QWidget *card = findCard(object_name);
            if (card) {
                QWidget *existing = card;
                if (auto *title_btn = existing->findChild<QToolButton *>(object_name + QStringLiteral("Title"))) {
                    title_btn->setText(title);
                    if (commit_failure) title_btn->setChecked(true);
                }
                if (auto *body_label = existing->findChild<QLabel *>(object_name + QStringLiteral("Body"))) {
                    body_label->setText(body);
                }
            } else {
                card = makeCard(object_name, title, body, !commit_failure);
                appendCard(card);
            }
            if (commit_failure && card) {
                if (auto *body_label = card->findChild<QLabel *>(
                        object_name + QStringLiteral("Body"))) {
                    body_label->setTextFormat(Qt::PlainText);
                    body_label->setVisible(true);
                }
                const QJsonObject outcomes = event.payload.value(
                    QStringLiteral("data")).toObject().value(
                    QStringLiteral("resource_outcomes")).toObject();
                card->setProperty("successfulResourceCount", outcomes.value(
                    QStringLiteral("successful_resource_count")).toInt());
                card->setProperty("failedResourceCount", outcomes.value(
                    QStringLiteral("failed_resource_count")).toInt());
                card->setProperty("transactionState", outcomes.value(
                    QStringLiteral("transaction_state")).toString());
            }
            break;
        }
        case AgentEventType::ToolApprovalRequested: {
            const QString id = event.payload.value(QStringLiteral("tool_call_id")).toString();
            const QString name = event.payload.value(QStringLiteral("name")).toString();
            const QString impact = event.payload.value(QStringLiteral("impact")).toString();
            const QJsonObject arguments =
                event.payload.value(QStringLiteral("arguments")).toObject();
            const bool requires_plan_review = name == QLatin1String("paragraphs.apply")
                || name == QLatin1String("toc.apply_transform");
            bool reviewed_plan_matches =
                !requires_plan_review || matchesReviewedPlan(name, arguments);
            const QString plan_id = arguments.value(
                QStringLiteral("plan_id")).toString();
            const QJsonObject reviewed_plan = m_reviewedPlans.value(plan_id);
            auto *frame = new QFrame(m_transcriptContents);
            frame->setObjectName(QStringLiteral("agentApprovalCard-%1").arg(id));
            frame->setProperty("planReviewRequired", requires_plan_review);
            auto *layout = new QVBoxLayout(frame);
            layout->addWidget(new QLabel(tr("Approve %1?").arg(name), frame));
            auto *impact_label = new QLabel(impact, frame);
            impact_label->setObjectName(QStringLiteral("agentApprovalImpact"));
            impact_label->setWordWrap(true);
            layout->addWidget(impact_label);
            QLabel *binding = nullptr;
            if (requires_plan_review) {
                binding = new QLabel(
                    reviewed_plan_matches
                        ? tr("Reviewed plan binding: matched.")
                        : tr("Approval blocked: this apply call does not match a reviewed plan."),
                    frame);
                binding->setObjectName(QStringLiteral("agentApprovalPlanBinding"));
                binding->setWordWrap(true);
                binding->setAccessibleName(binding->text());
                layout->addWidget(binding);
            }
            QListWidget *group_list = nullptr;
            const bool paragraph_groups = reviewed_plan_matches
                && name == QLatin1String("paragraphs.apply")
                && reviewed_plan.value(
                    QStringLiteral("operation_groups_independent")).toBool();
            const QJsonArray groups = reviewed_plan.value(
                QStringLiteral("operation_groups")).toArray();
            if (paragraph_groups && !groups.isEmpty()) {
                auto *group_header = new QWidget(frame);
                auto *group_header_layout = new QHBoxLayout(group_header);
                group_header_layout->setContentsMargins(0, 0, 0, 0);
                auto *group_label = new QLabel(
                    tr("Independent XHTML groups: choose one or more files to stage."),
                    group_header);
                group_label->setWordWrap(true);
                auto *select_all = new QPushButton(tr("Select all"), group_header);
                select_all->setObjectName(
                    QStringLiteral("agentPlanGroupsSelectAll-%1").arg(id));
                auto *clear = new QPushButton(tr("Clear"), group_header);
                clear->setObjectName(
                    QStringLiteral("agentPlanGroupsClear-%1").arg(id));
                group_header_layout->addWidget(group_label, 1);
                group_header_layout->addWidget(select_all);
                group_header_layout->addWidget(clear);
                layout->addWidget(group_header);

                group_list = new QListWidget(frame);
                group_list->setObjectName(
                    QStringLiteral("agentPlanGroupList-%1").arg(id));
                group_list->setSelectionMode(QAbstractItemView::NoSelection);
                group_list->setAlternatingRowColors(true);
                group_list->setProperty("planId", plan_id);
                QSet<QString> seen_resources;
                for (const QJsonValue &value : groups) {
                    const QJsonObject group = value.toObject();
                    const QJsonArray resources = group.value(
                        QStringLiteral("resource_ids")).toArray();
                    const QString resource_id = resources.size() == 1
                        ? resources.first().toString() : QString();
                    if (!group.value(
                            QStringLiteral("independently_applicable")).toBool()
                        || resource_id.isEmpty()
                        || seen_resources.contains(resource_id)) {
                        reviewed_plan_matches = false;
                        continue;
                    }
                    seen_resources.insert(resource_id);
                    const QString label = group.value(
                        QStringLiteral("label")).toString();
                    auto *item = new QListWidgetItem(
                        tr("%1 · %2 conversion(s) · %3 protected item(s)")
                            .arg(label.isEmpty() ? resource_id : label)
                            .arg(group.value(
                                QStringLiteral("conversion_count")).toInt())
                            .arg(group.value(
                                QStringLiteral("protected_count")).toInt()),
                        group_list);
                    item->setData(Qt::UserRole, resource_id);
                    item->setData(Qt::UserRole + 1, group.value(
                        QStringLiteral("group_id")).toString());
                    item->setFlags((item->flags() | Qt::ItemIsUserCheckable)
                                   & ~Qt::ItemIsSelectable);
                    item->setCheckState(Qt::Checked);
                    item->setToolTip(label);
                }
                if (group_list->count() != groups.size()) {
                    reviewed_plan_matches = false;
                }
                group_list->setEnabled(reviewed_plan_matches);
                if (!reviewed_plan_matches && binding) {
                    binding->setText(tr("Approval blocked: this apply call does not match a reviewed plan."));
                    binding->setAccessibleName(binding->text());
                }
                group_list->setProperty("totalGroups", group_list->count());
                const int visible_rows = qMin(6, group_list->count());
                group_list->setFixedHeight(
                    qMax(72, visible_rows * group_list->fontMetrics().lineSpacing() + 14));
                layout->addWidget(group_list);
                connect(select_all, &QPushButton::clicked, group_list,
                        [group_list]() {
                    {
                        const QSignalBlocker blocker(group_list);
                        for (int row = 0; row < group_list->count(); ++row) {
                            group_list->item(row)->setCheckState(Qt::Checked);
                        }
                    }
                    emit group_list->itemChanged(group_list->item(0));
                });
                connect(clear, &QPushButton::clicked, group_list,
                        [group_list]() {
                    {
                        const QSignalBlocker blocker(group_list);
                        for (int row = 0; row < group_list->count(); ++row) {
                            group_list->item(row)->setCheckState(Qt::Unchecked);
                        }
                    }
                    emit group_list->itemChanged(group_list->item(0));
                });
            } else if (reviewed_plan_matches
                       && name == QLatin1String("toc.apply_transform")) {
                auto *dependency = new QLabel(
                    tr("TOC hierarchy changes form one dependent group and cannot be split safely."),
                    frame);
                dependency->setObjectName(QStringLiteral("agentApprovalPlanDependency"));
                dependency->setWordWrap(true);
                dependency->setAccessibleName(dependency->text());
                layout->addWidget(dependency);
            }
            auto *buttons = new QHBoxLayout();
            auto *approve = new QPushButton(tr("Approve"), frame);
            approve->setObjectName(QStringLiteral("agentApproveButton-%1").arg(id));
            approve->setEnabled(reviewed_plan_matches);
            approve->setProperty("reviewedPlanMatched", reviewed_plan_matches);
            auto *deny = new QPushButton(tr("Deny"), frame);
            deny->setObjectName(QStringLiteral("agentDenyButton-%1").arg(id));
            buttons->addWidget(approve);
            buttons->addWidget(deny);
            layout->addLayout(buttons);
            frame->setProperty("reviewedPlanMatched", reviewed_plan_matches);
            if (group_list) {
                auto update_selection = [this, frame, binding, approve, group_list,
                                         reviewed_plan_matches]() {
                    const QStringList selected =
                        checkedPlanGroupResourceIds(group_list);
                    group_list->setProperty("selectedResourceIds", selected);
                    frame->setProperty("selectedResourceIds", selected);
                    approve->setProperty("selectedResourceIds", selected);
                    approve->setEnabled(reviewed_plan_matches
                                        && group_list->isEnabled()
                                        && !selected.isEmpty());
                    if (binding && reviewed_plan_matches) {
                        binding->setText(selected.isEmpty()
                            ? tr("Approval blocked: select at least one independent operation group.")
                            : tr("Reviewed plan binding: matched. Selected groups: %1 of %2.")
                                  .arg(selected.size()).arg(group_list->count()));
                        binding->setAccessibleName(binding->text());
                    }
                };
                connect(group_list, &QListWidget::itemChanged, this,
                        [update_selection](QListWidgetItem *) {
                    update_selection();
                });
                update_selection();
            }
            connect(approve, &QPushButton::clicked, this,
                    [this, id, group_list]() {
                QJsonObject overrides;
                if (group_list) {
                    const QStringList selected =
                        checkedPlanGroupResourceIds(group_list);
                    if (selected.isEmpty()) return;
                    overrides.insert(
                        QStringLiteral("selected_resource_ids"),
                        QJsonArray::fromStringList(selected));
                }
                settleApproval(id, true);
                emit approvalResponded(id, true, overrides);
            });
            connect(deny, &QPushButton::clicked, this, [this, id]() {
                settleApproval(id, false);
                emit approvalResponded(id, false, QJsonObject());
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
        case AgentEventType::PlanCreated:
            if (!event.payload.value(QStringLiteral("plan_id")).toString().isEmpty()) {
                m_reviewedPlans.insert(
                    event.payload.value(QStringLiteral("plan_id")).toString(),
                    event.payload);
            }
            appendCard(makePlanReviewCard(event.payload));
            break;
        case AgentEventType::TransactionPreviewed:
            appendCard(makeCard(QStringLiteral("agentPreviewCard"),
                                tr("Preview"),
                                previewBody(event.payload),
                                false));
            break;
        case AgentEventType::TransactionCommitted: {
            QWidget *card = makeCard(QStringLiteral("agentAppliedCard"),
                                     tr("Applied"),
                                     appliedBody(event.payload),
                                     false);
            const QJsonObject outcomes = event.payload.value(
                QStringLiteral("resource_outcomes")).toObject();
            card->setProperty("successfulResourceCount", outcomes.value(
                QStringLiteral("successful_resource_count")).toInt());
            card->setProperty("failedResourceCount", outcomes.value(
                QStringLiteral("failed_resource_count")).toInt());
            card->setProperty("transactionState", outcomes.value(
                QStringLiteral("transaction_state")).toString());
            const QJsonObject recovery =
                event.payload.value(QStringLiteral("recovery")).toObject();
            const QString checkpoint_id =
                recovery.value(QStringLiteral("checkpoint_id")).toString();
            const QString book_session_id =
                recovery.value(QStringLiteral("book_session_id")).toString();
            if (recovery.value(QStringLiteral("task_restore_point")).toString()
                    == QLatin1String("available")
                && !checkpoint_id.isEmpty() && !book_session_id.isEmpty()) {
                auto *restore = new QPushButton(tr("Restore this task"), card);
                restore->setObjectName(
                    QStringLiteral("agentTaskRestoreButton-%1").arg(checkpoint_id));
                restore->setProperty("checkpointId", checkpoint_id);
                restore->setProperty("bookSessionId", book_session_id);
                restore->setProperty("restoreAvailable", true);
                card->layout()->addWidget(restore);
                connect(restore, &QPushButton::clicked, this,
                        [this, restore, checkpoint_id, book_session_id]() {
                    if (!restore->isEnabled()) return;
                    restore->setEnabled(false);
                    emit taskRestoreRequested(checkpoint_id, book_session_id);
                });
                m_taskRestoreButtons.insert(checkpoint_id, restore);
            }
            appendCard(card);
            refreshTaskRestoreState();
            break;
        }
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
        case AgentEventType::TaskRestoreCompleted: {
            const QString checkpoint_id =
                event.payload.value(QStringLiteral("checkpoint_id")).toString();
            if (QPushButton *button = m_taskRestoreButtons.value(checkpoint_id)) {
                button->setProperty("restoreAvailable", false);
                button->setEnabled(false);
                button->setText(tr("Restored"));
                button->setToolTip(tr("This task has already been restored."));
            }
            const int count = event.payload
                .value(QStringLiteral("affected_resources")).toArray().size();
            appendCard(makeCard(
                QStringLiteral("agentTaskRestoreCompletedCard-%1").arg(checkpoint_id),
                tr("Task restored"),
                tr("Restored %1 text resource(s). Later unrelated edits were preserved.")
                    .arg(count),
                false));
            break;
        }
        case AgentEventType::TaskRestoreFailed: {
            const QString checkpoint_id =
                event.payload.value(QStringLiteral("checkpoint_id")).toString();
            if (QPushButton *button = m_taskRestoreButtons.value(checkpoint_id)) {
                button->setProperty("restoreAvailable", true);
            }
            refreshTaskRestoreState();
            const QString code = event.payload.value(QStringLiteral("code")).toString();
            QString body;
            if (code == QLatin1String("TASK_RESTORE_CONFLICT")) {
                const int count = event.payload.value(QStringLiteral("conflicts")).toArray().size();
                body = tr("Restore was blocked because %1 affected resource(s) changed after this task. No book content was changed.")
                           .arg(count);
            } else if (code == QLatin1String("BOOK_TARGET_CHANGED")) {
                body = tr("Restore was blocked because this restore point belongs to another book.");
            } else {
                body = event.payload.value(QStringLiteral("message")).toString();
                if (body.isEmpty()) body = tr("The task could not be restored.");
            }
            appendCard(makeCard(
                QStringLiteral("agentTaskRestoreFailedCard-%1").arg(checkpoint_id),
                tr("Restore blocked"), body, false));
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
        case AgentEventType::RunStateChanged:
            captureRunEvent(event);
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
