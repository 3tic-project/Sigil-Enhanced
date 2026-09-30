/************************************************************************
**
**  This file is part of Sigil.
**
*************************************************************************/

#pragma once
#ifndef TEMPCLEANUP_H
#define TEMPCLEANUP_H

#include <functional>

#include <QMutex>
#include <QObject>
#include <QStringList>
#include <QTimer>

// Removes Sigil's temporary directories. A directory that is still in use
// (Windows keeps open files and working directories undeletable) is retried
// in the background, and the application waits for it before exiting.
class TempCleanup : public QObject
{
    Q_OBJECT

public:
    static TempCleanup &instance();

    // Safe to call from any thread.
    void Remove(const QString &path);

    QStringList PendingPaths() const;

    // Called on the main thread before retrying a path that is still in use.
    void SetHandleReleaser(std::function<void(const QString &)> releaser);

    // Shows a waiting dialog until pending directories are gone or the user
    // stops waiting; whatever remains is removed on the next start.
    void FinishBeforeExit();

    void RemoveLeftoversFromPreviousRun();

    static bool RemoveTree(const QString &path);

private slots:
    void RetryPending();

private:
    TempCleanup();
    bool RetryOnce(bool release_handles);
    void ScheduleRetry();
    void RecordLeftovers(const QStringList &paths);

    mutable QMutex m_mutex;
    QStringList m_pending;
    std::function<void(const QString &)> m_releaser;
    QTimer m_timer;
    int m_retries = 0;
};

#endif
