/************************************************************************
**
**  Copyright (C) 2026 Sigil Enhanced contributors
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Exporters/ExportMetadataPolicy.h"

#include <QtGlobal>

bool ExportMetadataPolicy::shouldWriteSigilVersion(bool publicationModified)
{
    if (!publicationModified) {
        return false;
    }
    if (!qEnvironmentVariableIsEmpty("SIGIL_DISABLE_VERSION_META")) {
        return false;
    }
    return !qEnvironmentVariableIsEmpty("SIGIL_ENABLE_VERSION_META");
}

bool ExportMetadataPolicy::shouldUpdateModificationDate(bool publicationModified)
{
    return publicationModified;
}
