#include "ResourceObjects/OPFSourceBytes.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QStringList>
#include <QTextCodec>

#include "Misc/TxtEncoding.h"
#include "ResourceObjects/OPFLegacyCodecTables.h"
#include "sigil_exception.h"

namespace {

struct Encoding {
    QString name;
    QByteArray bom;
};

// Byte trie and encode table frozen from one CPython codec by
// tests/generate_opf_legacy_codecs.py.
struct FrozenCodec {
    std::vector<quint32> nodes;
    std::vector<quint32> encodeCodepoints;
    std::vector<quint32> encodeBytes;
};

struct FrozenData {
    QHash<QString, QString> aliases;
    // Importable encodings modules; an empty codec name marks a module without getregentry.
    QHash<QString, QString> modules;
    QHash<QString, FrozenCodec> codecs;
};

constexpr quint32 kMissing = 0xFFFFFFFFu;
constexpr quint32 kChild = 0x80000000u;

const FrozenData &Frozen()
{
    static const FrozenData data = [] {
        FrozenData result;
        QFile file(QStringLiteral(":/opf_legacy_codecs/codecs.bin"));
        if (!file.open(QFile::ReadOnly)) return result;
        const QByteArray raw = qUncompress(file.readAll());
        const auto *bytes = reinterpret_cast<const uchar *>(raw.constData());
        qsizetype position = 4;
        bool valid = raw.startsWith("OLC1");
        const auto u32 = [&]() {
            if (position + 4 > raw.size()) {
                valid = false;
                return quint32(0);
            }
            const quint32 value = quint32(bytes[position]) | quint32(bytes[position + 1]) << 8 |
                                  quint32(bytes[position + 2]) << 16 | quint32(bytes[position + 3]) << 24;
            position += 4;
            return value;
        };
        const auto text = [&]() {
            if (position >= raw.size() || position + 1 + bytes[position] > raw.size()) {
                valid = false;
                return QString();
            }
            const int length = bytes[position];
            const QString value = QString::fromLatin1(raw.constData() + position + 1, length);
            position += 1 + length;
            return value;
        };
        for (quint32 count = valid ? u32() : 0; valid && count > 0; --count) {
            const QString alias = text();
            result.aliases.insert(alias, text());
        }
        for (quint32 count = valid ? u32() : 0; valid && count > 0; --count) {
            const QString module = text();
            result.modules.insert(module, text());
        }
        for (quint32 count = valid ? u32() : 0; valid && count > 0; --count) {
            const QString name = text();
            FrozenCodec codec;
            const quint32 nodes = u32();
            if (!valid || position + qsizetype(nodes) * 1024 > raw.size()) break;
            codec.nodes.resize(size_t(nodes) * 256);
            for (quint32 &entry : codec.nodes) entry = u32();
            const quint32 encodable = u32();
            if (!valid || position + qsizetype(encodable) * 8 > raw.size()) break;
            for (quint32 index = 0; index < encodable; ++index) {
                codec.encodeCodepoints.push_back(u32());
                codec.encodeBytes.push_back(u32());
            }
            result.codecs.insert(name, std::move(codec));
        }
        if (!valid || position != raw.size()) return FrozenData();
        return result;
    }();
    return data;
}

const FrozenCodec *FindFrozenCodec(const QString &name)
{
    const auto found = Frozen().codecs.constFind(name);
    return found == Frozen().codecs.cend() ? nullptr : &found.value();
}

QString DecodeFrozen(const QByteArray &bytes, const FrozenCodec &codec)
{
    QString result;
    result.reserve(bytes.size());
    const auto *data = reinterpret_cast<const uchar *>(bytes.constData());
    for (qsizetype i = 0; i < bytes.size();) {
        quint32 node = 0;
        while (true) {
            const quint32 entry = codec.nodes[size_t(node) * 256 + data[i++]];
            if (entry == kMissing) throw ErrorParsingXml("Invalid OPF byte sequence");
            if (entry & kChild) {
                if (i >= bytes.size()) throw ErrorParsingXml("Invalid OPF byte sequence");
                node = entry & ~kChild;
                continue;
            }
            const char32_t scalar = entry;
            result += QString::fromUcs4(&scalar, 1);
            break;
        }
    }
    return result;
}

QByteArray EncodeFrozen(const QString &source, const QString &name, const FrozenCodec &codec)
{
    QByteArray result;
    result.reserve(source.size() * 2);
    for (char32_t codepoint : source.toUcs4()) {
        const auto found = std::lower_bound(codec.encodeCodepoints.begin(), codec.encodeCodepoints.end(), codepoint);
        if (found == codec.encodeCodepoints.end() || *found != codepoint) {
            const QString error = QStringLiteral("OPF text cannot be represented in ") + name +
                QStringLiteral(". Change the XML encoding declaration to UTF-8 before saving.");
            throw ErrorParsingXml(error.toStdString());
        }
        const quint32 packed = codec.encodeBytes[size_t(found - codec.encodeCodepoints.begin())];
        const int length = int(packed & 0xFF);
        for (int index = 1; index <= length; ++index) result += char((packed >> (8 * index)) & 0xFF);
    }
    return result;
}

QString DeclaredEncoding(const QString &source)
{
    static const QRegularExpression declaration(
        QStringLiteral(R"(\A<\?xml\s+[^?]*?\bencoding\s*=\s*(["'])([^"']+)\1)"));
    const QRegularExpressionMatch match = declaration.match(source);
    return match.hasMatch() ? match.captured(2) : QString();
}

