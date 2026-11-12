#include "BookManipulation/FontSubset/FontFaceAssigner.h"

#include <algorithm>
#include <tuple>

#include <QHash>
#include <QObject>
#include <QPair>
#include <QRegularExpression>
#include <QSet>
#include <QVector>
#include <QXmlStreamReader>

#include "BookManipulation/FontSubset/HarfBuzzRAII.h"
#include "Misc/XMLEntities.h"

namespace FontSubset
{
namespace
{

constexpr quint32 kDuokanProbe = 0x2E3A;
constexpr quint32 kDuokanDe = 0x7684;

struct Element {
    QString tag;
    QHash<QString, QString> attributes;
    QString text;
    int parent = -1;
    int previousSibling = -1;
    QVector<int> children;
};

enum class Combinator { None, Descendant, Child, Adjacent, General };

struct SimpleSelector {
    QString tag;
    QString id;
    QStringList classes;
    struct Attribute {
        QString name;
        QString op;
        QString value;
    };
    QVector<Attribute> attributes;
    int ids = 0;
    int classesAndAttributes = 0;
    int elements = 0;
    int wildcards = 0;
};

struct ComplexSelector {
    QVector<SimpleSelector> compounds;
    QVector<Combinator> combinators;
};

struct Rule {
    ComplexSelector selector;
    QStringList families;
    bool inherit = false;
    bool hasFont = false;
    int important = 0;
    int inlineRank = 0;
    int order = 0;
    int matchElement = -1;
    QString listType;
    bool hasList = false;
    int listImportant = 0;
};

struct FaceOverride {
    bool hasSet = false;
    QString setRule;
    QString addRule;
    QString delRule;
};

struct RawFace {
    QString family;
    QStringList urls;
    QString setRule;
    QString addRule;
    QString delRule;
};

struct RawRule {
    QString selector;
    bool hasFont = false;
    QString fontValue;
    bool fontImportant = false;
    bool hasList = false;
    QString listValue;
    bool listImportant = false;
};

struct ParsedSheet {
    QStringList importUrls;
    QVector<RawFace> faces;
    QVector<RawRule> rules;
};

struct CssPiece {
    bool inlineText = false;
    QString value;
};

QString AppendScalar(QString text, quint32 codepoint)
{
    const char32_t scalar = char32_t(codepoint);
    text.append(QString::fromUcs4(&scalar, 1));
    return text;
}

class SigilEntityResolver : public QXmlStreamEntityResolver
{
public:
    QString resolveUndeclaredEntity(const QString& name) override
    {
        const ushort codepoint = XMLEntities::instance().GetEntityCode(
            QLatin1Char('&') + name + QLatin1Char(';'));
        return codepoint == 0 ? QString() : QString(QChar(codepoint));
    }
};

QString StripCssComments(const QString& css)
{
    QString out;
    out.reserve(css.size());
    QChar quote;
    for (int i = 0; i < css.size(); ++i) {
        const QChar ch = css.at(i);
        if (!quote.isNull()) {
            out.append(ch);
            if (ch == QLatin1Char('\\') && i + 1 < css.size()) {
                out.append(css.at(++i));
            } else if (ch == quote) {
                quote = QChar();
            }
            continue;
        }
        if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
            quote = ch;
            out.append(ch);
            continue;
        }
        if (ch == QLatin1Char('/') && i + 1 < css.size() &&
            css.at(i + 1) == QLatin1Char('*')) {
            i += 2;
            while (i + 1 < css.size() &&
                   !(css.at(i) == QLatin1Char('*') && css.at(i + 1) == QLatin1Char('/'))) {
                ++i;
            }
            if (i + 1 < css.size()) {
                ++i;
            }
            out.append(QLatin1Char(' '));
            continue;
        }
        out.append(ch);
    }
    return out;
}

QString ResolveRelative(const QString& baseFile, const QString& rawUrl)
{
    QString url = QString::fromUtf8(QByteArray::fromPercentEncoding(rawUrl.trimmed().toUtf8()));
    if (url.size() >= 2 &&
        ((url.startsWith(QLatin1Char('"')) && url.endsWith(QLatin1Char('"'))) ||
         (url.startsWith(QLatin1Char('\'')) && url.endsWith(QLatin1Char('\''))))) {
        url = url.mid(1, url.size() - 2).trimmed();
    }
    const int hash = url.indexOf(QLatin1Char('#'));
    if (hash >= 0) {
        url = url.left(hash);
    }
    const int query = url.indexOf(QLatin1Char('?'));
    if (query >= 0) {
        url = url.left(query);
    }
    if (url.isEmpty() || url.contains(QLatin1String("://")) ||
        url.startsWith(QLatin1String("data:"), Qt::CaseInsensitive)) {
        return QString();
    }
    url.replace(QLatin1Char('\\'), QLatin1Char('/'));

    QStringList parts;
    if (!url.startsWith(QLatin1Char('/'))) {
        parts = baseFile.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (!parts.isEmpty()) {
            parts.removeLast();
        }
    }
    for (const QString& part : url.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        if (part == QLatin1String(".")) {
            continue;
        }
        if (part == QLatin1String("..")) {
            if (!parts.isEmpty()) {
                parts.removeLast();
            }
            continue;
        }
        parts.append(part);
    }
    return parts.join(QLatin1Char('/'));
}

int SkipSpace(const QString& text, int index)
{
    while (index < text.size() && text.at(index).isSpace()) {
        ++index;
    }
    return index;
}

int FindBalancedEnd(const QString& text, int open, QChar openChar, QChar closeChar)
{
    int depth = 0;
    QChar quote;
    for (int i = open; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (!quote.isNull()) {
            if (ch == QLatin1Char('\\') && i + 1 < text.size()) {
                ++i;
                continue;
            }
            if (ch == quote) {
                quote = QChar();
            }
            continue;
        }
        if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
            quote = ch;
            continue;
        }
        if (ch == openChar) {
            ++depth;
        } else if (ch == closeChar) {
            --depth;
            if (depth == 0) {
                return i;
            }
        }
    }
    return text.size() - 1;
}

struct Declaration {
    QString name;
    QString value;
    bool important = false;
};

