/************************************************************************
**
**  Copyright (C) 2026 3TIC-Project
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "BuiltinPlugins/DivParagraphStylesheetResolver.h"

#include <QDir>
#include <QDomDocument>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

namespace BuiltinPlugins
{

namespace
{

using CssSource = DivParagraphCssAnalyzer::Source;

QString elementLocalName(const QDomElement& element)
{
    const QString local = element.localName();
    if (!local.isEmpty()) {
        return local.toLower();
    }
    return element.tagName().section(QLatin1Char(':'), -1).toLower();
}

QString resolveBookReference(const QString& referrer,
                             const QString& reference,
                             bool& local)
{
    const QUrl url(reference.trimmed());
    if (url.hasFragment() && url.path().isEmpty()) {
        local = true;
        return QString();
    }
    if (!url.scheme().isEmpty() || !url.host().isEmpty() ||
        reference.startsWith(QLatin1String("//"))) {
        local = false;
        return reference;
    }
    const QString decoded = QUrl::fromPercentEncoding(url.path().toUtf8());
    if (decoded.isEmpty() || decoded.startsWith(QLatin1Char('/'))) {
        local = false;
        return reference;
    }
    local = true;
    return QDir::cleanPath(QFileInfo(referrer).dir().filePath(decoded));
}

QString withoutCssComments(const QString& css)
{
    QString stripped = css;
    QChar quote;
    bool comment = false;
    for (int i = 0; i < stripped.length(); ++i) {
        const QChar ch = stripped.at(i);
        const QChar next = i + 1 < stripped.length() ? stripped.at(i + 1) : QChar();
        if (comment) {
            if (ch == QLatin1Char('*') && next == QLatin1Char('/')) {
                stripped[i] = QLatin1Char(' ');
                stripped[i + 1] = QLatin1Char(' ');
                comment = false;
                i++;
            } else if (ch != QLatin1Char('\n') && ch != QLatin1Char('\r')) {
                stripped[i] = QLatin1Char(' ');
            }
            continue;
        }
        if (!quote.isNull()) {
            if (ch == QLatin1Char('\\')) {
                i++;
            } else if (ch == quote) {
                quote = QChar();
            }
            continue;
        }
        if (ch == QLatin1Char('\'') || ch == QLatin1Char('"')) {
            quote = ch;
        } else if (ch == QLatin1Char('/') && next == QLatin1Char('*')) {
            stripped[i] = QLatin1Char(' ');
            stripped[i + 1] = QLatin1Char(' ');
            comment = true;
            i++;
        }
    }
    return stripped;
}

QStringList cssImports(const QString& css)
{
    QStringList imports;
    static const QRegularExpression pattern(
        QStringLiteral("@import\\s+(?:url\\(\\s*)?(?:[\"']([^\"']+)[\"']|([^\\s\\);]+))"),
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator matches = pattern.globalMatch(withoutCssComments(css));
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        const QString reference = match.captured(1).isEmpty()
            ? match.captured(2) : match.captured(1);
        if (!reference.isEmpty()) {
            imports << reference;
        }
    }
    return imports;
}

void collectLinkedStyles(const QDomNode& node,
                         QStringList& links,
                         QStringList& inline_styles)
{
    if (node.isElement()) {
        const QDomElement element = node.toElement();
        const QString name = elementLocalName(element);
        if (name == QLatin1String("link")) {
            const QStringList rel = element.attribute(QStringLiteral("rel"))
                .toLower().split(QRegularExpression(QStringLiteral("\\s+")),
                                 Qt::SkipEmptyParts);
            if (rel.contains(QStringLiteral("stylesheet")) &&
                element.hasAttribute(QStringLiteral("href"))) {
                links << element.attribute(QStringLiteral("href"));
            }
        } else if (name == QLatin1String("style")) {
            inline_styles << element.text();
        }
    }
    for (QDomNode child = node.firstChild(); !child.isNull(); child = child.nextSibling()) {
        collectLinkedStyles(child, links, inline_styles);
    }
}

void addUnavailable(const QString& id,
                    QSet<QString>& visited,
                    QVector<CssSource>& sources)
{
    const QString key = QStringLiteral("unavailable:") + id;
    if (!visited.contains(key)) {
        visited.insert(key);
        sources << CssSource { id, QString(), false };
    }
}

void addCssSource(const QString& path,
                  const QHash<QString, QString>& css_by_path,
                  QSet<QString>& visited,
                  QVector<CssSource>& sources)
{
    if (path.isEmpty() || visited.contains(path)) {
        return;
    }
    visited.insert(path);
    if (!css_by_path.contains(path)) {
        sources << CssSource { path, QString(), false };
        return;
    }

    const QString css = css_by_path.value(path);
    sources << CssSource { path, css, true };
    for (const QString& import_reference : cssImports(css)) {
        bool local = false;
        const QString imported = resolveBookReference(path, import_reference, local);
        if (!local) {
            addUnavailable(import_reference, visited, sources);
        } else {
            addCssSource(imported, css_by_path, visited, sources);
        }
    }
}

}

QVector<DivParagraphCssAnalyzer::Source>
DivParagraphStylesheetResolver::resolve(
    const QString& xhtml,
    const QString& xhtml_book_path,
    const QHash<QString, QString>& css_by_book_path)
{
    QStringList links;
    QStringList inline_styles;
    QDomDocument document;
    if (document.setContent(xhtml, false)) {
        collectLinkedStyles(document, links, inline_styles);
    }
    static const QRegularExpression xml_stylesheet(
        QStringLiteral("<\\?xml-stylesheet\\b[^?]*\\bhref\\s*=\\s*[\"']([^\"']+)[\"'][^?]*\\?>"),
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator processing_instructions =
        xml_stylesheet.globalMatch(xhtml);
    while (processing_instructions.hasNext()) {
        links << processing_instructions.next().captured(1);
    }

    QHash<QString, QString> normalized_css;
    for (auto css = css_by_book_path.constBegin(); css != css_by_book_path.constEnd(); ++css) {
        normalized_css.insert(QDir::cleanPath(css.key()), css.value());
    }

    QVector<CssSource> styles;
    QSet<QString> visited;
    for (const QString& link : links) {
        bool local = false;
        const QString path = resolveBookReference(xhtml_book_path, link, local);
        if (!local) {
            addUnavailable(link, visited, styles);
        } else {
            addCssSource(path, normalized_css, visited, styles);
        }
    }
    for (const QString& inline_style : inline_styles) {
        for (const QString& import_reference : cssImports(inline_style)) {
            bool local = false;
            const QString path = resolveBookReference(
                xhtml_book_path, import_reference, local);
            if (!local) {
                addUnavailable(import_reference, visited, styles);
            } else {
                addCssSource(path, normalized_css, visited, styles);
            }
        }
    }
    return styles;
}

}
