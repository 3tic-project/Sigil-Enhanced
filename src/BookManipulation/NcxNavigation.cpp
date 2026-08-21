#include "BookManipulation/NcxNavigation.h"

#include <QStringList>
#include <QXmlStreamReader>

namespace {

QString LocalName(const QXmlStreamReader &xml)
{
    return xml.name().toString();
}

bool IsNamed(const QXmlStreamReader &xml, const char *name)
{
    return LocalName(xml).compare(QLatin1String(name), Qt::CaseInsensitive) == 0;
}

QString SplitFragment(const QString &href, QString *fragment)
{
    const int hash = href.indexOf(QLatin1Char('#'));
    if (hash < 0) {
        if (fragment) {
            fragment->clear();
        }
        return href;
    }
    if (fragment) {
        *fragment = href.mid(hash);
    }
    return href.left(hash);
}

QString ResolveRelativeSegments(const QString &path)
{
    const bool had_leading_slash = path.startsWith(QLatin1Char('/'));
    const QStringList segs = path.split(QLatin1Char('/'), Qt::KeepEmptyParts);
    QStringList resolved;
    for (const QString &seg : segs) {
        if (seg.isEmpty() || seg == QLatin1String(".")) {
            continue;
        }
        if (seg == QLatin1String("..")) {
            if (!resolved.isEmpty()) {
                resolved.removeLast();
            }
            continue;
        }
        resolved.append(seg);
    }
    QString result = resolved.join(QLatin1Char('/'));
    if (had_leading_slash) {
        result.prepend(QLatin1Char('/'));
    }
    return result;
}

QString ReadNestedText(QXmlStreamReader &xml)
{
    const QString outer = LocalName(xml);
    QString value;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && IsNamed(xml, "text")) {
            value = xml.readElementText(QXmlStreamReader::IncludeChildElements).simplified();
        } else if (xml.isEndElement() && LocalName(xml).compare(outer, Qt::CaseInsensitive) == 0) {
            break;
        }
    }
    return value;
}

void ParseNavPoint(QXmlStreamReader &xml, QList<NcxNavPoint> &toc, int level)
{
    NcxNavPoint point;
    point.level = level;
    QList<NcxNavPoint> children;

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            if (IsNamed(xml, "navLabel")) {
                point.label = ReadNestedText(xml);
            } else if (IsNamed(xml, "content")) {
                point.src = xml.attributes().value(QLatin1String("src")).toString();
            } else if (IsNamed(xml, "navPoint")) {
                ParseNavPoint(xml, children, level + 1);
            }
        } else if (xml.isEndElement() && IsNamed(xml, "navPoint")) {
            break;
        }
    }

    toc.append(point);
    toc.append(children);
}

void ParseNavMap(QXmlStreamReader &xml, QList<NcxNavPoint> &toc)
{
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && IsNamed(xml, "navPoint")) {
            ParseNavPoint(xml, toc, 1);
        } else if (xml.isEndElement() && IsNamed(xml, "navMap")) {
            break;
        }
    }
}

void ParsePageList(QXmlStreamReader &xml, QList<NcxPageTarget> &pages)
{
    NcxPageTarget current;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            if (IsNamed(xml, "pageTarget")) {
                current = NcxPageTarget();
                current.value = xml.attributes().value(QLatin1String("value")).toString();
            } else if (IsNamed(xml, "content")) {
                current.src = xml.attributes().value(QLatin1String("src")).toString();
            } else if (IsNamed(xml, "navLabel")) {
                const QString label = ReadNestedText(xml);
                if (current.value.isEmpty()) {
                    current.value = label;
                }
            }
        } else if (xml.isEndElement()) {
            if (IsNamed(xml, "pageTarget")) {
                if (!current.src.isEmpty() || !current.value.isEmpty()) {
                    pages.append(current);
                }
                current = NcxPageTarget();
            } else if (IsNamed(xml, "pageList")) {
                break;
            }
        }
    }
}

QString StripDoctype(QString source)
{
    const int start = source.indexOf(QLatin1String("<!DOCTYPE"), 0, Qt::CaseInsensitive);
    if (start < 0) {
        return source;
    }
    const int end = source.indexOf(QLatin1Char('>'), start);
    if (end < start) {
        return source;
    }
    source.remove(start, end - start + 1);
    return source;
}

}

NcxNavigation NcxNavigation::parse(const QString &ncx_source)
{
    NcxNavigation result;
    QXmlStreamReader xml(StripDoctype(ncx_source));
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement()) {
            continue;
        }
        if (IsNamed(xml, "docTitle")) {
            result.doctitle = ReadNestedText(xml);
        } else if (IsNamed(xml, "navMap")) {
            ParseNavMap(xml, result.toc);
        } else if (IsNamed(xml, "pageList")) {
            ParsePageList(xml, result.pages);
        }
    }
    return result;
}

QString NcxNavigation::srcToBookPath(const QString &src, const QString &ncx_folder)
{
    if (src.contains(QLatin1Char(':'))) {
        return src;
    }

    QString fragment;
    QString base = SplitFragment(src, &fragment);
    if (base.startsWith(QLatin1String("./"))) {
        base = base.mid(2);
    }

    QString folder = ncx_folder;
    if (folder == QLatin1String(".") || folder == QLatin1String("./")) {
        folder.clear();
    }
    while (folder.endsWith(QLatin1Char('/'))) {
        folder.chop(1);
    }

    QString bookpath;
    if (base.isEmpty()) {
        bookpath = folder;
    } else if (folder.isEmpty()) {
        bookpath = base;
    } else {
        bookpath = folder + QLatin1Char('/') + base;
    }
    bookpath = ResolveRelativeSegments(bookpath);
    return bookpath + fragment;
}

QString NcxNavigation::bookPathToNavHref(const QString &bookpath, const QString &nav_bookpath)
{
    if (bookpath.contains(QLatin1Char(':'))) {
        return bookpath;
    }

    QString fragment;
    const QString dest = SplitFragment(bookpath, &fragment);
    QString start_dir = nav_bookpath;
    const int slash = start_dir.lastIndexOf(QLatin1Char('/'));
    start_dir = slash >= 0 ? start_dir.left(slash) : QString();
    if (start_dir == QLatin1String(".")) {
        start_dir.clear();
    }

    const QStringList dest_segs = dest.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    const QStringList start_segs = start_dir.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    int i = 0;
    while (i < dest_segs.size() && i < start_segs.size() && dest_segs.at(i) == start_segs.at(i)) {
        ++i;
    }

    QStringList href_segs;
    for (int p = i; p < start_segs.size(); ++p) {
        href_segs.append(QStringLiteral(".."));
    }
    for (int p = i; p < dest_segs.size(); ++p) {
        href_segs.append(dest_segs.at(p));
    }

    QString href = href_segs.join(QLatin1Char('/'));
    if (href.isEmpty()) {
        return fragment.isEmpty() ? QStringLiteral("#") : fragment;
    }
    return href + fragment;
}