QVector<Declaration> ParseDeclarations(const QString& block)
{
    QVector<Declaration> declarations;
    QString current;
    QChar quote;
    int paren = 0;
    auto flush = [&declarations](QString piece) {
        piece = piece.trimmed();
        if (piece.isEmpty()) {
            return;
        }
        const int colon = piece.indexOf(QLatin1Char(':'));
        if (colon <= 0) {
            return;
        }
        Declaration declaration;
        declaration.name = piece.left(colon).trimmed().toLower();
        declaration.value = piece.mid(colon + 1).trimmed();
        const int important = declaration.value.indexOf(QLatin1String("!important"), 0, Qt::CaseInsensitive);
        if (important >= 0) {
            declaration.important = true;
            declaration.value = declaration.value.left(important).trimmed();
        }
        if (!declaration.name.isEmpty()) {
            declarations.append(declaration);
        }
    };
    for (int i = 0; i < block.size(); ++i) {
        const QChar ch = block.at(i);
        if (!quote.isNull()) {
            current.append(ch);
            if (ch == QLatin1Char('\\') && i + 1 < block.size()) {
                current.append(block.at(++i));
            } else if (ch == quote) {
                quote = QChar();
            }
            continue;
        }
        if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
            quote = ch;
            current.append(ch);
            continue;
        }
        if (ch == QLatin1Char('(')) {
            ++paren;
        } else if (ch == QLatin1Char(')') && paren > 0) {
            --paren;
        }
        if (ch == QLatin1Char(';') && paren == 0) {
            flush(current);
            current.clear();
            continue;
        }
        current.append(ch);
    }
    flush(current);
    return declarations;
}

QString LastDeclaration(const QVector<Declaration>& declarations,
                        const QStringList& names, bool* important)
{
    QString normal;
    QString forced;
    bool forcedImportant = false;
    for (const Declaration& declaration : declarations) {
        if (!names.contains(declaration.name)) {
            continue;
        }
        if (declaration.important) {
            forced = declaration.value;
            forcedImportant = true;
        } else if (!forcedImportant) {
            normal = declaration.value;
        }
    }
    if (important) {
        *important = forcedImportant;
    }
    return forcedImportant ? forced : normal;
}

QStringList CssUrls(const QString& value)
{
    QStringList urls;
    int position = 0;
    while (position < value.size()) {
        const int found = value.indexOf(QLatin1String("url("), position, Qt::CaseInsensitive);
        if (found < 0) {
            break;
        }
        const int end = value.indexOf(QLatin1Char(')'), found + 4);
        if (end < 0) {
            break;
        }
        urls.append(value.mid(found + 4, end - (found + 4)).trimmed());
        position = end + 1;
    }
    return urls;
}

QStringList SplitSelectors(const QString& selector)
{
    QStringList parts;
    QString current;
    QChar quote;
    int bracket = 0;
    int paren = 0;
    for (int i = 0; i < selector.size(); ++i) {
        const QChar ch = selector.at(i);
        if (!quote.isNull()) {
            current.append(ch);
            if (ch == QLatin1Char('\\') && i + 1 < selector.size()) {
                current.append(selector.at(++i));
            } else if (ch == quote) {
                quote = QChar();
            }
            continue;
        }
        if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
            quote = ch;
            current.append(ch);
            continue;
        }
        if (ch == QLatin1Char('[')) {
            ++bracket;
        } else if (ch == QLatin1Char(']') && bracket > 0) {
            --bracket;
        } else if (ch == QLatin1Char('(')) {
            ++paren;
        } else if (ch == QLatin1Char(')') && paren > 0) {
            --paren;
        }
        if (ch == QLatin1Char(',') && bracket == 0 && paren == 0) {
            parts.append(current.trimmed());
            current.clear();
            continue;
        }
        current.append(ch);
    }
    if (!current.trimmed().isEmpty()) {
        parts.append(current.trimmed());
    }
    return parts;
}

bool HasPseudo(const QString& selector)
{
    QChar quote;
    int bracket = 0;
    for (int i = 0; i < selector.size(); ++i) {
        const QChar ch = selector.at(i);
        if (!quote.isNull()) {
            if (ch == QLatin1Char('\\') && i + 1 < selector.size()) {
                ++i;
                continue;
            }
            if (ch == quote) {
                quote = QChar();
            }
            continue;
        }
        if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
            quote = ch;
            continue;
        }
        if (ch == QLatin1Char('[')) {
            ++bracket;
        } else if (ch == QLatin1Char(']') && bracket > 0) {
            --bracket;
        } else if (ch == QLatin1Char(':') && bracket == 0) {
            return true;
        }
    }
    return false;
}

QString CleanFamilyToken(QString token)
{
    token = token.trimmed();
    if (token.size() >= 2 &&
        ((token.startsWith(QLatin1Char('"')) && token.endsWith(QLatin1Char('"'))) ||
         (token.startsWith(QLatin1Char('\'')) && token.endsWith(QLatin1Char('\''))))) {
        token = token.mid(1, token.size() - 2).trimmed();
    }
    return token.toLower();
}

QStringList SplitFamilies(const QString& value)
{
    QStringList families;
    QString current;
    QChar quote;
    for (int i = 0; i < value.size(); ++i) {
        const QChar ch = value.at(i);
        if (!quote.isNull()) {
            if (ch == quote) {
                quote = QChar();
            } else {
                current.append(ch);
            }
            continue;
        }
        if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
            quote = ch;
            continue;
        }
        if (ch == QLatin1Char(',')) {
            const QString token = CleanFamilyToken(current);
            if (!token.isEmpty()) {
                families.append(token);
            }
            current.clear();
            continue;
        }
        current.append(ch);
    }
    const QString token = CleanFamilyToken(current);
    if (!token.isEmpty()) {
        families.append(token);
    }
    return families;
}

