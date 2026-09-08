#include <cstdlib>
#include <iostream>

#include <QKeySequence>

#include "Widgets/ShortcutBadgeModel.h"

namespace {

void Require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void TestDefaultDigitBadges()
{
    for (int digit = 0; digit <= 9; ++digit) {
        const QKeySequence shortcut(
            Qt::ControlModifier | Qt::AltModifier
            | static_cast<Qt::Key>(Qt::Key_0 + digit));
        const ShortcutBadgePresentation presentation =
            ShortcutBadgeModel::Present(shortcut, shortcut);
        Require(presentation.numeric, "A default numeric shortcut lost its numeric badge");
        Require(presentation.badgeText == QString::number(digit),
                "A numeric shortcut badge does not show the actual key");
        Require(presentation.shortcutText == shortcut.toString(QKeySequence::NativeText),
                "The native shortcut label was not preserved");
    }
}

void TestCustomizedShortcuts()
{
    const QKeySequence original(Qt::ControlModifier | Qt::AltModifier | Qt::Key_3);
    const QKeySequence changed_modifiers(
        Qt::ControlModifier | Qt::ShiftModifier | Qt::Key_3);
    ShortcutBadgePresentation presentation =
        ShortcutBadgeModel::Present(changed_modifiers, original);
    Require(!presentation.numeric && presentation.badgeText == presentation.shortcutText,
            "A shortcut with customized modifiers retained a misleading digit badge");

    const QKeySequence letter(Qt::ControlModifier | Qt::AltModifier | Qt::Key_K);
    presentation = ShortcutBadgeModel::Present(letter, original);
    Require(!presentation.numeric && presentation.badgeText == presentation.shortcutText,
            "A non-digit shortcut was presented as a numeric badge");

    const QKeySequence chord(QStringLiteral("Ctrl+K, Ctrl+C"));
    presentation = ShortcutBadgeModel::Present(chord, original);
    Require(!presentation.numeric && presentation.badgeText == presentation.shortcutText,
            "A multi-stroke shortcut was presented as a numeric badge");
}

void TestUnassignedShortcut()
{
    const QKeySequence original(Qt::ControlModifier | Qt::AltModifier | Qt::Key_1);
    const ShortcutBadgePresentation presentation =
        ShortcutBadgeModel::Present(QKeySequence(), original);
    Require(!presentation.isVisible() && presentation.shortcutText.isEmpty(),
            "An unassigned shortcut retained a badge or shortcut label");
}

}

int main()
{
    TestDefaultDigitBadges();
    TestCustomizedShortcuts();
    TestUnassignedShortcut();
    return EXIT_SUCCESS;
}
