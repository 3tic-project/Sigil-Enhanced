/************************************************************************
**
**  This file is part of Sigil.
**
**  Sigil is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  any later version.
**
*************************************************************************/

#ifndef CHECKPOINTIDENTIFIER_H
#define CHECKPOINTIDENTIFIER_H

#include <QString>

namespace CheckpointIdentifier
{

struct Result {
    bool ok = false;
    bool changed = false;
    QString bookId;
    QString text;
    QString error;
};

// Return OPF text with a UUID identifier suitable for checkpoint repository
// identity. Existing UUID-bearing OPFs are returned byte-for-byte unchanged.
// Missing UUIDs are added only to the returned text; the caller decides when
// (or whether) to apply that text to the live resource.
Result ensureUuid(const QString& source);

}

#endif // CHECKPOINTIDENTIFIER_H