QSet<quint32> UnicodeRule(const QString& rule)
{
    QSet<quint32> codepoints;
    if (rule.trimmed().isEmpty()) {
        return codepoints;
    }
    const QStringList segments = rule.split(QLatin1Char(','));
    for (QString segment : segments) {
        segment = segment.trimmed();
        if (segment.isEmpty()) {
            continue;
        }
        if (segment.startsWith(QLatin1String("U+"), Qt::CaseInsensitive)) {
            const QString body = segment.mid(2);
            const int dash = body.indexOf(QLatin1Char('-'));
            bool firstOk = false;
            bool secondOk = false;
            const quint32 first = body.left(dash < 0 ? body.size() : dash).toUInt(&firstOk, 16);
            if (!firstOk || first > 0x10FFFF) {
                continue;
            }
            if (dash < 0) {
                codepoints.insert(first);
                continue;
            }
            const quint32 second = body.mid(dash + 1).toUInt(&secondOk, 16);
            if (!secondOk || second < first || second > 0x10FFFF) {
                continue;
            }
            for (quint32 codepoint = first; codepoint <= second; ++codepoint) {
                codepoints.insert(codepoint);
            }
            continue;
        }
        if (segment.size() == 3 && segment.at(1) == QLatin1Char('-') &&
            segment.at(0) < segment.at(2)) {
            for (ushort codepoint = segment.at(0).unicode();
                 codepoint <= segment.at(2).unicode(); ++codepoint) {
                codepoints.insert(codepoint);
            }
            continue;
        }
        for (uint codepoint : segment.toUcs4()) {
            codepoints.insert(codepoint);
        }
    }
    return codepoints;
}

bool IsListStyle(const QString& token)
{
    static const QStringList kStyles = {
        QStringLiteral("none"), QStringLiteral("disc"), QStringLiteral("circle"),
        QStringLiteral("square"), QStringLiteral("decimal"),
        QStringLiteral("lower-roman"), QStringLiteral("upper-roman"),
        QStringLiteral("lower-alpha"), QStringLiteral("upper-alpha"),
        QStringLiteral("lower-latin"), QStringLiteral("upper-latin"),
        QStringLiteral("lower-greek"), QStringLiteral("cjk-ideographic"),
        QStringLiteral("decimal-leading-zero")
    };
    return kStyles.contains(token);
}

QString ListStyleValue(const QString& value)
{
    const QStringList tokens = value.split(QRegularExpression(QStringLiteral("\\s+")),
                                           Qt::SkipEmptyParts);
    QString found;
    for (const QString& token : tokens) {
        if (IsListStyle(token.toLower())) {
            found = token.toLower();
        }
    }
    return found;
}

QString ReadIdent(const QString& text, int* index)
{
    const int start = *index;
    while (*index < text.size()) {
        const QChar ch = text.at(*index);
        if (ch.isLetterOrNumber() || ch == QLatin1Char('-') || ch == QLatin1Char('_') ||
            ch.unicode() > 127) {
            ++(*index);
            continue;
        }
        break;
    }
    return text.mid(start, *index - start);
}

bool ParseCompound(const QString& text, int* index, SimpleSelector* selector)
{
    *index = SkipSpace(text, *index);
    if (*index >= text.size()) {
        return false;
    }
    const QChar first = text.at(*index);
    if (first == QLatin1Char('*')) {
        selector->tag = QStringLiteral("*");
        selector->wildcards = 1;
        ++(*index);
    } else if (first.isLetter() || first == QLatin1Char('_') || first.unicode() > 127) {
        selector->tag = ReadIdent(text, index).toLower();
        selector->elements = 1;
    }
    bool saw = !selector->tag.isEmpty();
    while (*index < text.size()) {
        const QChar ch = text.at(*index);
        if (ch == QLatin1Char('.')) {
            ++(*index);
            const QString name = ReadIdent(text, index);
            if (name.isEmpty()) {
                return false;
            }
            selector->classes.append(name);
            ++selector->classesAndAttributes;
            saw = true;
            continue;
        }
        if (ch == QLatin1Char('#')) {
            ++(*index);
            selector->id = ReadIdent(text, index);
            if (selector->id.isEmpty()) {
                return false;
            }
            ++selector->ids;
            saw = true;
            continue;
        }
        if (ch == QLatin1Char('[')) {
            const int end = FindBalancedEnd(text, *index, QLatin1Char('['), QLatin1Char(']'));
            const QString body = text.mid(*index + 1, end - *index - 1).trimmed();
            *index = end + 1 < text.size() ? end + 1 : int(text.size());
            SimpleSelector::Attribute attribute;
            static const QStringList kOperators = {
                QStringLiteral("~="), QStringLiteral("|="), QStringLiteral("^="),
                QStringLiteral("$="), QStringLiteral("*="), QStringLiteral("=")
            };
            int opAt = -1;
            QString op;
            for (const QString& candidate : kOperators) {
                const int found = body.indexOf(candidate);
                if (found > 0 && (opAt < 0 || found < opAt)) {
                    opAt = found;
                    op = candidate;
                }
            }
            if (opAt < 0) {
                attribute.name = body.trimmed().toLower();
            } else {
                attribute.name = body.left(opAt).trimmed().toLower();
                attribute.op = op;
                attribute.value = CleanFamilyToken(body.mid(opAt + op.size()));
            }
            if (attribute.name.isEmpty()) {
                return false;
            }
            selector->attributes.append(attribute);
            ++selector->classesAndAttributes;
            saw = true;
            continue;
        }
        break;
    }
    return saw;
}

bool ParseComplex(const QString& text, ComplexSelector* selector)
{
    int index = 0;
    SimpleSelector compound;
    if (!ParseCompound(text, &index, &compound)) {
        return false;
    }
    selector->compounds.append(compound);
    while (true) {
        const int beforeSpace = index;
        index = SkipSpace(text, index);
        if (index >= text.size()) {
            break;
        }
        Combinator combinator = Combinator::Descendant;
        if (text.at(index) == QLatin1Char('>')) {
            combinator = Combinator::Child;
            ++index;
            index = SkipSpace(text, index);
        } else if (text.at(index) == QLatin1Char('+')) {
            combinator = Combinator::Adjacent;
            ++index;
            index = SkipSpace(text, index);
        } else if (text.at(index) == QLatin1Char('~')) {
            combinator = Combinator::General;
            ++index;
            index = SkipSpace(text, index);
        } else if (index == beforeSpace) {
            return false;
        }
        if (index >= text.size()) {
            return false;
        }
        SimpleSelector next;
        if (!ParseCompound(text, &index, &next)) {
            return false;
        }
        selector->combinators.append(combinator);
        selector->compounds.append(next);
    }
    return index >= text.size();
}

