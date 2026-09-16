#include "Widgets/ShortcutBadgeModel.h"

#include <QCoreApplication>
#include <QTextBoundaryFinder>
#include <Qt>

namespace {

QString ElideGraphemes(const QString &text, int limit)
{
    if (limit <= 0 || text.isEmpty()) return QString();
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text);
    int end = 0;
    for (int count = 0; count < limit; ++count) {
        const int next = finder.toNextBoundary();
        if (next < 0) return text;
        end = next;
    }
    if (end >= text.size()) return text;
    return text.left(end) + QChar(0x2026);
}

}

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

ClipShortcutText ShortcutBadgeModel::FormatClipText(
    const QString &name, int slot, const QString &clipText,
    const QKeySequence &shortcut, int previewGraphemeLimit)
{
    ClipShortcutText text;
    const QString native_shortcut = shortcut.toString(QKeySequence::NativeText);
    QString first_line;
    if (native_shortcut.isEmpty()) {
        first_line = QCoreApplication::translate(
            "ShortcutBadgeModel", "%1 · No shortcut assigned").arg(name);
        text.accessibleName = QCoreApplication::translate(
            "ShortcutBadgeModel", "%1, Clip %2, no shortcut assigned")
            .arg(name).arg(slot);
    } else {
        first_line = QCoreApplication::translate(
            "ShortcutBadgeModel", "%1 · %2").arg(name, native_shortcut);
        text.accessibleName = QCoreApplication::translate(
            "ShortcutBadgeModel", "%1, Clip %2, %3")
            .arg(name).arg(slot).arg(native_shortcut);
    }

    QString preview = ElideGraphemes(clipText, previewGraphemeLimit).toHtmlEscaped();
    preview.replace(QLatin1Char('\n'), QStringLiteral("<br/>"));
    text.tooltip = QStringLiteral("<p><b>%1</b>").arg(first_line.toHtmlEscaped());
    if (!preview.isEmpty()) text.tooltip += QStringLiteral("<br/>%1").arg(preview);
    text.tooltip += QStringLiteral("</p>");
    return text;
}
