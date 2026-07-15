/************************************************************************
**
**  This file is part of Sigil.
**
*************************************************************************/

#pragma once
#ifndef PLUGININPUTVALIDATOR_H
#define PLUGININPUTVALIDATOR_H

#include <QString>

namespace PluginApi
{

bool ValidateInputEpub(const QString &path, QString *error = nullptr);

}

#endif // PLUGININPUTVALIDATOR_H
