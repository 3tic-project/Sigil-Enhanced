/************************************************************************
**
**  This file is part of Sigil.
**
*************************************************************************/

#include "PluginAPI/PluginSessionManager.h"

#include "MainUI/MainWindow.h"
#include "Misc/Plugin.h"
#include "PluginAPI/PluginSession.h"

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
    if (plugin.get_api_version() != 2 || plugin.get_api_interface() != QStringLiteral("live")) {
        if (error) {
            *error = tr("This plugin does not declare Plugin API v2 live support.");
        }
        return false;
    }
    if (plugin.get_type() == QStringLiteral("input")) {
        if (error) {
            *error = tr("Input plugins currently require the legacy plugin runtime.");
        }
        return false;
    }
    if (plugin.get_lifetime() != QStringLiteral("command")) {
        if (error) {
            *error = tr("Book-session plugins are not supported by this live runtime stage.");
        }
        return false;
    }

    auto *session = new PluginSession(plugin, m_MainWindow, m_TabManager, this);
    const QUuid id = session->SessionId();
    connect(session, &PluginSession::Ended, this, [this, id]() {
        PluginSession *finished = m_Sessions.take(id);
        if (finished) {
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
