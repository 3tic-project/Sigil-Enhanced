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
    void StopAll();
    int SessionCount() const;

private:
    MainWindow *m_MainWindow;
    TabManager *m_TabManager;
    QHash<QUuid, PluginSession *> m_Sessions;
};

#endif // PLUGINSESSIONMANAGER_H
