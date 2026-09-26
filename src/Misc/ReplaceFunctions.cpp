#include "Misc/ReplaceFunctions.h"

#include <algorithm>
#include <vector>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include "Misc/ReplaceFunctionsData.h"

namespace ReplaceFunctions {

namespace {

using Data = std::u32string;
namespace T = ReplaceFunctionsData;

template <std::size_t N>
bool InRanges(const T::Range (&ranges)[N], char32_t value)
{
    const auto found = std::upper_bound(std::begin(ranges), std::end(ranges), value,
                                        [](char32_t item, const T::Range &range) { return item < range.first; });
    return found != std::begin(ranges) && value <= std::prev(found)->last;
}

template <std::size_t N>
const char32_t *Mapped(const T::Mapping (&table)[N], char32_t value)
{
    const auto found = std::lower_bound(std::begin(table), std::end(table), value,
                                        [](const T::Mapping &item, char32_t key) { return item.codepoint < key; });
    return found != std::end(table) && found->codepoint == value ? found->text : nullptr;
}

template <std::size_t N>
bool Contains(const char32_t (&values)[N], char32_t value)
{
    return std::find(std::begin(values), std::end(values), value) != std::end(values);
}

Data UpperChar(char32_t value)
{
    const char32_t *text = Mapped(T::UPPER, value);
    return text ? Data(text) : Data(1, value);
}

Data LowerChar(char32_t value)
{
    const char32_t *text = Mapped(T::LOWER, value);
    return text ? Data(text) : Data(1, value);
}

bool IsSpace(char32_t value) { return InRanges(T::SPACE, value); }
bool IsWord(char32_t value) { return InRanges(T::WORD, value); }
bool IsDigit(char32_t value) { return InRanges(T::DIGIT, value); }

// Python re.IGNORECASE equivalents of one ASCII letter.
bool Folds(char32_t value, char letter)
{
    switch (letter) {
    case 'a': return Contains(T::FOLD_A, value);
    case 'b': return Contains(T::FOLD_B, value);
    case 'c': return Contains(T::FOLD_C, value);
    case 'd': return Contains(T::FOLD_D, value);
    case 'e': return Contains(T::FOLD_E, value);
    case 'f': return Contains(T::FOLD_F, value);
    case 'g': return Contains(T::FOLD_G, value);
    case 'h': return Contains(T::FOLD_H, value);
    case 'i': return Contains(T::FOLD_I, value);
    case 'j': return Contains(T::FOLD_J, value);
    case 'k': return Contains(T::FOLD_K, value);
    case 'l': return Contains(T::FOLD_L, value);
    case 'm': return Contains(T::FOLD_M, value);
    case 'n': return Contains(T::FOLD_N, value);
    case 'o': return Contains(T::FOLD_O, value);
    case 'p': return Contains(T::FOLD_P, value);
    case 'q': return Contains(T::FOLD_Q, value);
    case 'r': return Contains(T::FOLD_R, value);
    case 's': return Contains(T::FOLD_S, value);
    case 't': return Contains(T::FOLD_T, value);
    case 'u': return Contains(T::FOLD_U, value);
    case 'v': return Contains(T::FOLD_V, value);
    case 'w': return Contains(T::FOLD_W, value);
    case 'x': return Contains(T::FOLD_X, value);
    case 'y': return Contains(T::FOLD_Y, value);
    case 'z': return Contains(T::FOLD_Z, value);
    default: return false;
    }
}

bool FoldsAsciiLetter(char32_t value)
{
    for (char letter = 'a'; letter <= 'z'; ++letter)
        if (Folds(value, letter)) return true;
    return false;
}

bool IsAsciiLetter(char32_t value) { return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z'); }
bool IsAsciiUpper(char32_t value) { return value >= 'A' && value <= 'Z'; }

bool IsPunct(char32_t value)
{
    static const Data punct = U"!\"#$%&'\u2018\u2019()*+,-\u2012\u2013\u2014\u2015./:;?@[\\]_`{|}~";
    return punct.find(value) != Data::npos;
}

Data Slice(const Data &text, long long start, long long end)
{
    const long long size = static_cast<long long>(text.size());
    if (start < 0) start = std::max(0LL, start + size);
    if (end < 0) end = std::max(0LL, end + size);
    start = std::min(start, size);
    end = std::min(end, size);
    return end <= start ? Data() : text.substr(std::size_t(start), std::size_t(end - start));
}

// Python re \b under re.UNICODE.
bool Boundary(const Data &text, std::size_t position)
{
    const bool before = position > 0 && IsWord(text[position - 1]);
    const bool after = position < text.size() && IsWord(text[position]);
    return before != after;
}

// Python re "$" without MULTILINE: end of text, or before a final newline.
bool AtEnd(const Data &text, std::size_t position)
{
    return position == text.size() || (position + 1 == text.size() && text[position] == U'\n');
}

const char *const SMALL[] = {"a", "an", "and", "as", "at", "but", "by", "en", "for", "if", "in", "of",
                             "on", "or", "the", "to", "v.", "v", "via", "vs.", "vs"};

// Matches an alternative of titlecase.SMALL at position; returns its length or 0.
std::size_t SmallAt(const Data &text, std::size_t position, const char *word, bool ignoreCase)
{
    std::size_t length = 0;
    for (const char *cursor = word; *cursor; ++cursor, ++length) {
        if (position + length >= text.size()) return 0;
        const char32_t value = text[position + length];
        if (*cursor == '.') {
            if (value != U'.') return 0;
        } else if (ignoreCase ? !Folds(value, *cursor) : value != char32_t(*cursor)) {
            return 0;
        }
    }
    return length;
}

// Python's leftmost-first alternation order of SMALL, including the greedy "\.?".
template <typename Accept>
std::size_t MatchSmall(const Data &text, std::size_t position, bool ignoreCase, Accept accept)
{
    for (const char *word : SMALL) {
        const std::size_t length = SmallAt(text, position, word, ignoreCase);
        if (length && accept(position + length)) return length;
    }
    return 0;
}

bool UcInitials(const Data &word)
{
    // ^(?:[A-Z]\.|[A-Z]\.[A-Z])+$
    std::vector<bool> reachable(word.size() + 1, false);
    reachable[0] = true;
    for (std::size_t index = 0; index < word.size(); ++index) {
        if (!reachable[index]) continue;
        if (index + 1 < word.size() && IsAsciiUpper(word[index]) && word[index + 1] == U'.') {
            reachable[index + 2] = true;
            if (index + 2 < word.size() && IsAsciiUpper(word[index + 2])) reachable[index + 3] = true;
        }
    }
    return word.size() > 0 && reachable[word.size()];
}

bool AposSecond(const Data &word)
{
    // ^[dol]['‘][a-z]+$ with re.IGNORECASE
    if (word.size() < 3) return false;
    if (!Folds(word[0], 'd') && !Folds(word[0], 'o') && !Folds(word[0], 'l')) return false;
    if (word[1] != U'\'' && word[1] != U'\u2018') return false;
    for (std::size_t index = 2; index < word.size(); ++index)
        if (!FoldsAsciiLetter(word[index])) return false;
    return true;
}

bool InlinePeriod(const Data &word)
{
    for (std::size_t index = 0; index + 2 < word.size(); ++index)
        if (FoldsAsciiLetter(word[index]) && word[index + 1] == U'.' && FoldsAsciiLetter(word[index + 2])) return true;
    return false;
}

bool UcElsewhere(const Data &word)
{
    // ^[PUNCT]*?[a-zA-Z]+[A-Z]+?
    for (std::size_t start = 0; start <= word.size(); ++start) {
        for (std::size_t index = start; index < word.size() && IsAsciiLetter(word[index]); ++index)
            if (index > start && IsAsciiUpper(word[index])) return true;
        if (start == word.size() || !IsPunct(word[start])) break;
    }
    return false;
}

bool SmallWord(const Data &word)
{
    return MatchSmall(word, 0, true, [&](std::size_t end) { return end == word.size(); }) > 0;
}

Data CapFirst(const Data &item)
{
    // ^[PUNCT]*?(\w) replaced by its upper case
    for (std::size_t index = 0; index < item.size(); ++index) {
        if (IsWord(item[index])) return Upper(item.substr(0, index + 1)) + item.substr(index + 1);
        if (!IsPunct(item[index])) break;
    }
    return item;
}

Data TitleWord(Data word, bool allCaps)
{
    if (allCaps) {
        if (UcInitials(word)) return word;
        word = Lower(word);
    }
    if (AposSecond(word)) {
        word = UpperChar(word[0]) + word.substr(1);
        return Slice(word, 0, 2) + Upper(Slice(word, 2, 3)) + Slice(word, 3, word.size());
    }
    if (InlinePeriod(word) || UcElsewhere(word)) return word;
    if (SmallWord(word)) return Lower(word);
    Data result;
    std::size_t start = 0;
    while (true) {
        const std::size_t hyphen = word.find(U'-', start);
        result += CapFirst(word.substr(start, hyphen == Data::npos ? Data::npos : hyphen - start));
        if (hyphen == Data::npos) break;
        result += U'-';
        start = hyphen + 1;
    }
    return result;
}

Data SmallFirst(const Data &text)
{
    std::size_t start = 0;
    while (start < text.size() && IsPunct(text[start])) ++start;
    const std::size_t length = MatchSmall(text, start, true, [&](std::size_t end) { return Boundary(text, end); });
    if (!length) return text;
    return text.substr(0, start) + Capitalize(text.substr(start, length)) + text.substr(start + length);
}

Data SmallAfterNumber(const Data &text)
{
    Data result;
    std::size_t index = 0;
    while (index < text.size()) {
        std::size_t cursor = index;
        while (cursor < text.size() && IsDigit(text[cursor])) ++cursor;
        std::size_t length = 0;
        std::size_t spaces = cursor;
        if (cursor > index) {
            while (spaces < text.size() && IsSpace(text[spaces])) ++spaces;
            if (spaces > cursor) {
                for (const char *word : {"a", "an", "the"}) {
                    const std::size_t found = SmallAt(text, spaces, word, true);
                    if (found && Boundary(text, spaces + found)) {
                        length = found;
                        break;
                    }
                }
            }
        }
        if (!length) {
            result += text[index++];
            continue;
        }
        result += text.substr(index, spaces - index) + Capitalize(text.substr(spaces, length));
        index = spaces + length;
    }
    return result;
}

Data SmallLast(const Data &text)
{
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (!Boundary(text, index)) continue;
        std::size_t end = 0;
        MatchSmall(text, index, true, [&](std::size_t after) {
            if (after < text.size() && IsPunct(text[after]) && AtEnd(text, after + 1)) end = after + 1;
            else if (AtEnd(text, after)) end = after;
            return end != 0;
        });
        if (end) return text.substr(0, index) + Capitalize(text.substr(index, end - index)) + text.substr(end);
    }
    return text;
}

Data Subphrase(const Data &text)
{
    static const Data marks = U":.;?!";
    Data result;
    std::size_t index = 0;
    while (index < text.size()) {
        std::size_t length = 0;
        if (index + 1 < text.size() && marks.find(text[index]) != Data::npos && text[index + 1] == U' ')
            length = MatchSmall(text, index + 2, false, [](std::size_t) { return true; });
        if (!length) {
            result += text[index++];
            continue;
        }
        result += text.substr(index, 2) + Capitalize(text.substr(index + 2, length));
        index += 2 + length;
    }
    return result;
}

const char32_t *Entity(const Data &name)
{
    const auto found = std::lower_bound(std::begin(T::HTML5), std::end(T::HTML5), name,
                                        [](const T::Entity &item, const Data &key) { return Data(item.name) < key; });
    return found != std::end(T::HTML5) && Data(found->name) == name ? found->text : nullptr;
}

bool IsAsciiDigit(char32_t value) { return value >= '0' && value <= '9'; }

bool IsHexDigit(char32_t value)
{
    return IsAsciiDigit(value) || (value >= 'a' && value <= 'f') || (value >= 'A' && value <= 'F');
}

Data NumericReference(const Data &digits, int base)
{
    unsigned long long number = 0;
    for (const char32_t value : digits) {
        const unsigned digit = IsAsciiDigit(value) ? value - '0' : (value | 0x20) - 'a' + 10;
        number = std::min<unsigned long long>(number * base + digit, 0x110000ULL);
    }
    for (const T::CharRef &reference : T::INVALID_CHARREFS)
        if (reference.number == number) return Data(1, reference.codepoint);
    if ((number >= 0xD800 && number <= 0xDFFF) || number > 0x10FFFF) return Data(1, U'\uFFFD');
    if (InRanges(T::INVALID_CODEPOINTS, char32_t(number))) return Data();
    return Data(1, char32_t(number));
}

Data NamedReference(const Data &name)
{
    if (const char32_t *text = Entity(name)) return text;
    for (std::size_t length = name.size() - 1; length > 1; --length)
        if (const char32_t *text = Entity(name.substr(0, length))) return Data(text) + name.substr(length);
    return U"&" + name;
}

Data ApplyToText(Builtin builtin, const Data &text)
{
    const Data decoded = Unescape(text);
    switch (builtin) {
    case Builtin::Uppercase: case Builtin::UppercaseIgnoreTags: return Upper(decoded);
    case Builtin::Lowercase: case Builtin::LowercaseIgnoreTags: return Lower(decoded);
    case Builtin::Capitalize: case Builtin::CapitalizeIgnoreTags: return Capitalize(decoded);
    case Builtin::Titlecase: case Builtin::TitlecaseIgnoreTags: return Titlecase(decoded);
    case Builtin::Swapcase: case Builtin::SwapcaseIgnoreTags: return Swapcase(decoded);
    case Builtin::Identity: break;
    }
    return text;
}

bool IgnoresTags(Builtin builtin)
{
    return builtin == Builtin::UppercaseIgnoreTags || builtin == Builtin::LowercaseIgnoreTags
        || builtin == Builtin::CapitalizeIgnoreTags || builtin == Builtin::TitlecaseIgnoreTags
        || builtin == Builtin::SwapcaseIgnoreTags;
}

// functionrep.apply_func_to_html_text: re.split(r'(<[^>]+>)') and leave tag parts alone.
Data ApplyOutsideTags(Builtin builtin, const Data &text)
{
    Data result;
    std::size_t part = 0;
    std::size_t index = 0;
    const auto flush = [&](const Data &piece) {
        result += !piece.empty() && piece[0] == U'<' ? piece : ApplyToText(builtin, piece);
    };
    while (index < text.size()) {
        const std::size_t close = text[index] == U'<' ? text.find(U'>', index + 1) : Data::npos;
        if (close == Data::npos || close == index + 1) {
            ++index;
            continue;
        }
        flush(text.substr(part, index - part));
        result += text.substr(index, close + 1 - index);
        index = part = close + 1;
    }
    flush(text.substr(part));
    return result;
}

long long CodepointOffset(const QString &text, int position)
{
    if (position < 0) return position;
    long long low = 0;
    for (int index = 0; index <= position && index < text.size(); ++index)
        if (text.at(index).isLowSurrogate()) ++low;
    return position - low;
}

Data ToData(const QString &text)
{
    const QList<uint> values = text.toUcs4();
    return Data(values.begin(), values.end());
}

QString FromData(const Data &text)
{
    // QString::fromUcs4 would drop a leading U+FEFF as a byte order mark.
    QString result;
    result.reserve(qsizetype(text.size()));
    for (const char32_t value : text) {
        if (QChar::requiresSurrogates(value)) {
            result += QChar(QChar::highSurrogate(value));
            result += QChar(QChar::lowSurrogate(value));
        } else {
            result += QChar(char16_t(value));
        }
    }
    return result;
}

QString Template(const char *helper)
{
    return QStringLiteral("def replace(match, number, file_name, metadata, data):\n\tif match:\n\t\treturn %1(match, number, file_name, metadata, data)\n")
        .arg(QLatin1String(helper));
}

}

Data Upper(const Data &text)
{
    Data result;
    for (const char32_t value : text) result += UpperChar(value);
    return result;
}

Data Lower(const Data &text)
{
    Data result;
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] != U'\u03A3') {
            result += LowerChar(text[index]);
            continue;
        }
        // CPython handle_capital_sigma
        std::size_t before = index;
        while (before > 0 && InRanges(T::IGNORABLE, text[before - 1])) --before;
        bool final = before > 0 && InRanges(T::CASED, text[before - 1]);
        if (final && index + 1 < text.size()) {
            std::size_t after = index + 1;
            while (after < text.size() && InRanges(T::IGNORABLE, text[after])) ++after;
            final = after == text.size() || !InRanges(T::CASED, text[after]);
        }
        result += final ? U'\u03C2' : U'\u03C3';
    }
    return result;
}

