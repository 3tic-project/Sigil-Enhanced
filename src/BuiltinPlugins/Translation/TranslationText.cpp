#include "BuiltinPlugins/Translation/TranslationText.h"

#include <algorithm>
#include <memory>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QSet>

#include "BookManipulation/XmlWellFormed.h"
#include "gumbo.h"

namespace BuiltinPlugins::Translation {
namespace {

QString tag(const GumboNode *node)
{
    if (!node || node->type != GUMBO_NODE_ELEMENT) return {};
    GumboStringPiece name = node->v.element.original_tag;
    gumbo_tag_from_original_text(&name);
    QString value = QString::fromUtf8(name.data, name.length).toLower();
    if (value.isEmpty()) value = QString::fromLatin1(gumbo_normalized_tagname(node->v.element.tag));
    return value.section(QLatin1Char(':'), -1);
}

QString attribute(const GumboNode *node, const char *name)
{
    if (const auto *attr = gumbo_get_attribute(&node->v.element.attributes, name)) {
        return QString::fromUtf8(attr->value);
    }
    return {};
}

bool generated(const GumboNode *node)
{
    if (!node || node->type != GUMBO_NODE_ELEMENT) return false;
    const QStringList classes = attribute(node, "class").split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &name : classes) {
        if (name == QLatin1String("se-translation") || name == QLatin1String("orion-translation")
            || name == QLatin1String("vela-translation") || name == QLatin1String("orion-toc-translation")
            || name == QLatin1String("vela-toc-translation")) return true;
    }
    return false;
}

bool ignored(const GumboNode *node)
{
    const QString name = tag(node);
    static const QSet<QString> skip {"head", "script", "style", "template", "rt", "rp", "rtc", "svg", "math"};
    if (skip.contains(name) || generated(node)) return true;
    if (node->type != GUMBO_NODE_ELEMENT) return false;
    if (gumbo_get_attribute(&node->v.element.attributes, "hidden")) return true;
    static const QRegularExpression invisible(QStringLiteral("(?:^|;)\\s*(?:display\\s*:\\s*none|visibility\\s*:\\s*hidden)\\b"),
                                              QRegularExpression::CaseInsensitiveOption);
    return invisible.match(attribute(node, "style")).hasMatch();
}

bool block(const GumboNode *node)
{
    static const QSet<QString> names {"body", "p", "div", "section", "article", "aside", "nav", "li",
        "ul", "ol", "dl", "dt", "dd", "blockquote", "td", "th", "tr", "table", "pre",
        "h1", "h2", "h3", "h4", "h5", "h6", "figure", "figcaption"};
    return names.contains(tag(node));
}

bool hasBlock(const GumboNode *node)
{
    if (node->type != GUMBO_NODE_ELEMENT) return false;
    const auto &children = node->v.element.children;
    for (unsigned i = 0; i < children.length; ++i) {
        auto *child = static_cast<GumboNode *>(children.data[i]);
        if (ignored(child)) continue;
        if (block(child) || hasBlock(child)) return true;
    }
    return false;
}

struct Span { int start; int end; QString text; };

class Reader {
public:
    explicit Reader(const QByteArray &source) : bytes(source)
    {
        for (int i = 0; i < bytes.size(); ++i) if (bytes.at(i) == '\n') lineBreaks.append(i);
    }
    int offset(const char *p) const
    {
        return p && p >= bytes.constData() && p <= bytes.constData() + bytes.size()
            ? int(p - bytes.constData()) : -1;
    }
    int start(const GumboNode *node) const { return offset(node->v.element.original_tag.data); }
    int close(const GumboNode *node) const { return offset(node->v.element.original_end_tag.data); }
    int end(const GumboNode *node) const
    {
        const int value = close(node);
        return value < 0 ? -1 : value + int(node->v.element.original_end_tag.length);
    }
    QString plain(const GumboNode *node, int from = 0, int until = INT_MAX, bool includeReadings = false) const
    {
        if (node->type == GUMBO_NODE_TEXT || node->type == GUMBO_NODE_WHITESPACE || node->type == GUMBO_NODE_CDATA) {
            const int position = offset(node->v.text.original_text.data);
            return position >= from && position < until ? QString::fromUtf8(node->v.text.text) : QString();
        }
        if (node->type != GUMBO_NODE_ELEMENT || generated(node)) return {};
        if (!includeReadings && ignored(node)) return {};
        QString text;
        const auto &children = node->v.element.children;
        for (unsigned i = 0; i < children.length; ++i) {
            text += plain(static_cast<GumboNode *>(children.data[i]), from, until, includeReadings);
        }
        return text;
    }
    void collect(const GumboNode *node)
    {
        if (node->type != GUMBO_NODE_ELEMENT) return;
        if (generated(node)) {
            const int a = start(node), b = end(node);
            if (a >= 0 && b >= a) translations.append({a, b, plainGenerated(node)});
            return;
        }
        const auto &children = node->v.element.children;
        for (unsigned i = 0; i < children.length; ++i) collect(static_cast<GumboNode *>(children.data[i]));
    }
    QString plainGenerated(const GumboNode *node) const
    {
        QString text;
        const auto &children = node->v.element.children;
        for (unsigned i = 0; i < children.length; ++i) {
            text += plain(static_cast<GumboNode *>(children.data[i]), 0, INT_MAX, true);
        }
        return text.trimmed();
    }
    void breaks(const GumboNode *node, QList<Span> &result) const
    {
        if (node->type != GUMBO_NODE_ELEMENT || ignored(node)) return;
        if (tag(node) == QLatin1String("br")) {
            const int a = start(node);
            if (a >= 0) result.append({a, a + int(node->v.element.original_tag.length), {}});
            return;
        }
        const auto &children = node->v.element.children;
        for (unsigned i = 0; i < children.length; ++i) breaks(static_cast<GumboNode *>(children.data[i]), result);
    }
    QString readings(const GumboNode *node, int a, int b) const
    {
        if (node->type != GUMBO_NODE_ELEMENT || generated(node)) return {};
        if (tag(node) == QLatin1String("rt")) {
            return plain(node, a, b, true).trimmed() + QLatin1Char(' ');
        }
        QString text;
        const auto &children = node->v.element.children;
        for (unsigned i = 0; i < children.length; ++i) text += readings(static_cast<GumboNode *>(children.data[i]), a, b);
        return text;
    }
    void append(const GumboNode *node, int a, int b, int insertion, bool inlined, bool toc,
                const QList<const GumboNode *> &run = {})
    {
        if (a < 0 || b <= a || insertion < 0) return;
        Paragraph item;
        if (run.isEmpty()) { item.text = plain(node, a, b); item.readings = readings(node, a, b); }
        else for (const auto *part : run) { item.text += plain(part, a, b); item.readings += readings(part, a, b); }
        for (const QChar ch : item.text) {
            if (!ch.isSpace()) break;
            if (ch == QChar(0x3000) || ch == QChar(0x00a0)) item.leadingWhitespace += ch;
        }
        item.text = item.text.trimmed();
        if (item.text.isEmpty()) return;
        item.readings = item.readings.trimmed();
        item.start = a; item.end = b; item.insertAt = insertion;
        item.inlineTranslation = inlined; item.toc = toc;
        item.classes = attribute(node, "class"); item.style = attribute(node, "style");
        item.line = int(std::lower_bound(lineBreaks.begin(), lineBreaks.end(), a) - lineBreaks.begin()) + 1;
        QByteArray original = bytes.mid(a, b - a);
        for (auto it = translations.crbegin(); it != translations.crend(); ++it) {
            if (it->start >= a && it->end <= b) {
                original.remove(it->start - a, it->end - it->start);
                item.translationStart = it->start; item.translationEnd = it->end;
                item.translation = it->text;
            }
        }
        if (item.translationStart < 0 && !inlined) {
            for (const auto &candidate : translations) {
                if (candidate.start >= b && bytes.mid(b, candidate.start - b).trimmed().isEmpty()) {
                    item.translationStart = candidate.start; item.translationEnd = candidate.end;
                    item.translation = candidate.text;
                    break;
                }
            }
        }
        if (toc && item.translation.startsWith(QLatin1Char('/'))) item.translation = item.translation.mid(1).trimmed();
        item.sourceKey = QCryptographicHash::hash(original, QCryptographicHash::Sha256);
        paragraphs.append(item);
    }
    void visit(const GumboNode *node, bool toc = false)
    {
        if (node->type != GUMBO_NODE_ELEMENT || ignored(node)) return;
        const QString name = tag(node);
        toc = toc || name == QLatin1String("nav") || attribute(node, "epub:type").split(QLatin1Char(' ')).contains(QStringLiteral("toc"));
        if ((toc && name == QLatin1String("a")) || (block(node) && !hasBlock(node))) {
            // A TOC leaf can contain an anchor: keep its original href and markup.
            if (toc && name != QLatin1String("a")) {
                const auto &children = node->v.element.children;
                bool anchor = false;
                for (unsigned i = 0; i < children.length; ++i) {
                    auto *child = static_cast<GumboNode *>(children.data[i]);
                    if (tag(child) == QLatin1String("a")) { visit(child, true); anchor = true; }
                }
                if (anchor) return;
            }
            const int a = start(node), b = end(node), c = close(node);
            QList<Span> boundaries;
            breaks(node, boundaries);
            if (!boundaries.isEmpty()) {
                int begin = a + int(node->v.element.original_tag.length);
                for (const auto &boundary : boundaries) {
                    append(node, begin, boundary.start, boundary.start, true, false);
                    begin = boundary.end;
                }
                append(node, begin, c, c, true, false);
            } else {
                static const QSet<QString> inlineContainers {"a", "li", "dt", "dd", "td", "th", "body", "pre"};
                const bool inlined = inlineContainers.contains(name);
                append(node, a, b, inlined ? c : b, inlined, toc && name == QLatin1String("a"));
            }
            return;
        }
        const auto &children = node->v.element.children;
        if (block(node)) {
            // Mixed body/div flows (including spacer p) still contain source
            // runs. Work only on each run's nodes, avoiding whole-tree scans.
            QList<const GumboNode *> run;
            int begin = start(node) + int(node->v.element.original_tag.length);
            auto flush = [&](int until) {
                if (run.isEmpty()) return;
                QList<Span> boundaries;
                for (const auto *part : run) breaks(part, boundaries);
                for (const auto &boundary : boundaries) {
                    append(node, begin, boundary.start, boundary.start, true, false, run);
                    begin = boundary.end;
                }
                append(node, begin, until, until, true, false, run);
                run.clear();
            };
            for (unsigned i = 0; i < children.length; ++i) {
                const auto *child = static_cast<GumboNode *>(children.data[i]);
                if (!ignored(child) && (block(child) || hasBlock(child))) {
                    flush(start(child)); visit(child, toc); begin = end(child);
                } else run.append(child);
            }
            flush(close(node));
        } else {
            for (unsigned i = 0; i < children.length; ++i) visit(static_cast<GumboNode *>(children.data[i]), toc);
        }
    }
    const QByteArray &bytes;
    QList<Span> translations;
    QList<int> lineBreaks;
    QList<Paragraph> paragraphs;
};

QString translationNode(const Paragraph &item, const QString &value, const QString &language)
{
    const QString name = item.inlineTranslation ? QStringLiteral("span") : QStringLiteral("p");
    const QString classes = item.classes + QStringLiteral(" se-translation")
        + (item.toc ? QStringLiteral(" se-toc-translation") : QString());
    QString attributes = QStringLiteral(" class=\"%1\" lang=\"%2\" xml:lang=\"%2\"")
        .arg(escapeText(classes.trimmed()), escapeText(language));
    if (!item.style.isEmpty()) attributes += QStringLiteral(" style=\"%1\"").arg(escapeText(item.style));
    const QString translated = item.toc || value.startsWith(item.leadingWhitespace) ? value : item.leadingWhitespace + value;
    return QLatin1Char('<') + name + attributes + QLatin1Char('>')
        + (item.toc ? QStringLiteral(" / ") : QString()) + escapeText(translated)
        + QStringLiteral("</") + name + QLatin1Char('>');
}

} // namespace

