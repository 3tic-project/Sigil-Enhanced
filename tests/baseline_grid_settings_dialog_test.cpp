#include "Dialogs/BaselineGridSettingsDialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QDoubleSpinBox>
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

    bool okay = true;
    okay &= expect(showGrid && verticalGrid && horizontalSpacing && verticalSpacing,
                   "live-preview controls must be discoverable");
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