Data Swapcase(const Data &text)
{
    Data result;
    for (const char32_t value : text) {
        if (InRanges(T::LOWER_SET, value)) result += UpperChar(value);
        else if (InRanges(T::UPPER_SET, value)) result += LowerChar(value);
        else result += value;
    }
    return result;
}

Data Capitalize(const Data &text)
{
    if (text.empty()) return text;
    return UpperChar(text[0]) + Lower(text.substr(1));
}

Data Titlecase(const Data &text)
{
    const bool allCaps = Upper(text) == text;
    Data line;
    std::size_t index = 0;
    while (index < text.size()) {
        std::size_t end = index;
        const bool space = IsSpace(text[index]);
        while (end < text.size() && IsSpace(text[end]) == space) ++end;
        const Data word = text.substr(index, end - index);
        line += space ? word : TitleWord(word, allCaps);
        index = end;
    }
    return Subphrase(SmallLast(SmallAfterNumber(SmallFirst(line))));
}

Data Unescape(const Data &text)
{
    if (text.find(U'&') == Data::npos) return text;
    static const Data excluded = U"\t\n\f <&#;";
    Data result;
    std::size_t index = 0;
    while (index < text.size()) {
        if (text[index] != U'&') {
            result += text[index++];
            continue;
        }
        std::size_t cursor = index + 1;
        Data replacement;
        bool matched = false;
        if (cursor < text.size() && text[cursor] == U'#') {
            const bool hex = cursor + 1 < text.size() && (text[cursor + 1] == U'x' || text[cursor + 1] == U'X');
            std::size_t digits = cursor + 1;
            while (digits < text.size() && IsAsciiDigit(text[digits])) ++digits;
            if (digits > cursor + 1) {
                replacement = NumericReference(text.substr(cursor + 1, digits - cursor - 1), 10);
                cursor = digits;
                matched = true;
            } else if (hex) {
                digits = cursor + 2;
                while (digits < text.size() && IsHexDigit(text[digits])) ++digits;
                if (digits > cursor + 2) {
                    replacement = NumericReference(text.substr(cursor + 2, digits - cursor - 2), 16);
                    cursor = digits;
                    matched = true;
                }
            }
            if (matched && cursor < text.size() && text[cursor] == U';') ++cursor;
        } else {
            std::size_t end = cursor;
            while (end < text.size() && end - cursor < 32 && excluded.find(text[end]) == Data::npos) ++end;
            if (end > cursor) {
                if (end < text.size() && text[end] == U';') ++end;
                replacement = NamedReference(text.substr(cursor, end - cursor));
                cursor = end;
                matched = true;
            }
        }
        if (!matched) {
            result += text[index++];
            continue;
        }
        result += replacement;
        index = cursor;
    }
    return result;
}