QString escapeText(const QString &value)
{
    QString out = value;
    out.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    out.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    out.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    out.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    return out;
}

QString normalizedTranslation(const QString &source, QString text)
{
    text = text.trimmed();
    // Only normalize paired quote styles. Never delete kana or other content.
    if (source.count(QChar(0x300c)) == text.count(QChar(0x201c))
        && source.count(QChar(0x300d)) == text.count(QChar(0x201d))) {
        text.replace(QChar(0x201c), QChar(0x300c)).replace(QChar(0x201d), QChar(0x300d));
    }
    if (source.count(QChar(0x300e)) == text.count(QChar(0x2018))
        && source.count(QChar(0x300f)) == text.count(QChar(0x2019))) {
        text.replace(QChar(0x2018), QChar(0x300e)).replace(QChar(0x2019), QChar(0x300f));
    }
    return text;
}

TextResult extract(const QString &xhtml)
{
    TextResult result;
    const auto valid = XmlWellFormed::Check(xhtml);
    if (valid.line >= 0) { result.error = valid.message; return result; }
    const QByteArray bytes = xhtml.toUtf8();
    GumboOptions options = kGumboDefaultOptions;
    options.use_xhtml_rules = true;
    const std::unique_ptr<GumboOutput, decltype(&gumbo_destroy_output)> tree(
        gumbo_parse_with_options(&options, bytes.constData(), bytes.size()), gumbo_destroy_output);
    if (!tree || tree->status != GUMBO_STATUS_OK) { result.error = QStringLiteral("XHTML parser limit exceeded"); return result; }
    Reader reader(bytes);
    reader.collect(tree->root);
    reader.visit(tree->root);
    result.paragraphs = std::move(reader.paragraphs);
    return result;
}

