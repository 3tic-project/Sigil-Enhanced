#include "BookManipulation/NcxGenerator.h"

#include <QHash>
#include <QStringList>
#include <QUrl>

namespace {

struct TocItem {
    int order;
    int level;
    QString href;
    QString title;
};

struct PageItem {
    int order;
    QString href;
    QString title;
};

struct Navigation {
    QList<TocItem> toc;
    QList<PageItem> pages;
    int maxLevel = -1;
};

QString StartingDirectory(const QString &path)
{
    const int slash = path.lastIndexOf('/');
    return slash < 0 ? QString() : path.left(slash);
}

QString RelativePath(QString destination, QString start)
{
    while (destination.endsWith('/')) destination.chop(1);
    while (start.endsWith('/')) start.chop(1);
    QStringList destParts = destination.split('/', Qt::KeepEmptyParts);
    QStringList startParts = start.split('/', Qt::KeepEmptyParts);
    if (destParts.size() == 1 && destParts.first().isEmpty()) destParts.clear();
    if (startParts.size() == 1 && startParts.first().isEmpty()) startParts.clear();
    int shared = 0;
    while (shared < destParts.size() && shared < startParts.size() &&
           destParts.at(shared) == startParts.at(shared)) ++shared;
    QStringList result;
    for (int i = shared; i < startParts.size(); ++i) result.append(QStringLiteral(".."));
    for (int i = shared; i < destParts.size(); ++i) result.append(destParts.at(i));
    return result.join('/');
}

QString BookPath(const QString &destination, QString start)
{
    if (start.trimmed().isEmpty()) return destination;
    while (start.endsWith('/')) start.chop(1);
    QStringList result;
    const QString combined = start + '/' + destination;
    for (const QString &part : combined.split('/', Qt::KeepEmptyParts)) {
        if (part == QLatin1String(".")) continue;
        if (part == QLatin1String("..")) {
            if (!result.isEmpty()) result.removeLast();
        } else {
            result.append(part);
        }
    }
    return result.join('/');
}

bool EncodeCharacter(char32_t codepoint)
{
    if (codepoint < 128)
        return !QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.-/~")
                    .contains(QChar(ushort(codepoint)));
    if (codepoint < 0xA0) return true;
    if (codepoint <= 0xD7FF) return false;
    if (codepoint < 0xF900) return true;
    if (codepoint <= 0xFDCF) return false;
    if (codepoint < 0xFDF0) return true;
    if (codepoint <= 0xFFEF) return false;
    if (codepoint < 0x10000) return true;
    if (codepoint <= 0x1FFFD) return false;
    if (codepoint < 0x20000) return true;
    if (codepoint <= 0x2FFFD) return false;
    if (codepoint < 0x30000) return true;
    if (codepoint <= 0x3FFFD) return false;
    return true;
}

QString EncodePart(const QString &part)
{
    static const char hex[] = "0123456789ABCDEF";
    QString result;
    for (char32_t codepoint : part.toUcs4()) {
        const QString character = QString::fromUcs4(&codepoint, 1);
        if (!EncodeCharacter(codepoint)) {
            result += character;
            continue;
        }
        for (unsigned char byte : character.toUtf8()) {
            result += '%';
            result += QChar(hex[byte >> 4]);
            result += QChar(hex[byte & 15]);
        }
    }
    return result;
}

QString ConvertHref(const QString &raw, const QString &navBookPath,
                    const QString &ncxDirectory, bool *valid)
{
    if (raw.contains(':')) return raw;
    const QStringList parts = raw.split('#');
    if (parts.size() > 2) {
        *valid = false; // The legacy split into path and fragment also fails.
        return {};
    }
    QString path = QUrl::fromPercentEncoding(parts.at(0).toUtf8());
    QString fragment = parts.size() == 2 ? QUrl::fromPercentEncoding(parts.at(1).toUtf8()) : QString();
    if (path.startsWith(QLatin1String("./"))) path.remove(0, 2);
    const QString destination = path.isEmpty() ? navBookPath : BookPath(path, StartingDirectory(navBookPath));
    QString result = EncodePart(RelativePath(destination, ncxDirectory));
    fragment = EncodePart(fragment);
    if (!fragment.isEmpty()) result += '#' + fragment;
    return result;
}

QHash<QString, QString> Attributes(const QString &tag, int position)
{
    QHash<QString, QString> result;
    while (position < tag.size()) {
        while (position < tag.size() && tag.at(position).isSpace()) ++position;
        if (position >= tag.size() || tag.at(position) == '>' || tag.at(position) == '/') break;
        const int start = position;
        while (position < tag.size() && tag.at(position) != '=' &&
               tag.at(position) != '>' && !tag.at(position).isSpace()) ++position;
        const QString name = tag.mid(start, position - start);
        while (position < tag.size() && tag.at(position).isSpace()) ++position;
        if (name.isEmpty() || position >= tag.size() || tag.at(position) != '=') {
            while (position < tag.size() && tag.at(position) != '>' && !tag.at(position).isSpace()) ++position;
            continue;
        }
        ++position;
        while (position < tag.size() && tag.at(position).isSpace()) ++position;
        QString value;
        if (position < tag.size() && (tag.at(position) == '"' || tag.at(position) == '\'')) {
            const QChar quote = tag.at(position++);
            const int begin = position;
            while (position < tag.size() && tag.at(position) != quote) ++position;
            value = tag.mid(begin, position - begin);
            if (position < tag.size()) ++position;
        } else {
            const int begin = position;
            while (position < tag.size() && tag.at(position) != '>' &&
                   tag.at(position) != '/' && !tag.at(position).isSpace()) ++position;
            value = tag.mid(begin, position - begin);
        }
        result.insert(name, value);
    }
    return result;
}

bool ParseNavigation(const QString &source, const QString &navBookPath,
                     const QString &ncxDirectory, Navigation *result)
{
    QStringList openTags;
    QString navType;
    QString href;
    QString title;
    int level = 0;
    int playOrder = 0;
    int pageOrder = 0;
    int position = 0;
    while (position < source.size()) {
        if (source.at(position) != '<') {
            const int next = source.indexOf('<', position);
            const QString value = source.mid(position, next < 0 ? -1 : next - position);
            if (openTags.contains(QLatin1String("a"))) title += value;
            else title.clear();
            position = next < 0 ? source.size() : next;
            continue;
        }
        if (source.mid(position, 4) == QLatin1String("<!--") ||
            source.mid(position, 9) == QLatin1String("<![CDATA[")) {
            const QString marker = source.at(position + 2) == '-' ? QStringLiteral("-->") : QStringLiteral("]]>");
            const int end = source.indexOf(marker, position + 4);
            position = end < 0 ? source.size() : end + marker.size();
            continue;
        }
        const int end = source.indexOf('>', position + 1);
        if (end < 0) break;
        const QString tag = source.mid(position, end - position + 1);
        position = end + 1;
        int cursor = 1;
        while (cursor < tag.size() && tag.at(cursor) == ' ') ++cursor;
        const bool closing = cursor < tag.size() && tag.at(cursor) == '/';
        if (closing) {
            ++cursor;
            while (cursor < tag.size() && tag.at(cursor) == ' ') ++cursor;
        }
        const int start = cursor;
        while (cursor < tag.size() && tag.at(cursor) != '>' && tag.at(cursor) != '/' &&
               !tag.at(cursor).isSpace() && tag.at(cursor) != '"' && tag.at(cursor) != '\'') ++cursor;
        const QString name = tag.mid(start, cursor - start).toLower();
        if (name.startsWith('!') || name.startsWith('?')) continue;
        const bool single = !closing && tag.trimmed().endsWith(QLatin1String("/>"));
        const QHash<QString, QString> attributes = closing ? QHash<QString, QString>() : Attributes(tag, cursor);

        if (name == QLatin1String("nav")) {
            if (closing) navType.clear();
            else if (!single) navType = attributes.value(QStringLiteral("epub:type"));
        } else if (name == QLatin1String("ol") &&
                   (navType == QLatin1String("toc") || navType == QLatin1String("page-list") ||
                    navType == QLatin1String("landmarks"))) {
            if (closing) --level;
            else if (!single) {
                ++level;
                if (navType == QLatin1String("toc") && level > result->maxLevel) result->maxLevel = level;
            }
        } else if (name == QLatin1String("a")) {
            if (closing) {
                if (navType == QLatin1String("toc")) result->toc.append({++playOrder, level, href, title});
                else if (navType == QLatin1String("page-list"))
                    result->pages.append({++pageOrder, href, title});
                title.clear();
            } else if (!single) {
                bool valid = true;
                href = ConvertHref(attributes.value(QStringLiteral("href")), navBookPath, ncxDirectory, &valid);
                if (!valid) return false;
            }
        }
        if (closing) {
            if (!openTags.isEmpty()) openTags.removeLast();
        } else if (!single) {
            openTags.append(name);
        }
    }
    return true;
}

QString Indent(int level)
{
    return QString(qMax(0, level) * 2, QChar(' '));
}

}