QString CanonicalName(const QString &name)
{
    return OPFSourceBytes::CodecName(name);
}

Encoding DetectEncoding(const QByteArray &bytes)
{
    if (bytes.startsWith(QByteArray::fromHex("fffe0000"))) return {QStringLiteral("utf-32-le"), QByteArray::fromHex("fffe0000")};
    if (bytes.startsWith(QByteArray::fromHex("0000feff"))) return {QStringLiteral("utf-32-be"), QByteArray::fromHex("0000feff")};
    if (bytes.startsWith(QByteArray::fromHex("efbbbf"))) return {QStringLiteral("utf-8"), QByteArray::fromHex("efbbbf")};
    if (bytes.startsWith(QByteArray::fromHex("fffe"))) return {QStringLiteral("utf-16-le"), QByteArray::fromHex("fffe")};
    if (bytes.startsWith(QByteArray::fromHex("feff"))) return {QStringLiteral("utf-16-be"), QByteArray::fromHex("feff")};
    if (bytes.startsWith(QByteArray::fromHex("0000003c"))) return {QStringLiteral("utf-32-be"), {}};
    if (bytes.startsWith(QByteArray::fromHex("3c000000"))) return {QStringLiteral("utf-32-le"), {}};
    if (bytes.startsWith(QByteArray::fromHex("003c003f"))) return {QStringLiteral("utf-16-be"), {}};
    if (bytes.startsWith(QByteArray::fromHex("3c003f00"))) return {QStringLiteral("utf-16-le"), {}};
    const QString header = QString::fromLatin1(bytes.left(1024));
    const QString declared = DeclaredEncoding(header);
    return {declared.isEmpty() ? QStringLiteral("utf-8") : CanonicalName(declared), {}};
}

bool NativeEncoding(const QString &name)
{
    return name == QLatin1String("utf-8") || name == QLatin1String("utf-8-sig") ||
           name == QLatin1String("utf-16") || name == QLatin1String("utf-16-le") ||
           name == QLatin1String("utf-16-be") || name == QLatin1String("utf-32") ||
           name == QLatin1String("utf-32-le") || name == QLatin1String("utf-32-be") ||
           name == QLatin1String("iso8859-1") || name == QLatin1String("ascii") ||
           name == QLatin1String("cp1252") || name == QLatin1String("gbk") ||
           name == QLatin1String("shift_jis") || name == QLatin1String("gb18030") ||
           FindFrozenCodec(name);
}

const OPFLegacyCodecTables::Codec *MappedCodec(const QString &name)
{
    if (name == QLatin1String("gbk")) return &OPFLegacyCodecTables::kGbk;
    if (name == QLatin1String("shift_jis")) return &OPFLegacyCodecTables::kShiftJis;
    return nullptr;
}

QString DecodeMapped(const QByteArray &bytes, const OPFLegacyCodecTables::Codec &codec)
{
    QString result;
    result.reserve(bytes.size());
    const auto *data = reinterpret_cast<const uchar *>(bytes.constData());
    for (qsizetype i = 0; i < bytes.size();) {
        const uchar byte = data[i];
        const std::uint16_t single = codec.single[byte];
        if (single != OPFLegacyCodecTables::kMissing) {
            result += QChar(ushort(single));
            ++i;
            continue;
        }
        const std::uint8_t lead = codec.lead[byte];
        if (lead == 0xFF || i + 1 >= bytes.size()) throw ErrorParsingXml("Invalid OPF byte sequence");
        const std::uint16_t point = codec.pairs[int(lead) * 256 + data[i + 1]];
        if (point == OPFLegacyCodecTables::kMissing) throw ErrorParsingXml("Invalid OPF byte sequence");
        result += QChar(ushort(point));
        i += 2;
    }
    return result;
}

