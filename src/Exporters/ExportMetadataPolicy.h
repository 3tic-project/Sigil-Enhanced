/************************************************************************
**
**  Copyright (C) 2026 Sigil Enhanced contributors
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once

class ExportMetadataPolicy
{
public:
    static bool shouldWriteSigilVersion(bool publicationModified);
    static bool shouldUpdateModificationDate(bool publicationModified);
};
