/************************************************************************
**
**  This file is part of Sigil.
**
*************************************************************************/

#include "FontPreview/FontGlyphCoverage.h"

#include <QtEndian>
#include <QSet>

namespace {

bool IsIgnoredCodepoint(uint codepoint)
{
    if (codepoint <= 0xFFFF && QChar(static_cast<char16_t>(codepoint)).isSpace()) {
        return true;
    }
    return false;
}

QString DecodeName(quint16 platform, quint16 encoding, const QByteArray &raw)
{
    if (platform == 0 || platform == 3) {
        QString value;
        value.reserve(raw.size() / 2);
        for (int i = 0; i + 1 < raw.size(); i += 2) {
            value.append(QChar(qFromBigEndian<quint16>(raw.constData() + i)));
        }
        return value;
    }
    if (platform == 1 && encoding == 0) {
        return QString::fromLatin1(raw);
    }
    return QString::fromUtf8(raw);
}

}

bool GlyphHasInk(const QRawFont &font, uint codepoint)
{
    if (!font.isValid() || !font.supportsCharacter(codepoint)) {
        return false;
    }
    const char32_t character = codepoint;
    const QList<quint32> glyphs = font.glyphIndexesForString(QString::fromUcs4(&character, 1));
    if (glyphs.isEmpty() || glyphs.first() == 0) {
        return false;
    }
    const QRectF bounds = font.boundingRect(glyphs.first());
    return bounds.width() > 0.0 && bounds.height() > 0.0;
}

QString PreferredFontFamilyName(const QRawFont &font)
{
    const QByteArray table = font.fontTable(QByteArrayLiteral("name"));
    QString fallback = font.familyName();
    if (table.size() < 6) {
        return fallback;
    }
    const quint16 count = qFromBigEndian<quint16>(table.constData() + 2);
    const quint16 storage = qFromBigEndian<quint16>(table.constData() + 4);
    QString unicodeFamily;
    QString unicodeTypographic;
    for (quint16 i = 0; i < count; ++i) {
        const int record = 6 + i * 12;
        if (record + 12 > table.size()) {
            break;
        }
        const auto *p = reinterpret_cast<const uchar *>(table.constData() + record);
        const quint16 platform = qFromBigEndian<quint16>(p);
        const quint16 encoding = qFromBigEndian<quint16>(p + 2);
        const quint16 nameId = qFromBigEndian<quint16>(p + 6);
        const quint16 length = qFromBigEndian<quint16>(p + 8);
        const quint16 offset = qFromBigEndian<quint16>(p + 10);
        if (nameId != 1 && nameId != 16) {
            continue;
        }
        if (storage + offset + length > table.size() || length < 2) {
            continue;
        }
        const QString value = DecodeName(platform, encoding,
                                          table.mid(storage + offset, length)).trimmed();
        if (value.isEmpty() || value.contains(QChar::ReplacementCharacter)) {
            continue;
        }
        if (platform == 3 || platform == 0) {
            if (nameId == 16 && unicodeTypographic.isEmpty()) unicodeTypographic = value;
            if (nameId == 1 && unicodeFamily.isEmpty()) unicodeFamily = value;
        }
    }
    if (!unicodeTypographic.isEmpty()) return unicodeTypographic;
    if (!unicodeFamily.isEmpty()) return unicodeFamily;
    return fallback;
}

GlyphCoverageResult AnalyzeGlyphCoverage(const QRawFont &font, const QString &text)
{
    GlyphCoverageResult result;
    QSet<uint> seen;
    for (int i = 0; i < text.size();) {
        uint codepoint = text.at(i).unicode();
        if (text.at(i).isHighSurrogate() && i + 1 < text.size() && text.at(i + 1).isLowSurrogate()) {
            codepoint = QChar::surrogateToUcs4(text.at(i), text.at(i + 1));
            i += 2;
        } else {
            ++i;
        }
        if (IsIgnoredCodepoint(codepoint) || seen.contains(codepoint)) {
            continue;
        }
        seen.insert(codepoint);
        ++result.total;
        if (GlyphHasInk(font, codepoint)) {
            ++result.supported;
        } else {
            result.missing.append(codepoint);
        }
    }
    return result;
}
