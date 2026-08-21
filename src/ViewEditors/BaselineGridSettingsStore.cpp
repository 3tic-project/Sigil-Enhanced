/************************************************************************
**
**  Copyright (C) 2026 Sigil-Ebook Contributors
**
**  This file is part of Sigil.
**
*************************************************************************/

#include "ViewEditors/BaselineGridSettingsStore.h"

#include <QSettings>
#include <QtMath>

namespace
{

const QString SETTINGS_GROUP = QStringLiteral("preview/visual_aids");

qreal finiteValue(const QVariant &value, qreal fallback, qreal minimum, qreal maximum)
{
    bool okay = false;
    const qreal candidate = value.toDouble(&okay);
    return okay && qIsFinite(candidate) && candidate >= minimum && candidate <= maximum
        ? candidate : fallback;
}

int boundedInt(const QVariant &value, int fallback, int minimum, int maximum)
{
    bool okay = false;
    const int candidate = value.toInt(&okay);
    return okay && candidate >= minimum && candidate <= maximum ? candidate : fallback;
}

QColor validColor(const QVariant &value, const QColor &fallback)
{
    const QColor candidate(value.toString());
    return candidate.isValid() ? candidate : fallback;
}

}

BaselineGridSettings BaselineGridSettingsStore::load(QSettings &settings, bool darkTheme)
{
    const BaselineGridSettings defaults = BaselineGridSettings::defaults(darkTheme);
    BaselineGridSettings loaded = defaults;

    settings.beginGroup(SETTINGS_GROUP);
    loaded.enabled = settings.value(QStringLiteral("baseline_grid_enabled"), defaults.enabled).toBool();
    // Layout metrics are intentionally dormant while the grid-only interface is active.
    loaded.metricsEnabled = false;
    loaded.horizontalEnabled = settings.value(
        QStringLiteral("horizontal_grid_enabled"), defaults.horizontalEnabled).toBool();
    loaded.verticalEnabled = settings.value(
        QStringLiteral("vertical_grid_enabled"), defaults.verticalEnabled).toBool();

    const QString unit = settings.value(QStringLiteral("grid_unit"), QStringLiteral("px")).toString();
    loaded.unit = unit == QStringLiteral("em") ? BaselineGridUnit::Em : BaselineGridUnit::Pixels;

    loaded.step = finiteValue(settings.value(QStringLiteral("grid_step"), defaults.step),
                              defaults.step, 0.00025, 1000.0);
    loaded.verticalStep = finiteValue(
        settings.value(QStringLiteral("vertical_grid_step"), defaults.verticalStep),
        defaults.verticalStep, 0.00025, 1000.0);
    loaded.referenceFontPx = finiteValue(
        settings.value(QStringLiteral("grid_reference_font_px"), defaults.referenceFontPx),
        defaults.referenceFontPx, 0.25, 1000.0);

    const QString origin = settings.value(QStringLiteral("grid_origin"),
                                           QStringLiteral("document-top")).toString();
    loaded.origin = origin == QStringLiteral("body-content-top")
        ? BaselineGridOrigin::BodyContentTop : BaselineGridOrigin::DocumentTop;

    loaded.offsetCssPx = finiteValue(settings.value(QStringLiteral("grid_offset_px"), defaults.offsetCssPx),
                                     defaults.offsetCssPx, -9999.0, 9999.0);
    loaded.majorEvery = boundedInt(settings.value(QStringLiteral("grid_major_every"), defaults.majorEvery),
                                   defaults.majorEvery, 1, 100);
    loaded.minorColor = validColor(settings.value(QStringLiteral("grid_minor_color"),
                                                   defaults.minorColor.name()), defaults.minorColor);
    loaded.minorOpacity = finiteValue(settings.value(QStringLiteral("grid_minor_opacity"), defaults.minorOpacity),
                                      defaults.minorOpacity, 0.0, 1.0);
    loaded.majorColor = validColor(settings.value(QStringLiteral("grid_major_color"),
                                                   defaults.majorColor.name()), defaults.majorColor);
    loaded.majorOpacity = finiteValue(settings.value(QStringLiteral("grid_major_opacity"), defaults.majorOpacity),
                                      defaults.majorOpacity, 0.0, 1.0);
    loaded.colorsCustomized = settings.value(QStringLiteral("grid_colors_customized"), false).toBool();
    if (!loaded.colorsCustomized) {
        loaded.minorColor = defaults.minorColor;
        loaded.majorColor = defaults.majorColor;
    }
    loaded.minimumZoomPercent = boundedInt(
        settings.value(QStringLiteral("grid_view_threshold"), defaults.minimumZoomPercent),
        defaults.minimumZoomPercent, 10, 400);
    settings.endGroup();

    // A corrupted em combination can be individually valid but resolve outside
    // the supported paint range. Restore only the values that determine step.
    if (!loaded.isValid()) {
        loaded.unit = defaults.unit;
        loaded.step = defaults.step;
        loaded.verticalStep = defaults.verticalStep;
        loaded.referenceFontPx = defaults.referenceFontPx;
    }
    return loaded;
}

void BaselineGridSettingsStore::save(QSettings &settings, const BaselineGridSettings &gridSettings)
{
    if (!gridSettings.isValid()) {
        return;
    }

    settings.beginGroup(SETTINGS_GROUP);
    settings.setValue(QStringLiteral("baseline_grid_enabled"), gridSettings.enabled);
    settings.setValue(QStringLiteral("layout_metrics_enabled"), false);
    settings.setValue(QStringLiteral("horizontal_grid_enabled"), gridSettings.horizontalEnabled);
    settings.setValue(QStringLiteral("vertical_grid_enabled"), gridSettings.verticalEnabled);
    settings.setValue(QStringLiteral("grid_unit"), BaselineGridUnitName(gridSettings.unit));
    settings.setValue(QStringLiteral("grid_step"), gridSettings.step);
    settings.setValue(QStringLiteral("vertical_grid_step"), gridSettings.verticalStep);
    settings.setValue(QStringLiteral("grid_reference_font_px"), gridSettings.referenceFontPx);
    settings.setValue(QStringLiteral("grid_origin"), BaselineGridOriginName(gridSettings.origin));
    settings.setValue(QStringLiteral("grid_offset_px"), gridSettings.offsetCssPx);
    settings.setValue(QStringLiteral("grid_major_every"), gridSettings.majorEvery);
    settings.setValue(QStringLiteral("grid_minor_color"), gridSettings.minorColor.name(QColor::HexRgb));
    settings.setValue(QStringLiteral("grid_minor_opacity"), gridSettings.minorOpacity);
    settings.setValue(QStringLiteral("grid_major_color"), gridSettings.majorColor.name(QColor::HexRgb));
    settings.setValue(QStringLiteral("grid_major_opacity"), gridSettings.majorOpacity);
    settings.setValue(QStringLiteral("grid_colors_customized"), gridSettings.colorsCustomized);
    settings.setValue(QStringLiteral("grid_view_threshold"), gridSettings.minimumZoomPercent);
    settings.endGroup();
}
