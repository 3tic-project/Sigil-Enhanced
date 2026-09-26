#pragma once

#include <QString>

namespace DefaultReplaceFunctions
{

// Create the editor's original default function file without replacing edits.
bool CreateIfMissing(const QString &path);

}
