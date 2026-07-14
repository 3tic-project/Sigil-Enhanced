/************************************************************************
**
**  This file is part of Sigil.
**
*************************************************************************/

#include "PluginAPI/PluginSessionManager.h"

#include "MainUI/MainWindow.h"
#include "Misc/Plugin.h"
#include "PluginAPI/PluginSession.h"

#include <QMessageBox>

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
    if (plugin.get_lifetime() == QStringLiteral("book-session")) {
        for (PluginSession *running : std::as_const(m_Sessions)) {
            if (running->IsBookSession() && running->PluginName() == plugin.get_name()) {
                if (error) {
                    *error = tr("This book-session plugin is already running for the current Book.");
                }
                return false;
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
                    m_MainWindow->LoadFile(input_path, true);
                }
            }
            finished->deleteLater();
        }
    });
    if (!session->Start(error)) {
        delete session;
        return false;
    }
    m_Sessions.insert(id, session);
    return true;
}

void PluginSessionManager::StopAll()
{
    const QList<PluginSession *> sessions = m_Sessions.values();
    m_Sessions.clear();
    for (PluginSession *session : sessions) {
        session->Cancel();
        delete session;
    }
}

int PluginSessionManager::SessionCount() const
{
    return m_Sessions.size();
}
