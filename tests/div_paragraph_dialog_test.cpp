#include <QApplication>
#include <QCheckBox>
#include <QElapsedTimer>
#include <QPushButton>
#include <QRadioButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTextStream>

#include "Dialogs/DivParagraphNormalizationDialog.h"
#include "Dialogs/DivParagraphNormalizationPreviewDialog.h"

using BuiltinPlugins::BookLiveParagraphNormalizer;
using BuiltinPlugins::DivParagraphNormalizationPlan;

namespace
{

int fail(const QString& message)
{
    QTextStream(stderr) << message << '\n';
    return 1;
}

DivParagraphNormalizationPlan::Result samplePlan()
{
    DivParagraphNormalizationPlan::Result plan;
    plan.ok = true;
    plan.planId = QStringLiteral("0123456789abcdef");
    plan.applyFiles = 1;
    plan.reviewFiles = 1;
    plan.conversionCount = 12;
    plan.protectedCount = 2;

    DivParagraphNormalizationPlan::Entry ready;
    ready.resourceId = QStringLiteral("Text/ready.xhtml");
    ready.source = QStringLiteral("<html><body><div>before</div></body></html>");
    ready.output = QStringLiteral("<html><body><p>before</p></body></html>");
    ready.status = DivParagraphNormalizationPlan::Status::Apply;
    ready.analysis.pageKind = BookLiveParagraphNormalizer::PageKind::NormalBodyFlow;
    ready.analysis.paragraphLeaves = 12;
    ready.analysis.protectedRanges.resize(2);
    ready.analysis.message = QStringLiteral("ready");
    plan.entries << ready;

    DivParagraphNormalizationPlan::Entry review;
    review.resourceId = QStringLiteral("Text/review.xhtml");
    review.source = QStringLiteral("<html><body><div>review</div></body></html>");
    review.status = DivParagraphNormalizationPlan::Status::Review;
    review.analysis.pageKind = BookLiveParagraphNormalizer::PageKind::CssRisk;
    review.analysis.paragraphLeaves = 4;
    review.analysis.message = QStringLiteral("review");
    plan.entries << review;
    return plan;
}

}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    BookLiveParagraphNormalizer::Options saved;
    saved.convertSceneBreaks = true;
    DivParagraphNormalizationDialog options(
        DivParagraphNormalizationDialog::Scope::CurrentFile,
        false, 2, saved, true);
    auto* current = options.findChild<QRadioButton*>(
        QStringLiteral("divParagraphCurrentFile"));
    auto* selected = options.findChild<QRadioButton*>(
        QStringLiteral("divParagraphSelectedFiles"));
    auto* scene = options.findChild<QCheckBox*>(
        QStringLiteral("divParagraphSceneBreaks"));
    auto* format = options.findChild<QCheckBox*>(
        QStringLiteral("divParagraphFormatSource"));
    if (!current || current->isEnabled() || !selected || !selected->isChecked() ||
        !scene || !scene->isChecked() || !format || !format->isChecked() ||
        options.SelectedScope() != DivParagraphNormalizationDialog::Scope::SelectedFiles) {
        return fail(QStringLiteral("scope fallback or persisted option state is incorrect"));
    }

    const DivParagraphNormalizationPlan::Result plan = samplePlan();
    DivParagraphNormalizationPreviewDialog preview(plan);
    auto* table = preview.findChild<QTableWidget*>(
        QStringLiteral("divParagraphResourceTable"));
    auto* tabs = preview.findChild<QTabWidget*>(
        QStringLiteral("divParagraphPreviewTabs"));
    auto* apply = preview.findChild<QPushButton*>(
        QStringLiteral("divParagraphApply"));
    if (!table || table->rowCount() != 2 || table->columnCount() != 8 ||
        !(table->item(0, 0)->flags() & Qt::ItemIsUserCheckable) ||
        table->item(0, 0)->checkState() != Qt::Checked ||
        (table->item(1, 0)->flags() & Qt::ItemIsUserCheckable) ||
        table->item(0, 2)->text() != QStringLiteral("Body flow") ||
        table->item(1, 2)->text() != QStringLiteral("CSS risk") ||
        table->item(0, 7)->text() != QStringLiteral("Ready to apply") ||
        table->item(1, 7)->text() != QStringLiteral("Review required") ||
        !tabs || tabs->count() != 3 || !apply || !apply->isEnabled() ||
        preview.SelectedResourceIds() != QSet<QString>{QStringLiteral("Text/ready.xhtml")}) {
        return fail(QStringLiteral("preview table status or tabs are incorrect"));
    }
    if (argc > 1) {
        preview.show();
        QApplication::processEvents();
        if (!preview.grab().save(QString::fromLocal8Bit(argv[1]))) {
            return fail(QStringLiteral("could not save the dialog inspection image"));
        }
        preview.hide();
    }
    table->item(0, 0)->setCheckState(Qt::Unchecked);
    QApplication::processEvents();
    if (apply->isEnabled() || !preview.SelectedResourceIds().isEmpty()) {
        return fail(QStringLiteral("Apply remained enabled with no selected files"));
    }

    DivParagraphNormalizationPlan::Result heavy_plan = plan;
    QString nested;
    nested += QStringLiteral("<html><body>");
    for (int i = 0; i < 180; ++i) {
        nested += QStringLiteral(
            "<div class='css_class_18'>テキスト<ruby>漢<rt>かん</rt></ruby>段落</div>");
    }
    nested += QStringLiteral("</body></html>");
    heavy_plan.entries[0].source = nested;
    heavy_plan.entries[0].output = nested;
    QElapsedTimer timer;
    timer.start();
    DivParagraphNormalizationPreviewDialog heavy(heavy_plan);
    QApplication::processEvents();
    if (timer.elapsed() > 2000) {
        return fail(QStringLiteral("Cmoa preview dialog froze on nested DIV/Ruby XHTML"));
    }
    auto* before_preview = heavy.findChild<QTextBrowser*>(
        QStringLiteral("divParagraphBeforePreview"));
    if (!before_preview || before_preview->toHtml().contains(QStringLiteral("<ruby"))) {
        return fail(QStringLiteral("preview still fed raw EPUB XHTML to Qt rich text"));
    }

    DivParagraphNormalizationPreviewDialog analysis_only(plan, true);
    auto* analysis_table = analysis_only.findChild<QTableWidget*>(
        QStringLiteral("divParagraphResourceTable"));
    if (!analysis_table ||
        (analysis_table->item(0, 0)->flags() & Qt::ItemIsUserCheckable) ||
        analysis_only.findChild<QPushButton*>(QStringLiteral("divParagraphApply"))) {
        return fail(QStringLiteral("analysis-only preview exposed apply controls"));
    }
    return 0;
}
