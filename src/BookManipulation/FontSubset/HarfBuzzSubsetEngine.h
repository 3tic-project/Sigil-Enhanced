#pragma once

#include <QByteArray>
#include <QSet>

#include <hb.h>

#include "BookManipulation/FontSubset/FontInspector.h"
#include "BookManipulation/FontSubset/FontSubsetTypes.h"

namespace FontSubset
{

class HarfBuzzSubsetEngine
{
public:
    Result Subset(const QByteArray& fontBytes,
                  const QSet<quint32>& codepoints,
                  const Options& options = Options(),
                  unsigned faceIndex = 0) const;

private:
    bool ValidateShaping(hb_face_t* inputFace,
                         hb_face_t* outputFace,
                         const hb_map_t* oldToNewGlyphs,
                         const QStringList& samples,
                         QString* error) const;

    FontInspector m_Inspector;
};

}
