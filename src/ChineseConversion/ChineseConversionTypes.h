/************************************************************************
**
**  Copyright (C) 2026 Sigil Enhanced contributors
**
**  This file is part of Sigil.
**
**  Sigil is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  (at your option) any later version.
**
*************************************************************************/

#pragma once

enum class ChineseConversionMode {
    S2T,
    T2S,
    S2TW,
    TW2S,
    S2HK,
    HK2S,
    S2TWP,
    TW2SP,
    T2TW,
    TW2T,
    T2HK,
    HK2T
};

enum class ChineseConversionScope {
    CurrentSelection,
    CurrentFile,
    SelectedResources,
    AllTextResources,
    WholeBookWithMetadata
};

struct ChineseConversionOptions {
    ChineseConversionMode mode = ChineseConversionMode::S2T;
    ChineseConversionScope scope = ChineseConversionScope::CurrentFile;
    bool includeNav = true;
    bool includeNcx = true;
    bool includeMetadata = false;
    bool includeAltText = true;
    bool includeTitleAttributes = true;
    bool includeAriaLabels = true;
    bool skipCodeElements = true;
    bool skipPreElements = false;
    bool preserveJapaneseText = true;
    bool previewBeforeApply = true;
    bool updateLanguageMetadata = false;
};