QByteArray EncodeMapped(const QString &source, const QString &name, const OPFLegacyCodecTables::Codec &codec)
{
    QByteArray result;
    result.reserve(source.size() * 2);
    const auto *begin = codec.encode_cp;
    const auto *end = begin + codec.encode_count;
    for (QChar character : source) {
        const std::uint16_t codepoint = character.unicode();
        const auto *found = std::lower_bound(begin, end, codepoint);
        if (found == end || *found != codepoint) {
            const QString error = QStringLiteral("OPF text cannot be represented in ") + name +
                QStringLiteral(". Change the XML encoding declaration to UTF-8 before saving.");
            throw ErrorParsingXml(error.toStdString());
        }
        const std::uint16_t unit = codec.encode_unit[found - begin];
        if (unit <= 0xFF) {
            result += char(unit);
        } else {
            result += char(unit >> 8);
            result += char(unit & 0xFF);
        }
    }
    return result;
}

QTextCodec *LegacyCodec(const QString &name)
{
    QByteArray qtName = name.toLatin1();
    if (name == QLatin1String("iso8859-1")) qtName = "ISO-8859-1";
    else if (name == QLatin1String("cp1252")) qtName = "Windows-1252";
    else if (name == QLatin1String("cp1251")) qtName = "Windows-1251";
    else if (name == QLatin1String("ascii")) qtName = "US-ASCII";
    QTextCodec *codec = QTextCodec::codecForName(qtName);
    if (!codec) {
        const QString error = QStringLiteral("Unknown OPF encoding: ") + name;
        throw ErrorParsingXml(error.toStdString());
    }
    return codec;
}

bool ValidUtf16(const QString &source)
{
    for (qsizetype i = 0; i < source.size(); ++i) {
        const QChar c = source.at(i);
        if (c.isHighSurrogate()) {
            if (++i >= source.size() || !source.at(i).isLowSurrogate()) return false;
        } else if (c.isLowSurrogate()) {
            return false;
        }
    }
    return true;
}

QString DecodeUtf16(const QByteArray &bytes, bool littleEndian)
{
    if (bytes.size() % 2 != 0) throw ErrorParsingXml("Invalid OPF byte sequence");
    QString result;
    result.reserve(bytes.size() / 2);
    for (qsizetype i = 0; i < bytes.size(); i += 2) {
        const uchar first = uchar(bytes.at(i));
        const uchar second = uchar(bytes.at(i + 1));
        const ushort value = littleEndian ? ushort(first | (second << 8)) : ushort((first << 8) | second);
        const QChar c(value);
        if (c.isHighSurrogate()) {
            if (i + 3 >= bytes.size()) throw ErrorParsingXml("Invalid OPF byte sequence");
            const uchar third = uchar(bytes.at(i + 2));
            const uchar fourth = uchar(bytes.at(i + 3));
            const ushort next = littleEndian ? ushort(third | (fourth << 8)) : ushort((third << 8) | fourth);
            if (!QChar(next).isLowSurrogate()) throw ErrorParsingXml("Invalid OPF byte sequence");
            result += c;
            result += QChar(next);
            i += 2;
        } else if (c.isLowSurrogate()) {
            throw ErrorParsingXml("Invalid OPF byte sequence");
        } else {
            result += c;
        }
    }
    return result;
}