WriteResult writePairs(const QString &xhtml, const QList<Replacement> &items, const QString &language, const TextResult *prepared)
{
    WriteResult result;
    result.text = xhtml;
    const TextResult current = prepared ? *prepared : extract(xhtml);
    if (!current.error.isEmpty()) { result.error = current.error; return result; }
    struct Edit { int start; int end; QByteArray bytes; };
    QList<Edit> edits;
    QSet<int> seen;
    for (const auto &item : items) {
        if (item.index < 0 || item.index >= current.paragraphs.size() || seen.contains(item.index)) {
            result.conflicts.append(item.index); continue;
        }
        seen.insert(item.index);
        const auto &paragraph = current.paragraphs.at(item.index);
        if (paragraph.sourceKey != item.expectedSource || paragraph.translation != item.expectedTranslation
            || item.text.trimmed().isEmpty() || item.text.contains(QChar(0))) {
            result.conflicts.append(item.index); continue;
        }
        const QByteArray node = translationNode(paragraph, normalizedTranslation(paragraph.text, item.text), language).toUtf8();
        const bool replacing = paragraph.translationStart >= 0;
        edits.append({replacing ? paragraph.translationStart : paragraph.insertAt,
                      replacing ? paragraph.translationEnd : paragraph.insertAt,
                      replacing || paragraph.inlineTranslation ? node : '\n' + node});
        result.applied.append(item.index);
    }
    if (edits.isEmpty()) return result;
    std::sort(edits.begin(), edits.end(), [](const Edit &a, const Edit &b) { return a.start > b.start; });
    QByteArray bytes = xhtml.toUtf8();
    int previous = bytes.size() + 1;
    for (const auto &edit : edits) {
        if (edit.end > previous) { result.error = QStringLiteral("Overlapping translation ranges"); result.applied.clear(); return result; }
        bytes.replace(edit.start, edit.end - edit.start, edit.bytes);
        previous = edit.start;
    }
    QString next = QString::fromUtf8(bytes);
    if (!next.contains(QStringLiteral("id=\"sigil-translation-style\""))) {
        static const QRegularExpression head(QStringLiteral("</(?:[A-Za-z0-9_-]+:)?head\\s*>"), QRegularExpression::CaseInsensitiveOption);
        const auto match = head.match(next);
        if (!match.hasMatch()) { result.error = QStringLiteral("XHTML head is missing"); result.applied.clear(); return result; }
        next.insert(match.capturedStart(), QStringLiteral(
            "<style type=\"text/css\" id=\"sigil-translation-style\">\n"
            ".se-translation { line-height: 1.65; margin-bottom: 1rem; }\n"
            "span.se-translation { display: block; }\n"
            "span.se-toc-translation { display: inline; margin: 0; }\n</style>\n"));
    }
    const auto valid = XmlWellFormed::Check(next);
    if (valid.line >= 0) { result.error = valid.message; result.applied.clear(); return result; }
    result.text = next;
    return result;
}

} // namespace BuiltinPlugins::Translation
