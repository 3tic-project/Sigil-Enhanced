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

    const QKeySequence changed_digit(
        Qt::ControlModifier | Qt::AltModifier | Qt::Key_7);
    presentation = ShortcutBadgeModel::Present(changed_digit, original);
    Require(presentation.numeric && presentation.badgeText == QLatin1String("7"),
            "A same-family numeric shortcut did not show its effective digit");

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

void TestTooltipAndAccessibilityText()
{
    const QString clip = QString::fromUtf8("<ruby>風&雨</ruby>\n第二行");
    const QKeySequence shortcut(Qt::ControlModifier | Qt::AltModifier | Qt::Key_3);
    ClipShortcutText text = ShortcutBadgeModel::FormatClipText(
        QStringLiteral("Ruby & emphasis"), 3, clip, shortcut);
    Require(text.tooltip.contains(QStringLiteral("&lt;ruby&gt;"))
                && text.tooltip.contains(QStringLiteral("&amp;"))
                && !text.tooltip.contains(QStringLiteral("<ruby>")),
            "Clip markup was not escaped in the rich tooltip");
    Require(text.tooltip.contains(shortcut.toString(QKeySequence::NativeText).toHtmlEscaped()),
            "The tooltip does not contain the native shortcut text");
    Require(text.accessibleName.contains(QStringLiteral("Clip 3"))
                && text.accessibleName.contains(shortcut.toString(QKeySequence::NativeText)),
            "The accessible name is missing the slot or shortcut");

    text = ShortcutBadgeModel::FormatClipText(
        QStringLiteral("Plain"), 4, QStringLiteral("content"), QKeySequence());
    Require(text.tooltip.contains(QStringLiteral("No shortcut assigned"))
                && text.accessibleName.contains(QStringLiteral("no shortcut assigned")),
            "The unassigned shortcut state is not exposed as text");

    const QString long_clip = QString::fromUtf8("👨‍👩‍👧‍👦AB");
    text = ShortcutBadgeModel::FormatClipText(
        QStringLiteral("Emoji"), 5, long_clip, shortcut, 1);
    Require(text.tooltip.contains(QString::fromUtf8("👨‍👩‍👧‍👦…")),
            "Tooltip truncation split a grapheme cluster");
}

}

int main()
{
    TestDefaultDigitBadges();
    TestCustomizedShortcuts();
    TestUnassignedShortcut();
    TestTooltipAndAccessibilityText();
    return EXIT_SUCCESS;
}
