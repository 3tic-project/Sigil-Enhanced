/************************************************************************
**
**  Copyright (C) 2026 Sigil Enhanced contributors
**
*************************************************************************/

#include "Dialogs/DivParagraphNormalizationPreviewDialog.h"

#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace
{

using Plan = BuiltinPlugins::DivParagraphNormalizationPlan;

QString StatusText(Plan::Status status)
{
    switch (status) {
    case Plan::Status::Apply:
        return DivParagraphNormalizationPreviewDialog::tr("Ready to apply");
    case Plan::Status::Review:
        return DivParagraphNormalizationPreviewDialog::tr("Review required");
    case Plan::Status::Skip:
        return DivParagraphNormalizationPreviewDialog::tr("Skipped");
    case Plan::Status::Error:
        return DivParagraphNormalizationPreviewDialog::tr("Parse/validation error");
    }
    return QString();
}

QString CssRiskText(const Plan::Entry& entry)
{
    if (entry.analysis.cssDependencies.isEmpty()) {
        return DivParagraphNormalizationPreviewDialog::tr("None detected");
    }
    return DivParagraphNormalizationPreviewDialog::tr("%1 selector(s)")
        .arg(entry.analysis.cssDependencies.count());
}

QString CssRiskDetails(const Plan::Entry& entry)
{
    QStringList details;
    for (const BuiltinPlugins::DivParagraphCssAnalyzer::Dependency& dependency :
         entry.analysis.cssDependencies) {
        details << QStringLiteral("%1: %2 — %3")
            .arg(dependency.sourceId, dependency.selector, dependency.reason);
    }
    return details.join(QLatin1Char('\n'));
}

}

DivParagraphNormalizationPreviewDialog::DivParagraphNormalizationPreviewDialog(
    const BuiltinPlugins::DivParagraphNormalizationPlan::Result& plan,
    QWidget* parent)
    : QDialog(parent),
      m_Plan(plan)
{
    setWindowTitle(tr("DIV Paragraph Normalization Preview"));
    resize(1180, 760);

    auto* root = new QVBoxLayout(this);
    m_Summary = new QLabel(this);
    m_Summary->setWordWrap(true);
    root->addWidget(m_Summary);

    m_Table = new QTableWidget(m_Plan.entries.count(), 7, this);
    m_Table->setAccessibleName(tr("DIV paragraph resource analysis"));
    m_Table->setHorizontalHeaderLabels({
        tr("Apply"), tr("File"), tr("Classification"), tr("Body candidates"),
        tr("Blank/separator"), tr("Protected"), tr("CSS risk")
    });
    m_Table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_Table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_Table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_Table->verticalHeader()->setVisible(false);
    m_Table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_Table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    for (int column = 2; column < 7; ++column) {
        m_Table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    }

    for (int row = 0; row < m_Plan.entries.count(); ++row) {
        const Plan::Entry& entry = m_Plan.entries.at(row);
        auto* enabled = new QTableWidgetItem;
        enabled->setData(Qt::UserRole, entry.resourceId);
        enabled->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        if (entry.status == Plan::Status::Apply) {
            enabled->setFlags(enabled->flags() | Qt::ItemIsUserCheckable);
            enabled->setCheckState(Qt::Checked);
        }
        auto* cssRisk = new QTableWidgetItem(CssRiskText(entry));
        cssRisk->setToolTip(CssRiskDetails(entry));
        auto* classification = new QTableWidgetItem(StatusText(entry.status));
        classification->setToolTip(entry.analysis.message);
        m_Table->setItem(row, 0, enabled);
        m_Table->setItem(row, 1, new QTableWidgetItem(entry.resourceId));
        m_Table->setItem(row, 2, classification);
        m_Table->setItem(row, 3, new QTableWidgetItem(
            QString::number(entry.analysis.paragraphLeaves)));
        m_Table->setItem(row, 4, new QTableWidgetItem(
            QStringLiteral("%1/%2")
                .arg(entry.analysis.spacerBrLeaves)
                .arg(entry.analysis.sceneBreaks)));
        m_Table->setItem(row, 5, new QTableWidgetItem(
            QString::number(entry.analysis.protectedRanges.count())));
        m_Table->setItem(row, 6, cssRisk);
    }
    root->addWidget(m_Table, 2);

    auto* details = new QTabWidget(this);
    auto* sourcePage = new QWidget(details);
    auto* sourceLayout = new QVBoxLayout(sourcePage);
    auto* sourceSplitter = new QSplitter(Qt::Horizontal, sourcePage);
    auto* beforeGroup = new QGroupBox(tr("Before"), sourceSplitter);
    auto* beforeLayout = new QVBoxLayout(beforeGroup);
    m_BeforeSource = new QPlainTextEdit(beforeGroup);
    m_BeforeSource->setReadOnly(true);
    m_BeforeSource->setLineWrapMode(QPlainTextEdit::NoWrap);
    beforeLayout->addWidget(m_BeforeSource);
    auto* afterGroup = new QGroupBox(tr("After"), sourceSplitter);
    auto* afterLayout = new QVBoxLayout(afterGroup);
    m_AfterSource = new QPlainTextEdit(afterGroup);
    m_AfterSource->setReadOnly(true);
    m_AfterSource->setLineWrapMode(QPlainTextEdit::NoWrap);
    afterLayout->addWidget(m_AfterSource);
    sourceSplitter->addWidget(beforeGroup);
    sourceSplitter->addWidget(afterGroup);
    sourceLayout->addWidget(sourceSplitter);
    details->addTab(sourcePage, tr("Source Diff"));

    m_BeforePreview = new QTextBrowser(details);
    m_BeforePreview->setOpenExternalLinks(false);
    details->addTab(m_BeforePreview, tr("Before Preview"));
    m_AfterPreview = new QTextBrowser(details);
    m_AfterPreview->setOpenExternalLinks(false);
    details->addTab(m_AfterPreview, tr("After Preview"));
    root->addWidget(details, 3);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_Apply = buttons->addButton(tr("Apply Selected Files"), QDialogButtonBox::AcceptRole);
    m_Apply->setDefault(true);
    connect(m_Apply, &QPushButton::clicked, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_Table, &QTableWidget::itemChanged,
            this, &DivParagraphNormalizationPreviewDialog::UpdateSelectionState);
    connect(m_Table, &QTableWidget::currentCellChanged,
            this, &DivParagraphNormalizationPreviewDialog::ShowEntry);
    root->addWidget(buttons);

    if (!m_Plan.entries.isEmpty()) {
        m_Table->setCurrentCell(0, 1);
        ShowEntry(0, 1, -1, -1);
    }
    UpdateSelectionState();
}

