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

#include "BuiltinPlugins/RegexWorkbench/RecursiveReplaceGuard.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QCoreApplication>

namespace BuiltinPlugins
{
namespace RegexWorkbench
{

namespace
{

qint64 SaturatingAdd(qint64 left, qint64 right)
{
    if (right > 0 && left > std::numeric_limits<qint64>::max() - right) {
        return std::numeric_limits<qint64>::max();
    }
    return left + right;
}

qint64 SaturatingScale(qint64 value, double factor)
{
    const long double scaled = static_cast<long double>(value) * factor;
    if (scaled >= static_cast<long double>(std::numeric_limits<qint64>::max())) {
        return std::numeric_limits<qint64>::max();
    }
    return static_cast<qint64>(std::ceil(scaled));
}

GuardResult Failure(GuardError error, const QString& message)
{
    GuardResult result;
    result.error = error;
    result.errorMessage = message;
    return result;
}

}

RecursiveReplaceGuard::RecursiveReplaceGuard(qint64 originalTextCodeUnits,
                                             RecursiveGuardOptions options) :
    m_options(options)
{
    if (originalTextCodeUnits < 0 || options.maxTotalReplacements <= 0 ||
        !std::isfinite(options.maxTextGrowthFactor) || options.maxTextGrowthFactor <= 0.0 ||
        options.maxAbsoluteGrowthCodeUnits < 0 || options.maxTextCodeUnits <= 0) {
        m_configurationError = QCoreApplication::translate(
            "RegexWorkbenchCore", "Invalid recursive replacement guard configuration");
        return;
    }

    m_effectiveMaxTextCodeUnits = std::min(options.maxTextCodeUnits, HardMaxTextCodeUnits);
    m_growthLimit = std::max(SaturatingScale(originalTextCodeUnits,
                                             options.maxTextGrowthFactor),
                             SaturatingAdd(originalTextCodeUnits,
                                           options.maxAbsoluteGrowthCodeUnits));
}

bool RecursiveReplaceGuard::isValid() const
{
    return m_configurationError.isEmpty();
}

qint64 RecursiveReplaceGuard::growthLimit() const
{
    return m_growthLimit;
}

qint64 RecursiveReplaceGuard::effectiveMaxTextCodeUnits() const
{
    return m_effectiveMaxTextCodeUnits;
}

GuardResult RecursiveReplaceGuard::check(qint64 newTextCodeUnits,
                                         qint64 totalReplacements) const
{
    if (!isValid() || newTextCodeUnits < 0 || totalReplacements < 0) {
        return Failure(GuardError::InvalidConfiguration,
                       m_configurationError.isEmpty()
                           ? QCoreApplication::translate(
                                 "RegexWorkbenchCore",
                                 "Invalid recursive replacement guard input")
                           : m_configurationError);
    }
    if (totalReplacements > m_options.maxTotalReplacements) {
        return Failure(GuardError::ReplacementLimit,
                       QCoreApplication::translate(
                           "RegexWorkbenchCore", "Replacement count %1 exceeds limit %2")
                           .arg(totalReplacements)
                           .arg(m_options.maxTotalReplacements));
    }
    if (newTextCodeUnits > m_effectiveMaxTextCodeUnits) {
        return Failure(GuardError::TextSizeLimit,
                       QCoreApplication::translate(
                           "RegexWorkbenchCore",
                           "Text length %1 exceeds absolute limit %2 UTF-16 units")
                           .arg(newTextCodeUnits)
                           .arg(m_effectiveMaxTextCodeUnits));
    }
    if (newTextCodeUnits > m_growthLimit) {
        return Failure(GuardError::TextGrowthLimit,
                       QCoreApplication::translate(
                           "RegexWorkbenchCore",
                           "Text length %1 exceeds original-relative growth limit %2 UTF-16 units")
                           .arg(newTextCodeUnits)
                           .arg(m_growthLimit));
    }

    GuardResult result;
    result.success = true;
    return result;
}

}
}
