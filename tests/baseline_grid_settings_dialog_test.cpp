#include "Dialogs/BaselineGridSettingsDialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QtMath>

#include <cmath>
#include <iostream>

namespace
{

bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << std::endl;
    }
    return condition;
}

bool near(qreal actual, qreal expected)
{
    return std::abs(actual - expected) < 0.001;
}

}

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    BaselineGridSettings initial = BaselineGridSettings::defaults(false);
    BaselineGridSettingsDialog dialog(initial, qQNaN(), false);

    int previewCount = 0;
    BaselineGridSettings preview;
    QObject::connect(
        &dialog, &BaselineGridSettingsDialog::previewSettingsChanged,
        [&previewCount, &preview](const BaselineGridSettings &settings) {
            ++previewCount;
            preview = settings;
        });

    QCheckBox *showGrid = dialog.findChild<QCheckBox *>(QStringLiteral("showGrid"));
    QCheckBox *verticalGrid = dialog.findChild<QCheckBox *>(QStringLiteral("verticalGrid"));
    QDoubleSpinBox *horizontalSpacing =
        dialog.findChild<QDoubleSpinBox *>(QStringLiteral("horizontalSpacing"));
    QDoubleSpinBox *verticalSpacing =
        dialog.findChild<QDoubleSpinBox *>(QStringLiteral("verticalSpacing"));
    QGroupBox *geometryGroup =
        dialog.findChild<QGroupBox *>(QStringLiteral("gridGeometryGroup"));
    QGroupBox *appearanceGroup =
        dialog.findChild<QGroupBox *>(QStringLiteral("gridAppearanceGroup"));
    QHBoxLayout *settingsColumns =
        dialog.findChild<QHBoxLayout *>(QStringLiteral("gridSettingsColumns"));

    bool okay = true;
    okay &= expect(showGrid && verticalGrid && horizontalSpacing && verticalSpacing
                       && geometryGroup && appearanceGroup && settingsColumns,
                   "live-preview controls and setting columns must be discoverable");
    okay &= expect(settingsColumns && settingsColumns->count() == 2
                       && dialog.minimumWidth() > dialog.minimumHeight(),
                   "grid settings must use a two-column landscape layout");
    okay &= expect((geometryGroup->layout()->alignment() & Qt::AlignTop)
                       && (appearanceGroup->layout()->alignment() & Qt::AlignTop),
                   "setting rows must stay top-aligned when the dialog grows");
    if (!okay) {
        return 1;
    }

    showGrid->setChecked(true);
    okay &= expect(previewCount == 1 && preview.enabled,
                   "changing the grid master switch must emit a preview");

    horizontalSpacing->setValue(12.0);
    okay &= expect(previewCount == 2 && near(preview.step, 12.0),
                   "changing horizontal spacing must emit its preview value");

    verticalGrid->setChecked(true);
    verticalSpacing->setValue(18.0);
    okay &= expect(previewCount == 4 && preview.verticalEnabled
                       && near(preview.verticalStep, 18.0),
                   "vertical enablement and spacing must preview independently");

    return okay ? 0 : 1;
}
