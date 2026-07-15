#pragma once

#include <QByteArray>

#include "BookManipulation/FontSubset/FontSubsetTypes.h"

namespace FontSubset
{

class FontInspector
{
public:
    Inspection Inspect(const QByteArray& fontBytes, unsigned faceIndex = 0) const;

    static ContainerFormat DetectContainer(const QByteArray& fontBytes);
    static LicenseStatus ClassifyLicense(std::optional<quint16> fsType);
};

}
