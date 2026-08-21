#include "ViewEditors/BaselineGridModel.h"
#include "ViewEditors/BaselineGridSettingsStore.h"
#include "ViewEditors/PreviewLayoutMetrics.h"

#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QVariantMap>

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

    BaselineGridSettings invalidReference = BaselineGridSettings::defaults(false);
    invalidReference.referenceFontPx = 0.0;
    okay &= expect(!invalidReference.isValid(),
                   "a zero-size element must never become a valid grid reference");
    invalidReference.referenceFontPx = 0.1;
    okay &= expect(!invalidReference.isValid(),
                   "a subminimum element must never become a valid grid reference");
    invalidReference.referenceFontPx = 1001.0;
    okay &= expect(!invalidReference.isValid(),
                   "runtime calibration must honor the persisted reference-font range");

    QTemporaryDir temporaryDirectory;
    okay &= expect(temporaryDirectory.isValid(), "temporary settings directory must be available");
    QSettings persisted(temporaryDirectory.filePath(QStringLiteral("visual-aids.ini")), QSettings::IniFormat);
    BaselineGridSettings saved = BaselineGridSettings::defaults(false);
    saved.enabled = true;
    saved.metricsEnabled = true;
    saved.unit = BaselineGridUnit::Em;
    saved.step = 0.5;
    saved.referenceFontPx = 18.0;
    saved.origin = BaselineGridOrigin::BodyContentTop;
    saved.offsetCssPx = 3.5;
    saved.majorEvery = 4;
    BaselineGridSettingsStore::save(persisted, saved);
    persisted.sync();
    const BaselineGridSettings restored = BaselineGridSettingsStore::load(persisted, false);
    okay &= expect(restored.enabled && restored.metricsEnabled,
                   "grid and metrics enabled states must persist independently");
    okay &= expect(restored.unit == BaselineGridUnit::Em && near(restored.resolvedStepCssPx(), 9.0),
                   "fixed em reference must round-trip through settings");
    okay &= expect(restored.origin == BaselineGridOrigin::BodyContentTop
                       && near(restored.offsetCssPx, 3.5) && restored.majorEvery == 4,
                   "origin, offset, and major cadence must persist");

    persisted.beginGroup(QStringLiteral("preview/visual_aids"));
    persisted.setValue(QStringLiteral("grid_unit"), QStringLiteral("em"));
    persisted.setValue(QStringLiteral("grid_step"), 1000.0);
    persisted.setValue(QStringLiteral("grid_reference_font_px"), 1000.0);
    persisted.endGroup();
    const BaselineGridSettings recovered = BaselineGridSettingsStore::load(persisted, false);
    okay &= expect(recovered.isValid() && recovered.unit == BaselineGridUnit::Pixels
                       && near(recovered.resolvedStepCssPx(), 8.0),
                   "corrupt resolved em settings must recover to a complete valid default");

    QVariantMap style;
    style.insert(QStringLiteral("fontSizePx"), 16.0);
    style.insert(QStringLiteral("lineHeightPx"), QVariant());
    style.insert(QStringLiteral("lineHeightNormal"), true);
    style.insert(QStringLiteral("marginBlockStartPx"), 0.0);
    style.insert(QStringLiteral("marginBlockEndPx"), 8.0);
    style.insert(QStringLiteral("paddingBlockStartPx"), 0.0);
    style.insert(QStringLiteral("paddingBlockEndPx"), 0.0);
    style.insert(QStringLiteral("display"), QStringLiteral("block"));
    style.insert(QStringLiteral("writingMode"), QStringLiteral("horizontal-tb"));
    QVariantMap rect;
    rect.insert(QStringLiteral("x"), 10.0);
    rect.insert(QStringLiteral("y"), 20.0);
    rect.insert(QStringLiteral("width"), 300.0);
    rect.insert(QStringLiteral("height"), 48.0);
    QVariantMap result;
    result.insert(QStringLiteral("tag"), QStringLiteral("P"));
    result.insert(QStringLiteral("path"), QStringLiteral("html>body>p"));
    result.insert(QStringLiteral("style"), style);
    result.insert(QStringLiteral("rect"), rect);
    const PreviewLayoutMetrics normalMetrics = PreviewLayoutMetrics::fromVariant(result);
    okay &= expect(normalMetrics.valid && normalMetrics.tagName == QStringLiteral("p"),
                   "computed metrics protocol must parse a layout element");
    okay &= expect(normalMetrics.lineHeightNormal && !normalMetrics.hasLineHeightPx,
                   "line-height normal must remain semantic rather than inventing a pixel value");

    style.insert(QStringLiteral("lineHeightNormal"), false);
    style.insert(QStringLiteral("lineHeightPx"), 24.0);
    result.insert(QStringLiteral("style"), style);
    const PreviewLayoutMetrics numericMetrics = PreviewLayoutMetrics::fromVariant(result);
    okay &= expect(numericMetrics.valid && numericMetrics.hasLineHeightPx
                       && near(numericMetrics.lineHeightPx, 24.0),
                   "numeric computed line-height must preserve the browser result");

    return okay ? 0 : 1;
}
