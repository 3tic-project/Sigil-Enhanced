/************************************************************************
**
**  Copyright (C) 2026 Sigil-Ebook contributors
**
**  This file is part of Sigil-Enhanced.
**
**  Sigil-Enhanced is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  (at your option) any later version.
**
*************************************************************************/

#pragma once
#ifndef RECURSIVE_REPLACE_GUARD_H
#define RECURSIVE_REPLACE_GUARD_H

#include <QtGlobal>

#include <QString>

namespace BuiltinPlugins
{
namespace RegexWorkbench
{

struct RecursiveGuardOptions
{
    qint64 maxTotalReplacements = 100000;
    double maxTextGrowthFactor = 4.0;
    qint64 maxAbsoluteGrowthCodeUnits = 1024 * 1024;
    qint64 maxTextCodeUnits = 64 * 1024 * 1024;
};

enum class GuardError {
    None,
    InvalidConfiguration,
    ReplacementLimit,
    TextGrowthLimit,
    TextSizeLimit
};

struct GuardResult
{
    bool success = false;
    GuardError error = GuardError::None;
    QString errorMessage;
};

class RecursiveReplaceGuard final
{
public:
    static constexpr qint64 HardMaxTextCodeUnits = 64 * 1024 * 1024;

    RecursiveReplaceGuard(qint64 originalTextCodeUnits,
                          RecursiveGuardOptions options = RecursiveGuardOptions());

    bool isValid() const;
    qint64 growthLimit() const;
    qint64 effectiveMaxTextCodeUnits() const;
    GuardResult check(qint64 newTextCodeUnits, qint64 totalReplacements) const;

private:
    qint64 m_growthLimit = 0;
    qint64 m_effectiveMaxTextCodeUnits = 0;
    RecursiveGuardOptions m_options;
    QString m_configurationError;
};

}
}

#endif // RECURSIVE_REPLACE_GUARD_H