bool IsPythonSpace(char32_t value) { return IsSpace(value); }
bool IsPythonWord(char32_t value) { return IsWord(value); }
bool IsPythonDigit(char32_t value) { return IsDigit(value); }

bool Resolve(const QString &functionName, const QString &jsonPath, Builtin *builtin)
{
    static const QList<QPair<QString, Builtin>> templates {
        {Template("replace_uppercase"), Builtin::Uppercase},
        {Template("replace_lowercase"), Builtin::Lowercase},
        {Template("replace_capitalize"), Builtin::Capitalize},
        {Template("replace_titlecase"), Builtin::Titlecase},
        {Template("replace_swapcase"), Builtin::Swapcase},
        {Template("replace_uppercase_ignore_tags"), Builtin::UppercaseIgnoreTags},
        {Template("replace_lowercase_ignore_tags"), Builtin::LowercaseIgnoreTags},
        {Template("replace_capitalize_ignore_tags"), Builtin::CapitalizeIgnoreTags},
        {Template("replace_titlecase_ignore_tags"), Builtin::TitlecaseIgnoreTags},
        {Template("replace_swapcase_ignore_tags"), Builtin::SwapcaseIgnoreTags},
        {QStringLiteral("def replace(match, number, file_name, metadata, data):\n\tif match:\n\t\treturn match.group(0)"),
         Builtin::Identity},
    };
    static const QStringList defaults {
        QStringLiteral("uppercase"), QStringLiteral("lowercase"), QStringLiteral("capitalize"),
        QStringLiteral("titlecase"), QStringLiteral("swapcase"), QStringLiteral("uppercase_ignore_tags"),
        QStringLiteral("lowercase_ignore_tags"), QStringLiteral("capitalize_ignore_tags"),
        QStringLiteral("titlecase_ignore_tags"), QStringLiteral("swapcase_ignore_tags"),
    };
    QJsonDocument document;
    QFile file(jsonPath);
    if (!jsonPath.isEmpty() && file.open(QIODevice::ReadOnly)) {
        const QByteArray bytes = file.readAll();
        // Python's json.load rejects a UTF-8 BOM and then falls back to the defaults.
        if (!bytes.startsWith("\xEF\xBB\xBF")) document = QJsonDocument::fromJson(bytes);
    }
    const bool empty = document.isNull() || (document.isObject() && document.object().isEmpty())
        || (document.isArray() && document.array().isEmpty());
    if (empty) {
        const int index = defaults.indexOf(functionName);
        *builtin = index < 0 ? Builtin::Identity : templates.at(index).second;
        return true;
    }
    // A non-object list of functions never yields a callable replace() in Python.
    if (!document.isObject() || !document.object().contains(functionName)) {
        *builtin = Builtin::Identity;
        return true;
    }
    const QJsonValue code = document.object().value(functionName);
    if (!code.isString()) {
        *builtin = Builtin::Identity;
        return true;
    }
    for (const auto &item : templates) {
        if (item.first == code.toString()) {
            *builtin = item.second;
            return true;
        }
    }
    return false;
}

QString Apply(Builtin builtin, const QString &text, const QList<std::pair<int, int>> &groups)
{
    if (groups.isEmpty()) return text;
    const Data string = ToData(text);
    const long long start = CodepointOffset(text, groups.at(0).first);
    const long long end = CodepointOffset(text, groups.at(0).second);
    if (start == -1) return QString();
    if (builtin == Builtin::Identity) return FromData(Slice(string, start, end));
    if (IgnoresTags(builtin)) return FromData(ApplyOutsideTags(builtin, Slice(string, start, end)));
    if (groups.size() == 1) return FromData(ApplyToText(builtin, Slice(string, start, end)));
    // functionrep.apply_func_to_match_groups, including its Python slice rules.
    Data result;
    long long position = start;
    for (int index = 1; index < groups.size(); ++index) {
        const long long groupStart = CodepointOffset(text, groups.at(index).first);
        const long long groupEnd = CodepointOffset(text, groups.at(index).second);
        if (groupStart <= -1) continue;
        result += Slice(string, position, groupStart);
        result += ApplyToText(builtin, Slice(string, groupStart, groupEnd));
        position = groupEnd;
    }
    result += Slice(string, position, end);
    return FromData(result);
}

}
