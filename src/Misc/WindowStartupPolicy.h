#pragma once

#include <QtCore/Qt>

namespace WindowStartupPolicy
{

// Keep native title bars/taskbars reachable on Windows. Legacy E13 profiles
// saved an automatic fullscreen state without distinguishing a user's choice.
inline Qt::WindowState Resolve(bool windows, bool restoreSavedState, bool fullscreen,
                               bool maximized, bool windowsStateMigrated)
{
    if (!restoreSavedState) return windows ? Qt::WindowMaximized : Qt::WindowFullScreen;
    if (fullscreen) {
        return windows && !windowsStateMigrated ? Qt::WindowMaximized : Qt::WindowFullScreen;
    }
    return maximized ? Qt::WindowMaximized : Qt::WindowNoState;
}

}
