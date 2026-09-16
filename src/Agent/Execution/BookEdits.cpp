/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/Execution/BookEdits.h"

#include <QCollator>
#include <QFileInfo>
#include <QJsonArray>
#include <QSet>

#include "Agent/Execution/ContentOps.h"
#include "Agent/Typeset/ManuscriptParser.h"

namespace SigilAgent
{

namespace
{

QJsonObject resourceById(IBookWorkspace *workspace, const QString &id)
{
    for (const QJsonValue &value : workspace->resources()) {
        const QJsonObject object = value.toObject();
        if (object.value(QStringLiteral("resource_id")).toString() == id
            || object.value(QStringLiteral("book_path")).toString() == id) {
            return object;
        }
    }
    return QJsonObject();
}

QStringList imageFileNames(IBookWorkspace *workspace)
{
    QStringList names;
    for (const QJsonValue &value : workspace->resources()) {
        const QJsonObject object = value.toObject();
        if (object.value(QStringLiteral("kind")).toString() == QLatin1String("image")) {
            names.append(object.value(QStringLiteral("book_path")).toString());
        }
    }
    return names;
}

} // namespace

BookOpResult stageWorkingReplace(IBookWorkspace *workspace, const QString &resource_id, const QString &text)
{
    quint64 revision = workspace->resourceRevision(resource_id);
    if (revision == 0) revision = 1;
    return workspace->replaceText(resource_id, text, revision);
}

BookOpResult replaceBody(IBookWorkspace *workspace, const QString &resource_id,
                         const QString &inner, const QString &source_id)
{
    QString body = inner;
    if (!source_id.isEmpty()) {
        const QString source = workspace->workingText(source_id);
        if (source.isEmpty()) {
            return BookOpResult::error(QStringLiteral("SOURCE_EMPTY"),
                                       QStringLiteral("source_resource_id has no text"));
        }
        body = source.contains(QLatin1String("<body")) ? extractBodyInner(source) : source;
    }
    const QString current = workspace->workingText(resource_id);
    if (current.isEmpty()) {
        return BookOpResult::error(QStringLiteral("RESOURCE_NOT_FOUND"), QStringLiteral("Unknown resource"));
    }
    return stageWorkingReplace(workspace, resource_id, replaceBodyInner(current, body));
}

BookOpResult insertHtml(IBookWorkspace *workspace, const QString &resource_id,
                        const QString &anchor, const QString &html, bool before)
{
    const QString current = workspace->workingText(resource_id);
    if (current.isEmpty()) {
        return BookOpResult::error(QStringLiteral("RESOURCE_NOT_FOUND"), QStringLiteral("Unknown resource"));
    }
    QString error;
    const QString next = insertAroundAnchor(current, anchor, html, before, &error);
    if (!error.isEmpty()) {
        const QString code = error.contains(QLatin1String("unique"))
            ? QStringLiteral("ANCHOR_AMBIGUOUS") : QStringLiteral("ANCHOR_NOT_FOUND");
        return BookOpResult::error(code, error);
    }
    BookOpResult result = stageWorkingReplace(workspace, resource_id, next);
    if (result.ok) result.data.insert(QStringLiteral("inserted_chars"), html.size());
    return result;
}

BookOpResult wrapInResource(IBookWorkspace *workspace, const QString &resource_id,
                            const QString &pattern, const QString &open, const QString &close, int max_hits)
{
    QString error;
    const QRegularExpression re = compileRegex(pattern, &error);
    if (!re.isValid()) return BookOpResult::error(QStringLiteral("REGEX_INVALID"), error);
    QJsonArray changed;
    int total = 0;
    auto apply = [&](const QString &id) {
        const QString current = workspace->workingText(id);
        if (current.isEmpty()) return;
        int count = 0;
        const QString next = wrapMatchesText(current, re, open, close, max_hits, &count);
        if (count == 0) return;
        const BookOpResult replaced = stageWorkingReplace(workspace, id, next);
        if (replaced.ok) {
            changed.append(QJsonObject {
                { QStringLiteral("resource_id"), id },
                { QStringLiteral("count"), count }
            });
            total += count;
        }
    };
    if (!resource_id.isEmpty()) apply(resource_id);
    else {
        for (const QJsonValue &value : workspace->resources()) {
            const QJsonObject object = value.toObject();
            const QString kind = object.value(QStringLiteral("kind")).toString();
            if (kind == QLatin1String("xhtml") || kind == QLatin1String("css") || kind == QLatin1String("text")) {
                apply(object.value(QStringLiteral("resource_id")).toString());
            }
        }
    }
    return BookOpResult::success(QJsonObject {
        { QStringLiteral("staged"), true },
        { QStringLiteral("match_count"), total },
        { QStringLiteral("changed"), changed }
    }, false, true);
}

BookOpResult regexReplaceInBook(IBookWorkspace *workspace, const QString &pattern,
                                const QString &replacement, const QString &resource_id, int max_hits)
{
    QString error;
    const QRegularExpression re = compileRegex(pattern, &error);
    if (!re.isValid()) return BookOpResult::error(QStringLiteral("REGEX_INVALID"), error);
    QJsonArray changed;
    int total = 0;
    auto apply = [&](const QString &id) {
        const QString current = workspace->workingText(id);
        if (current.isEmpty()) return;
        int count = 0;
        const QString next = regexReplaceText(current, re, replacement, max_hits, &count);
        if (count == 0) return;
        if (stageWorkingReplace(workspace, id, next).ok) {
            changed.append(QJsonObject {
                { QStringLiteral("resource_id"), id },
                { QStringLiteral("count"), count }
            });
            total += count;
        }
    };
    if (!resource_id.isEmpty()) apply(resource_id);
    else {
        for (const QJsonValue &value : workspace->resources()) {
            const QJsonObject object = value.toObject();
            const QString kind = object.value(QStringLiteral("kind")).toString();
            if (kind == QLatin1String("xhtml") || kind == QLatin1String("css") || kind == QLatin1String("text")) {
                apply(object.value(QStringLiteral("resource_id")).toString());
            }
        }
    }
    return BookOpResult::success(QJsonObject {
        { QStringLiteral("staged"), true },
        { QStringLiteral("match_count"), total },
        { QStringLiteral("changed"), changed }
    }, false, true);
}

QJsonObject regexSearchInBook(IBookWorkspace *workspace, const QString &pattern,
                              const QString &resource_id, int max_hits)
{
    QString error;
    const QRegularExpression re = compileRegex(pattern, &error);
    if (!re.isValid()) {
        return QJsonObject {
            { QStringLiteral("ok"), false },
            { QStringLiteral("code"), QStringLiteral("REGEX_INVALID") },
            { QStringLiteral("message"), error }
        };
    }
    const int limit = qBound(1, max_hits, MAX_REGEX_SEARCH_MATCHES);
    QJsonArray matches;
    auto search = [&](const QJsonObject &resource) {
        const QString id = resource.value(QStringLiteral("resource_id")).toString();
        const QString path = resource.value(QStringLiteral("book_path")).toString();
        const QString kind = resource.value(QStringLiteral("kind")).toString();
        if (kind == QLatin1String("image") || kind == QLatin1String("font")) return;
        const QList<RegexHit> hits = regexHits(workspace->workingText(id), re,
                                               limit - matches.size());
        const QJsonArray part = regexHitsJson(hits, id, path);
        for (const QJsonValue &value : part) matches.append(value);
    };
    if (!resource_id.isEmpty()) {
        search(resourceById(workspace, resource_id));
    } else {
        for (const QJsonValue &value : workspace->resources()) {
            if (matches.size() >= limit) break;
            search(value.toObject());
        }
    }
    return QJsonObject {
        { QStringLiteral("ok"), true },
        { QStringLiteral("matches"), matches },
        { QStringLiteral("match_count"), matches.size() },
        { QStringLiteral("max_matches"), limit },
        { QStringLiteral("match_limit_reached"), matches.size() >= limit }
    };
}

BookOpResult splitResourceByHeading(IBookWorkspace *workspace, const QString &resource_id,
                                    const QString &heading_pattern)
{
    QString error;
    const QString pattern = heading_pattern.isEmpty()
        ? QStringLiteral("<h[1-6]\\b[^>]*>[\\s\\S]*?</h[1-6]>") : heading_pattern;
    const QRegularExpression re = compileRegex(pattern, &error);
    if (!re.isValid()) return BookOpResult::error(QStringLiteral("REGEX_INVALID"), error);
    const QString current = workspace->workingText(resource_id);
    if (current.isEmpty()) {
        return BookOpResult::error(QStringLiteral("RESOURCE_NOT_FOUND"), QStringLiteral("Unknown resource"));
    }
    const QList<SplitPiece> pieces = splitByHeadingRegex(current, re);
    if (pieces.size() < 2) {
        return BookOpResult::error(QStringLiteral("NOTHING_TO_SPLIT"),
                                   QStringLiteral("Heading pattern matched fewer than two sections"));
    }
    const QJsonObject source = resourceById(workspace, resource_id);
    const BookOpResult first = stageWorkingReplace(
        workspace, resource_id, replaceBodyInner(current, pieces.first().inner));
    if (!first.ok) return first;
    if (!pieces.first().heading.isEmpty()) {
        stageWorkingReplace(workspace, resource_id,
                            setXhtmlTitle(workspace->workingText(resource_id), pieces.first().heading));
    }
    QJsonArray created;
    QString after = source.value(QStringLiteral("resource_id")).toString();
    if (after.isEmpty()) after = resource_id;
    for (int i = 1; i < pieces.size(); ++i) {
        const BookOpResult copy = workspace->copyResource(after, QString(), true);
        if (!copy.ok) return copy;
        const QString new_id = copy.data.value(QStringLiteral("resource_id")).toString();
        QString filled = replaceBodyInner(workspace->workingText(new_id), pieces.at(i).inner);
        if (!pieces.at(i).heading.isEmpty()) filled = setXhtmlTitle(filled, pieces.at(i).heading);
        const BookOpResult replaced = stageWorkingReplace(workspace, new_id, filled);
        if (!replaced.ok) return replaced;
        created.append(copy.data);
        after = new_id;
    }
    return BookOpResult::success(QJsonObject {
        { QStringLiteral("staged"), true },
        { QStringLiteral("source_id"), resource_id },
        { QStringLiteral("section_count"), pieces.size() },
        { QStringLiteral("created"), created }
    }, false, true);
}

BookOpResult mergeResources(IBookWorkspace *workspace, const QStringList &resource_ids, bool delete_sources)
{
    if (resource_ids.size() < 2) {
        return BookOpResult::error(QStringLiteral("NEED_TWO_RESOURCES"),
                                   QStringLiteral("merge needs at least two resource ids in order"));
    }
    QStringList inners;
    QString skeleton;
    for (const QString &id : resource_ids) {
        const QString text = workspace->workingText(id);
        if (text.isEmpty()) {
            return BookOpResult::error(QStringLiteral("RESOURCE_NOT_FOUND"),
                                       QStringLiteral("Unknown resource %1").arg(id));
        }
        if (skeleton.isEmpty()) skeleton = text;
        inners.append(extractBodyInner(text));
    }
    const BookOpResult replaced = stageWorkingReplace(
        workspace, resource_ids.first(), mergeBodyInners(skeleton, inners));
    if (!replaced.ok) return replaced;
    QJsonArray removed;
    if (delete_sources) {
        for (int i = 1; i < resource_ids.size(); ++i) {
            const BookOpResult deleted = workspace->deleteResource(resource_ids.at(i));
            if (!deleted.ok) return deleted;
            removed.append(resource_ids.at(i));
        }
    }
    return BookOpResult::success(QJsonObject {
        { QStringLiteral("staged"), true },
        { QStringLiteral("target_id"), resource_ids.first() },
        { QStringLiteral("merged_count"), resource_ids.size() },
        { QStringLiteral("removed"), removed }
    }, false, true);
}

BookOpResult insertImageTag(IBookWorkspace *workspace, const QString &page_id,
                            const QString &image_id, const QString &anchor, bool before,
                            const QString &klass, const QString &alt)
{
    const QJsonObject page = resourceById(workspace, page_id);
    const QJsonObject image = resourceById(workspace, image_id);
    if (page.isEmpty()) {
        return BookOpResult::error(QStringLiteral("RESOURCE_NOT_FOUND"), QStringLiteral("Unknown page"));
    }
    if (image.isEmpty() || image.value(QStringLiteral("kind")).toString() != QLatin1String("image")) {
        return BookOpResult::error(QStringLiteral("IMAGE_NOT_FOUND"),
                                   QStringLiteral("image_id must refer to an image already in the book"));
    }
    const QString href = relativeBookHref(page.value(QStringLiteral("book_path")).toString(),
                                          image.value(QStringLiteral("book_path")).toString());
    const QString name = alt.isEmpty()
        ? QFileInfo(image.value(QStringLiteral("book_path")).toString()).completeBaseName() : alt;
    const QString class_attr = klass.isEmpty()
        ? QString() : QStringLiteral(" class=\"%1\"").arg(xmlEscape(klass));
    const QString wrapper = klass.isEmpty()
        ? QStringLiteral("<div class=\"illus\"><img alt=\"%1\" src=\"%2\"/></div>")
              .arg(xmlEscape(name), xmlEscape(href))
        : QStringLiteral("<div%1><img alt=\"%2\" src=\"%3\"/></div>")
              .arg(class_attr, xmlEscape(name), xmlEscape(href));
    return insertHtml(workspace, page.value(QStringLiteral("resource_id")).toString(),
                      anchor, wrapper, before);
}

BookOpResult wrapPlainResource(IBookWorkspace *workspace, const QString &source_id,
                               const QString &target_id, const QJsonObject &rules)
{
    const QString source = workspace->workingText(source_id);
    if (source.isEmpty()) {
        return BookOpResult::error(QStringLiteral("SOURCE_EMPTY"),
                                   QStringLiteral("source_resource_id has no text"));
    }
    const QString inner = wrapPlainText(manuscriptPlainText(source), rules);
    const QString dest = target_id.isEmpty() ? source_id : target_id;
    const QString current = workspace->workingText(dest);
    if (current.isEmpty()) {
        return BookOpResult::error(QStringLiteral("RESOURCE_NOT_FOUND"), QStringLiteral("Unknown target"));
    }
    QString next = current.contains(QLatin1String("<body")) ? replaceBodyInner(current, inner) : inner;
    const QString title = rules.value(QStringLiteral("title")).toString();
    if (!title.isEmpty()) next = setXhtmlTitle(next, title);
    BookOpResult result = stageWorkingReplace(workspace, dest, next);
    if (result.ok) {
        result.data.insert(QStringLiteral("source_id"), source_id);
        result.data.insert(QStringLiteral("target_id"), dest);
        result.data.insert(QStringLiteral("inner_chars"), inner.size());
    }
    return result;
}

BookOpResult generateTocFromHeadings(IBookWorkspace *workspace, const QString &heading_pattern)
{
    QString error;
    const QString pattern = heading_pattern.isEmpty()
        ? QStringLiteral("<h([1-6])\\b[^>]*>([\\s\\S]*?)</h\\1>") : heading_pattern;
    const QRegularExpression re = compileRegex(pattern, &error);
    if (!re.isValid()) return BookOpResult::error(QStringLiteral("REGEX_INVALID"), error);
    QJsonArray entries;
    for (const QJsonValue &value : workspace->spine()) {
        const QJsonObject item = value.toObject();
        const QString id = item.value(QStringLiteral("resource_id")).toString();
        const QString path = item.value(QStringLiteral("book_path")).toString();
        const QJsonArray heads = headingsInXhtml(workspace->workingText(id), re, 1);
        for (const QJsonValue &head : heads) {
            QJsonObject entry = head.toObject();
            entry.insert(QStringLiteral("resource_id"), id);
            entry.insert(QStringLiteral("book_path"), path);
            entry.insert(QStringLiteral("href"), path);
            entries.append(entry);
        }
    }
    return workspace->updateToc(entries);
}

BookOpResult sortSpine(IBookWorkspace *workspace)
{
    QList<QPair<QString, QString>> items;
    for (const QJsonValue &value : workspace->spine()) {
        const QJsonObject object = value.toObject();
        items.append({ object.value(QStringLiteral("resource_id")).toString(),
                       object.value(QStringLiteral("book_path")).toString() });
    }
    QCollator collator;
    collator.setNumericMode(true);
    std::sort(items.begin(), items.end(), [&](const auto &left, const auto &right) {
        return collator.compare(left.second, right.second) < 0;
    });
    QStringList ids;
    for (const auto &item : items) ids.append(item.first);
    return workspace->updateSpine(ids);
}

BookOpResult linkStylesheets(IBookWorkspace *workspace, const QStringList &html_ids,
                             const QStringList &css_ids)
{
    QList<QJsonObject> pages;
    if (html_ids.isEmpty()) {
        for (const QJsonValue &value : workspace->resources()) {
            const QJsonObject object = value.toObject();
            if (object.value(QStringLiteral("kind")).toString() == QLatin1String("xhtml")) {
                pages.append(object);
            }
        }
    } else {
        for (const QString &id : html_ids) {
            const QJsonObject object = resourceById(workspace, id);
            if (object.isEmpty()) {
                return BookOpResult::error(QStringLiteral("RESOURCE_NOT_FOUND"),
                                           QStringLiteral("Unknown HTML resource %1").arg(id));
            }
            pages.append(object);
        }
    }
    QList<QJsonObject> sheets;
    for (const QString &id : css_ids) {
        const QJsonObject object = resourceById(workspace, id);
        if (object.isEmpty() || object.value(QStringLiteral("kind")).toString() != QLatin1String("css")) {
            return BookOpResult::error(QStringLiteral("CSS_NOT_FOUND"),
                                       QStringLiteral("Unknown CSS resource %1").arg(id));
        }
        sheets.append(object);
    }
    int changed = 0;
    for (const QJsonObject &page : pages) {
        QStringList hrefs;
        const QString page_path = page.value(QStringLiteral("book_path")).toString();
        for (const QJsonObject &sheet : sheets) {
            hrefs.append(relativeBookHref(page_path, sheet.value(QStringLiteral("book_path")).toString()));
        }
        const QString id = page.value(QStringLiteral("resource_id")).toString();
        const QString current = workspace->workingText(id);
        const QString next = replaceStylesheetLinks(current, hrefs);
        if (next == current) continue;
        const BookOpResult replaced = stageWorkingReplace(workspace, id, next);
        if (!replaced.ok) return replaced;
        ++changed;
    }
    return BookOpResult::success(QJsonObject {
        { QStringLiteral("staged"), true },
        { QStringLiteral("changed"), changed },
        { QStringLiteral("stylesheet_count"), sheets.size() }
    }, false, true);
}

QJsonObject inspectBook(IBookWorkspace *workspace)
{
    QJsonObject report = workspace->validate();
    QJsonArray unused_images;
    QSet<QString> used;
    const QStringList images = imageFileNames(workspace);
    for (const QJsonValue &value : workspace->resources()) {
        const QJsonObject object = value.toObject();
        if (object.value(QStringLiteral("kind")).toString() != QLatin1String("xhtml")) continue;
        QRegularExpression img(QStringLiteral("(?:src|xlink:href)=\"([^\"]+)\""),
                               QRegularExpression::CaseInsensitiveOption);
        auto it = img.globalMatch(workspace->workingText(object.value(QStringLiteral("resource_id")).toString()));
        while (it.hasNext()) used.insert(QFileInfo(it.next().captured(1)).fileName().toLower());
    }
    for (const QString &path : images) {
        const QString file = QFileInfo(path).fileName().toLower();
        if (!used.contains(file)) unused_images.append(path);
    }
    report.insert(QStringLiteral("unused_images"), unused_images);
    report.insert(QStringLiteral("image_count"), images.size());
    report.insert(QStringLiteral("spine_count"), workspace->spine().size());
    QJsonArray wellformed;
    for (const QJsonValue &value : workspace->resources()) {
        const QJsonObject object = value.toObject();
        const QString kind = object.value(QStringLiteral("kind")).toString();
        if (kind != QLatin1String("xhtml")) continue;
        const QString id = object.value(QStringLiteral("resource_id")).toString();
        QJsonObject check = wellformedReport(workspace->workingText(id), kind);
        check.insert(QStringLiteral("resource_id"), id);
        check.insert(QStringLiteral("book_path"), object.value(QStringLiteral("book_path")).toString());
        wellformed.append(check);
    }
    report.insert(QStringLiteral("wellformed"), wellformed);
    return report;
}

} // namespace SigilAgent
