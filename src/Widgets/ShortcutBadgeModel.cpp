#include "Widgets/ShortcutBadgeModel.h"

#include <Qt>

ShortcutBadgePresentation ShortcutBadgeModel::Present(
    const QKeySequence &shortcut, const QKeySequence &defaultShortcut)
{
    ShortcutBadgePresentation presentation;
    if (shortcut.isEmpty()) return presentation;

    presentation.shortcutText = shortcut.toString(QKeySequence::NativeText);
    presentation.badgeText = presentation.shortcutText;
    if (shortcut.count() != 1 || defaultShortcut.count() != 1) {
        return presentation;
    }

    const QKeyCombination key = shortcut[0];
    const QKeyCombination default_key = defaultShortcut[0];
    if (key.keyboardModifiers() != default_key.keyboardModifiers()
        || key.key() < Qt::Key_0 || key.key() > Qt::Key_9) {
        return presentation;
    }

    presentation.badgeText = QString::number(
        static_cast<int>(key.key()) - static_cast<int>(Qt::Key_0));
    presentation.numeric = true;
    return presentation;
}
