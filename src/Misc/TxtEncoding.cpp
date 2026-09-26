#include "Misc/TxtEncoding.h"
#include "Misc/TxtEncodingTables.h"

#include <QHash>
#include <QStringDecoder>
#include <QSysInfo>

#include <algorithm>
#include <iterator>
#include <vector>

namespace TxtEncoding {

bool DecodeGb18030(const QByteArray& bytes, QString& result)
{
    const uchar* data = reinterpret_cast<const uchar*>(bytes.constData());
    result.clear();
    result.reserve(bytes.size());
    for (qsizetype i = 0; i < bytes.size();) {
        if (data[i] < 0x80) {
            result += QChar(data[i]);
            ++i;
            continue;
        }
        if (data[i] < 0x81 || data[i] > 0xfe || i + 1 >= bytes.size()) {
            return false;
        }

        const uchar second = data[i + 1];
        if ((second >= 0x40 && second <= 0x7e)
            || (second >= 0x80 && second <= 0xfe)) {
            const int trail = second < 0x7f ? second - 0x40 : second - 0x41;
            result += QChar(TxtEncodingTables::kTwoByte[(data[i] - 0x81) * 190 + trail]);
            i += 2;
            continue;
        }
        if (second < 0x30 || second > 0x39 || i + 3 >= bytes.size()
            || data[i + 2] < 0x81 || data[i + 2] > 0xfe
            || data[i + 3] < 0x30 || data[i + 3] > 0x39) {
            return false;
        }

        const int pointer = (((data[i] - 0x81) * 10 + (second - 0x30)) * 126
                             + (data[i + 2] - 0x81)) * 10 + (data[i + 3] - 0x30);
        const auto* range = std::lower_bound(std::begin(TxtEncodingTables::kFourByteRanges),
            std::end(TxtEncodingTables::kFourByteRanges), pointer,
            [](const TxtEncodingTables::FourByteRange& item, int value) {
                return item.last < value;
            });
        if (range == std::end(TxtEncodingTables::kFourByteRanges)
            || pointer < range->first) {
            return false;
        }
        const char32_t scalar = range->first_scalar + pointer - range->first;
        result += QString::fromUcs4(&scalar, 1);
        i += 4;
    }
    return true;
}

bool EncodeGb18030(const QString& text, QByteArray& result)
{
    // The two-byte table is a bijection and the four-byte ranges hold every
    // other scalar once, so encoding is their inverse.
    static const QHash<char16_t, int> two_byte = [] {
        QHash<char16_t, int> map;
        for (int index = 0; index < int(std::size(TxtEncodingTables::kTwoByte)); ++index) {
            map.insert(TxtEncodingTables::kTwoByte[index], index);
        }
        return map;
    }();
    static const std::vector<TxtEncodingTables::FourByteRange> by_scalar = [] {
        std::vector<TxtEncodingTables::FourByteRange> ranges(std::begin(TxtEncodingTables::kFourByteRanges),
                                                             std::end(TxtEncodingTables::kFourByteRanges));
        std::sort(ranges.begin(), ranges.end(), [](const auto& left, const auto& right) {
            return left.first_scalar < right.first_scalar;
        });
        return ranges;
    }();
    result.clear();
    result.reserve(text.size() * 2);
    for (qsizetype i = 0; i < text.size(); ++i) {
        char32_t scalar = text.at(i).unicode();
        if (QChar::isHighSurrogate(scalar) && i + 1 < text.size() && text.at(i + 1).isLowSurrogate()) {
            scalar = QChar::surrogateToUcs4(text.at(i), text.at(i + 1));
            ++i;
        } else if (QChar::isSurrogate(scalar)) {
            return false;
        }
        if (scalar < 0x80) {
            result += char(scalar);
            continue;
        }
        if (scalar <= 0xffff) {
            const auto found = two_byte.constFind(char16_t(scalar));
            if (found != two_byte.cend()) {
                result += char(0x81 + found.value() / 190);
                const int trail = found.value() % 190;
                result += char(trail < 0x3f ? 0x40 + trail : 0x41 + trail);
                continue;
            }
        }
        const auto range = std::upper_bound(by_scalar.begin(), by_scalar.end(), scalar,
            [](char32_t value, const auto& item) { return value < item.first_scalar; });
        if (range == by_scalar.begin()) {
            return false;
        }
        const auto& item = *std::prev(range);
        if (scalar - item.first_scalar > char32_t(item.last - item.first)) {
            return false;
        }
        int pointer = item.first + int(scalar - item.first_scalar);
        const char last = char(0x30 + pointer % 10);
        pointer /= 10;
        const char third = char(0x81 + pointer % 126);
        pointer /= 126;
        result += char(0x81 + pointer / 10);
        result += char(0x30 + pointer % 10);
        result += third;
        result += last;
    }
    return true;
}

namespace {

QString DecodeUtf16(const QByteArray& bytes)
{
    if (bytes.size() % 2 != 0) {
        return QString();
    }
    const uchar* data = reinterpret_cast<const uchar*>(bytes.constData());
    bool little_endian = QSysInfo::ByteOrder == QSysInfo::LittleEndian;
    qsizetype offset = 0;
    if (bytes.startsWith("\xFF\xFE")) {
        little_endian = true;
        offset = 2;
    } else if (bytes.startsWith("\xFE\xFF")) {
        little_endian = false;
        offset = 2;
    }

    QString result;
    result.reserve((bytes.size() - offset) / 2);
    const auto code_unit = [&](qsizetype pos) {
        return little_endian ? (data[pos] | data[pos + 1] << 8)
                             : (data[pos] << 8 | data[pos + 1]);
    };
    for (qsizetype i = offset; i < bytes.size(); i += 2) {
        const int unit = code_unit(i);
        if (unit >= 0xd800 && unit <= 0xdbff) {
            if (i + 3 >= bytes.size()) {
                return QString();
            }
            const int low = code_unit(i + 2);
            if (low < 0xdc00 || low > 0xdfff) {
                return QString();
            }
            result += QChar(unit);
            result += QChar(low);
            i += 2;
        } else if (unit >= 0xdc00 && unit <= 0xdfff) {
            return QString();
        } else {
            result += QChar(unit);
        }
    }
    return result;
}

}

QString Decode(const QByteArray& bytes)
{
    // Python's UTF-8 decoder retains a BOM as U+FEFF. Qt discards it unless
    // ConvertInitialBom is set.
    QStringDecoder utf8(QStringDecoder::Utf8,
                        QStringConverter::Flag::Stateless | QStringConverter::Flag::ConvertInitialBom);
    const QString utf8_text = utf8(bytes);
    if (!utf8.hasError()) {
        return utf8_text;
    }

    QString gb18030_text;
    if (DecodeGb18030(bytes, gb18030_text)) {
        return gb18030_text;
    }

    return DecodeUtf16(bytes);
}

}