void ParseSheet(const QString& css, ParsedSheet* sheet)
{
    const QString text = StripCssComments(css);
    int index = 0;
    while (index < text.size()) {
        index = SkipSpace(text, index);
        if (index >= text.size()) {
            break;
        }
        if (text.at(index) == QLatin1Char('@')) {
            const int at = index;
            if (text.mid(index, 7).compare(QLatin1String("@import"), Qt::CaseInsensitive) == 0) {
                const int end = text.indexOf(QLatin1Char(';'), index);
                const QString prelude = text.mid(index, (end < 0 ? text.size() : end) - index);
                const QStringList urls = CssUrls(prelude);
                if (!urls.isEmpty()) {
                    sheet->importUrls.append(urls.constFirst());
                } else {
                    const int quote = prelude.indexOf(QLatin1Char('"'));
                    const int apos = prelude.indexOf(QLatin1Char('\''));
                    int start = quote;
                    QChar mark = QLatin1Char('"');
                    if (apos >= 0 && (start < 0 || apos < start)) {
                        start = apos;
                        mark = QLatin1Char('\'');
                    }
                    if (start >= 0) {
                        const int stop = prelude.indexOf(mark, start + 1);
                        if (stop > start) {
                            sheet->importUrls.append(prelude.mid(start + 1, stop - start - 1));
                        }
                    }
                }
                index = end < 0 ? text.size() : end + 1;
                continue;
            }
            const int brace = text.indexOf(QLatin1Char('{'), index);
            const int semi = text.indexOf(QLatin1Char(';'), index);
            if (brace < 0 || (semi >= 0 && semi < brace)) {
                index = semi < 0 ? text.size() : semi + 1;
                continue;
            }
            const QString prelude = text.mid(at, brace - at).trimmed();
            const int blockEnd = FindBalancedEnd(text, brace, QLatin1Char('{'), QLatin1Char('}'));
            const QString block = text.mid(brace + 1, blockEnd - brace - 1);
            index = blockEnd + 1 < text.size() ? blockEnd + 1 : int(text.size());
            if (prelude.compare(QLatin1String("@font-face"), Qt::CaseInsensitive) == 0) {
                const QVector<Declaration> declarations = ParseDeclarations(block);
                RawFace face;
                face.family = CleanFamilyToken(LastDeclaration(declarations, {QStringLiteral("font-family")}, nullptr));
                face.urls = CssUrls(LastDeclaration(declarations, {QStringLiteral("src")}, nullptr));
                face.setRule = LastDeclaration(declarations, {QStringLiteral("unicode-set")}, nullptr);
                face.addRule = LastDeclaration(declarations, {QStringLiteral("unicode-add")}, nullptr);
                face.delRule = LastDeclaration(declarations, {QStringLiteral("unicode-del")}, nullptr);
                if (!face.family.isEmpty()) {
                    sheet->faces.append(face);
                }
                continue;
            }
            ParseSheet(block, sheet);
            continue;
        }

        const int brace = text.indexOf(QLatin1Char('{'), index);
        if (brace < 0) {
            break;
        }
        const QString selector = text.mid(index, brace - index).trimmed();
        const int blockEnd = FindBalancedEnd(text, brace, QLatin1Char('{'), QLatin1Char('}'));
        const QString block = text.mid(brace + 1, blockEnd - brace - 1);
        index = blockEnd + 1 < text.size() ? blockEnd + 1 : int(text.size());
        const QVector<Declaration> declarations = ParseDeclarations(block);
        RawRule rule;
        rule.selector = selector;
        bool important = false;
        rule.fontValue = LastDeclaration(declarations, {QStringLiteral("font-family")}, &important);
        rule.hasFont = !rule.fontValue.isEmpty();
        rule.fontImportant = important;
        important = false;
        rule.listValue = ListStyleValue(LastDeclaration(
            declarations, {QStringLiteral("list-style"), QStringLiteral("list-style-type")},
            &important));
        rule.hasList = !rule.listValue.isEmpty();
        rule.listImportant = important;
        if (rule.hasFont || rule.hasList) {
            sheet->rules.append(rule);
        }
    }
}

QString Roman(int number, bool upper)
{
    if (number <= 0 || number > 5000) {
        return QString();
    }
    const QList<QPair<int, QString>> table = {
        {1000, QStringLiteral("M")}, {900, QStringLiteral("CM")},
        {500, QStringLiteral("D")}, {400, QStringLiteral("CD")},
        {100, QStringLiteral("C")}, {90, QStringLiteral("XC")},
        {50, QStringLiteral("L")}, {40, QStringLiteral("XL")},
        {10, QStringLiteral("X")}, {9, QStringLiteral("IX")},
        {5, QStringLiteral("V")}, {4, QStringLiteral("IV")},
        {1, QStringLiteral("I")}
    };
    QString roman;
    for (const auto& item : table) {
        while (number >= item.first) {
            roman.append(item.second);
            number -= item.first;
        }
    }
    return upper ? roman : roman.toLower();
}

QString CjkNumber(int number)
{
    if (number <= 0 || number > 100000) {
        return QString();
    }
    const QString digits = QStringLiteral("零一二三四五六七八九");
    const QStringList units = {
        QString(), QString(), QStringLiteral("十"), QStringLiteral("百"),
        QStringLiteral("千"), QStringLiteral("万")
    };
    const QString arab = QString::number(number);
    QString result;
    int length = arab.size();
    int last = -1;
    for (QChar ch : arab) {
        const int digit = ch.digitValue();
        if (digit > 0) {
            result.append(digits.at(digit));
            if (length < units.size()) {
                result.append(units.at(length));
            }
        } else if (last != 0) {
            result.append(digits.at(0));
        }
        --length;
        last = digit;
    }
    if (result.startsWith(QStringLiteral("一十"))) {
        result.remove(0, 1);
    }
    if (result.size() > 1 && result.endsWith(QStringLiteral("零"))) {
        result.chop(1);
    }
    return result;
}

