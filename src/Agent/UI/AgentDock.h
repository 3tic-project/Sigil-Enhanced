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

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
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
    void setRunState(AgentRunState state);
    void appendEvent(const AgentEvent &event);
    void resetTranscript();

    QString composerText() const;
    QStringList contextHandles() const;

signals:
    void sendRequested(const QString &text, const QStringList &handles);
    void stopRequested();
    void newSessionRequested();
    void modeChanged(AgentMode mode);
    void approvalResponded(const QString &toolCallId, bool approved);

private slots:
    void onSend();
    void onModeChanged();

private:
    QWidget *makeCard(const QString &object_name,
                      const QString &title,
                      const QString &body,
                      bool collapsed);
    QWidget *findCard(const QString &object_name) const;
    void appendCard(QWidget *card);
    void setThinkingText(const QString &text, bool append);
    void setAnswerText(const QString &text, bool append);

    QComboBox *m_modeCombo = nullptr;
    QLineEdit *m_modelEdit = nullptr;
    QLabel *m_contextScope = nullptr;
    QLabel *m_runState = nullptr;
    QPushButton *m_stopButton = nullptr;
    QPushButton *m_newSessionButton = nullptr;
    QPushButton *m_sendButton = nullptr;
    QPlainTextEdit *m_composer = nullptr;
    QLineEdit *m_handles = nullptr;
    QScrollArea *m_transcript = nullptr;
    QWidget *m_transcriptContents = nullptr;
    QVBoxLayout *m_transcriptLayout = nullptr;
    QHash<QString, QWidget *> m_approvalCards;
};

} // namespace SigilAgent

#endif
