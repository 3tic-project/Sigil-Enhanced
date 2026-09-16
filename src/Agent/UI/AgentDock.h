/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef SIGIL_AGENT_DOCK_H
#define SIGIL_AGENT_DOCK_H

#include <QDockWidget>
#include <QHash>

#include "Agent/AgentTypes.h"
#include "Agent/Model/AgentProviderPreset.h"

class QComboBox;
class QButtonGroup;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QToolButton;
class QTimer;
class QVBoxLayout;
class QWidget;

namespace SigilAgent
{

struct AgentPlanComparisonContent;

class AgentDock : public QDockWidget
{
    Q_OBJECT

public:
    explicit AgentDock(QWidget *parent = nullptr);

    AgentMode mode() const;
    void setMode(AgentMode mode);
    void setContextScope(const QString &scope);
    void setSessionId(const QString &sessionId);
    void setModelName(const QString &model);
    void setProviderConfiguration(const AgentProviderReadiness &readiness);
    void setRunState(AgentRunState state);
    void setBookContext(const QString &title,
                        const QString &fileName,
                        int resourceCount,
                        bool modified,
                        quint64 revision,
                        const QString &bookSessionId);
    void setCurrentFile(const QString &book_path, const QString &resource_id);
    void setSelectedFiles(const QStringList &book_paths, const QStringList &resource_ids);
    void setSelection(const QString &resource_id, int start, int end, const QString &snippet);
    void appendEvent(const AgentEvent &event);
    void resetTranscript();

    QString composerText() const;
    QStringList contextHandles() const;

signals:
    void sendRequested(const QString &text, const QStringList &handles);
    void stopRequested();
    void newSessionRequested();
    void exportConversationRequested();
    void exportDebugLogRequested();
    void modeChanged(AgentMode mode);
    void approvalResponded(const QString &toolCallId, bool approved,
                           const QJsonObject &argumentOverrides);
    void openPlanResourceRequested(const QString &bookPath,
                                   const QString &bookSessionId);
    void taskRestoreRequested(const QString &checkpointId,
                              const QString &bookSessionId);

private slots:
    void onSend();
    void onModeChanged();

private:
    enum class ProviderRequestState {
        Configured,
        Requesting,
        Succeeded,
        Failed,
        Cancelled
    };

    bool eventFilter(QObject *watched, QEvent *event) override;
    QString thinkingCardName() const;
    QString answerCardName() const;
    QWidget *makeCard(const QString &object_name,
                      const QString &title,
                      const QString &body,
                      bool collapsed);
    QWidget *makePlanReviewCard(const QJsonObject &payload);
    QWidget *findCard(const QString &object_name) const;
    void appendCard(QWidget *card);
    void beginUserTurn();
    void beginModelStep();
    void settleApproval(const QString &toolCallId, bool approved);
    void queueAssistantDelta(const QString &kind, const QString &text);
    void flushAssistantDeltas();
    void discardAssistantDeltas();
    void setThinkingText(const QString &text, bool append);
    void setAnswerText(const QString &text, bool append);
    void chooseDefaultScope();
    void refreshScopeLabel();
    void refreshProviderStatus();
    void refreshTechnicalDetails();
    void refreshRetryState();
    void refreshTaskRestoreState();
    void refreshPlanNavigationState();
    void closePlanComparisons();
    void showPlanComparison(const QJsonObject &payload,
                            const QString &comparisonId,
                            const QString &subject,
                            const AgentPlanComparisonContent &content);
    void recordReviewedPlanPage(const QJsonObject &payload);
    bool matchesReviewedPlan(const QString &toolName,
                             const QJsonObject &arguments) const;
    void captureRequestEvent(const AgentEvent &event, const QString &status);
    void captureRunEvent(const AgentEvent &event);
    QString providerFailureSummary(const QString &message) const;
    QString planReviewBody(const QJsonObject &payload) const;
    QString previewBody(const QJsonObject &payload) const;
    QString resourceOutcomesBody(const QJsonObject &outcomes) const;
    QString appliedBody(const QJsonObject &payload) const;
    QString failedCommitBody(const QJsonObject &payload) const;