QString ListMarker(int order, const QString& type)
{
    if (order <= 0 || type.isEmpty() || type == QLatin1String("none") ||
        type == QLatin1String("disc") || type == QLatin1String("circle") ||
        type == QLatin1String("square")) {
        return QString();
    }
    if (type == QLatin1String("decimal")) {
        return QString::number(order);
    }
    if (type == QLatin1String("decimal-leading-zero")) {
        return order < 10 ? QStringLiteral("0") + QString::number(order)
                          : QString::number(order);
    }
    if (type == QLatin1String("upper-alpha") || type == QLatin1String("upper-latin")) {
        return order > 26 ? QString() : QString(QChar(u'A' + order - 1));
    }
    if (type == QLatin1String("lower-alpha") || type == QLatin1String("lower-latin")) {
        return order > 26 ? QString() : QString(QChar(u'a' + order - 1));
    }
    if (type == QLatin1String("lower-greek")) {
        return order > 24 ? QString() : QString(QChar(0x03B0 + order));
    }
    if (type == QLatin1String("lower-roman") || type == QLatin1String("upper-roman")) {
        return Roman(order, type.startsWith(QLatin1String("upper")));
    }
    if (type == QLatin1String("cjk-ideographic")) {
        return CjkNumber(order);
    }
    return QString();
}

using Specificity = std::tuple<int, int, int, int, int, int, int>;

Specificity RuleKey(const Rule& rule, const ComplexSelector& selector, bool listRule)
{
    int ids = 0;
    int classes = 0;
    int elements = 0;
    int wildcards = 0;
    for (const SimpleSelector& compound : selector.compounds) {
        ids += compound.ids;
        classes += compound.classesAndAttributes;
        elements += compound.elements;
        wildcards += compound.wildcards;
    }
    return Specificity(listRule ? rule.listImportant : rule.important,
                       rule.inlineRank, ids, classes, elements, wildcards, rule.order);
}

bool AttributeMatches(const Element& element, const SimpleSelector::Attribute& attribute)
{
    const auto found = element.attributes.constFind(attribute.name);
    if (found == element.attributes.cend()) {
        return false;
    }
    const QString& value = found.value();
    if (attribute.op.isEmpty()) {
        return true;
    }
    if (attribute.op == QLatin1String("=")) {
        return value == attribute.value;
    }
    if (attribute.op == QLatin1String("*=")) {
        return value.contains(attribute.value);
    }
    if (attribute.op == QLatin1String("^=")) {
        return value.startsWith(attribute.value);
    }
    if (attribute.op == QLatin1String("$=")) {
        return value.endsWith(attribute.value);
    }
    if (attribute.op == QLatin1String("|=")) {
        return value == attribute.value ||
               value.startsWith(attribute.value + QLatin1Char('-'));
    }
    if (attribute.op == QLatin1String("~=")) {
        return value.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts)
            .contains(attribute.value);
    }
    return false;
}

bool CompoundMatches(const SimpleSelector& selector, const Element& element)
{
    if (!selector.tag.isEmpty() && selector.tag != QLatin1String("*") &&
        selector.tag != element.tag) {
        return false;
    }
    if (!selector.id.isEmpty() && element.attributes.value(QStringLiteral("id")) != selector.id) {
        return false;
    }
    if (!selector.classes.isEmpty()) {
        const QStringList classes = element.attributes.value(QStringLiteral("class"))
                                        .split(QRegularExpression(QStringLiteral("\\s+")),
                                               Qt::SkipEmptyParts);
        for (const QString& name : selector.classes) {
            if (!classes.contains(name)) {
                return false;
            }
        }
    }
    for (const SimpleSelector::Attribute& attribute : selector.attributes) {
        if (!AttributeMatches(element, attribute)) {
            return false;
        }
    }
    return true;
}

bool SelectorMatches(const ComplexSelector& selector, int compoundIndex, int elementIndex,
                     const QVector<Element>& elements)
{
    if (elementIndex < 0 || compoundIndex < 0) {
        return false;
    }
    if (!CompoundMatches(selector.compounds.at(compoundIndex), elements.at(elementIndex))) {
        return false;
    }
    if (compoundIndex == 0) {
        return true;
    }
    const Combinator combinator = selector.combinators.at(compoundIndex - 1);
    const Element& element = elements.at(elementIndex);
    if (combinator == Combinator::Child) {
        return SelectorMatches(selector, compoundIndex - 1, element.parent, elements);
    }
    if (combinator == Combinator::Descendant) {
        for (int parent = element.parent; parent >= 0; parent = elements.at(parent).parent) {
            if (SelectorMatches(selector, compoundIndex - 1, parent, elements)) {
                return true;
            }
        }
        return false;
    }
    if (combinator == Combinator::Adjacent) {
        return SelectorMatches(selector, compoundIndex - 1, element.previousSibling, elements);
    }
    for (int sibling = element.previousSibling; sibling >= 0;
         sibling = elements.at(sibling).previousSibling) {
        if (SelectorMatches(selector, compoundIndex - 1, sibling, elements)) {
            return true;
        }
    }
    return false;
}

bool RuleMatches(const Rule& rule, int elementIndex, const QVector<Element>& elements)
{
    if (rule.matchElement >= 0) {
        return rule.matchElement == elementIndex;
    }
    if (rule.selector.compounds.isEmpty()) {
        return false;
    }
    return SelectorMatches(rule.selector, rule.selector.compounds.size() - 1,
                           elementIndex, elements);
}

struct AssignmentState {
    QHash<QString, QSet<quint32>> coverage;
    QHash<QString, FontTextAssignment> assigned;
    QHash<QString, FaceOverride> overrides;
    QStringList warnings;
};

void AddSample(FontTextAssignment* assignment, const QString& sample)
{
    const QString trimmed = sample.left(256);
    if (trimmed.isEmpty() || assignment->shapingSamples.size() >= 64 ||
        assignment->shapingSamples.contains(trimmed)) {
        return;
    }
    assignment->shapingSamples.append(trimmed);
}