QSet<QString> DivParagraphNormalizationPreviewDialog::SelectedResourceIds() const
{
    QSet<QString> selected;
    for (int row = 0; row < m_Table->rowCount(); ++row) {
        QTableWidgetItem* item = m_Table->item(row, 0);
        if (item && (item->flags() & Qt::ItemIsUserCheckable) &&
            item->checkState() == Qt::Checked) {
            selected.insert(item->data(Qt::UserRole).toString());
        }
    }
    return selected;
}

void DivParagraphNormalizationPreviewDialog::UpdateSelectionState(QTableWidgetItem* item)
{
    Q_UNUSED(item)
    const int selected = SelectedResourceIds().count();
    m_Summary->setText(
        tr("Plan %1 · %2 of %3 ready files selected · %4 paragraph(s) planned · "
           "%5 protected block(s) · %6 review-only file(s) · %7 error(s)")
            .arg(m_Plan.planId.left(12))
            .arg(selected)
            .arg(m_Plan.applyFiles)
            .arg(m_Plan.conversionCount)
            .arg(m_Plan.protectedCount)
            .arg(m_Plan.reviewFiles)
            .arg(m_Plan.errorFiles));
    m_Apply->setEnabled(selected > 0 && m_Plan.ok);
}

void DivParagraphNormalizationPreviewDialog::ShowEntry(
    int currentRow, int currentColumn, int previousRow, int previousColumn)
{
    Q_UNUSED(currentColumn)
    Q_UNUSED(previousRow)
    Q_UNUSED(previousColumn)
    if (currentRow < 0 || currentRow >= m_Plan.entries.count()) {
        return;
    }
    const Plan::Entry& entry = m_Plan.entries.at(currentRow);
    const QString after = entry.output.isEmpty() ? entry.source : entry.output;
    m_BeforeSource->setPlainText(entry.source);
    m_AfterSource->setPlainText(after);
    m_BeforePreview->setHtml(entry.source);
    m_AfterPreview->setHtml(after);
}
