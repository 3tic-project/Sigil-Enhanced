#pragma once

#include <QByteArray>

class QImage;

namespace StaticGif {

// Encodes one GIF89a frame. Images with at most 256 opaque colours keep their
// exact colours; larger ones use an undithered median-cut palette, as Pillow
// did. Fully transparent pixels share one transparent index. Returns an empty
// array for a null image.
QByteArray Encode(const QImage &image);

}