void AssignText(const QString& text, const QStringList& families, AssignmentState* state)
{
    if (text.isEmpty() || families.isEmpty()) {
        return;
    }
    QHash<QString, QString> kept;
    for (uint codepoint : text.toUcs4()) {
        if (codepoint < 0x20 || codepoint == ' ' || codepoint == '\t' ||
            codepoint == '\n' || codepoint == '\r') {
            continue;
        }
        bool placed = false;
        for (const QString& identifier : families) {
            if (state->coverage.value(identifier).contains(codepoint)) {
                state->assigned[identifier].codepoints.insert(codepoint);
                kept[identifier] = AppendScalar(kept.value(identifier), codepoint);
                placed = true;
                break;
            }
        }
        if (!placed) {
            const QString& last = families.last();
            state->assigned[last].codepoints.insert(codepoint);
            state->assigned[last].uncoveredCodepoints.insert(codepoint);
        }
    }
    for (auto it = kept.cbegin(); it != kept.cend(); ++it) {
        AddSample(&state->assigned[it.key()], it.value());
    }
}

QStringList ResolveFamilies(const QString& value, const QHash<QString, QString>& familyToId,
                            bool* inherit)
{
    *inherit = false;
    QStringList identifiers;
    for (const QString& family : SplitFamilies(value)) {
        if (family == QLatin1String("inherit")) {
            *inherit = true;
            return {};
        }
        const auto found = familyToId.constFind(family);
        if (found != familyToId.cend()) {
            identifiers.append(found.value());
        }
    }
    return identifiers;
}

void RememberOverride(const RawFace& face, const QString& identifier, AssignmentState* state)
{
    if (state->overrides.contains(identifier)) {
        return;
    }
    if (face.setRule.isEmpty() && face.addRule.isEmpty() && face.delRule.isEmpty()) {
        return;
    }
    FaceOverride override;
    override.hasSet = !face.setRule.isEmpty();
    override.setRule = face.setRule;
    override.addRule = face.addRule;
    override.delRule = face.delRule;
    state->overrides.insert(identifier, override);
}

QString LookupFont(const QHash<QString, QString>& pathToId, const QString& path)
{
    return pathToId.value(path.toLower());
}

void ConsumeSheet(const ParsedSheet& sheet, const QString& sheetPath,
                  const QHash<QString, QString>& pathToId,
                  const QHash<QString, const UsageSource*>& sources,
                  QSet<QString>* importStack,
                  QHash<QString, QString>* familyToId,
                  QVector<RawRule>* rules,
                  AssignmentState* state)
{
    const QString key = sheetPath.toLower();
    if (importStack->contains(key)) {
        return;
    }
    importStack->insert(key);
    for (const QString& importUrl : sheet.importUrls) {
        const QString path = ResolveRelative(sheetPath, importUrl);
        const UsageSource* source = sources.value(path.toLower(), nullptr);
        if (!source) {
            continue;
        }
        ParsedSheet imported;
        ParseSheet(source->content, &imported);
        ConsumeSheet(imported, source->path, pathToId, sources, importStack,
                     familyToId, rules, state);
    }
    for (const RawFace& face : sheet.faces) {
        QString identifier;
        for (const QString& url : face.urls) {
            identifier = LookupFont(pathToId, ResolveRelative(sheetPath, url));
            if (!identifier.isEmpty()) {
                break;
            }
        }
        if (identifier.isEmpty()) {
            continue;
        }
        if (!familyToId->contains(face.family)) {
            familyToId->insert(face.family, identifier);
        }
        RememberOverride(face, identifier, state);
    }
    rules->append(sheet.rules);
    importStack->remove(key);
}

Rule MakeRule(const RawRule& raw, const QHash<QString, QString>& familyToId, int order)
{
    Rule rule;
    rule.order = order;
    rule.hasFont = raw.hasFont;
    rule.important = raw.fontImportant ? 1 : 0;
    rule.hasList = raw.hasList;
    rule.listImportant = raw.listImportant ? 1 : 0;
    rule.listType = raw.listValue;
    if (raw.hasFont) {
        rule.families = ResolveFamilies(raw.fontValue, familyToId, &rule.inherit);
    }
    if (!HasPseudo(raw.selector)) {
        ParseComplex(raw.selector, &rule.selector);
    }
    return rule;
}

bool InHead(const QVector<Element>& elements, const QVector<int>& stack)
{
    for (int index : stack) {
        if (elements.at(index).tag == QLatin1String("head")) {
            return true;
        }
    }
    return false;
}

void ParseDocument(const UsageSource& source, QVector<Element>* elements,
                   QVector<CssPiece>* pieces, QString* error)
{
    QXmlStreamReader reader(source.content);
    SigilEntityResolver resolver;
    reader.setEntityResolver(&resolver);
    QVector<int> stack;
    while (!reader.atEnd()) {
        const QXmlStreamReader::TokenType token = reader.readNext();
        if (token == QXmlStreamReader::StartElement) {
            const QString tag = reader.name().toString().toLower();
            if (tag == QLatin1String("script")) {
                reader.skipCurrentElement();
                continue;
            }
            if (tag == QLatin1String("style")) {
                const QString css = reader.readElementText(QXmlStreamReader::IncludeChildElements);
                if (InHead(*elements, stack)) {
                    pieces->append({true, css});
                }
                continue;
            }
            Element element;
            element.tag = tag;
            element.parent = stack.isEmpty() ? -1 : stack.last();
            for (const QXmlStreamAttribute& attribute : reader.attributes()) {
                element.attributes.insert(attribute.name().toString().toLower(),
                                          attribute.value().toString());
            }
            if (element.parent >= 0 && !elements->at(element.parent).children.isEmpty()) {
                element.previousSibling = elements->at(element.parent).children.last();
            } else if (element.parent < 0) {
                for (int i = elements->size() - 1; i >= 0; --i) {
                    if (elements->at(i).parent < 0) {
                        element.previousSibling = i;
                        break;
                    }
                }
            }
            const int index = elements->size();
            elements->append(element);
            if (element.parent >= 0) {
                (*elements)[element.parent].children.append(index);
            }
            if (tag == QLatin1String("link") && InHead(*elements, stack)) {
                const QString rel = element.attributes.value(QStringLiteral("rel"));
                if (rel.contains(QLatin1String("stylesheet"), Qt::CaseInsensitive)) {
                    const QString href = element.attributes.value(QStringLiteral("href"));
                    if (!href.isEmpty()) {
                        pieces->append({false, ResolveRelative(source.path, href)});
                    }
                }
            }
            stack.append(index);
            continue;
        }
        if (token == QXmlStreamReader::EndElement) {
            if (!stack.isEmpty()) {
                stack.removeLast();
            }
            continue;
        }
        if (token == QXmlStreamReader::Characters && !stack.isEmpty()) {
            (*elements)[stack.last()].text.append(reader.text());
        }
    }
    if (reader.hasError()) {
        *error = QObject::tr("%1: XML parsing stopped at line %2: %3")
                     .arg(source.path)
                     .arg(reader.lineNumber())
                     .arg(reader.errorString());
    }
}

