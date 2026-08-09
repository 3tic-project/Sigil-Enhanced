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
#ifndef SECONDARY_REGEX_MATCHER_H
#define SECONDARY_REGEX_MATCHER_H

#include "BuiltinPlugins/RegexWorkbench/RegexWorkbenchTypes.h"

namespace BuiltinPlugins
{
namespace RegexWorkbench
{

class SecondaryRegexMatcher final
{
public:
    static SecondaryMatchResult Enumerate(const RegexWorkbenchRule& rule,
                                          const QString& text,
                                          RegexSearch::MatchOptions options = RegexSearch::MatchOptions());
};

}
}

#endif // SECONDARY_REGEX_MATCHER_H
