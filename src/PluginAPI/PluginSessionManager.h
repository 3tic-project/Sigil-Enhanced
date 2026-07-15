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
#include <QUuid>

#include "PluginAPI/PluginWriterLock.h"

class MainWindow;
class Plugin;
class PluginSession;
class TabManager;

class PluginSessionManager : public QObject
{
    Q_OBJECT

public:
    PluginSessionManager(MainWindow *main_window, TabManager *tab_manager);
    ~PluginSessionManager() override;

    bool StartPlugin(const Plugin &plugin, QString *error = nullptr);
    bool RunPluginAndWait(const Plugin &plugin, QString *status, QString *plugin_type,
                          int *validation_error_count, QString *error = nullptr,
                          int timeout_ms = 30 * 60 * 1000);
    bool AcquireWriter(const QUuid &session_id);
    void ReleaseWriter(const QUuid &session_id);
    void StopAll();
    int SessionCount() const;
    bool HasWriter() const;

private:
    PluginSession *StartSession(const Plugin &plugin, QString *error);

    MainWindow *m_MainWindow;
    TabManager *m_TabManager;
    QHash<QUuid, PluginSession *> m_Sessions;
    PluginApi::WriterLock m_WriterLock;
};

#endif // PLUGINSESSIONMANAGER_H