void Walk(int index, const QVector<Element>& elements, const QVector<Rule>& fontRules,
          const QVector<Rule>& listRules, const QStringList& inheritedFamilies,
          const QString& inheritedList, int listOrder, AssignmentState* state)
{
    const Element& element = elements.at(index);
    QStringList families = inheritedFamilies;
    for (const Rule& rule : fontRules) {
        if (!rule.hasFont || !RuleMatches(rule, index, elements)) {
            continue;
        }
        families = rule.inherit ? inheritedFamilies : rule.families;
        break;
    }
    QString listType = inheritedList;
    if (element.tag == QLatin1String("ul") || element.tag == QLatin1String("ol") ||
        element.tag == QLatin1String("li")) {
        bool matched = false;
        for (const Rule& rule : listRules) {
            if (!rule.hasList || !RuleMatches(rule, index, elements)) {
                continue;
            }
            listType = rule.listType;
            matched = true;
            break;
        }
        if (!matched && element.tag == QLatin1String("ol")) {
            listType = QStringLiteral("decimal");
        } else if (!matched && element.tag == QLatin1String("ul")) {
            listType = QStringLiteral("disc");
        }
    }
    AssignText(element.text, families, state);
    if (element.tag == QLatin1String("li")) {
        AssignText(ListMarker(listOrder, listType), families, state);
    }

    int order = 0;
    for (int child : element.children) {
        int childOrder = 0;
        if (elements.at(child).tag == QLatin1String("li")) {
            bool ok = false;
            const int value = elements.at(child).attributes.value(QStringLiteral("value")).toInt(&ok);
            order = ok ? value : order + 1;
            childOrder = order;
        }
        Walk(child, elements, fontRules, listRules, families, listType, childOrder, state);
    }
}

QString FilterSample(const QString& sample, const QSet<quint32>& keep)
{
    QString filtered;
    for (uint codepoint : sample.toUcs4()) {
        if (keep.contains(codepoint)) {
            filtered = AppendScalar(filtered, codepoint);
        }
    }
    return filtered.left(256);
}

void FinishAssignment(const QList<FontSnapshot>& fonts, AssignmentState* state,
                      FontFaceUsage* usage)
{
    for (const FontSnapshot& font : fonts) {
        FontTextAssignment& assignment = state->assigned[font.identifier];
        const QSet<quint32>& cmap = state->coverage[font.identifier];
        const FaceOverride override = state->overrides.value(font.identifier);
        if (override.hasSet) {
            assignment.codepoints = UnicodeRule(override.setRule);
            assignment.uncoveredCodepoints.clear();
            assignment.shapingSamples.clear();
        } else {
            bool cjk = false;
            for (quint32 codepoint : assignment.codepoints) {
                if (codepoint > kDuokanProbe) {
                    cjk = true;
                    break;
                }
            }
            if (cjk && cmap.contains(kDuokanDe)) {
                assignment.codepoints.insert(kDuokanDe);
            }
            assignment.codepoints.unite(UnicodeRule(override.addRule));
        }
        assignment.codepoints.subtract(UnicodeRule(override.delRule));
        assignment.uncoveredCodepoints.subtract(UnicodeRule(override.delRule));
        for (quint32 codepoint : assignment.codepoints) {
            if (!cmap.contains(codepoint)) {
                assignment.uncoveredCodepoints.insert(codepoint);
            }
        }
        const QSet<quint32> covered = assignment.codepoints - assignment.uncoveredCodepoints;
        QStringList samples;
        for (const QString& sample : assignment.shapingSamples) {
            const QString filtered = FilterSample(sample, covered);
            if (!filtered.isEmpty() && !samples.contains(filtered) && samples.size() < 64) {
                samples.append(filtered);
            }
        }
        assignment.shapingSamples = samples;
        usage->fonts.insert(font.identifier, assignment);
        usage->codepoints.unite(assignment.codepoints);
        for (const QString& sample : samples) {
            if (usage->shapingSamples.size() >= 64) {
                break;
            }
            if (!usage->shapingSamples.contains(sample)) {
                usage->shapingSamples.append(sample);
            }
        }
    }
    usage->warnings = state->warnings;
}

bool IsHtml(const QString& mediaType, const QString& path)
{
    const QString type = mediaType.toLower();
    if (type.contains(QLatin1String("html"))) {
        return true;
    }
    return path.endsWith(QLatin1String(".xhtml"), Qt::CaseInsensitive) ||
           path.endsWith(QLatin1String(".html"), Qt::CaseInsensitive);
}

}

