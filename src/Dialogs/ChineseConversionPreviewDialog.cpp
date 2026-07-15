/************************************************************************
**
**  Copyright (C) 2026 Sigil Enhanced contributors
**
*************************************************************************/

#include "Dialogs/ChineseConversionPreviewDialog.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace
{

QString CompactText(QString text)
{
    text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    text.replace(QLatin1Char('\r'), QLatin1Char(' '));
    return text.simplified().left(180);
}

}

ChineseConversionPreviewDialog::ChineseConversionPreviewDialog(
    const QString& resourcePath,
    const QList<ChineseTextChange>& changes,
    int skippedJapaneseSegments,
    int skippedProtectedSegments,
    QWidget *parent)
    : QDialog(parent),
      m_SkippedJapaneseSegments(skippedJapaneseSegments),
      m_SkippedProtectedSegments(skippedProtectedSegments)
{
    setWindowTitle(tr("Chinese Conversion Preview"));
    resize(900, 560);

    auto *root = new QVBoxLayout(this);
    auto *path = new QLabel(resourcePath, this);
    path->setTextInteractionFlags(Qt::TextSelectableByMouse);
    path->setWordWrap(true);
    m_Summary = new QLabel(this);

    m_Table = new QTableWidget(changes.size(), 4, this);
    m_Table->setHorizontalHeaderLabels({tr("Apply"), tr("Location"), tr("Before"), tr("After")});
    m_Table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_Table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_Table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_Table->verticalHeader()->setVisible(false);
    m_Table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_Table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_Table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_Table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);

    for (int row = 0; row < changes.size(); ++row) {
        const ChineseTextChange& change = changes.at(row);
        auto *enabled = new QTableWidgetItem;
        enabled->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
        enabled->setCheckState(Qt::Checked);
        const QString location = change.attributeName.isEmpty()
            ? change.nodePath
            : QStringLiteral("%1/@%2").arg(change.nodePath, change.attributeName);
        m_Table->setItem(row, 0, enabled);
        m_Table->setItem(row, 1, new QTableWidgetItem(location));
        m_Table->setItem(row, 2, new QTableWidgetItem(CompactText(change.before)));
        m_Table->setItem(row, 3, new QTableWidgetItem(CompactText(change.after)));
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_Apply = buttons->addButton(tr("Apply Selected Changes"), QDialogButtonBox::AcceptRole);
    m_Apply->setDefault(true);

    root->addWidget(path);
    root->addWidget(m_Summary);
    root->addWidget(m_Table, 1);
    root->addWidget(buttons);

    connect(m_Table, &QTableWidget::itemChanged,
            this, &ChineseConversionPreviewDialog::UpdateSelectionState);
    connect(m_Apply, &QPushButton::clicked, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    UpdateSelectionState();
}

QSet<int> ChineseConversionPreviewDialog::EnabledChanges() const
{
    QSet<int> enabled;
    for (int row = 0; row < m_Table->rowCount(); ++row) {
        if (m_Table->item(row, 0)->checkState() == Qt::Checked) {
            enabled.insert(row);
        }
    }
    return enabled;
}

void ChineseConversionPreviewDialog::UpdateSelectionState(QTableWidgetItem *item)
{
    Q_UNUSED(item)
    const int enabledCount = EnabledChanges().size();
    m_Summary->setText(
        tr("%1 of %2 changes selected; %3 Japanese segments and %4 protected segments skipped.")
            .arg(enabledCount)
            .arg(m_Table->rowCount())
            .arg(m_SkippedJapaneseSegments)
            .arg(m_SkippedProtectedSegments));
    m_Apply->setEnabled(enabledCount > 0);
}
