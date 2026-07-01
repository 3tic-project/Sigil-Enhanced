/************************************************************************
**
**  Copyright (C) 2026 3TIC-Project
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
#ifndef RESOURCEINSERTION_H
#define RESOURCEINSERTION_H

#include <QString>

class Resource;

namespace ResourceInsertion
{
    extern const char * const BOOK_BROWSER_RESOURCE_MIME;

    enum class Context {
        HTML,
        CSS
    };

    bool CanInsertResource(const Resource *resource, Context context);
    bool ContextFromTargetResource(const Resource *target_resource, Context &context);
    QString TextForResource(const Resource *resource, const Resource *relative_to_resource, Context context);
}

#endif // RESOURCEINSERTION_H
