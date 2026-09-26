#pragma once

#include <QByteArray>
#include <QString>

namespace TxtEncoding {

// Mirrors importtxt.read_unicode: try each strict decoder in order and return
// an empty string if none accepts the input. Line endings and normalization
// are handled by Utility::ReadUnicodeTextFile_M after decoding.
QString Decode(const QByteArray& bytes);

// Strict CPython 3.11 gb18030 codec, from the same frozen tables. Both return
// false where CPython raises.
bool DecodeGb18030(const QByteArray& bytes, QString& result);
bool EncodeGb18030(const QString& text, QByteArray& result);

}
