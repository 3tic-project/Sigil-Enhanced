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
class QVBoxLayout;
class QWidget;

namespace SigilAgent
{

class AgentDock : public QDockWidget
{
    Q_OBJECT

public:
    explicit AgentDock(QWidget *parent = nullptr);

    AgentMode mode() const;
    void setMode(AgentMode mode);
    void setContextScope(const QString &scope);
    void setModelName(const QString &model);
    void setProviderConfiguration(const AgentProviderReadiness &readiness);
    void setRunState(AgentRunState state);
    void setBookContext(const QString &title,
                        const QString &fileName,
                        int resourceCount,
                        bool modified,
                        quint64 revision);
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
    void approvalResponded(const QString &toolCallId, bool approved);

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
    QWidget *findCard(const QString &object_name) const;
    void appendCard(QWidget *card);
    void beginUserTurn();
    void beginModelStep();
    void settleApproval(const QString &toolCallId, bool approved);
    void setThinkingText(const QString &text, bool append);
    void setAnswerText(const QString &text, bool append);
    void chooseDefaultScope();
    void refreshScopeLabel();
    void refreshProviderStatus();
    QString providerFailureSummary(const QString &message) const;
    QString previewBody(const QJsonObject &payload) const;
    QString appliedBody(const QJsonObject &payload) const;

    QComboBox *m_modeCombo = nullptr;
    QLabel *m_modelLabel = nullptr;
    QLabel *m_contextScope = nullptr;
    QLabel *m_runState = nullptr;
    QLabel *m_providerStatus = nullptr;
    QLabel *m_bookStatus = nullptr;
    QLabel *m_composerHint = nullptr;
    QButtonGroup *m_scopeGroup = nullptr;
    QPushButton *m_stopButton = nullptr;
    QPushButton *m_newSessionButton = nullptr;
    QPushButton *m_sendButton = nullptr;
    QToolButton *m_chipBook = nullptr;
    QToolButton *m_chipFile = nullptr;
    QToolButton *m_chipSelectedFiles = nullptr;
    QToolButton *m_chipSelection = nullptr;
    QPlainTextEdit *m_composer = nullptr;
    QScrollArea *m_transcript = nullptr;
    QWidget *m_transcriptContents = nullptr;
    QVBoxLayout *m_transcriptLayout = nullptr;
    QHash<QString, QWidget *> m_approvalCards;
    QWidget *m_currentThinking = nullptr;
    QWidget *m_currentAnswer = nullptr;
    int m_turn = 0;
    int m_step = 0;
    QString m_bookTitle;
    QString m_bookFileName;
    int m_bookResourceCount = 0;
    bool m_bookModified = false;
    quint64 m_bookRevision = 0;
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
};

} // namespace SigilAgent

#endif
