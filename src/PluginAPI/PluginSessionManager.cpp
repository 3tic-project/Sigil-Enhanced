/************************************************************************
**
**  This file is part of Sigil.
**
*************************************************************************/

#include "PluginAPI/PluginSessionManager.h"

#include "MainUI/MainWindow.h"
#include "Misc/Plugin.h"
#include "PluginAPI/PluginSession.h"

#include <QEventLoop>
#include <QMessageBox>
#include <QTimer>

#include <utility>

PluginSessionManager::PluginSessionManager(MainWindow *main_window, TabManager *tab_manager) :
    QObject(main_window),
    m_MainWindow(main_window),
    m_TabManager(tab_manager)
{
}

PluginSessionManager::~PluginSessionManager()
{
    StopAll();
}

bool PluginSessionManager::StartPlugin(const Plugin &plugin, QString *error)
{
    return StartSession(plugin, error) != nullptr;
}

PluginSession *PluginSessionManager::StartSession(const Plugin &plugin, QString *error)
{
    if (plugin.get_lifetime() == QStringLiteral("book-session")) {
        for (PluginSession *running : std::as_const(m_Sessions)) {
            if (running->IsBookSession() && running->PluginName() == plugin.get_name()) {
                if (error) {
                    *error = tr("This book-session plugin is already running for the current Book.");
                }
                return nullptr;
            }
        }
    }

    auto *session = new PluginSession(plugin, m_MainWindow, m_TabManager, this);
    const QUuid id = session->SessionId();
    connect(session, &PluginSession::Ended, this, [this, id]() {
        PluginSession *finished = m_Sessions.take(id);
        if (finished) {
            const QString input_path = finished->PendingInputEpubPath();
            finished->disconnect(this);
            if (!input_path.isEmpty()) {
                bool proceed = true;
                if (m_MainWindow->GetCurrentBook()->IsModified()) {
                    proceed = QMessageBox::question(
                        m_MainWindow, tr("Input Plugin"),
                        tr("Your current book will be completely replaced, losing any unsaved changes. Continue?"),
                        QMessageBox::Yes | QMessageBox::No,
                        QMessageBox::No) == QMessageBox::Yes;
                }
                if (proceed) {
                    m_MainWindow->LoadFile(input_path, true, true);
                }
            }
            finished->deleteLater();
        }
    });
    if (!session->Start(error)) {
        delete session;
        return nullptr;
    }
    m_Sessions.insert(id, session);
    return session;
}

bool PluginSessionManager::RunPluginAndWait(const Plugin &plugin, QString *status,
                                            QString *plugin_type,
                                            int *validation_error_count,
                                            QString *error, int timeout_ms)
{
    if (plugin.get_lifetime() == QStringLiteral("book-session")) {
        if (error) *error = tr("Book-session plugins cannot run as an Automate step.");
        return false;
    }
    PluginSession *session = StartSession(plugin, error);
    if (!session) return false;

    QEventLoop loop;
    bool timed_out = false;
    QString completed_status;
    connect(session, &PluginSession::Ended, &loop, [&]() {
        completed_status = session->Status();
        if (status) *status = completed_status;
        if (plugin_type) *plugin_type = session->PluginType();
        if (validation_error_count) {
            *validation_error_count = session->ValidationErrorCount();
        }
        loop.quit();
    });
    QTimer timeout;
    timeout.setSingleShot(true);
    connect(&timeout, &QTimer::timeout, &loop, [&]() {
        timed_out = true;
        session->Cancel();
    });
    timeout.start(qMax(1, timeout_ms));
    loop.exec();
    if (timed_out) {
        if (error) *error = tr("Live plugin timed out.");
        return false;
    }
    return completed_status == QStringLiteral("success");
}

bool PluginSessionManager::AcquireWriter(const QUuid &session_id)
{
    return m_WriterLock.Acquire(session_id);
}

void PluginSessionManager::ReleaseWriter(const QUuid &session_id)
{
    m_WriterLock.Release(session_id);
}

void PluginSessionManager::StopAll()
{
    const QList<PluginSession *> sessions = m_Sessions.values();
    m_Sessions.clear();
    for (PluginSession *session : sessions) {
        session->Cancel();
        delete session;
    }
    m_WriterLock.Clear();
}

int PluginSessionManager::SessionCount() const
{
    return m_Sessions.size();
}

bool PluginSessionManager::HasWriter() const
{
    return m_WriterLock.IsHeld();
}