QString DecodeUtf32(const QByteArray &bytes, bool littleEndian)
{
    if (bytes.size() % 4 != 0) throw ErrorParsingXml("Invalid OPF byte sequence");
    QString result;
    for (qsizetype i = 0; i < bytes.size(); i += 4) {
        char32_t value = 0;
        for (int j = 0; j < 4; ++j) {
            const int shift = littleEndian ? j * 8 : (3 - j) * 8;
            value |= char32_t(uchar(bytes.at(i + j))) << shift;
        }
        if (value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
            throw ErrorParsingXml("Invalid OPF byte sequence");
        if (value <= 0xffff) {
            result += QChar(ushort(value));
        } else {
            result += QChar(QChar::highSurrogate(value));
            result += QChar(QChar::lowSurrogate(value));
        }
    }
    return result;
}

QByteArray EncodeUtf16(const QString &source, bool littleEndian)
{
    QByteArray result;
    result.reserve(source.size() * 2);
    for (QChar c : source) {
        const ushort value = c.unicode();
        result += char(littleEndian ? value & 0xff : value >> 8);
        result += char(littleEndian ? value >> 8 : value & 0xff);
    }
    return result;
}

QByteArray EncodeUtf32(const QString &source, bool littleEndian)
{
    QByteArray result;
    result.reserve(source.size() * 4);
    for (char32_t value : source.toUcs4()) {
        for (int i = 0; i < 4; ++i) {
            const int shift = littleEndian ? i * 8 : (3 - i) * 8;
            result += char((value >> shift) & 0xff);
        }
    }
    return result;
}

QString DecodeAs(const QByteArray &bytes, const QString &name)
{
    if (name == QLatin1String("cp1252")) {
        for (unsigned char byte : bytes) {
            if (byte == 0x81 || byte == 0x8d || byte == 0x8f || byte == 0x90 || byte == 0x9d)
                throw ErrorParsingXml("Invalid OPF byte sequence");
        }
    }
    if (name == QLatin1String("ascii")) {
        for (unsigned char byte : bytes)
            if (byte > 0x7f) throw ErrorParsingXml("Invalid OPF byte sequence");
        return QString::fromLatin1(bytes);
    }
    if (name == QLatin1String("utf-16-le")) return DecodeUtf16(bytes, true);
    if (name == QLatin1String("utf-16-be")) return DecodeUtf16(bytes, false);
    if (name == QLatin1String("utf-32-le")) return DecodeUtf32(bytes, true);
    if (name == QLatin1String("utf-32-be")) return DecodeUtf32(bytes, false);
    if (name == QLatin1String("utf-16") || name == QLatin1String("utf-32")) {
        const Encoding detected = DetectEncoding(bytes);
        if (detected.bom.isEmpty()) throw ErrorParsingXml("UTF OPF source has no byte order mark");
        return DecodeAs(bytes.mid(detected.bom.size()), detected.name);
    }
    if (name == QLatin1String("utf-8") || name == QLatin1String("utf-8-sig")) {
        QStringDecoder decoder(QStringConverter::Utf8,
            QStringConverter::Flag::Stateless | QStringConverter::Flag::ConvertInitialBom);
        const QString source = decoder.decode(bytes);
        if (decoder.hasError()) throw ErrorParsingXml("Invalid OPF byte sequence");
        return source;
    }
    if (const OPFLegacyCodecTables::Codec *mapped = MappedCodec(name)) return DecodeMapped(bytes, *mapped);
    if (const FrozenCodec *frozen = FindFrozenCodec(name)) return DecodeFrozen(bytes, *frozen);
    if (name == QLatin1String("gb18030")) {
        QString source;
        if (!TxtEncoding::DecodeGb18030(bytes, source)) throw ErrorParsingXml("Invalid OPF byte sequence");
        return source;
    }
    QTextCodec *codec = LegacyCodec(name);
    QTextCodec::ConverterState state;
    const QString source = codec->toUnicode(bytes.constData(), bytes.size(), &state);
    if (state.invalidChars || state.remainingChars) throw ErrorParsingXml("Invalid OPF byte sequence");
    return source;
}

QByteArray EncodeAs(const QString &source, const QString &name)
{
    if (!ValidUtf16(source)) throw ErrorParsingXml("Invalid OPF Unicode text");
    if (name == QLatin1String("ascii")) {
        for (QChar c : source)
            if (c.unicode() > 0x7f) throw ErrorParsingXml("OPF text cannot be represented in ascii. Change the XML encoding declaration to UTF-8 before saving.");
        return source.toLatin1();
    }
    if (name == QLatin1String("utf-16-le")) return EncodeUtf16(source, true);
    if (name == QLatin1String("utf-16-be")) return EncodeUtf16(source, false);
    if (name == QLatin1String("utf-32-le")) return EncodeUtf32(source, true);
    if (name == QLatin1String("utf-32-be")) return EncodeUtf32(source, false);
    if (name == QLatin1String("utf-16")) return QByteArray::fromHex("fffe") + EncodeUtf16(source, true);
    if (name == QLatin1String("utf-32")) return QByteArray::fromHex("fffe0000") + EncodeUtf32(source, true);
    if (name == QLatin1String("utf-8")) return source.toUtf8();
    if (name == QLatin1String("utf-8-sig")) return QByteArray::fromHex("efbbbf") + source.toUtf8();
    if (const OPFLegacyCodecTables::Codec *mapped = MappedCodec(name)) return EncodeMapped(source, name, *mapped);
    if (const FrozenCodec *frozen = FindFrozenCodec(name)) return EncodeFrozen(source, name, *frozen);
    if (name == QLatin1String("gb18030")) {
        QByteArray bytes;
        if (!TxtEncoding::EncodeGb18030(source, bytes)) throw ErrorParsingXml("OPF Unicode encoding failed");
        return bytes;
    }
    QTextCodec *codec = LegacyCodec(name);
    if (!codec->canEncode(source)) {
        const QString error = QStringLiteral("OPF text cannot be represented in ") + name +
            QStringLiteral(". Change the XML encoding declaration to UTF-8 before saving.");
        throw ErrorParsingXml(error.toStdString());
    }
    QTextCodec::ConverterState state;
    const QByteArray bytes = codec->fromUnicode(source.constData(), source.size(), &state);
    if (state.invalidChars || state.remainingChars) throw ErrorParsingXml("OPF Unicode encoding failed");
    return bytes;
}

}

