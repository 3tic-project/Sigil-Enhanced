/************************************************************************
**
**  Copyright (C) 2026 Sigil-Ebook contributors
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
#ifndef REPLACEMENT_VARIABLE_RESOLVER_H
#define REPLACEMENT_VARIABLE_RESOLVER_H

#include <functional>

#include <QString>

using ReplacementVariableResolver = std::function<bool(const QString& name, QString& value)>;

#endif // REPLACEMENT_VARIABLE_RESOLVER_H
