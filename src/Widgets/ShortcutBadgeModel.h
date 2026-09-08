#pragma once

#include <QKeySequence>
#include <QString>

struct ShortcutBadgePresentation
{
    QString badgeText;
    QString shortcutText;
    bool numeric = false;

    bool isVisible() const { return !badgeText.isEmpty(); }
};

class ShortcutBadgeModel
{
public:
    static ShortcutBadgePresentation Present(
        const QKeySequence &shortcut, const QKeySequence &defaultShortcut);
};