bool OPFSourceBytes::CanDecodeNatively(const QByteArray &bytes)
{
    try {
        const Encoding encoding = DetectEncoding(bytes);
        if (!NativeEncoding(encoding.name)) return false;
        if (encoding.bom.isEmpty()) return true;
        // A BOM hides the declaration until decoding; CodecName defers non-ASCII names.
        const QString declaration = DeclaredEncoding(DecodeAs(bytes.mid(encoding.bom.size()), encoding.name));
        if (!declaration.isEmpty()) CanonicalName(declaration);
        return true;
    } catch (const ErrorParsingXml &) {
        return false;
    }
}

QString OPFSourceBytes::CodecName(const QString &name)
{
    const auto unknown = [&name]() {
        const QString error = QStringLiteral("Unknown OPF encoding: ") + name;
        return ErrorParsingXml(error.toStdString());
    };
    // codecs.lookup lowercases ASCII only; encodings.normalize_encoding then
    // joins alphanumeric runs (and dots) with single underscores.
    QString normalized;
    bool punctuation = false;
    for (QChar c : name) {
        if (c.unicode() > 0x7F) throw unknown();
        const char ascii = char(c.toLower().unicode());
        if ((ascii >= 'a' && ascii <= 'z') || (ascii >= '0' && ascii <= '9') || ascii == '.') {
            if (punctuation && !normalized.isEmpty()) normalized += '_';
            normalized += QLatin1Char(ascii);
            punctuation = false;
        } else {
            punctuation = true;
        }
    }
    const FrozenData &data = Frozen();
    QString aliased = data.aliases.value(normalized);
    if (aliased.isEmpty()) aliased = data.aliases.value(QString(normalized).replace('.', '_'));
    QStringList candidates;
    if (!aliased.isEmpty()) candidates << aliased;
    candidates << normalized;
    for (const QString &module : candidates) {
        if (module.isEmpty() || module.contains('.')) continue;
        const auto found = data.modules.constFind(module);
        if (found == data.modules.cend()) continue;
        if (found.value().isEmpty()) throw unknown();
        return found.value();
    }
    throw unknown();
}

bool OPFSourceBytes::CanEncodeNatively(const QByteArray &original, const QString &source)
{
    if (!CanDecodeNatively(original)) return false;
    const QString declaration = DeclaredEncoding(source);
    if (declaration.isEmpty()) return true;
    try {
        return NativeEncoding(CanonicalName(declaration));
    } catch (const ErrorParsingXml &) {
        return false;
    }
}

QString OPFSourceBytes::Decode(const QByteArray &bytes)
{
    const Encoding encoding = DetectEncoding(bytes);
    const QString source = DecodeAs(bytes.mid(encoding.bom.size()), encoding.name);
    if (!encoding.bom.isEmpty()) {
        const QString declaration = DeclaredEncoding(source);
        if (!declaration.isEmpty()) {
            const QString name = CanonicalName(declaration);
            QString generic = encoding.name;
            if (generic.endsWith(QLatin1String("-le")) || generic.endsWith(QLatin1String("-be"))) generic.chop(3);
            if (name != encoding.name && name != generic)
                throw ErrorParsingXml("OPF byte order mark conflicts with its encoding declaration");
        }
    }
    return source;
}

QByteArray OPFSourceBytes::Encode(const QByteArray &original, const QString &source)
{
    Encoding encoding = DetectEncoding(original);
    const QString oldDeclaration = DeclaredEncoding(Decode(original));
    const QString newDeclaration = DeclaredEncoding(source);
    const QString oldName = oldDeclaration.isEmpty() ? QString() : CanonicalName(oldDeclaration);
    const QString newName = newDeclaration.isEmpty() ? QString() : CanonicalName(newDeclaration);
    if (!newName.isEmpty() && newName != oldName) {
        encoding.name = newName;
        encoding.bom.clear();
    } else if (!oldName.isEmpty() && newName.isEmpty() && encoding.bom.isEmpty()) {
        encoding.name = QStringLiteral("utf-8");
    }
    return encoding.bom + EncodeAs(source, encoding.name);
}
