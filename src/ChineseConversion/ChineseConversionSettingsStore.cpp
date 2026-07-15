/************************************************************************
**
**  Copyright (C) 2026 Sigil Enhanced contributors
**
*************************************************************************/

#include "ChineseConversion/ChineseConversionSettings.h"

#include "Misc/SettingsStore.h"

ChineseConversionOptions ChineseConversionSettings::Load()
{
    SettingsStore settings;
    return LoadFrom(settings);
}

void ChineseConversionSettings::Save(const ChineseConversionOptions& options)
{
    SettingsStore settings;
    SaveTo(settings, options);
}