    QComboBox *m_modeCombo = nullptr;
    QLabel *m_modelLabel = nullptr;
    QLabel *m_contextScope = nullptr;
    QLabel *m_runState = nullptr;
    QLabel *m_providerStatus = nullptr;
    QLabel *m_bookStatus = nullptr;
    QToolButton *m_technicalDetailsToggle = nullptr;
    QLabel *m_technicalDetails = nullptr;
    QLabel *m_composerHint = nullptr;
    QButtonGroup *m_scopeGroup = nullptr;
    QPushButton *m_stopButton = nullptr;
    QPushButton *m_newSessionButton = nullptr;
    QPushButton *m_sendButton = nullptr;
    QPushButton *m_retryButton = nullptr;
    QToolButton *m_chipBook = nullptr;
    QToolButton *m_chipFile = nullptr;
    QToolButton *m_chipSelectedFiles = nullptr;
    QToolButton *m_chipSelection = nullptr;
    QPlainTextEdit *m_composer = nullptr;
    QScrollArea *m_transcript = nullptr;
    QWidget *m_transcriptContents = nullptr;
    QVBoxLayout *m_transcriptLayout = nullptr;
    QTimer *m_streamFlushTimer = nullptr;
    QString m_pendingThinkingText;
    QString m_pendingAnswerText;
    quint64 m_streamRenderBatches = 0;
    QHash<QString, QWidget *> m_approvalCards;
    QHash<QString, QJsonObject> m_reviewedPlans;
    QHash<QString, QPushButton *> m_taskRestoreButtons;
    QWidget *m_currentThinking = nullptr;
    QWidget *m_currentAnswer = nullptr;
    int m_turn = 0;
    int m_step = 0;
    QString m_bookTitle;
    QString m_bookFileName;
    int m_bookResourceCount = 0;
    bool m_bookModified = false;
    quint64 m_bookRevision = 0;
    QString m_bookSessionId;
    QString m_sessionId;
    QString m_filePath;
    QString m_fileId;
    QStringList m_selectedFilePaths;
    QStringList m_selectedFileIds;
    QString m_selectionId;
    QString m_selectionSnippet;
    int m_selectionStart = 0;
    int m_selectionEnd = 0;
    bool m_scopeChoiceExplicit = false;
    AgentProviderReadiness m_providerReadiness;
    ProviderRequestState m_providerRequestState = ProviderRequestState::Configured;
    QString m_providerFailure;
    QString m_requestId;
    QString m_requestModel;
    QString m_requestMode;
    QString m_requestStatus;
    QString m_requestBookSessionId;
    QStringList m_requestHandles;
    qint64 m_requestBookRevision = 0;
    int m_requestStep = 0;
    qint64 m_requestDurationMs = -1;
    qint64 m_requestFinishedAtMs = 0;
    bool m_requestUsageRequested = false;
    ModelUsage m_requestUsage;
    ModelResponseTiming m_requestTiming;
    QJsonObject m_requestHistoryContext;
    QJsonObject m_requestToolContext;
    QString m_runId;
    QString m_runStatus;
    qint64 m_runDurationMs = -1;
    qint64 m_runFinishedAtMs = 0;
    int m_runModelSteps = -1;
    int m_runMaxModelSteps = -1;
    int m_runToolCalls = -1;
    int m_runMaxToolCalls = -1;
    bool m_runUsageRequested = false;
    bool m_runUsageComplete = false;
    int m_runUsageRequestCount = 0;
    int m_runUsageReportedRequests = 0;
    ModelUsage m_runUsage;
    int m_runInputUsageRequests = 0;
    int m_runOutputUsageRequests = 0;
    int m_runTotalUsageRequests = 0;
    int m_runCachedUsageRequests = 0;
    int m_runReasoningUsageRequests = 0;
    QString m_lastSubmittedText;
    QStringList m_lastSubmittedHandles;
    QString m_lastSubmittedBookSessionId;
    bool m_retryAvailable = false;
    bool m_runActive = false;
};

} // namespace SigilAgent

#endif
