/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/UI/AgentPlanComparisonDialog.h"

#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QGroupBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QVBoxLayout>
#include <QVariant>

namespace SigilAgent
{

namespace
{

constexpr int MAX_COMPARISON_CHARACTERS = 8192;

QString boundedText(const QString &text, bool *truncated)
{
    *truncated = text.size() > MAX_COMPARISON_CHARACTERS;
    if (!*truncated) return text;
    return text.left(MAX_COMPARISON_CHARACTERS - 1) + QChar(0x2026);
}

void synchronizeScrollBars(QScrollBar *first, QScrollBar *second)
{
    QObject::connect(first, &QScrollBar::valueChanged, second,
                     [second](int value) {
        const QSignalBlocker blocker(second);
        second->setValue(value);
    });
    QObject::connect(second, &QScrollBar::valueChanged, first,
                     [first](int value) {
        const QSignalBlocker blocker(first);
        first->setValue(value);
    });
}

} // namespace

AgentPlanComparisonDialog::AgentPlanComparisonDialog(
    const AgentPlanComparisonContent &content, QWidget *parent) :
    QDialog(parent)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(content.windowTitle);
    setModal(false);
    resize(900, 520);

    auto *root = new QVBoxLayout(this);
    auto *summary = new QLabel(content.summary, this);
    summary->setObjectName(QStringLiteral("agentPlanComparisonSummary"));
    summary->setWordWrap(true);
    summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(summary);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setObjectName(QStringLiteral("agentPlanComparisonSplitter"));
    auto *before_group = new QGroupBox(content.beforeTitle, splitter);
    auto *before_layout = new QVBoxLayout(before_group);
    auto *before = new QPlainTextEdit(before_group);
    before->setObjectName(QStringLiteral("agentPlanComparisonBefore"));
    before->setAccessibleName(content.beforeAccessibleName);
    before->setReadOnly(true);
    before->setLineWrapMode(QPlainTextEdit::NoWrap);
    before->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    bool before_bounded = false;
    before->setPlainText(boundedText(content.beforeText, &before_bounded));
    before_layout->addWidget(before);

    auto *after_group = new QGroupBox(content.afterTitle, splitter);
    auto *after_layout = new QVBoxLayout(after_group);
    auto *after = new QPlainTextEdit(after_group);
    after->setObjectName(QStringLiteral("agentPlanComparisonAfter"));
    after->setAccessibleName(content.afterAccessibleName);
    after->setReadOnly(true);
    after->setLineWrapMode(QPlainTextEdit::NoWrap);
    after->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    bool after_bounded = false;
    after->setPlainText(boundedText(content.afterText, &after_bounded));
    after_layout->addWidget(after);

    splitter->addWidget(before_group);
    splitter->addWidget(after_group);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    root->addWidget(splitter, 1);

    synchronizeScrollBars(before->verticalScrollBar(), after->verticalScrollBar());
    synchronizeScrollBars(before->horizontalScrollBar(), after->horizontalScrollBar());

    const bool truncated = content.prefixTruncated || content.suffixTruncated
        || before_bounded || after_bounded;
    setProperty("prefixTruncated", content.prefixTruncated);
    setProperty("suffixTruncated", content.suffixTruncated);
    setProperty("displayTruncated", truncated);
    setProperty("comparisonCharacterLimit", MAX_COMPARISON_CHARACTERS);
    if (truncated) {
        auto *notice = new QLabel(content.truncationNotice, this);
        notice->setObjectName(QStringLiteral("agentPlanComparisonTruncation"));
        notice->setWordWrap(true);
        notice->setAccessibleName(notice->text());
        root->addWidget(notice);
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    if (QPushButton *close = buttons->button(QDialogButtonBox::Close)) {
        close->setObjectName(QStringLiteral("agentPlanComparisonClose"));
    }
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
}

} // namespace SigilAgent
