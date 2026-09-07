#include "Dialogs/NavigationRepairDialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

NavigationRepairDialog::NavigationRepairDialog(const NavigationRepair::Plan &plan, QWidget *parent)
    : QDialog(parent)
{
    setObjectName("navigationRepairPreview");
    setWindowTitle(tr("Generate Navigation Document"));
    auto *layout = new QVBoxLayout(this);
    QString summary = tr("Create %1. No CSS file or spine entry will be added.").arg(plan.navBookPath);
    summary += "\n" + (plan.fromNCX ? tr("Navigation will use NCX entries.") : tr("Navigation will use the current spine order."));
    summary += "\n" + (plan.opfBefore == plan.opfAfter
        ? tr("The existing manifest declaration will be used; OPF will not change.")
        : tr("One navigation item will be added to the OPF manifest."));
    auto *description = new QLabel(summary, this);
    description->setWordWrap(true);
    layout->addWidget(description);
    auto *tabs = new QTabWidget(this);
    const QList<QPair<QString, QString>> pages {
        { tr("New navigation source"), plan.navText }, { tr("OPF before"), plan.opfBefore }, { tr("OPF after"), plan.opfAfter }
    };
    for (const auto &page : pages) {
        auto *source = new QPlainTextEdit(page.second, tabs);
        source->setReadOnly(true);
        source->setLineWrapMode(QPlainTextEdit::NoWrap);
        tabs->addTab(source, page.first);
    }
    layout->addWidget(tabs);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Cancel)->setDefault(true);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    resize(820, 600);
}
