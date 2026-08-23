/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/Execution/ContentOps.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

namespace SigilAgent
{

QString xmlEscape(const QString &text)
{
    QString out = text;
    out.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    out.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    out.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    out.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    return out;
}

QString relativeBookHref(const QString &from_book_path, const QString &to_book_path)
{
    const QString from_dir = QFileInfo(from_book_path).path();
    QStringList from_parts = from_dir.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (from_dir == QLatin1String(".") || from_dir.isEmpty()) from_parts.clear();
    const QStringList to_parts = to_book_path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    int common = 0;
    while (common < from_parts.size() && common + 1 < to_parts.size()
           && from_parts.at(common) == to_parts.at(common)) {
        ++common;
    }
    QStringList bits;
    for (int i = common; i < from_parts.size(); ++i) bits.append(QStringLiteral(".."));
    bits += to_parts.mid(common);
    return bits.join(QLatin1Char('/'));
}

QString extractBodyInner(const QString &xhtml)
{
    QRegularExpression open(QStringLiteral("<body\\b[^>]*>"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = open.match(xhtml);
    const int close = xhtml.lastIndexOf(QStringLiteral("</body>"), -1, Qt::CaseInsensitive);
    if (!match.hasMatch() || close < match.capturedEnd()) return xhtml;
    return xhtml.mid(match.capturedEnd(), close - match.capturedEnd());
}

QString replaceBodyInner(const QString &xhtml, const QString &inner)
{
    QRegularExpression open(QStringLiteral("<body\\b[^>]*>"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = open.match(xhtml);
    const int close = xhtml.lastIndexOf(QStringLiteral("</body>"), -1, Qt::CaseInsensitive);
    if (!match.hasMatch() || close < match.capturedEnd()) {
        return xhtml + QLatin1Char('\n') + inner;
    }
    return xhtml.left(match.capturedEnd()) + inner + xhtml.mid(close);
}

QString setXhtmlTitle(const QString &xhtml, const QString &title)
{
    QRegularExpression title_re(QStringLiteral("<title>[\\s\\S]*?</title>"),
                                QRegularExpression::CaseInsensitiveOption);
    if (!title_re.match(xhtml).hasMatch()) return xhtml;
    QString out = xhtml;
    out.replace(title_re, QStringLiteral("<title>") + xmlEscape(title) + QStringLiteral("</title>"));
    return out;
}

QRegularExpression compileRegex(const QString &pattern, QString *error)
{
    QRegularExpression re(pattern, QRegularExpression::UseUnicodePropertiesOption
                                       | QRegularExpression::MultilineOption);
    if (!re.isValid()) {
        if (error) *error = re.errorString();
        return QRegularExpression();
    }
    return re;
}

QList<RegexHit> regexHits(const QString &text, const QRegularExpression &re, int max_hits)
{
    QList<RegexHit> hits;
    if (!re.isValid() || text.isEmpty()) return hits;
    const int limit = qBound(1, max_hits, 500);
    auto it = re.globalMatch(text);
    while (it.hasNext() && hits.size() < limit) {
        const QRegularExpressionMatch match = it.next();
        RegexHit hit;
        hit.offset = match.capturedStart();
        hit.length = match.capturedLength();
        hit.match = match.captured();
        hit.line = text.left(hit.offset).count(QLatin1Char('\n')) + 1;
        for (int i = 1; i <= match.lastCapturedIndex(); ++i) hit.captures.append(match.captured(i));
        hits.append(hit);
    }
    return hits;
}

QJsonArray regexHitsJson(const QList<RegexHit> &hits, const QString &resource_id, const QString &book_path)
{
    QJsonArray array;
    for (const RegexHit &hit : hits) {
        QJsonArray captures;
        for (const QString &capture : hit.captures) captures.append(capture);
        QJsonObject object {
            { QStringLiteral("resource_id"), resource_id },
            { QStringLiteral("book_path"), book_path },
            { QStringLiteral("offset"), hit.offset },
            { QStringLiteral("length"), hit.length },
            { QStringLiteral("line"), hit.line },
            { QStringLiteral("match"), hit.match.left(240) },
            { QStringLiteral("captures"), captures }
        };
        array.append(object);
    }
    return array;
}

QString regexReplaceText(const QString &text, const QRegularExpression &re,
                         const QString &replacement, int max_replacements, int *count)
{
    if (count) *count = 0;
    if (!re.isValid()) return text;
    const int limit = max_replacements <= 0 ? 100000 : max_replacements;
    const QList<RegexHit> hits = regexHits(text, re, limit);
    if (hits.isEmpty()) return text;
    QString out = text;
    for (int i = hits.size() - 1; i >= 0; --i) {
        const RegexHit &hit = hits.at(i);
        QString replaced = replacement;
        for (int c = hit.captures.size(); c >= 1; --c) {
            replaced.replace(QStringLiteral("$%1").arg(c), hit.captures.at(c - 1));
        }
        replaced.replace(QStringLiteral("$0"), hit.match);
        out.replace(hit.offset, hit.length, replaced);
    }
    if (count) *count = hits.size();
    return out;
}

QString wrapMatchesText(const QString &text, const QRegularExpression &re,
                        const QString &open, const QString &close, int max_replacements, int *count)
{
    if (count) *count = 0;
    if (!re.isValid()) return text;
    const int limit = max_replacements <= 0 ? 100000 : max_replacements;
    const QList<RegexHit> hits = regexHits(text, re, limit);
    QString out = text;
    for (int i = hits.size() - 1; i >= 0; --i) {
        const RegexHit &hit = hits.at(i);
        out.replace(hit.offset, hit.length, open + hit.match + close);
    }
    if (count) *count = hits.size();
    return out;
}

QString insertAroundAnchor(const QString &text, const QString &anchor,
                           const QString &insert, bool before, QString *error)
{
    if (anchor.isEmpty()) {
        if (error) *error = QStringLiteral("anchor text is required");
        return text;
    }
    const int first = text.indexOf(anchor);
    if (first < 0) {
        if (error) *error = QStringLiteral("anchor not found");
        return text;
    }
    if (text.indexOf(anchor, first + qMax(1, anchor.size())) >= 0) {
        if (error) *error = QStringLiteral("anchor is not unique");
        return text;
    }
    if (before) return text.left(first) + insert + text.mid(first);
    return text.left(first + anchor.size()) + insert + text.mid(first + anchor.size());
}

QList<SplitPiece> splitByHeadingRegex(const QString &xhtml, const QRegularExpression &heading_re)
{
    QList<SplitPiece> pieces;
    const QString inner = extractBodyInner(xhtml);
    if (!heading_re.isValid()) {
        SplitPiece piece;
        piece.inner = inner;
        pieces.append(piece);
        return pieces;
    }
    const QList<RegexHit> hits = regexHits(inner, heading_re, 500);
    if (hits.isEmpty()) {
        SplitPiece piece;
        piece.inner = inner;
        pieces.append(piece);
        return pieces;
    }
    if (hits.first().offset > 0) {
        SplitPiece lead;
        lead.inner = inner.left(hits.first().offset);
        if (!lead.inner.trimmed().isEmpty()) pieces.append(lead);
    }
    for (int i = 0; i < hits.size(); ++i) {
        SplitPiece piece;
        piece.heading = hits.at(i).match;
        if (!hits.at(i).captures.isEmpty()) piece.heading = hits.at(i).captures.first();
        const int start = hits.at(i).offset;
        const int end = (i + 1 < hits.size()) ? hits.at(i + 1).offset : inner.size();
        piece.inner = inner.mid(start, end - start);
        pieces.append(piece);
    }
    return pieces;
}

QString mergeBodyInners(const QString &skeleton, const QStringList &inners)
{
    return replaceBodyInner(skeleton, inners.join(QStringLiteral("\n")));
}

namespace
{

QString substCaptures(QString templ, const QRegularExpressionMatch &match)
{
    for (int i = match.lastCapturedIndex(); i >= 1; --i) {
        templ.replace(QStringLiteral("$%1").arg(i), xmlEscape(match.captured(i)));
    }
    templ.replace(QStringLiteral("$0"), xmlEscape(match.captured()));
    return templ;
}

} // namespace

QString wrapPlainText(const QString &plain, const QJsonObject &rules)
{
    const QString para_open = rules.value(QStringLiteral("paragraph_open")).toString(QStringLiteral("<p>"));
    const QString para_close = rules.value(QStringLiteral("paragraph_close")).toString(QStringLiteral("</p>"));
    const QString heading_open = rules.value(QStringLiteral("heading_open")).toString(QStringLiteral("<h1>"));
    const QString heading_close = rules.value(QStringLiteral("heading_close")).toString(QStringLiteral("</h1>"));
    const QString heading_pattern = rules.value(QStringLiteral("heading_pattern")).toString();
    const QString illustration_pattern = rules.value(QStringLiteral("illustration_pattern")).toString();
    const QString illustration_html = rules.value(QStringLiteral("illustration_html")).toString();
    const bool keep_blank = rules.value(QStringLiteral("keep_blank")).toBool(true);

    QRegularExpression heading_re;
    QRegularExpression illustration_re;
    if (!heading_pattern.isEmpty()) {
        heading_re = QRegularExpression(heading_pattern, QRegularExpression::UseUnicodePropertiesOption);
    }
    if (!illustration_pattern.isEmpty()) {
        illustration_re = QRegularExpression(illustration_pattern, QRegularExpression::UseUnicodePropertiesOption);
    }

    QString html = QStringLiteral("\n");
    const QStringList lines = QString(plain).replace(QStringLiteral("\r\n"), QStringLiteral("\n"))
                                  .replace(QLatin1Char('\r'), QLatin1Char('\n'))
                                  .split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty()) {
            if (keep_blank) html += QStringLiteral("    <p><br/></p>\n");
            continue;
        }
        if (illustration_re.isValid() && !illustration_pattern.isEmpty()) {
            const QRegularExpressionMatch match = illustration_re.match(line);
            if (match.hasMatch() && match.capturedStart() == 0
                && match.capturedLength() >= line.size() - 2) {
                QString block = illustration_html;
                if (block.isEmpty()) {
                    const QString name = match.lastCapturedIndex() >= 1 ? match.captured(1) : match.captured();
                    block = QStringLiteral("<div class=\"illus\"><img alt=\"%1\" src=\"../Images/%1.jpg\"/></div>")
                                .arg(xmlEscape(name));
                } else {
                    block = substCaptures(block, match);
                }
                html += QStringLiteral("    %1\n").arg(block);
                continue;
            }
        }
        if (heading_re.isValid() && !heading_pattern.isEmpty() && heading_re.match(line).hasMatch()) {
            html += QStringLiteral("    %1%2%3\n").arg(heading_open, xmlEscape(line), heading_close);
            continue;
        }
        html += QStringLiteral("    %1%2%3\n").arg(para_open, xmlEscape(line), para_close);
    }
    return html;
}

QJsonArray headingsInXhtml(const QString &xhtml, const QRegularExpression &re, int default_level)
{
    QJsonArray array;
    const QList<RegexHit> hits = regexHits(xhtml, re, 500);
    for (const RegexHit &hit : hits) {
        QString label = hit.captures.isEmpty() ? hit.match : hit.captures.first();
        label.replace(QRegularExpression(QStringLiteral("<[^>]+>")), QString());
        label = label.simplified();
        int level = default_level;
        const QRegularExpression htag(QStringLiteral("<h([1-6])\\b"), QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch tag = htag.match(hit.match);
        if (tag.hasMatch()) level = tag.captured(1).toInt();
        array.append(QJsonObject {
            { QStringLiteral("label"), label },
            { QStringLiteral("level"), level },
            { QStringLiteral("offset"), hit.offset }
        });
    }
    return array;
}

QString ncxFromEntries(const QJsonArray &entries, const QString &title)
{
    QString body;
    int order = 1;
    for (const QJsonValue &value : entries) {
        const QJsonObject object = value.toObject();
        const QString label = object.value(QStringLiteral("label")).toString();
        QString href = object.value(QStringLiteral("href")).toString();
        if (href.isEmpty()) href = object.value(QStringLiteral("book_path")).toString();
        if (href.startsWith(QLatin1String("OEBPS/"))) href = href.mid(6);
        body += QStringLiteral(
            "    <navPoint id=\"navPoint-%1\" playOrder=\"%1\">\n"
            "      <navLabel><text>%2</text></navLabel>\n"
            "      <content src=\"%3\"/>\n"
            "    </navPoint>\n")
                    .arg(order)
                    .arg(xmlEscape(label.isEmpty() ? QStringLiteral("Untitled") : label),
                         xmlEscape(href));
        ++order;
    }
    if (body.isEmpty()) {
        body = QStringLiteral(
            "    <navPoint id=\"navPoint-1\" playOrder=\"1\">\n"
            "      <navLabel><text>Start</text></navLabel>\n"
            "      <content src=\"Text/nav.xhtml\"/>\n"
            "    </navPoint>\n");
    }
    return QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<ncx xmlns=\"http://www.daisy.org/z3986/2005/ncx/\" version=\"2005-1\">\n"
        "  <head>\n"
        "    <meta name=\"dtb:uid\" content=\"urn:uuid:sigil-agent\"/>\n"
        "    <meta name=\"dtb:depth\" content=\"1\"/>\n"
        "    <meta name=\"dtb:totalPageCount\" content=\"0\"/>\n"
        "    <meta name=\"dtb:maxPageNumber\" content=\"0\"/>\n"
        "  </head>\n"
        "  <docTitle><text>%1</text></docTitle>\n"
        "  <navMap>\n%2  </navMap>\n"
        "</ncx>\n")
        .arg(xmlEscape(title.isEmpty() ? QStringLiteral("Untitled") : title), body);
}

QJsonArray brokenImageRefs(const QString &xhtml, const QStringList &image_names)
{
    QSet<QString> names;
    for (const QString &name : image_names) names.insert(QFileInfo(name).fileName().toLower());
    QJsonArray missing;
    QRegularExpression img(QStringLiteral("(?:src|xlink:href)=\"([^\"]+)\""),
                           QRegularExpression::CaseInsensitiveOption);
    auto it = img.globalMatch(xhtml);
    QSet<QString> seen;
    while (it.hasNext()) {
        const QString href = it.next().captured(1);
        const QString file = QFileInfo(href).fileName().toLower();
        if (file.isEmpty() || seen.contains(file)) continue;
        seen.insert(file);
        if (!names.contains(file)) {
            missing.append(QJsonObject {
                { QStringLiteral("href"), href },
                { QStringLiteral("file"), QFileInfo(href).fileName() }
            });
        }
    }
    return missing;
}

} // namespace SigilAgent
