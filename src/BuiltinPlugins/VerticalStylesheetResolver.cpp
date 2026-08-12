/************************************************************************
**
**  Copyright (C) 2026 3TIC-Project
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "BuiltinPlugins/VerticalStylesheetResolver.h"

#include <QDir>
#include <QDomDocument>
#include <QDomElement>
#include <QList>
#include <QRegularExpression>
#include <QUrl>

#include "Parsers/CSSParser.h"

namespace BuiltinPlugins
{

namespace
{

QString localName(const QDomNode& node)
{
    if (!node.isElement()) {
        return QString();
    }
    const QDomElement element = node.toElement();
    QString name = element.localName().isEmpty() ? element.tagName() : element.localName();
    const int colon = name.lastIndexOf(QLatin1Char(':'));
    if (colon >= 0) {
        name = name.mid(colon + 1);
    }
    return name.toLower();
}

QString resolveRelativeHref(const QString& baseBookPath, QString href)
{
    const int fragment = href.indexOf(QLatin1Char('#'));
    if (fragment >= 0) {
        href = href.left(fragment);
    }
    const int query = href.indexOf(QLatin1Char('?'));
    if (query >= 0) {
        href = href.left(query);
    }
    static const QRegularExpression scheme_re(
        QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]*:"));
    if (href.isEmpty() || href.startsWith(QLatin1Char('/'))
        || scheme_re.match(href).hasMatch()) {
        return QString();
    }

    href = QUrl::fromPercentEncoding(href.toUtf8());
    const int slash = baseBookPath.lastIndexOf(QLatin1Char('/'));
    const QString directory = slash >= 0 ? baseBookPath.left(slash) : QString();
    const QString resolved = QDir::cleanPath(
        directory.isEmpty() ? href : directory + QLatin1Char('/') + href);
    if (resolved.startsWith(QLatin1Char('/'))
        || resolved == QStringLiteral("..")
        || resolved.startsWith(QStringLiteral("../"))) {
        return QString();
    }
    return resolved;
}

QString importedHref(const QString& importRule)
{
    static const QRegularExpression import_re(
        QStringLiteral("^\\s*(?:url\\(\\s*)?(?:\\\"([^\\\"]+)\\\"|'([^']+)'|([^\\s\\)]+))"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = import_re.match(importRule);
    if (!match.hasMatch()) {
        return QString();
    }
    for (int i = 1; i <= 3; ++i) {
        if (!match.captured(i).isEmpty()) {
            return match.captured(i);
        }
    }
    return QString();
}

}

QStringList VerticalStylesheetResolver::resolve(
    const QString& xhtmlSource,
    const QString& xhtmlBookPath,
    const QHash<QString, QString>& cssByBookPath)
{
    QStringList result;
    QDomDocument document;
    if (!document.setContent(xhtmlSource, false)) {
        return result;
    }

    QList<QDomNode> nodes { document.documentElement() };
    while (!nodes.isEmpty()) {
        const QDomNode node = nodes.takeLast();
        if (node.isElement() && localName(node) == QStringLiteral("link")) {
            const QDomElement link = node.toElement();
            const QStringList rel = link.attribute(QStringLiteral("rel")).toLower()
                .split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
            if (rel.contains(QStringLiteral("stylesheet"))) {
                const QString path = resolveRelativeHref(
                    xhtmlBookPath, link.attribute(QStringLiteral("href")));
                if (!path.isEmpty() && cssByBookPath.contains(path) && !result.contains(path)) {
                    result.append(path);
                }
            }
        }
        for (QDomNode child = node.firstChild(); !child.isNull(); child = child.nextSibling()) {
            nodes.append(child);
        }
    }

    for (int index = 0; index < result.size(); ++index) {
        const QString cssPath = result.at(index);
        CSSParser parser;
        parser.parse_css(cssByBookPath.value(cssPath));
        for (const QString& importRule : parser.get_imports()) {
            const QString path = resolveRelativeHref(cssPath, importedHref(importRule));
            if (!path.isEmpty() && cssByBookPath.contains(path) && !result.contains(path)) {
                result.append(path);
            }
        }
    }
    return result;
}

}
