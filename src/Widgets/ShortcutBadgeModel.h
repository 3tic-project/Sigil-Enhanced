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

struct ClipShortcutText
{
    QString tooltip;
    QString accessibleName;
};

class ShortcutBadgeModel
{
public:
    static ShortcutBadgePresentation Present(
        const QKeySequence &shortcut, const QKeySequence &defaultShortcut);
    static ClipShortcutText FormatClipText(
        const QString &name, int slot, const QString &clipText,
        const QKeySequence &shortcut, int previewGraphemeLimit = 160);
};
