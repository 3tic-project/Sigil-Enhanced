#pragma once

#include <QByteArray>
#include <QString>

namespace OPFSourceBytes {

// Only codecs whose strict behavior has been checked against the legacy
// implementation use this path. Other codecs retain the existing fallback.
bool CanDecodeNatively(const QByteArray &bytes);
bool CanEncodeNatively(const QByteArray &original, const QString &source);

// Throws ErrorParsingXml for an unsupported encoding, invalid byte sequence,
// conflicting BOM/declaration, or text that cannot be encoded losslessly.
QString Decode(const QByteArray &bytes);
QByteArray Encode(const QByteArray &original, const QString &source);

// codecs.lookup(name).name as CPython 3.11.12 resolves it, from the frozen
// encodings.aliases and module list. Throws ErrorParsingXml where CPython
// raises LookupError, and for non-ASCII names, which stay with the fallback.
QString CodecName(const QString &name);

}
