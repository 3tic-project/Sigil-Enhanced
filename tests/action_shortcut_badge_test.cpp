#include <cstdlib>
#include <iostream>

#include <QAction>
#include <QApplication>
#include <QImage>
#include <QToolButton>

#include "Widgets/ActionShortcutBadge.h"

namespace {

void Require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

}

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    const QKeySequence default_shortcut(
        Qt::ControlModifier | Qt::AltModifier | Qt::Key_1);
    QAction action(QStringLiteral("Ruby"));
    action.setShortcut(default_shortcut);
    QToolButton button;
    button.setDefaultAction(&action);
    button.resize(140, 40);
    button.show();

    ActionShortcutBadge badge(&button, &action, default_shortcut);
    int shortcut_change_count = 0;
    badge.setShortcutChangedCallback([&shortcut_change_count]() {
        ++shortcut_change_count;
    });
    application.processEvents();

    Require(badge.isVisible() && badge.displayedText() == QLatin1String("1")
                && badge.usesNumericBadge(),
            "The default shortcut did not produce a visible numeric badge");
    Require(badge.testAttribute(Qt::WA_TransparentForMouseEvents)
                && button.rect().contains(badge.badgeRect()),
            "The badge intercepts input or paints outside its button");
    Require(badge.font().pixelSize() >= 9,
            "The shortcut badge font is smaller than 9 logical pixels");
    Require(button.styleSheet().contains(QStringLiteral("padding-right")),
            "The button did not reserve space for its shortcut badge");

    button.resize(36, 40);
    application.processEvents();
    Require(badge.displayedText() == QLatin1String("1")
                && button.rect().contains(badge.badgeRect()),
            "Resizing a narrow Clip button displaced its numeric badge");
    button.resize(140, 40);

    action.setShortcut(QKeySequence(Qt::ControlModifier | Qt::ShiftModifier | Qt::Key_K));
    application.processEvents();
    Require(shortcut_change_count == 1 && badge.isVisible()
                && !badge.usesNumericBadge() && badge.displayedText() != QLatin1String("1"),
            "A customized shortcut did not refresh the badge presentation");
    button.resize(42, 40);
    application.processEvents();
    Require(badge.displayedText() == QLatin1String("Key"),
            "A narrow customized shortcut did not use the compact keyboard label");
    button.resize(140, 40);

    QPalette dark_palette = button.palette();
    dark_palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#4455aa")));
    dark_palette.setColor(QPalette::HighlightedText, QColor(Qt::white));
    button.setPalette(dark_palette);
    application.processEvents();
    const QImage rendered = button.grab().toImage();
    Require(!rendered.isNull()
                && rendered.deviceIndependentSize().toSize() == button.size(),
            "The themed shortcut button did not render at its device pixel ratio");

    button.setStyleSheet(QStringLiteral("QToolButton { color: #123456; }"));
    application.processEvents();
    const QString runtimeStyle = button.styleSheet();
    if (!runtimeStyle.contains(QStringLiteral("color: #123456"))
        || !runtimeStyle.contains(QStringLiteral("padding-right"))) {
        std::cerr << "Runtime style: " << runtimeStyle.toStdString() << '\n';
    }
    Require(runtimeStyle.contains(QStringLiteral("color: #123456"))
                && runtimeStyle.contains(QStringLiteral("padding-right")),
            "A runtime button style change was not preserved with badge padding");

    action.setShortcut(QKeySequence());
    application.processEvents();
    Require(shortcut_change_count == 2 && !badge.isVisible()
                && button.styleSheet() == QLatin1String("QToolButton { color: #123456; }"),
            "Clearing the shortcut did not remove the badge and reserved padding");

    action.setShortcut(default_shortcut);
    badge.setBadgesVisible(false);
    application.processEvents();
    Require(!badge.isVisible()
                && button.styleSheet() == QLatin1String("QToolButton { color: #123456; }"),
            "The display preference did not hide the badge cleanly");
    return EXIT_SUCCESS;
}
