#include "ViewEditors/BaselineGridModel.h"

#include <QCoreApplication>

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
    QCoreApplication application(argc, argv);
    bool okay = true;

    BaselineGridSettings settings;
    settings.enabled = true;
    settings.step = 8.0;
    settings.majorEvery = 5;
    settings.minimumZoomPercent = 60;

    QVector<BaselineGridLine> lines = BaselineGridModel::linesForViewport(
        50.0, 13.0, 1.0, 0.0, settings);
    okay &= expect(lines.size() == 6, "scroll-anchored grid should expose six lines");
    okay &= expect(near(lines.at(0).position, 3.0), "scroll phase must be -(scroll mod step)");
    okay &= expect(near(lines.at(1).position, 11.0), "minor grid spacing must remain eight pixels");
    okay &= expect(lines.at(3).major, "major classification must stay anchored to document indices");

    lines = BaselineGridModel::linesForViewport(50.0, 13.0, 2.0, 0.0, settings);
    okay &= expect(near(lines.at(0).position, 6.0), "zoom must scale grid phase with rendered content");
    okay &= expect(near(lines.at(1).position - lines.at(0).position, 16.0),
                   "zoom must scale CSS-pixel grid spacing exactly once");

    lines = BaselineGridModel::linesForViewport(100.0, 0.0, 0.5, 0.0, settings);
    okay &= expect(lines.size() == 6, "below threshold only major grid lines should remain");
    for (const BaselineGridLine &line : lines) {
        okay &= expect(line.major, "minor lines must be hidden below the zoom threshold");
    }

    settings.unit = BaselineGridUnit::Em;
    settings.step = 0.5;
    settings.referenceFontPx = 18.0;
    okay &= expect(near(settings.resolvedStepCssPx(), 9.0),
                   "em step must resolve from the fixed reference font");
    const qreal stableStep = settings.resolvedStepCssPx();
    const qreal unrelatedCurrentElementFont = 32.0;
    Q_UNUSED(unrelatedCurrentElementFont);
    okay &= expect(near(settings.resolvedStepCssPx(), stableStep),
                   "resolved em step must not follow current element font size");

    settings.offsetCssPx = -5.0;
    lines = BaselineGridModel::linesForViewport(30.0, 0.0, 1.0, 0.0, settings);
    okay &= expect(!lines.isEmpty() && near(lines.first().position, 4.0),
                   "negative offset must use a stable positive grid phase");

    settings.step = 0.01;
    okay &= expect(!settings.isValid(), "resolved steps below 0.25px must be rejected");
    okay &= expect(BaselineGridModel::linesForViewport(100.0, 0.0, 1.0, 0.0, settings).isEmpty(),
                   "invalid settings must never reach painting");

    return okay ? 0 : 1;
}
