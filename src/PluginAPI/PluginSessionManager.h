/************************************************************************
**
**  This file is part of Sigil.
**
*************************************************************************/

#pragma once
#ifndef PLUGINSESSIONMANAGER_H
#define PLUGINSESSIONMANAGER_H

#include <QHash>
#include <QObject>
#include <QString>
#include <QUuid>
#include <functional>

#include "PluginAPI/PluginWriterLock.h"

class MainWindow;
class Plugin;
class PluginSession;
class TabManager;

struct SnippetRunOutcome {
    QString status;
    QString message;
    QString stdoutText;
    QString stderrText;
    qint64 stdoutLength = 0;
    qint64 stderrLength = 0;
    QString output;
    int exitCode = -1;
    bool bookChanged = false;
};

class PluginSessionManager : public QObject
{
    Q_OBJECT

public:
    PluginSessionManager(MainWindow *main_window, TabManager *tab_manager);
    ~PluginSessionManager() override;

    bool StartPlugin(const Plugin &plugin, QString *error = nullptr);
    bool RunPluginAndWait(const Plugin &plugin, QString *status, QString *plugin_type,
                          int *validation_error_count, QString *error = nullptr,
                          int timeout_ms = 30 * 60 * 1000,
                          QString *output = nullptr, bool quiet = false);
    bool RunSnippetAndWait(const QString &script, QString *status, QString *error = nullptr,
                           int timeout_ms = 30 * 1000, QString *output = nullptr);
    bool RunSnippetDetailed(const QString &script, SnippetRunOutcome *outcome,
                            QString *error = nullptr, int timeout_ms = 30 * 1000,
                            bool read_only = false);
    bool AcquireWriter(const QUuid &session_id);
    void ReleaseWriter(const QUuid &session_id);
    void StopAll();
    int SessionCount() const;
    bool HasWriter() const;
    // Native integration tests use this one-shot hook to prove that a live
    // commit restores already-applied mutations. Negative values disable it.
    void SetCommitFailureAfterMutationForTesting(int successful_mutations);
    // Native crash tests stop the host at a deterministic physical mutation.
    void SetCommitMutationCallbackForTesting(std::function<void()> callback);

private:
    friend class PluginSession;
    bool ConsumeCommitMutationForTesting();

    PluginSession *StartSession(const Plugin &plugin, QString *error, bool quiet = false,
                                const QString &snippet_path = QString(),
                                bool snippet_read_only = false);
    bool WaitForSession(PluginSession *session, QString *status, QString *plugin_type,
                        int *validation_error_count, QString *error, int timeout_ms,
                        QString *output, SnippetRunOutcome *outcome = nullptr);

    MainWindow *m_MainWindow;
    TabManager *m_TabManager;
    QHash<QUuid, PluginSession *> m_Sessions;
    PluginApi::WriterLock m_WriterLock;
    int m_CommitMutationsBeforeFailure = -1;
    std::function<void()> m_CommitMutationCallbackForTesting;
};

#endif // PLUGINSESSIONMANAGER_H