FontFaceUsage FontFaceAssigner::Assign(const QList<UsageSource>& sources,
                                       const QList<FontSnapshot>& fonts) const
{
    AssignmentState state;
    for (const FontSnapshot& font : fonts) {
        state.assigned.insert(font.identifier, FontTextAssignment());
        if (font.bytes.isEmpty()) {
            continue;
        }
        auto blob = TakeHb<hb_blob_t, hb_blob_destroy>(
            hb_blob_create(font.bytes.constData(), unsigned(font.bytes.size()),
                           HB_MEMORY_MODE_DUPLICATE, nullptr, nullptr));
        auto face = TakeHb<hb_face_t, hb_face_destroy>(hb_face_create(blob.get(), 0));
        auto unicodes = TakeHb<hb_set_t, hb_set_destroy>(hb_set_create());
        hb_face_collect_unicodes(face.get(), unicodes.get());
        QSet<quint32> coverage;
        hb_codepoint_t codepoint = HB_SET_VALUE_INVALID;
        while (hb_set_next(unicodes.get(), &codepoint)) {
            coverage.insert(codepoint);
        }
        state.coverage.insert(font.identifier, coverage);
    }

    QHash<QString, QString> pathToId;
    for (const FontSnapshot& font : fonts) {
        pathToId.insert(font.relativePath.toLower(), font.identifier);
    }
    QHash<QString, const UsageSource*> sourceByPath;
    QVector<const UsageSource*> cssSources;
    QVector<const UsageSource*> documents;
    for (const UsageSource& source : sources) {
        sourceByPath.insert(source.path.toLower(), &source);
        if (source.mediaType.compare(QLatin1String("text/css"), Qt::CaseInsensitive) == 0 ||
            source.path.endsWith(QLatin1String(".css"), Qt::CaseInsensitive)) {
            cssSources.append(&source);
        } else if (IsHtml(source.mediaType, source.path)) {
            documents.append(&source);
        }
    }

    QHash<QString, ParsedSheet> parsedSheets;
    for (const UsageSource* source : cssSources) {
        ParsedSheet sheet;
        ParseSheet(source->content, &sheet);
        parsedSheets.insert(source->path.toLower(), sheet);
    }

    for (const UsageSource* source : documents) {
        QVector<Element> elements;
        QVector<CssPiece> pieces;
        QString error;
        ParseDocument(*source, &elements, &pieces, &error);
        if (!error.isEmpty()) {
            state.warnings.append(error);
            continue;
        }
        QHash<QString, QString> familyToId;
        QVector<RawRule> rawRules;
        int skipped = 0;
        for (const CssPiece& piece : pieces) {
            ParsedSheet sheet;
            QString basePath = source->path;
            if (!piece.inlineText) {
                const ParsedSheet* cached = nullptr;
                const auto found = parsedSheets.constFind(piece.value.toLower());
                if (found == parsedSheets.cend()) {
                    const UsageSource* linked = sourceByPath.value(piece.value.toLower(), nullptr);
                    if (!linked) {
                        continue;
                    }
                    ParseSheet(linked->content, &sheet);
                    basePath = linked->path;
                } else {
                    cached = &found.value();
                    basePath = piece.value;
                }
                QSet<QString> stack;
                ConsumeSheet(cached ? *cached : sheet, basePath, pathToId, sourceByPath,
                             &stack, &familyToId, &rawRules, &state);
            } else {
                ParseSheet(piece.value, &sheet);
                QSet<QString> stack;
                ConsumeSheet(sheet, source->path, pathToId, sourceByPath, &stack,
                             &familyToId, &rawRules, &state);
            }
        }

        QVector<Rule> fontRules;
        QVector<Rule> listRules;
        int order = 0;
        for (const RawRule& raw : rawRules) {
            const int ruleOrder = order++;
            for (const QString& part : SplitSelectors(raw.selector)) {
                if (HasPseudo(part)) {
                    ++skipped;
                    continue;
                }
                RawRule one = raw;
                one.selector = part;
                Rule rule = MakeRule(one, familyToId, ruleOrder);
                if (rule.selector.compounds.isEmpty()) {
                    continue;
                }
                if (rule.hasFont) {
                    fontRules.append(rule);
                }
                if (rule.hasList) {
                    listRules.append(rule);
                }
            }
        }
        for (int index = 0; index < elements.size(); ++index) {
            const QString style = elements.at(index).attributes.value(QStringLiteral("style"));
            if (style.isEmpty()) {
                continue;
            }
            const QVector<Declaration> declarations = ParseDeclarations(style);
            bool fontImportant = false;
            const QString fontValue =
                LastDeclaration(declarations, {QStringLiteral("font-family")}, &fontImportant);
            bool listImportant = false;
            const QString listValue = ListStyleValue(LastDeclaration(
                declarations, {QStringLiteral("list-style"), QStringLiteral("list-style-type")},
                &listImportant));
            if (!fontValue.isEmpty()) {
                Rule rule;
                rule.hasFont = true;
                rule.inlineRank = 1;
                rule.important = fontImportant ? 1 : 0;
                rule.order = 1000000 + index;
                rule.matchElement = index;
                rule.families = ResolveFamilies(fontValue, familyToId, &rule.inherit);
                fontRules.append(rule);
            }
            if (!listValue.isEmpty()) {
                Rule rule;
                rule.hasList = true;
                rule.inlineRank = 1;
                rule.listType = listValue;
                rule.listImportant = listImportant ? 1 : 0;
                rule.order = 1000000 + index;
                rule.matchElement = index;
                listRules.append(rule);
            }
        }
        auto byFont = [](const Rule& left, const Rule& right) {
            return RuleKey(left, left.selector, false) > RuleKey(right, right.selector, false);
        };
        auto byList = [](const Rule& left, const Rule& right) {
            return RuleKey(left, left.selector, true) > RuleKey(right, right.selector, true);
        };
        std::stable_sort(fontRules.begin(), fontRules.end(), byFont);
        std::stable_sort(listRules.begin(), listRules.end(), byList);
        for (int index = 0; index < elements.size(); ++index) {
            if (elements.at(index).parent < 0) {
                Walk(index, elements, fontRules, listRules, {}, QStringLiteral("disc"), 0, &state);
            }
        }
        if (skipped > 0) {
            state.warnings.append(
                QObject::tr("%1: skipped %2 CSS selector(s) that use pseudo-classes or pseudo-elements.")
                    .arg(source->path)
                    .arg(skipped));
        }
    }

    for (const UsageSource* source : cssSources) {
        const ParsedSheet sheet = parsedSheets.value(source->path.toLower());
        for (const RawFace& face : sheet.faces) {
            QString identifier;
            for (const QString& url : face.urls) {
                identifier = LookupFont(pathToId, ResolveRelative(source->path, url));
                if (!identifier.isEmpty()) {
                    break;
                }
            }
            if (!identifier.isEmpty()) {
                RememberOverride(face, identifier, &state);
            }
        }
    }

    FontFaceUsage usage;
    FinishAssignment(fonts, &state, &usage);
    return usage;
}

}
