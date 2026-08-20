/************************************************************************
**
**  Copyright (C) 2026 Sigil-Ebook Contributors
**
**  This file is part of Sigil.
**
*************************************************************************/

#pragma once
#ifndef BASELINEGRIDSETTINGSSTORE_H
#define BASELINEGRIDSETTINGSSTORE_H

#include "ViewEditors/BaselineGridModel.h"

class QSettings;

class BaselineGridSettingsStore
{
public:
    static BaselineGridSettings load(QSettings &settings, bool darkTheme);
    static void save(QSettings &settings, const BaselineGridSettings &gridSettings);
};

#endif // BASELINEGRIDSETTINGSSTORE_H