QString NcxGenerator::Generate(const QString &navSource, const QString &navBookPath,
                               const QString &ncxDirectory, const QString &documentTitle,
                               const QString &mainIdentifier)
{
    Navigation nav;
    if (!ParseNavigation(navSource, navBookPath, ncxDirectory, &nav)) return {};
    QString out;
    out += QStringLiteral("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
    out += QStringLiteral("<ncx xmlns=\"http://www.daisy.org/z3986/2005/ncx/\" version=\"2005-1\">\n");
    out += QStringLiteral("  <head>\n");
    out += QStringLiteral("    <meta name=\"dtb:uid\" content=\"") + mainIdentifier + QStringLiteral("\" />\n");
    out += QStringLiteral("    <meta name=\"dtb:depth\" content=\"") + QString::number(nav.maxLevel) + QStringLiteral("\" />\n");
    out += QStringLiteral("    <meta name=\"dtb:totalPageCount\" content=\"") + QString::number(nav.pages.size()) + QStringLiteral("\" />\n");
    out += QStringLiteral("    <meta name=\"dtb:maxPageNumber\" content=\"") + QString::number(nav.pages.size()) + QStringLiteral("\" />\n");
    out += QStringLiteral("  </head>\n<docTitle>\n  <text>") + documentTitle + QStringLiteral("</text>\n</docTitle>\n<navMap>\n");
    int previousLevel = -1;
    for (const TocItem &item : nav.toc) {
        while (item.level <= previousLevel) {
            out += Indent(previousLevel) + QStringLiteral("</navPoint>\n");
            --previousLevel;
        }
        const QString space = Indent(item.level);
        out += space + QStringLiteral("<navPoint id=\"navPoint") + QString::number(item.order) + QStringLiteral("\">\n");
        out += space + QStringLiteral("  <navLabel>\n");
        out += space + QStringLiteral("    <text>") + item.title + QStringLiteral("</text>\n");
        out += space + QStringLiteral("  </navLabel>\n");
        out += space + QStringLiteral("  <content src=\"") + item.href + QStringLiteral("\" />\n");
        previousLevel = item.level;
    }
    while (previousLevel > 0) {
        out += Indent(previousLevel) + QStringLiteral("</navPoint>\n");
        --previousLevel;
    }
    out += QStringLiteral("</navMap>\n");
    if (!nav.pages.isEmpty()) {
        out += QStringLiteral("<pageList>\n");
        for (const PageItem &page : nav.pages) {
            const QString order = QString::number(nav.toc.size() + page.order);
            out += QStringLiteral("  <pageTarget id=\"navPoint") + order +
                   QStringLiteral("\" type=\"normal\" value=\"") + page.title + QStringLiteral("\">\n");
            out += QStringLiteral("    <navLabel><text>") + page.title + QStringLiteral("</text></navLabel>\n");
            out += QStringLiteral("    <content src=\"") + page.href + QStringLiteral("\" />\n");
            out += QStringLiteral("  </pageTarget>\n");
        }
        out += QStringLiteral("</pageList>\n");
    }
    out += QStringLiteral("</ncx>\n");
    return out;
}
