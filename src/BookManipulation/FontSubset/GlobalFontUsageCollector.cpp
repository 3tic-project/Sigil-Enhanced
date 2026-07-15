#include "BookManipulation/FontSubset/GlobalFontUsageCollector.h"

#include <algorithm>

#include <QObject>
#include <QXmlStreamReader>

namespace FontSubset
{
namespace
{

bool IsXmlMediaType(const QString& mediaType)
{
    return mediaType.contains(QStringLiteral("xml"), Qt::CaseInsensitive) ||
           mediaType.contains(QStringLiteral("html"), Qt::CaseInsensitive) ||
           mediaType.contains(QStringLiteral("svg"), Qt::CaseInsensitive) ||
           mediaType.contains(QStringLiteral("opf"), Qt::CaseInsensitive) ||
           mediaType.contains(QStringLiteral("ncx"), Qt::CaseInsensitive);
}

bool IsCssMediaType(const QString& mediaType)
{
    return mediaType.compare(QStringLiteral("text/css"), Qt::CaseInsensitive) == 0;
}

bool IsIdentifierCharacter(QChar character)
{
    return character.isLetterOrNumber() || character == QLatin1Char('-') ||
           character == QLatin1Char('_');
}

void SkipWhitespaceAndComments(const QString& css, qsizetype& position)
{
    while (position < css.size()) {
        if (css.at(position).isSpace()) {
            ++position;
            continue;
        }
        if (position + 1 < css.size() && css.at(position) == QLatin1Char('/') &&
            css.at(position + 1) == QLatin1Char('*')) {
            const qsizetype end = css.indexOf(QStringLiteral("*/"), position + 2);
            position = end < 0 ? css.size() : end + 2;
            continue;
        }
        break;
    }
}

QString ReadCssString(const QString& css, qsizetype& position)
{
    const QChar quote = css.at(position++);
    QString result;
    while (position < css.size()) {
        QChar character = css.at(position++);
        if (character == quote) {
            return result;
        }
        if (character != QLatin1Char('\\') || position >= css.size()) {
            result.append(character);
            continue;
        }

        character = css.at(position);
        if (character == QLatin1Char('\n') || character == QLatin1Char('\r')) {
            ++position;
            if (character == QLatin1Char('\r') && position < css.size() &&
                css.at(position) == QLatin1Char('\n')) {
                ++position;
            }
            continue;
        }

        qsizetype hexLength = 0;
        quint32 codepoint = 0;
        while (position + hexLength < css.size() && hexLength < 6) {
            const QChar digit = css.at(position + hexLength);
            const int value = digit.digitValue();
            int hexValue = value;
            if (hexValue < 0 && digit.toLower() >= QLatin1Char('a') &&
                digit.toLower() <= QLatin1Char('f')) {
                hexValue = digit.toLower().unicode() - QLatin1Char('a').unicode() + 10;
            }
            if (hexValue < 0 || hexValue > 15) {
                break;
            }
            codepoint = codepoint * 16 + quint32(hexValue);
            ++hexLength;
        }
        if (hexLength > 0) {
            position += hexLength;
            if (position < css.size() && css.at(position).isSpace()) {
                ++position;
            }
            if (codepoint <= 0x10ffff &&
                !(codepoint >= 0xd800 && codepoint <= 0xdfff)) {
                const char32_t scalar = char32_t(codepoint);
                result.append(QString::fromUcs4(&scalar, 1));
            }
        } else {
            result.append(css.at(position++));
        }
    }
    return result;
}

}

GlobalFontUsage GlobalFontUsageCollector::Collect(
    const QList<UsageSource>& sources) const
{
    GlobalFontUsage usage;
    usage.codepoints.insert(0x0020);
    usage.codepoints.insert(0x00a0);
    usage.codepoints.insert(0x3000);
    for (const UsageSource& source : sources) {
        if (IsCssMediaType(source.mediaType)) {
            CollectCss(source.content, source.path, usage);
        } else if (IsXmlMediaType(source.mediaType)) {
            CollectXml(source, usage);
        }
    }
    return usage;
}

void GlobalFontUsageCollector::CollectXml(const UsageSource& source,
                                          GlobalFontUsage& usage) const
{
    QXmlStreamReader reader(source.content);
    while (!reader.atEnd()) {
        const QXmlStreamReader::TokenType token = reader.readNext();
        if (token == QXmlStreamReader::StartElement) {
            const QString name = reader.name().toString().toLower();
            if (name == QStringLiteral("script")) {
                reader.skipCurrentElement();
                continue;
            }
            if (name == QStringLiteral("style")) {
                CollectCss(reader.readElementText(QXmlStreamReader::IncludeChildElements),
                           source.path, usage);
                continue;
            }
            const auto attributes = reader.attributes();
            for (const QXmlStreamAttribute& attribute : attributes) {
                const QString attributeName = attribute.name().toString().toLower();
                if (attributeName == QStringLiteral("alt") ||
                    attributeName == QStringLiteral("title") ||
                    attributeName == QStringLiteral("aria-label") ||
                    attributeName == QStringLiteral("label")) {
                    AddText(attribute.value().toString(), usage);
                }
            }
        } else if (token == QXmlStreamReader::Characters &&
                   !reader.isWhitespace()) {
            AddText(reader.text().toString(), usage);
        }
    }
    if (reader.hasError()) {
        usage.warnings.append(
            QObject::tr("%1: XML parsing stopped at line %2: %3")
                .arg(source.path)
                .arg(reader.lineNumber())
                .arg(reader.errorString()));
    }
}

void GlobalFontUsageCollector::CollectCss(const QString& css,
                                          const QString& path,
                                          GlobalFontUsage& usage) const
{
    qsizetype position = 0;
    bool declarationStart = false;
    while (position < css.size()) {
        if (position + 1 < css.size() && css.at(position) == QLatin1Char('/') &&
            css.at(position + 1) == QLatin1Char('*')) {
            SkipWhitespaceAndComments(css, position);
            continue;
        }
        if (!IsIdentifierCharacter(css.at(position))) {
            if (css.at(position) == QLatin1Char('{') ||
                css.at(position) == QLatin1Char(';')) {
                declarationStart = true;
            } else if (css.at(position) == QLatin1Char('}')) {
                declarationStart = false;
            } else if (css.at(position) == QLatin1Char('\'') ||
                       css.at(position) == QLatin1Char('"')) {
                ReadCssString(css, position);
                continue;
            }
            ++position;
            continue;
        }
        const qsizetype identifierStart = position;
        while (position < css.size() && IsIdentifierCharacter(css.at(position))) {
            ++position;
        }
        if (!declarationStart) {
            continue;
        }
        declarationStart = false;
        const bool isContent =
            css.mid(identifierStart, position - identifierStart)
                .compare(QStringLiteral("content"), Qt::CaseInsensitive) == 0;
        SkipWhitespaceAndComments(css, position);
        if (position >= css.size() || css.at(position) != QLatin1Char(':')) {
            continue;
        }
        ++position;
        if (!isContent) {
            continue;
        }
        bool foundString = false;
        bool foundDynamicValue = false;
        while (position < css.size() && css.at(position) != QLatin1Char(';') &&
               css.at(position) != QLatin1Char('}')) {
            SkipWhitespaceAndComments(css, position);
            if (position >= css.size() || css.at(position) == QLatin1Char(';') ||
                css.at(position) == QLatin1Char('}')) {
                break;
            }
            if (css.at(position) == QLatin1Char('\'') ||
                css.at(position) == QLatin1Char('"')) {
                AddText(ReadCssString(css, position), usage);
                foundString = true;
            } else {
                foundDynamicValue = true;
                ++position;
            }
        }
        if (foundDynamicValue && !foundString) {
            usage.warnings.append(QObject::tr(
                "%1: a dynamic CSS content value could not be resolved.").arg(path));
        }
    }
}

void GlobalFontUsageCollector::AddText(const QString& text,
                                       GlobalFontUsage& usage,
                                       bool addShapingSample) const
{
    const QList<uint> codepoints = text.toUcs4();
    for (uint codepoint : codepoints) {
        if (codepoint == 0 || codepoint == '\n' || codepoint == '\r' ||
            codepoint == '\t' || (codepoint < 0x20)) {
            continue;
        }
        usage.codepoints.insert(codepoint);
    }
    if (addShapingSample && !text.trimmed().isEmpty() &&
        usage.shapingSamples.size() < 64) {
        const QString sample = text.left(256);
        if (!usage.shapingSamples.contains(sample)) {
            usage.shapingSamples.append(sample);
        }
    }
}

}
