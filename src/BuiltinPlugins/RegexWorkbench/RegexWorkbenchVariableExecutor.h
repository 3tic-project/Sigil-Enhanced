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
#ifndef REGEX_WORKBENCH_VARIABLE_EXECUTOR_H
#define REGEX_WORKBENCH_VARIABLE_EXECUTOR_H

#include "BuiltinPlugins/RegexWorkbench/RegexWorkbenchEngine.h"
#include "BuiltinPlugins/RegexWorkbench/SearchVariableStore.h"

namespace BuiltinPlugins
{
namespace RegexWorkbench
{

class RegexWorkbenchVariableExecutor final
{
public:
    static RegexWorkbenchEngineResult ApplyRule(
        const RegexWorkbenchRule& rule,
        const QString& text,
        SearchVariableStore& store,
        RegexWorkbenchEngineOptions options = RegexWorkbenchEngineOptions());
};

}
}

#endif // REGEX_WORKBENCH_VARIABLE_EXECUTOR_H
