#include <cstdlib>
#include <iostream>

#include <QApplication>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollBar>
#include <QSplitter>

#include "Agent/UI/AgentPlanComparisonDialog.h"

namespace
{

void Require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    QApplication application(argc, argv);

    QString before_text;
    QString after_text;
    for (int line = 0; line < 600; ++line) {
        before_text.append(QStringLiteral("%1 <div>source before</div>\n").arg(line, 4, 10, QLatin1Char('0')));
        after_text.append(QStringLiteral("%1 <p>source after</p>    \n").arg(line, 4, 10, QLatin1Char('0')));
    }
    SigilAgent::AgentPlanComparisonContent content;
    content.windowTitle = QStringLiteral("Plan comparison");
    content.summary = QStringLiteral("Read-only reviewed content");
    content.beforeTitle = QStringLiteral("Before");
    content.afterTitle = QStringLiteral("After");
    content.beforeAccessibleName = QStringLiteral("Before source");
    content.afterAccessibleName = QStringLiteral("After source");
    content.beforeText = before_text;
    content.afterText = after_text;
    content.truncationNotice = QStringLiteral("More content was omitted");
    content.prefixTruncated = true;

    QPointer<SigilAgent::AgentPlanComparisonDialog> dialog =
        new SigilAgent::AgentPlanComparisonDialog(content);
    dialog->show();
    application.processEvents();

    auto *summary = dialog->findChild<QLabel *>(
        QStringLiteral("agentPlanComparisonSummary"));
    auto *splitter = dialog->findChild<QSplitter *>(
        QStringLiteral("agentPlanComparisonSplitter"));
    auto *before = dialog->findChild<QPlainTextEdit *>(
        QStringLiteral("agentPlanComparisonBefore"));
    auto *after = dialog->findChild<QPlainTextEdit *>(
        QStringLiteral("agentPlanComparisonAfter"));
    auto *notice = dialog->findChild<QLabel *>(
        QStringLiteral("agentPlanComparisonTruncation"));
    Require(summary && summary->text() == content.summary && splitter
                && splitter->count() == 2,
            "comparison dialog must expose a two-pane read-only review surface");
    Require(before && after && before->isReadOnly() && after->isReadOnly()
                && before->lineWrapMode() == QPlainTextEdit::NoWrap
                && after->lineWrapMode() == QPlainTextEdit::NoWrap,
            "both comparison panes must be read-only and preserve source lines");
    Require(before->accessibleName() == content.beforeAccessibleName
                && after->accessibleName() == content.afterAccessibleName,
            "comparison panes must expose distinct accessible names");
    Require(before->toPlainText().size() == 8192
                && after->toPlainText().size() == 8192
                && before->toPlainText().endsWith(QChar(0x2026))
                && after->toPlainText().endsWith(QChar(0x2026))
                && dialog->property("comparisonCharacterLimit").toInt() == 8192
                && dialog->property("displayTruncated").toBool(),
            "comparison content must remain plain text and obey the defensive display bound");
    Require(notice && notice->text() == content.truncationNotice
                && dialog->property("prefixTruncated").toBool()
                && !dialog->property("suffixTruncated").toBool(),
            "bounded plan data must carry an explicit truncation notice");

    QScrollBar *before_scroll = before->verticalScrollBar();
    QScrollBar *after_scroll = after->verticalScrollBar();
    Require(before_scroll->maximum() > 0 && after_scroll->maximum() > 0,
            "long comparison panes must be independently scrollable");
    const int position = qMin(before_scroll->maximum(), after_scroll->maximum()) / 2;
    before_scroll->setValue(position);
    Require(after_scroll->value() == position,
            "scrolling the before pane must keep the after pane aligned");
    after_scroll->setValue(position / 2);
    Require(before_scroll->value() == position / 2,
            "scrolling the after pane must keep the before pane aligned");

    auto *close = dialog->findChild<QPushButton *>(
        QStringLiteral("agentPlanComparisonClose"));
    Require(close, "comparison dialog must have a discoverable Close action");
    close->click();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    Require(dialog.isNull(), "closing a comparison must release its bounded text widgets");
    return EXIT_SUCCESS;
}
