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
#ifndef CAPTURE_NAME_TABLE_H
#define CAPTURE_NAME_TABLE_H

#ifndef PCRE2_CODE_UNIT_WIDTH
#define PCRE2_CODE_UNIT_WIDTH 16
#endif
#include <pcre2.h>

#include <QStringList>

namespace PCRE2Helpers
{

QStringList CaptureNames(const pcre2_code_16* code);

}

#endif // CAPTURE_NAME_TABLE_H
