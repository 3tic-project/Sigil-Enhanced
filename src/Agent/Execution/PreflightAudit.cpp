/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/Execution/PreflightAudit.h"

#include <algorithm>

#include <QCryptographicHash>
#include <QHash>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <QUuid>
#include <QXmlStreamReader>

#include "Agent/Execution/BookEdits.h"
#include "Agent/Execution/IBookWorkspace.h"

namespace SigilAgent
{

namespace
{

constexpr int kDefaultIssuePage = 50;
constexpr int kMaxIssuePage = 200;
constexpr int kMaxEvidence = 160;

const QString kProfileCore = QStringLiteral("epub-core");
const QString kProfileA11y = QStringLiteral("a11y-basic");
const QString kProfileRelease = QStringLiteral("release-basic");

struct BookFile {
    QString id;
    QString path;
    QString kind;
    QString mediaType;
};

struct Issue {
    QString rule;
    QString severity;
    QString resourceId;
    QString bookPath;
    int line = 0;
    QString evidence;
    QString message;
    QString fixKind;
    bool manual = false;
    QJsonObject fix;
};

struct Reference {
    QString sourceId;
    QString sourcePath;
    int line = 0;
    QString raw;
    QString attribute;
};

struct XhtmlScan {
    bool wellFormed = true;
    QString error;
    int errorLine = 0;
    QString rootLang;
    bool hasRootLang = false;
    QSet<QString> ids;
    QList<Reference> links;
    QList<QPair<int, int>> headings;
    QString firstHeading;
    int firstHeadingLevel = 0;
    int firstHeadingLine = 0;
    QStringList epubTypes;
    int visibleChars = 0;
    int imageCount = 0;
    QString title;
    QList<Issue> local;
};

class EntityCollector final : public QXmlStreamEntityResolver
{
public:
    QString resolveUndeclaredEntity(const QString &name) override
    {
        if (!names.contains(name)) names.append(name);
        return QStringLiteral(" ");
    }
    QStringList names;
};

int severityRank(const QString &severity)
{
    if (severity == QLatin1String("error")) return 0;
    if (severity == QLatin1String("warning")) return 1;
    return 2;
}

// QString::simplified() also turns U+3000 into ASCII space, which rewrites CJK headings.
QString collapseAsciiWhitespace(QString text)
{
    static const QRegularExpression runs(QStringLiteral("[ \\t\\r\\n\\f\\v]+"));
    text.replace(runs, QStringLiteral(" "));
    return text.trimmed();
}

QString bounded(QString text)
{
    text = collapseAsciiWhitespace(text);
    return text.size() > kMaxEvidence ? text.left(kMaxEvidence) + QStringLiteral("…") : text;
}

QString directoryOf(const QString &path)
{
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    return slash < 0 ? QString() : path.left(slash);
}

QString normalizePath(const QString &path)
{
    QStringList out;
    for (const QString &part : path.split(QLatin1Char('/'))) {
        if (part.isEmpty() || part == QLatin1String(".")) continue;
        if (part == QLatin1String("..")) {
            if (out.isEmpty()) return QString();
            out.removeLast();
            continue;
        }
        out.append(part);
    }
    return out.join(QLatin1Char('/'));
}

bool isExternal(const QString &href)
{
    static const QRegularExpression scheme(QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]*:"));
    return scheme.match(href).hasMatch() || href.startsWith(QLatin1String("//"));
}

// Returns the resolved book path (without fragment) and the fragment, or an empty path for
// same-document references.
QPair<QString, QString> resolveHref(const QString &base_path, const QString &href)
{
    QString target = href.trimmed();
    QString fragment;
    const int hash = target.indexOf(QLatin1Char('#'));
    if (hash >= 0) {
        fragment = QUrl::fromPercentEncoding(target.mid(hash + 1).toUtf8());
        target = target.left(hash);
    }
    const int query = target.indexOf(QLatin1Char('?'));
    if (query >= 0) target = target.left(query);
    if (target.isEmpty()) return { QString(), fragment };
    target = QUrl::fromPercentEncoding(target.toUtf8());
    const QString dir = directoryOf(base_path);
    const QString joined = target.startsWith(QLatin1Char('/'))
        ? target.mid(1) : (dir.isEmpty() ? target : dir + QLatin1Char('/') + target);
    const QString normalized = normalizePath(joined);
    return { normalized.isEmpty() ? QStringLiteral("\x01invalid") : normalized, fragment };
}

int lineAt(const QString &text, int offset)
{
    return text.left(qMax(0, offset)).count(QLatin1Char('\n')) + 1;
}

QString attributeValue(const QXmlStreamAttributes &attributes, const QString &local)
{
    for (const QXmlStreamAttribute &attribute : attributes) {
        if (attribute.name() == local) return attribute.value().toString();
    }
    return QString();
}

bool hasAttribute(const QXmlStreamAttributes &attributes, const QString &local)
{
    for (const QXmlStreamAttribute &attribute : attributes) {
        if (attribute.name() == local) return true;
    }
    return false;
}

XhtmlScan scanXhtml(const BookFile &file, const QString &text, bool epub3)
{
    XhtmlScan scan;
    QXmlStreamReader reader(text);
    EntityCollector entities;
    reader.setEntityResolver(&entities);
    bool root = true;
    bool has_doctype = false;
    int in_body = 0;
    int heading_depth = 0;
    int title_depth = 0;
    int skip_depth = 0;
    QString heading_text;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.tokenType() == QXmlStreamReader::DTD) has_doctype = true;
        if (reader.isCharacters()) {
            const QString chunk = reader.text().toString();
            if (title_depth > 0) scan.title += chunk;
            if (in_body > 0 && skip_depth == 0) {
                for (const QChar ch : chunk) {
                    if (!ch.isSpace()) ++scan.visibleChars;
                }
                if (heading_depth > 0) heading_text += chunk;
            }
            continue;
        }
        if (reader.isEndElement()) {
            const QString end = reader.name().toString().toLower();
            if (end == QLatin1String("body")) in_body = 0;
            if (end == QLatin1String("title") && title_depth > 0) --title_depth;
            if ((end == QLatin1String("rt") || end == QLatin1String("rp") || end == QLatin1String("script")
                 || end == QLatin1String("style")) && skip_depth > 0) {
                --skip_depth;
            }
            if (heading_depth > 0 && end.size() == 2 && end.at(0) == QLatin1Char('h') && end.at(1).isDigit()) {
                --heading_depth;
                if (heading_depth == 0 && scan.firstHeading.isEmpty()) scan.firstHeading = collapseAsciiWhitespace(heading_text);
            }
            continue;
        }
        if (!reader.isStartElement()) continue;
        const QString name = reader.name().toString().toLower();
        const QXmlStreamAttributes attributes = reader.attributes();
        const int line = static_cast<int>(reader.lineNumber());
        if (name == QLatin1String("body")) in_body = 1;
        if (name == QLatin1String("title") && in_body == 0) ++title_depth;
        if (name == QLatin1String("rt") || name == QLatin1String("rp") || name == QLatin1String("script")
            || name == QLatin1String("style")) {
            ++skip_depth;
        }
        if (in_body > 0 && (name == QLatin1String("body") || name == QLatin1String("section")
                            || name == QLatin1String("nav") || name == QLatin1String("div"))) {
            for (const QXmlStreamAttribute &attribute : attributes) {
                if (attribute.name() == QLatin1String("type")) {
                    for (const QString &type : attribute.value().toString().split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
                        if (!scan.epubTypes.contains(type)) scan.epubTypes.append(type);
                    }
                }
            }
        }
        if (in_body > 0 && (name == QLatin1String("img") || name == QLatin1String("image"))) ++scan.imageCount;
        if (root) {
            root = false;
            const QString lang = attributeValue(attributes, QStringLiteral("lang"));
            scan.hasRootLang = hasAttribute(attributes, QStringLiteral("lang"));
            scan.rootLang = lang.trimmed();
        }
        const QString id = attributeValue(attributes, QStringLiteral("id"));
        if (!id.isEmpty()) scan.ids.insert(id);
        const QString anchor_name = attributeValue(attributes, QStringLiteral("name"));
        if (name == QLatin1String("a") && !anchor_name.isEmpty()) scan.ids.insert(anchor_name);
        auto add_link = [&](const QString &attribute) {
            const QString value = attributeValue(attributes, attribute);
            if (value.isEmpty()) return;
            scan.links.append({ file.id, file.path, line, value, attribute });
        };
        if (name == QLatin1String("a") || name == QLatin1String("link") || name == QLatin1String("area")) {
            add_link(QStringLiteral("href"));
        } else if (name == QLatin1String("img") || name == QLatin1String("source")
                   || name == QLatin1String("audio") || name == QLatin1String("video")
                   || name == QLatin1String("script")) {
            add_link(QStringLiteral("src"));
        } else if (name == QLatin1String("image") || name == QLatin1String("use")) {
            add_link(QStringLiteral("href"));
        }
        if (name == QLatin1String("img")) {
            const QString src = attributeValue(attributes, QStringLiteral("src"));
            if (!hasAttribute(attributes, QStringLiteral("alt"))) {
                scan.local.append({ QStringLiteral("IMG_ALT_MISSING"), QStringLiteral("warning"),
                                    file.id, file.path, line, bounded(src),
                                    QStringLiteral("Image has no alt attribute; decide whether it is decorative (alt=\"\") or needs a description."),
                                    QStringLiteral("manual"), true });
            } else {
                const QString alt = attributeValue(attributes, QStringLiteral("alt")).trimmed();
                const QString base = src.section(QLatin1Char('/'), -1).section(QLatin1Char('.'), 0, 0);
                if (!alt.isEmpty() && (alt.compare(base, Qt::CaseInsensitive) == 0
                                       || alt.compare(src.section(QLatin1Char('/'), -1), Qt::CaseInsensitive) == 0)) {
                    scan.local.append({ QStringLiteral("IMG_ALT_FILENAME"), QStringLiteral("info"),
                                        file.id, file.path, line, bounded(QStringLiteral("alt=\"%1\" src=\"%2\"").arg(alt, src)),
                                        QStringLiteral("alt repeats the file name; it does not describe the image. Mark decorative or write a description."),
                                        QStringLiteral("manual"), true });
                }
            }
        }
        if (name.size() == 2 && name.at(0) == QLatin1Char('h') && name.at(1) >= QLatin1Char('1')
            && name.at(1) <= QLatin1Char('6')) {
            scan.headings.append({ name.at(1).digitValue(), line });
            if (in_body > 0) {
                if (heading_depth == 0 && scan.firstHeading.isEmpty()) {
                    heading_text.clear();
                    scan.firstHeadingLevel = name.at(1).digitValue();
                    scan.firstHeadingLine = line;
                }
                ++heading_depth;
            }
        }
    }
    if (reader.hasError()) {
        scan.wellFormed = false;
        scan.error = reader.errorString();
        scan.errorLine = static_cast<int>(reader.lineNumber());
    }
    if (!entities.names.isEmpty()) {
        const bool fatal = epub3 || !has_doctype;
        scan.local.append({ QStringLiteral("XHTML_UNDECLARED_ENTITY"),
                            fatal ? QStringLiteral("error") : QStringLiteral("info"),
                            file.id, file.path, 0,
                            bounded(QStringLiteral("&") + entities.names.join(QStringLiteral("; &")) + QLatin1Char(';')),
                            fatal ? QStringLiteral("Named HTML entities are not declared in this XHTML; replace them with characters or numeric references.")
                                  : QStringLiteral("Named entities rely on the XHTML 1.1 DTD; numeric references are safer across readers."),
                            QStringLiteral("mechanical"), false });
    }
    return scan;
}

QJsonObject issueJson(const Issue &issue, int index)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QStringLiteral("%1\x1f%2\x1f%3\x1f%4").arg(issue.rule, issue.bookPath)
                     .arg(issue.line).arg(issue.evidence).toUtf8());
    QJsonObject object {
        { QStringLiteral("issue_id"), QStringLiteral("PF-%1").arg(index + 1) },
        { QStringLiteral("fingerprint"), QString::fromLatin1(hash.result().toHex().left(16)) },
        { QStringLiteral("rule_id"), issue.rule },
        { QStringLiteral("severity"), issue.severity },
        { QStringLiteral("message"), issue.message },
        { QStringLiteral("fix_kind"), issue.fixKind },
        { QStringLiteral("manual_required"), issue.manual }
    };
    if (!issue.resourceId.isEmpty()) object.insert(QStringLiteral("resource_id"), issue.resourceId);
    if (!issue.bookPath.isEmpty()) object.insert(QStringLiteral("book_path"), issue.bookPath);
    if (issue.line > 0) object.insert(QStringLiteral("line"), issue.line);
    if (!issue.evidence.isEmpty()) object.insert(QStringLiteral("evidence"), issue.evidence);
    if (!issue.fix.isEmpty()) object.insert(QStringLiteral("fix"), issue.fix);
    return object;
}

QStringList profileRules(const QString &profile)
{
    static const QStringList core = {
        QStringLiteral("SPINE_EMPTY"), QStringLiteral("SPINE_ITEM_MISSING"),
        QStringLiteral("XHTML_NOT_WELLFORMED"), QStringLiteral("XHTML_UNDECLARED_ENTITY"),
        QStringLiteral("LINK_TARGET_MISSING"), QStringLiteral("LINK_PATH_CASE_MISMATCH"),
        QStringLiteral("LINK_FRAGMENT_MISSING"), QStringLiteral("CSS_URL_MISSING"),
        QStringLiteral("TOC_TARGET_MISSING"), QStringLiteral("MANIFEST_HREF_MISSING"),
        QStringLiteral("NAV_MISSING"), QStringLiteral("METADATA_REQUIRED_MISSING"),
        QStringLiteral("RESOURCE_UNREFERENCED")
    };
    static const QStringList a11y = {
        QStringLiteral("METADATA_LANGUAGE_MISSING"), QStringLiteral("XHTML_LANG_MISSING"),
        QStringLiteral("XHTML_LANG_MISMATCH"), QStringLiteral("IMG_ALT_MISSING"),
        QStringLiteral("IMG_ALT_FILENAME"), QStringLiteral("HEADING_LEVEL_SKIP")
    };
    if (profile == kProfileCore) return core;
    if (profile == kProfileA11y) return a11y;
    return core + a11y;
}

QString primaryLanguage(const QString &tag)
{
    return tag.section(QLatin1Char('-'), 0, 0).section(QLatin1Char('_'), 0, 0).toLower();
}

} // namespace

QJsonObject inspectStructure(IBookWorkspace *workspace)
{
    QHash<QString, QJsonObject> by_path;
    QHash<QString, QString> kind_by_path;
    QString nav_base;
    QString opf_path;
    for (const QJsonValue &value : workspace->resources()) {
        const QJsonObject object = value.toObject();
        const QString path = object.value(QStringLiteral("book_path")).toString();
        by_path.insert(path, object);
        const QString kind = object.value(QStringLiteral("kind")).toString();
        if (kind == QLatin1String("opf")) opf_path = path;
        if (kind == QLatin1String("ncx") && nav_base.isEmpty()) nav_base = path;
    }
    const bool epub3 = workspace->summary().value(QStringLiteral("epub_version")).toString().startsWith(QLatin1Char('3'));

    struct TocHit {
        int index = 0;
        QString label;
        QString href;
        int level = 1;
    };
    QHash<QString, QList<TocHit>> toc_by_path;
    QJsonArray toc_unresolved;
    const QJsonArray toc = workspace->toc();
    for (int i = 0; i < toc.size(); ++i) {
        const QJsonObject entry = toc.at(i).toObject();
        const QString href = entry.value(QStringLiteral("href")).toString();
        QStringList candidates;
        if (!nav_base.isEmpty()) candidates << resolveHref(nav_base, href).first;
        if (!opf_path.isEmpty()) candidates << resolveHref(opf_path, href).first;
        candidates << resolveHref(QString(), href).first;
        QString found;
        for (const QString &candidate : candidates) {
            if (by_path.contains(candidate)) {
                found = candidate;
                break;
            }
        }
        const TocHit hit { i, entry.value(QStringLiteral("label")).toString(), href,
                           qMax(1, entry.value(QStringLiteral("level")).toInt(1)) };
        if (found.isEmpty()) {
            toc_unresolved.append(QJsonObject {
                { QStringLiteral("toc_index"), i }, { QStringLiteral("label"), hit.label },
                { QStringLiteral("href"), href } });
        } else {
            toc_by_path[found].append(hit);
        }
    }

    static const QRegularExpression afterword(QStringLiteral("^(後[\\s\\x{3000}]*記|后[\\s\\x{3000}]*记|あとがき|afterword)"),
                                              QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression prologue(QStringLiteral("^(序[\\s\\x{3000}]*章|序[\\s\\x{3000}]*幕|楔子|プロローグ|prologue)"),
                                             QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression epilogue(QStringLiteral("^(終[\\s\\x{3000}]*章|终[\\s\\x{3000}]*章|尾[\\s\\x{3000}]*聲|尾[\\s\\x{3000}]*声|エピローグ|epilogue)"),
                                             QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression chapter_title(
        QStringLiteral("^(第[\\s\\x{3000}]*[0-9０-９一二三四五六七八九十百千零〇]+[\\s\\x{3000}]*[話话章回幕卷部篇節节]|幕[\\s\\x{3000}]*間|幕[\\s\\x{3000}]*间|間[\\s\\x{3000}]*章|间[\\s\\x{3000}]*章|番外|chapter[\\s\\x{3000}]*\\d)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression contents_title(QStringLiteral("^(目[\\s\\x{3000}]*[录錄次]|contents)$"),
                                                   QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression chapter_file(QStringLiteral("(section|chapter|chap|ch|part|text)[-_]?\\d+"),
                                                 QRegularExpression::CaseInsensitiveOption);

    QJsonArray pages;
    QJsonArray differences;
    QHash<QString, int> role_counts;
    int previous_toc_index = -1;
    const QJsonArray spine = workspace->spine();
    for (int position = 0; position < spine.size(); ++position) {
        const QJsonObject item = spine.at(position).toObject();
        const QString path = item.value(QStringLiteral("book_path")).toString();
        const QString id = item.value(QStringLiteral("resource_id")).toString();
        const BookFile file { id, path, QStringLiteral("xhtml"), QString() };
        const XhtmlScan scan = scanXhtml(file, workspace->workingText(id), epub3);
        const QString base = path.section(QLatin1Char('/'), -1).section(QLatin1Char('.'), 0, 0).toLower();
        const QString heading = scan.firstHeading;
        const QString label_text = heading.isEmpty() ? collapseAsciiWhitespace(scan.title) : heading;

        QString role = QStringLiteral("unknown");
        QString confidence = QStringLiteral("low");
        QJsonArray evidence;
        auto decide = [&](const QString &candidate, const QString &level, const QString &why) {
            if (role == QLatin1String("unknown")) {
                role = candidate;
                confidence = level;
            }
            evidence.append(why);
        };
        static const QStringList semantic = {
            QStringLiteral("cover"), QStringLiteral("titlepage"), QStringLiteral("toc"),
            QStringLiteral("preface"), QStringLiteral("foreword"), QStringLiteral("prologue"),
            QStringLiteral("chapter"), QStringLiteral("epilogue"), QStringLiteral("afterword"),
            QStringLiteral("appendix"), QStringLiteral("colophon"), QStringLiteral("copyright-page"),
            QStringLiteral("acknowledgments"), QStringLiteral("dedication")
        };
        for (const QString &type : scan.epubTypes) {
            if (semantic.contains(type)) {
                decide(type == QLatin1String("copyright-page") || type == QLatin1String("colophon")
                           ? QStringLiteral("credits") : type,
                       QStringLiteral("high"), QStringLiteral("epub:type=\"%1\"").arg(type));
                break;
            }
        }
        auto classify_text = [&](const QString &text, const QString &level, const QString &source) {
            if (text.isEmpty()) return;
            const QString why = QStringLiteral("%1 \"%2\"").arg(source, bounded(text));
            if (contents_title.match(text).hasMatch()) decide(QStringLiteral("toc"), level, why);
            else if (afterword.match(text).hasMatch()) decide(QStringLiteral("afterword"), level, why);
            else if (prologue.match(text).hasMatch()) decide(QStringLiteral("prologue"), level, why);
            else if (epilogue.match(text).hasMatch()) decide(QStringLiteral("epilogue"), level, why);
            else if (chapter_title.match(text).hasMatch()) decide(QStringLiteral("chapter"), level, why);
        };
        const QList<TocHit> toc_hits = toc_by_path.value(path);
        classify_text(label_text, QStringLiteral("medium"), heading.isEmpty() ? QStringLiteral("title") : QStringLiteral("heading"));
        if (scan.imageCount > 0 && scan.visibleChars < 20) {
            decide(position == 0 || base.contains(QStringLiteral("cover")) ? QStringLiteral("cover")
                                                                              : QStringLiteral("illustration"),
                   QStringLiteral("medium"),
                   QStringLiteral("%1 image(s), %2 visible characters").arg(scan.imageCount).arg(scan.visibleChars));
        }
        if (!toc_hits.isEmpty()) classify_text(toc_hits.first().label, QStringLiteral("low"), QStringLiteral("TOC label"));
        if (base.contains(QStringLiteral("cover"))) {
            decide(QStringLiteral("cover"), QStringLiteral("low"), QStringLiteral("file name %1").arg(base));
        } else if (base == QLatin1String("toc") || base.contains(QStringLiteral("contents"))) {
            decide(QStringLiteral("toc"), QStringLiteral("low"), QStringLiteral("file name %1").arg(base));
        } else if (base.startsWith(QStringLiteral("title"))) {
            decide(QStringLiteral("titlepage"), QStringLiteral("low"), QStringLiteral("file name %1").arg(base));
        } else if (base.startsWith(QStringLiteral("illus")) || base.startsWith(QStringLiteral("color"))
                   || base.startsWith(QStringLiteral("insert"))) {
            decide(QStringLiteral("illustration"), QStringLiteral("low"), QStringLiteral("file name %1").arg(base));
        } else if (base == QLatin1String("message") || base.contains(QStringLiteral("credit"))
                   || base.contains(QStringLiteral("copyright")) || base.contains(QStringLiteral("colophon"))) {
            decide(QStringLiteral("credits"), QStringLiteral("low"), QStringLiteral("file name %1").arg(base));
        } else if (base == QLatin1String("summary") || base.contains(QStringLiteral("synopsis"))) {
            decide(QStringLiteral("synopsis"), QStringLiteral("low"), QStringLiteral("file name %1").arg(base));
        } else if (chapter_file.match(base).hasMatch() && scan.visibleChars > 200) {
            decide(QStringLiteral("chapter"), QStringLiteral("low"), QStringLiteral("file name %1 with body text").arg(base));
        }
        if (role == QLatin1String("unknown") && scan.visibleChars > 500) {
            decide(QStringLiteral("body"), QStringLiteral("low"),
                   QStringLiteral("%1 visible characters").arg(scan.visibleChars));
        }
        role_counts[role] += 1;

        const QList<TocHit> &hits = toc_hits;
        QJsonObject page {
            { QStringLiteral("spine_index"), position },
            { QStringLiteral("resource_id"), id },
            { QStringLiteral("book_path"), path },
            { QStringLiteral("role"), role },
            { QStringLiteral("confidence"), confidence },
            { QStringLiteral("evidence"), evidence },
            { QStringLiteral("visible_characters"), scan.visibleChars },
            { QStringLiteral("image_count"), scan.imageCount },
            { QStringLiteral("well_formed"), scan.wellFormed }
        };
        if (!heading.isEmpty()) {
            page.insert(QStringLiteral("heading"), QJsonObject {
                { QStringLiteral("text"), bounded(heading) },
                { QStringLiteral("level"), scan.firstHeadingLevel },
                { QStringLiteral("line"), scan.firstHeadingLine } });
        }
        if (!collapseAsciiWhitespace(scan.title).isEmpty()) page.insert(QStringLiteral("title"), bounded(scan.title));
        if (!hits.isEmpty()) {
            page.insert(QStringLiteral("toc"), QJsonObject {
                { QStringLiteral("toc_index"), hits.first().index },
                { QStringLiteral("label"), hits.first().label },
                { QStringLiteral("level"), hits.first().level },
                { QStringLiteral("entry_count"), hits.size() } });
        }
        pages.append(page);

        auto difference = [&](const QString &kind, const QString &message, const QJsonObject &extra = QJsonObject()) {
            QJsonObject object {
                { QStringLiteral("kind"), kind },
                { QStringLiteral("book_path"), path },
                { QStringLiteral("spine_index"), position },
                { QStringLiteral("message"), message }
            };
            for (auto it = extra.constBegin(); it != extra.constEnd(); ++it) object.insert(it.key(), it.value());
            differences.append(object);
        };
        const bool content_role = role == QLatin1String("chapter") || role == QLatin1String("prologue")
            || role == QLatin1String("epilogue") || role == QLatin1String("afterword") || role == QLatin1String("body");
        if (content_role && hits.isEmpty()) {
            difference(QStringLiteral("heading_without_toc"),
                       QStringLiteral("Body page is not in the table of contents."),
                       QJsonObject { { QStringLiteral("heading"), bounded(label_text) } });
        }
        if (content_role && heading.isEmpty() && scan.wellFormed) {
            difference(QStringLiteral("page_without_heading"),
                       QStringLiteral("Body page has no h1-h6 heading; navigation and screen readers rely on it."));
        }
        if (!hits.isEmpty() && !heading.isEmpty()) {
            QString toc_label = hits.first().label;
            QString normalized_heading = heading;
            toc_label.remove(QRegularExpression(QStringLiteral("[\\s\\x{3000}]")));
            normalized_heading.remove(QRegularExpression(QStringLiteral("[\\s\\x{3000}]")));
            if (!toc_label.isEmpty() && toc_label != normalized_heading
                && !normalized_heading.contains(toc_label) && !toc_label.contains(normalized_heading)) {
                difference(QStringLiteral("toc_label_mismatch"),
                           QStringLiteral("TOC label differs from the page heading; confirm which one is intended."),
                           QJsonObject {
                               { QStringLiteral("toc_label"), hits.first().label },
                               { QStringLiteral("heading"), bounded(heading) },
                               { QStringLiteral("line"), scan.firstHeadingLine } });
            }
        }
        if (!hits.isEmpty()) {
            if (hits.first().index < previous_toc_index) {
                difference(QStringLiteral("toc_order_differs_from_spine"),
                           QStringLiteral("This TOC entry appears earlier in the TOC than a page that precedes it in the spine."),
                           QJsonObject { { QStringLiteral("toc_index"), hits.first().index } });
            }
            previous_toc_index = qMax(previous_toc_index, hits.first().index);
        }
    }
    for (auto it = toc_by_path.constBegin(); it != toc_by_path.constEnd(); ++it) {
        bool in_spine = false;
        for (const QJsonValue &value : spine) {
            if (value.toObject().value(QStringLiteral("book_path")).toString() == it.key()) in_spine = true;
        }
        if (!in_spine && by_path.value(it.key()).value(QStringLiteral("kind")).toString() == QLatin1String("xhtml")) {
            differences.append(QJsonObject {
                { QStringLiteral("kind"), QStringLiteral("toc_target_not_in_spine") },
                { QStringLiteral("book_path"), it.key() },
                { QStringLiteral("label"), it.value().first().label },
                { QStringLiteral("message"), QStringLiteral("The TOC links to a document that is not in the reading order.") } });
        }
    }
    for (const QJsonValue &value : toc_unresolved) {
        QJsonObject object = value.toObject();
        object.insert(QStringLiteral("kind"), QStringLiteral("toc_target_missing"));
        object.insert(QStringLiteral("message"), QStringLiteral("The TOC entry points to a file that is not in the book."));
        differences.append(object);
    }
    QJsonObject counts;
    for (auto it = role_counts.constBegin(); it != role_counts.constEnd(); ++it) counts.insert(it.key(), it.value());
    return QJsonObject {
        { QStringLiteral("spine_count"), spine.size() },
        { QStringLiteral("toc_count"), toc.size() },
        { QStringLiteral("role_counts"), counts },
        { QStringLiteral("pages"), pages },
        { QStringLiteral("differences"), differences },
        { QStringLiteral("role_source"), QStringLiteral("epub:type first, then heading text, image-only layout and file name; confidence says which signal decided. CSS classes are not used as semantics.") }
    };
}

QJsonArray PreflightAudit::profiles()
{
    auto profile = [](const QString &id, const QString &description) {
        return QJsonObject {
            { QStringLiteral("profile_id"), id },
            { QStringLiteral("version"), 1 },
            { QStringLiteral("description"), description },
            { QStringLiteral("rules"), QJsonArray::fromStringList(profileRules(id)) },
            { QStringLiteral("external_validators"), QJsonArray {
                QStringLiteral("epubcheck"), QStringLiteral("ace") } }
        };
    };
    return QJsonArray {
        profile(kProfileRelease, QStringLiteral("Structure, links, metadata and basic accessibility checks for a reflowable release candidate (default).")),
        profile(kProfileCore, QStringLiteral("Package, spine, XHTML well-formedness, internal links, CSS URLs and required metadata.")),
        profile(kProfileA11y, QStringLiteral("Language tags, image alt attributes and heading order. Automated checks do not establish accessibility conformance."))
    };
}

QString preflightSnapshotDigest(IBookWorkspace *workspace)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(workspace->bookSessionId().toUtf8());
    hash.addData(QByteArray::number(static_cast<qulonglong>(workspace->revision())));
    for (const QJsonValue &value : workspace->resources()) {
        const QJsonObject object = value.toObject();
        const QString id = object.value(QStringLiteral("resource_id")).toString();
        hash.addData(id.toUtf8());
        hash.addData(object.value(QStringLiteral("book_path")).toString().toUtf8());
        const QString kind = object.value(QStringLiteral("kind")).toString();
        if (kind != QLatin1String("image") && kind != QLatin1String("font")) {
            hash.addData(QCryptographicHash::hash(workspace->workingText(id).toUtf8(),
                                                  QCryptographicHash::Sha256));
        }
    }
    hash.addData(QJsonDocument(workspace->spine()).toJson(QJsonDocument::Compact));
    hash.addData(QJsonDocument(workspace->toc()).toJson(QJsonDocument::Compact));
    hash.addData(QJsonDocument(workspace->metadata()).toJson(QJsonDocument::Compact));
    return QString::fromLatin1(hash.result().toHex());
}

QJsonArray preflightIssues(IBookWorkspace *workspace, const QString &profile_id)
{
    const QStringList rule_list = profileRules(profile_id);
    const QSet<QString> enabled(rule_list.cbegin(), rule_list.cend());
    QList<Issue> issues;
    auto add = [&](const Issue &issue) {
        if (enabled.contains(issue.rule)) issues.append(issue);
    };

    QList<BookFile> files;
    QHash<QString, int> by_path;
    QHash<QString, int> by_lower_path;
    for (const QJsonValue &value : workspace->resources()) {
        const QJsonObject object = value.toObject();
        BookFile file {
            object.value(QStringLiteral("resource_id")).toString(),
            object.value(QStringLiteral("book_path")).toString(),
            object.value(QStringLiteral("kind")).toString(),
            object.value(QStringLiteral("media_type")).toString()
        };
        by_path.insert(file.path, files.size());
        by_lower_path.insert(file.path.toLower(), files.size());
        files.append(file);
    }
    const QString epub_version = workspace->summary().value(QStringLiteral("epub_version")).toString();
    const bool epub3 = epub_version.startsWith(QLatin1Char('3'));

    QHash<QString, QString> metadata;
    const QJsonObject meta = workspace->metadata();
    for (auto it = meta.constBegin(); it != meta.constEnd(); ++it) {
        if (it.value().isString()) metadata.insert(it.key().toLower(), it.value().toString().trimmed());
    }
    for (const QJsonValue &value : meta.value(QStringLiteral("entries")).toArray()) {
        const QJsonObject entry = value.toObject();
        QString name = entry.value(QStringLiteral("name")).toString().toLower();
        if (name.startsWith(QLatin1String("dc:"))) name = name.mid(3);
        if (!metadata.value(name).isEmpty()) continue;
        metadata.insert(name, entry.value(QStringLiteral("content")).toString().trimmed());
    }
    for (const QString &field : { QStringLiteral("title"), QStringLiteral("identifier"), QStringLiteral("language") }) {
        if (!metadata.value(field).isEmpty()) continue;
        add({ QStringLiteral("METADATA_REQUIRED_MISSING"), QStringLiteral("error"), QString(), QString(), 0,
              QStringLiteral("dc:%1").arg(field),
              QStringLiteral("Required Dublin Core metadata dc:%1 is missing or empty.").arg(field),
              QStringLiteral("mechanical"), field != QLatin1String("language") });
        if (field == QLatin1String("language")) {
            add({ QStringLiteral("METADATA_LANGUAGE_MISSING"), QStringLiteral("warning"), QString(), QString(), 0,
                  QStringLiteral("dc:language"),
                  QStringLiteral("Reading systems need dc:language to choose voices, hyphenation and fonts."),
                  QStringLiteral("mechanical"), false });
        }
    }
    const QString book_language = primaryLanguage(metadata.value(QStringLiteral("language")));

    const QJsonArray spine = workspace->spine();
    if (spine.isEmpty()) {
        add({ QStringLiteral("SPINE_EMPTY"), QStringLiteral("error"), QString(), QString(), 0, QString(),
              QStringLiteral("The spine has no reading-order items."), QStringLiteral("manual"), true });
    }
    QSet<QString> spine_paths;
    for (const QJsonValue &value : spine) {
        const QJsonObject item = value.toObject();
        const QString id = item.value(QStringLiteral("resource_id")).toString();
        const QString path = item.value(QStringLiteral("book_path")).toString();
        if (id.isEmpty() || (!path.isEmpty() && !by_path.contains(path))) {
            add({ QStringLiteral("SPINE_ITEM_MISSING"), QStringLiteral("error"), id, path, 0,
                  bounded(path.isEmpty() ? id : path),
                  QStringLiteral("A spine item does not resolve to a resource in the book."),
                  QStringLiteral("manual"), true });
        }
        if (!path.isEmpty()) spine_paths.insert(path);
    }

    QSet<QString> referenced;
    QList<Reference> references;
    QHash<QString, QSet<QString>> ids_by_path;
    QHash<QString, bool> wellformed_by_path;
    for (const BookFile &file : files) {
        if (file.kind != QLatin1String("xhtml")) continue;
        const QString text = workspace->workingText(file.id);
        const XhtmlScan scan = scanXhtml(file, text, epub3);
        ids_by_path.insert(file.path, scan.ids);
        wellformed_by_path.insert(file.path, scan.wellFormed);
        for (const Issue &issue : scan.local) add(issue);
        if (!scan.wellFormed) {
            add({ QStringLiteral("XHTML_NOT_WELLFORMED"), QStringLiteral("error"), file.id, file.path,
                  scan.errorLine, bounded(scan.error),
                  QStringLiteral("XHTML is not well-formed XML; reading systems may refuse the whole file. Checks after this point were not run for this file."),
                  QStringLiteral("manual"), true });
        }
        references += scan.links;
        const bool is_nav = text.contains(QStringLiteral("epub:type=\"toc\""))
            || text.contains(QStringLiteral("epub:type='toc'"));
        if (spine_paths.contains(file.path) && !is_nav && scan.wellFormed) {
            if (!scan.hasRootLang || scan.rootLang.isEmpty()) {
                Issue missing_lang { QStringLiteral("XHTML_LANG_MISSING"), QStringLiteral("warning"), file.id, file.path, 0,
                      QStringLiteral("<html>"),
                      QStringLiteral("The html element declares no lang/xml:lang; set it to the content language."),
                      book_language.isEmpty() ? QStringLiteral("manual") : QStringLiteral("mechanical"),
                      book_language.isEmpty() };
                if (!book_language.isEmpty()) {
                    missing_lang.fix = QJsonObject { { QStringLiteral("lang"), metadata.value(QStringLiteral("language")) } };
                }
                add(missing_lang);
            } else if (!book_language.isEmpty() && primaryLanguage(scan.rootLang) != book_language) {
                add({ QStringLiteral("XHTML_LANG_MISMATCH"), QStringLiteral("info"), file.id, file.path, 0,
                      bounded(QStringLiteral("lang=\"%1\" dc:language=\"%2\"").arg(scan.rootLang, metadata.value(QStringLiteral("language")))),
                      QStringLiteral("Document language differs from dc:language; confirm this page really is in another language."),
                      QStringLiteral("manual"), true });
            }
        }
        int previous = 0;
        for (const QPair<int, int> &heading : scan.headings) {
            if (previous > 0 && heading.first > previous + 1) {
                add({ QStringLiteral("HEADING_LEVEL_SKIP"), QStringLiteral("warning"), file.id, file.path, heading.second,
                      QStringLiteral("h%1 → h%2").arg(previous).arg(heading.first),
                      QStringLiteral("Heading level jumps by more than one; assistive navigation relies on a continuous outline."),
                      QStringLiteral("manual"), true });
                break;
            }
            previous = heading.first;
        }
    }

    static const QRegularExpression css_url(
        QStringLiteral("url\\(\\s*(['\"]?)([^'\")]+)\\1\\s*\\)|@import\\s+(['\"])([^'\"]+)\\3"),
        QRegularExpression::CaseInsensitiveOption);
    for (const BookFile &file : files) {
        if (file.kind != QLatin1String("css")) continue;
        const QString text = workspace->workingText(file.id);
        auto it = css_url.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            const QString raw = match.captured(2).isEmpty() ? match.captured(4) : match.captured(2);
            if (raw.startsWith(QLatin1String("data:"))) continue;
            references.append({ file.id, file.path, lineAt(text, match.capturedStart()), raw, QStringLiteral("url") });
        }
    }

    QString opf_path;
    QString opf_text;
    QString nav_base;
    for (const BookFile &file : files) {
        if (file.kind == QLatin1String("opf")) {
            opf_path = file.path;
            opf_text = workspace->workingText(file.id);
        } else if (file.kind == QLatin1String("ncx") && nav_base.isEmpty()) {
            nav_base = file.path;
        }
    }
    if (!opf_text.isEmpty()) {
        static const QRegularExpression item_re(QStringLiteral("<item\\b[^>]*>"), QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression href_re(QStringLiteral("\\bhref\\s*=\\s*(['\"])(.*?)\\1"));
        static const QRegularExpression props_re(QStringLiteral("\\bproperties\\s*=\\s*(['\"])(.*?)\\1"));
        bool has_nav = false;
        auto items = item_re.globalMatch(opf_text);
        while (items.hasNext()) {
            const QRegularExpressionMatch match = items.next();
            const QString tag = match.captured();
            const QString href = href_re.match(tag).captured(2);
            const QStringList properties = props_re.match(tag).captured(2).split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (properties.contains(QStringLiteral("nav"))) {
                has_nav = true;
                nav_base = resolveHref(opf_path, href).first;
            }
            if (href.isEmpty() || isExternal(href)) continue;
            const QString target = resolveHref(opf_path, href).first;
            if (properties.contains(QStringLiteral("cover-image"))) referenced.insert(target);
            if (!by_path.contains(target)) {
                add({ QStringLiteral("MANIFEST_HREF_MISSING"), QStringLiteral("error"), QString(), opf_path,
                      lineAt(opf_text, match.capturedStart()), bounded(tag),
                      QStringLiteral("A manifest item points to a file that is not in the book."),
                      QStringLiteral("manual"), true });
            }
        }
        if (epub3 && !has_nav) {
            add({ QStringLiteral("NAV_MISSING"), QStringLiteral("error"), QString(), opf_path, 0, QString(),
                  QStringLiteral("EPUB 3 requires one manifest item with properties=\"nav\"."),
                  QStringLiteral("manual"), true });
        }
        static const QRegularExpression cover_meta(
            QStringLiteral("<meta\\b[^>]*name\\s*=\\s*['\"]cover['\"][^>]*content\\s*=\\s*['\"]([^'\"]+)['\"]"),
            QRegularExpression::CaseInsensitiveOption);
        const QString cover_id = cover_meta.match(opf_text).captured(1);
        if (!cover_id.isEmpty()) {
            static const QRegularExpression id_item(QStringLiteral("<item\\b[^>]*>"), QRegularExpression::CaseInsensitiveOption);
            auto all = id_item.globalMatch(opf_text);
            while (all.hasNext()) {
                const QString tag = all.next().captured();
                if (tag.contains(QStringLiteral("id=\"%1\"").arg(cover_id))
                    || tag.contains(QStringLiteral("id='%1'").arg(cover_id))) {
                    referenced.insert(resolveHref(opf_path, href_re.match(tag).captured(2)).first);
                }
            }
        }
    }

    for (const Reference &reference : references) {
        if (isExternal(reference.raw)) continue;
        const QPair<QString, QString> resolved = resolveHref(reference.sourcePath, reference.raw);
        const QString target = resolved.first.isEmpty() ? reference.sourcePath : resolved.first;
        if (!by_path.contains(target)) {
            const bool case_only = by_lower_path.contains(target.toLower());
            const bool css = reference.attribute == QLatin1String("url");
            QString rule = QStringLiteral("LINK_TARGET_MISSING");
            QString message = QStringLiteral("Link or resource reference points to a file that is not in the book.");
            if (case_only) {
                rule = QStringLiteral("LINK_PATH_CASE_MISMATCH");
                message = QStringLiteral("Target exists only with different letter case; case-sensitive reading systems will not find it.");
            } else if (css) {
                rule = QStringLiteral("CSS_URL_MISSING");
                message = QStringLiteral("CSS references a file that is not in the book.");
            }
            Issue link_issue { rule, QStringLiteral("error"), reference.sourceId, reference.sourcePath, reference.line,
                  bounded(QStringLiteral("%1=\"%2\"").arg(reference.attribute, reference.raw)),
                  message, case_only ? QStringLiteral("mechanical") : QStringLiteral("manual"), !case_only };
            if (case_only) {
                const QString actual = files.at(by_lower_path.value(target.toLower())).path;
                QString replacement = reference.raw;
                const QString fragment_suffix = reference.raw.contains(QLatin1Char('#'))
                    ? reference.raw.mid(reference.raw.indexOf(QLatin1Char('#'))) : QString();
                QString without_fragment = reference.raw.left(reference.raw.size() - fragment_suffix.size());
                const QStringList from_parts = directoryOf(reference.sourcePath).split(QLatin1Char('/'), Qt::SkipEmptyParts);
                const QStringList to_parts = actual.split(QLatin1Char('/'), Qt::SkipEmptyParts);
                int common = 0;
                while (common < from_parts.size() && common < to_parts.size() - 1
                       && from_parts.at(common) == to_parts.at(common)) {
                    ++common;
                }
                QStringList relative;
                for (int i = common; i < from_parts.size(); ++i) relative << QStringLiteral("..");
                for (int i = common; i < to_parts.size(); ++i) relative << to_parts.at(i);
                if (!without_fragment.contains(QLatin1Char('%'))) {
                    replacement = relative.join(QLatin1Char('/')) + fragment_suffix;
                    link_issue.fix = QJsonObject {
                        { QStringLiteral("attribute"), reference.attribute },
                        { QStringLiteral("from"), reference.raw },
                        { QStringLiteral("to"), replacement }
                    };
                } else {
                    link_issue.fixKind = QStringLiteral("manual");
                    link_issue.manual = true;
                }
            }
            add(link_issue);
            if (case_only) referenced.insert(files.at(by_lower_path.value(target.toLower())).path);
            continue;
        }
        referenced.insert(target);
        if (!resolved.second.isEmpty() && ids_by_path.contains(target)
            && wellformed_by_path.value(target) && !ids_by_path.value(target).contains(resolved.second)) {
            add({ QStringLiteral("LINK_FRAGMENT_MISSING"), QStringLiteral("error"), reference.sourceId,
                  reference.sourcePath, reference.line,
                  bounded(QStringLiteral("%1=\"%2\"").arg(reference.attribute, reference.raw)),
                  QStringLiteral("The fragment identifier does not exist in the target document."),
                  QStringLiteral("manual"), true });
        }
    }

    for (const QJsonValue &value : workspace->toc()) {
        const QJsonObject entry = value.toObject();
        const QString href = entry.value(QStringLiteral("href")).toString();
        if (href.isEmpty() || isExternal(href)) continue;
        QStringList candidates;
        if (!nav_base.isEmpty()) candidates << resolveHref(nav_base, href).first;
        if (!opf_path.isEmpty()) candidates << resolveHref(opf_path, href).first;
        candidates << resolveHref(QString(), href).first;
        const QString fragment = resolveHref(QString(), href).second;
        QString found;
        for (const QString &candidate : candidates) {
            if (by_path.contains(candidate)) {
                found = candidate;
                break;
            }
        }
        if (found.isEmpty()) {
            add({ QStringLiteral("TOC_TARGET_MISSING"), QStringLiteral("error"), QString(), nav_base, 0,
                  bounded(QStringLiteral("%1 → %2").arg(entry.value(QStringLiteral("label")).toString(), href)),
                  QStringLiteral("A table-of-contents entry points to a file that is not in the book."),
                  QStringLiteral("manual"), true });
            continue;
        }
        referenced.insert(found);
        if (!fragment.isEmpty() && wellformed_by_path.value(found) && ids_by_path.contains(found)
            && !ids_by_path.value(found).contains(fragment)) {
            add({ QStringLiteral("LINK_FRAGMENT_MISSING"), QStringLiteral("error"), QString(), nav_base, 0,
                  bounded(QStringLiteral("%1 → %2").arg(entry.value(QStringLiteral("label")).toString(), href)),
                  QStringLiteral("A table-of-contents fragment does not exist in the target document."),
                  QStringLiteral("manual"), true });
        }
    }

    for (const BookFile &file : files) {
        if (file.kind != QLatin1String("image") && file.kind != QLatin1String("font")
            && file.kind != QLatin1String("css")) {
            continue;
        }
        if (referenced.contains(file.path)) continue;
        add({ QStringLiteral("RESOURCE_UNREFERENCED"), QStringLiteral("info"), file.id, file.path, 0,
              bounded(file.path),
              QStringLiteral("No XHTML, CSS, navigation or cover metadata references this %1; remove it or place it deliberately.").arg(file.kind),
              QStringLiteral("manual"), true });
    }

    std::stable_sort(issues.begin(), issues.end(), [](const Issue &a, const Issue &b) {
        const int rank_a = severityRank(a.severity);
        const int rank_b = severityRank(b.severity);
        if (rank_a != rank_b) return rank_a < rank_b;
        if (a.bookPath != b.bookPath) return a.bookPath < b.bookPath;
        return a.line < b.line;
    });
    QJsonArray array;
    for (int i = 0; i < issues.size(); ++i) array.append(issueJson(issues.at(i), i));
    return array;
}

ToolResult PreflightAudit::run(IBookWorkspace *workspace, const QJsonObject &arguments)
{
    const QString profile = arguments.value(QStringLiteral("profile_id")).toString(kProfileRelease);
    bool known = false;
    for (const QJsonValue &value : profiles()) {
        if (value.toObject().value(QStringLiteral("profile_id")).toString() == profile) known = true;
    }
    if (!known) {
        return ToolResult::failure(QStringLiteral("PREFLIGHT_PROFILE_UNKNOWN"),
                                   QStringLiteral("Unknown profile_id; call preflight.profiles"));
    }
    const QString digest_before = preflightSnapshotDigest(workspace);
    const QJsonArray found = preflightIssues(workspace, profile);
    if (preflightSnapshotDigest(workspace) != digest_before) {
        return ToolResult::failure(QStringLiteral("PREFLIGHT_SOURCE_CHANGED"),
                                   QStringLiteral("The book changed while preflight was running; run it again"));
    }
    m_runId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_profileId = profile;
    m_sessionId = workspace->bookSessionId();
    m_snapshotDigest = digest_before;
    m_issues = found;
    QJsonObject severity_counts {
        { QStringLiteral("error"), 0 }, { QStringLiteral("warning"), 0 }, { QStringLiteral("info"), 0 }
    };
    QJsonObject rule_counts;
    int manual = 0;
    for (const QJsonValue &value : found) {
        const QJsonObject issue = value.toObject();
        const QString severity = issue.value(QStringLiteral("severity")).toString();
        severity_counts.insert(severity, severity_counts.value(severity).toInt() + 1);
        const QString rule = issue.value(QStringLiteral("rule_id")).toString();
        rule_counts.insert(rule, rule_counts.value(rule).toInt() + 1);
        if (issue.value(QStringLiteral("manual_required")).toBool()) ++manual;
    }
    const bool has_errors = severity_counts.value(QStringLiteral("error")).toInt() > 0;
    m_summary = QJsonObject {
        { QStringLiteral("run_id"), m_runId },
        { QStringLiteral("profile_id"), profile },
        { QStringLiteral("profile_version"), 1 },
        { QStringLiteral("book_session_id"), m_sessionId },
        { QStringLiteral("book_revision"), static_cast<qint64>(workspace->revision()) },
        { QStringLiteral("snapshot_digest"), m_snapshotDigest },
        { QStringLiteral("includes_staged_changes"), workspace->hasOpenTransaction() },
        { QStringLiteral("saved_epub_checked"), false },
        { QStringLiteral("total_count"), found.size() },
        { QStringLiteral("severity_counts"), severity_counts },
        { QStringLiteral("rule_counts"), rule_counts },
        { QStringLiteral("manual_review_count"), manual },
        { QStringLiteral("validation_status"), QJsonObject {
            { QStringLiteral("local"), has_errors ? QStringLiteral("errors_found")
                                                  : (found.isEmpty() ? QStringLiteral("passed")
                                                                     : QStringLiteral("passed_with_findings")) },
            { QStringLiteral("epubcheck"), QJsonObject {
                { QStringLiteral("status"), QStringLiteral("not_run") },
                { QStringLiteral("reason"), QStringLiteral("No EPUBCheck adapter is configured; local rules are not a conformance result.") } } },
            { QStringLiteral("ace"), QJsonObject {
                { QStringLiteral("status"), QStringLiteral("not_run") },
                { QStringLiteral("reason"), QStringLiteral("No Ace adapter is configured.") } } },
            { QStringLiteral("human_review"), QStringLiteral("pending") }
        } }
    };
    QJsonObject first_page = arguments;
    first_page.insert(QStringLiteral("run_id"), m_runId);
    first_page.remove(QStringLiteral("profile_id"));
    ToolResult page = issues(workspace, first_page);
    if (!page.ok) return page;
    QJsonObject data = m_summary;
    for (auto it = page.data.constBegin(); it != page.data.constEnd(); ++it) {
        if (!data.contains(it.key())) data.insert(it.key(), it.value());
    }
    return ToolResult::success(data);
}

ToolResult PreflightAudit::issues(IBookWorkspace *workspace, const QJsonObject &arguments)
{
    const QString run_id = arguments.value(QStringLiteral("run_id")).toString();
    if (run_id.isEmpty() || run_id != m_runId) {
        return ToolResult::failure(QStringLiteral("PREFLIGHT_RUN_UNKNOWN"),
                                   QStringLiteral("Unknown or superseded run_id; call preflight.run"));
    }
    if (workspace->bookSessionId() != m_sessionId
        || preflightSnapshotDigest(workspace) != m_snapshotDigest) {
        return ToolResult::failure(QStringLiteral("PREFLIGHT_STALE"),
                                   QStringLiteral("The book or staged edits changed after this run; call preflight.run again"),
                                   QJsonObject { { QStringLiteral("run_id"), run_id } });
    }
    const int offset = qMax(0, arguments.value(QStringLiteral("offset")).toInt(0));
    const int limit = qBound(1, arguments.value(QStringLiteral("limit")).toInt(kDefaultIssuePage), kMaxIssuePage);
    const QString severity = arguments.value(QStringLiteral("severity")).toString();
    const QString rule = arguments.value(QStringLiteral("rule_id")).toString();
    QJsonArray filtered;
    for (const QJsonValue &value : m_issues) {
        const QJsonObject issue = value.toObject();
        if (!severity.isEmpty() && issue.value(QStringLiteral("severity")).toString() != severity) continue;
        if (!rule.isEmpty() && issue.value(QStringLiteral("rule_id")).toString() != rule) continue;
        filtered.append(issue);
    }
    QJsonArray page;
    for (int i = offset; i < filtered.size() && page.size() < limit; ++i) page.append(filtered.at(i));
    const bool has_more = offset + page.size() < filtered.size();
    QJsonObject data {
        { QStringLiteral("run_id"), m_runId },
        { QStringLiteral("profile_id"), m_profileId },
        { QStringLiteral("snapshot_digest"), m_snapshotDigest },
        { QStringLiteral("offset"), offset },
        { QStringLiteral("limit"), limit },
        { QStringLiteral("filtered_count"), filtered.size() },
        { QStringLiteral("returned_count"), page.size() },
        { QStringLiteral("has_more"), has_more },
        { QStringLiteral("issues"), page }
    };
    if (has_more) data.insert(QStringLiteral("next_offset"), offset + page.size());
    return ToolResult::success(data);
}

ToolResult PreflightAudit::applyFixes(IBookWorkspace *workspace, const QJsonObject &arguments)
{
    const QString run_id = arguments.value(QStringLiteral("run_id")).toString();
    if (run_id.isEmpty() || run_id != m_runId) {
        return ToolResult::failure(QStringLiteral("PREFLIGHT_RUN_UNKNOWN"),
                                   QStringLiteral("Unknown or superseded run_id; call preflight.run"));
    }
    if (workspace->bookSessionId() != m_sessionId
        || preflightSnapshotDigest(workspace) != m_snapshotDigest) {
        return ToolResult::failure(QStringLiteral("PREFLIGHT_STALE"),
                                   QStringLiteral("The book or staged edits changed after this run; call preflight.run again"));
    }
    const QJsonArray requested = arguments.value(QStringLiteral("issue_ids")).toArray();
    const QJsonObject image_alts = arguments.value(QStringLiteral("image_alts")).toObject();
    if (requested.isEmpty() && image_alts.isEmpty()) {
        return ToolResult::failure(QStringLiteral("INVALID_ARGUMENT"),
                                   QStringLiteral("Pass issue_ids of mechanical issues and/or image_alts"));
    }
    if (requested.size() > 200 || image_alts.size() > 500) {
        return ToolResult::failure(QStringLiteral("INVALID_ARGUMENT"),
                                   QStringLiteral("At most 200 issue_ids and 500 image_alts per call"));
    }
    QHash<QString, QJsonObject> by_id;
    for (const QJsonValue &value : m_issues) {
        const QJsonObject issue = value.toObject();
        by_id.insert(issue.value(QStringLiteral("issue_id")).toString(), issue);
    }
    QList<QJsonObject> selected;
    for (const QJsonValue &value : requested) {
        const QString id = value.toString();
        if (!by_id.contains(id)) {
            return ToolResult::failure(QStringLiteral("PREFLIGHT_ISSUE_UNKNOWN"),
                                       QStringLiteral("Issue %1 is not in run %2").arg(id, run_id));
        }
        const QJsonObject issue = by_id.value(id);
        if (issue.value(QStringLiteral("fix_kind")).toString() != QLatin1String("mechanical")
            || issue.value(QStringLiteral("fix")).toObject().isEmpty()) {
            return ToolResult::failure(QStringLiteral("PREFLIGHT_FIX_NOT_MECHANICAL"),
                                       QStringLiteral("Issue %1 (%2) needs an editor decision and cannot be fixed automatically")
                                           .arg(id, issue.value(QStringLiteral("rule_id")).toString()),
                                       QJsonObject { { QStringLiteral("issue_id"), id } });
        }
        selected.append(issue);
    }
    for (auto it = image_alts.constBegin(); it != image_alts.constEnd(); ++it) {
        if (!it.value().isString()) {
            return ToolResult::failure(QStringLiteral("INVALID_ARGUMENT"),
                                       QStringLiteral("image_alts values must be strings; use \"\" for decorative images"));
        }
        if (it.value().toString().size() > 1000) {
            return ToolResult::failure(QStringLiteral("INVALID_ARGUMENT"),
                                       QStringLiteral("alt text for %1 is longer than 1000 characters; use a caption or extended description").arg(it.key()));
        }
    }

    QHash<QString, QString> path_of;
    QHash<QString, QString> kind_of;
    for (const QJsonValue &value : workspace->resources()) {
        const QJsonObject object = value.toObject();
        path_of.insert(object.value(QStringLiteral("resource_id")).toString(),
                       object.value(QStringLiteral("book_path")).toString());
        kind_of.insert(object.value(QStringLiteral("resource_id")).toString(),
                       object.value(QStringLiteral("kind")).toString());
    }
    for (auto it = image_alts.constBegin(); it != image_alts.constEnd(); ++it) {
        if (!path_of.values().contains(it.key())) {
            return ToolResult::failure(QStringLiteral("IMAGE_NOT_FOUND"),
                                       QStringLiteral("image_alts key %1 is not a book_path in this book").arg(it.key()));
        }
    }

    QHash<QString, QString> texts;
    QStringList order;
    auto text_of = [&](const QString &id) -> QString & {
        if (!texts.contains(id)) {
            texts.insert(id, workspace->workingText(id));
            order.append(id);
        }
        return texts[id];
    };
    QJsonArray changes;
    auto excerpt = [](const QString &text) {
        return text.size() > 200 ? text.left(200) + QStringLiteral("…") : text;
    };

    for (const QJsonObject &issue : selected) {
        const QString id = issue.value(QStringLiteral("resource_id")).toString();
        const QString rule = issue.value(QStringLiteral("rule_id")).toString();
        const QJsonObject fix = issue.value(QStringLiteral("fix")).toObject();
        QString &text = text_of(id);
        if (rule == QLatin1String("LINK_PATH_CASE_MISMATCH")) {
            const QString attribute = fix.value(QStringLiteral("attribute")).toString();
            const QString from = fix.value(QStringLiteral("from")).toString();
            const QString to = fix.value(QStringLiteral("to")).toString();
            const int line = issue.value(QStringLiteral("line")).toInt();
            QStringList lines = text.split(QLatin1Char('\n'));
            if (line < 1 || line > lines.size()) {
                return ToolResult::failure(QStringLiteral("PREFLIGHT_FIX_AMBIGUOUS"),
                                           QStringLiteral("Line %1 is outside %2").arg(line).arg(path_of.value(id)));
            }
            QString &source = lines[line - 1];
            QStringList needles;
            if (attribute == QLatin1String("url")) {
                needles << QStringLiteral("url(\"%1\")").arg(from) << QStringLiteral("url('%1')").arg(from)
                        << QStringLiteral("url(%1)").arg(from);
            } else {
                needles << QStringLiteral("%1=\"%2\"").arg(attribute, from) << QStringLiteral("%1='%2'").arg(attribute, from);
            }
            int hits = 0;
            QString needle_hit;
            for (const QString &needle : needles) {
                const int count = source.count(needle);
                if (count > 0) needle_hit = needle;
                hits += count;
            }
            if (hits != 1) {
                return ToolResult::failure(QStringLiteral("PREFLIGHT_FIX_AMBIGUOUS"),
                                           QStringLiteral("Expected exactly one %1 on %2 L%3, found %4")
                                               .arg(from, path_of.value(id)).arg(line).arg(hits),
                                           QJsonObject { { QStringLiteral("issue_id"), issue.value(QStringLiteral("issue_id")) } });
            }
            const QString before = source;
            QString replacement = needle_hit;
            replacement.replace(from, to);
            source.replace(needle_hit, replacement);
            text = lines.join(QLatin1Char('\n'));
            changes.append(QJsonObject {
                { QStringLiteral("issue_id"), issue.value(QStringLiteral("issue_id")) },
                { QStringLiteral("rule_id"), rule },
                { QStringLiteral("book_path"), path_of.value(id) },
                { QStringLiteral("line"), line },
                { QStringLiteral("before"), excerpt(before.trimmed()) },
                { QStringLiteral("after"), excerpt(source.trimmed()) } });
        } else if (rule == QLatin1String("XHTML_LANG_MISSING")) {
            const QString lang = fix.value(QStringLiteral("lang")).toString().toHtmlEscaped();
            static const QRegularExpression html_open(QStringLiteral("<html\\b[^>]*>"), QRegularExpression::CaseInsensitiveOption);
            const QRegularExpressionMatch match = html_open.match(text);
            if (!match.hasMatch()) {
                return ToolResult::failure(QStringLiteral("PREFLIGHT_FIX_AMBIGUOUS"),
                                           QStringLiteral("No <html> start tag in %1").arg(path_of.value(id)));
            }
            QString tag = match.captured();
            const QString before = tag;
            const bool self_close = tag.endsWith(QStringLiteral("/>"));
            tag.chop(self_close ? 2 : 1);
            static const QRegularExpression has_xml_lang(QStringLiteral("\\bxml:lang\\s*="));
            static const QRegularExpression has_lang(QStringLiteral("(^|\\s)lang\\s*="));
            if (!has_xml_lang.match(tag).hasMatch()) tag += QStringLiteral(" xml:lang=\"%1\"").arg(lang);
            if (!has_lang.match(tag).hasMatch()) tag += QStringLiteral(" lang=\"%1\"").arg(lang);
            tag += self_close ? QStringLiteral("/>") : QStringLiteral(">");
            text.replace(match.capturedStart(), match.capturedLength(), tag);
            changes.append(QJsonObject {
                { QStringLiteral("issue_id"), issue.value(QStringLiteral("issue_id")) },
                { QStringLiteral("rule_id"), rule },
                { QStringLiteral("book_path"), path_of.value(id) },
                { QStringLiteral("line"), lineAt(text, match.capturedStart()) },
                { QStringLiteral("before"), excerpt(before) },
                { QStringLiteral("after"), excerpt(tag) } });
        }
    }

    int alt_images = 0;
    if (!image_alts.isEmpty()) {
        static const QRegularExpression img_tag(QStringLiteral("<img\\b[^>]*>"), QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression src_attr(QStringLiteral("\\bsrc\\s*=\\s*(['\"])(.*?)\\1"));
        static const QRegularExpression alt_attr(QStringLiteral("\\balt\\s*=\\s*(['\"])(.*?)\\1"));
        QSet<QString> matched_images;
        for (auto rid = path_of.constBegin(); rid != path_of.constEnd(); ++rid) {
            if (kind_of.value(rid.key()) != QLatin1String("xhtml")) continue;
            const QString original = workspace->workingText(rid.key());
            if (!original.contains(QLatin1String("<img"), Qt::CaseInsensitive)) continue;
            QString &text = text_of(rid.key());
            QString rebuilt;
            int cursor = 0;
            bool touched = false;
            auto it = img_tag.globalMatch(text);
            while (it.hasNext()) {
                const QRegularExpressionMatch match = it.next();
                const QString tag = match.captured();
                const QString src = src_attr.match(tag).captured(2);
                const QString target = resolveHref(rid.value(), src).first;
                if (!image_alts.contains(target)) continue;
                const QString alt = image_alts.value(target).toString().toHtmlEscaped();
                QString updated = tag;
                const QRegularExpressionMatch existing = alt_attr.match(tag);
                if (existing.hasMatch()) {
                    updated.replace(existing.capturedStart(), existing.capturedLength(),
                                    QStringLiteral("alt=\"%1\"").arg(alt));
                } else {
                    updated.insert(4, QStringLiteral(" alt=\"%1\"").arg(alt));
                }
                matched_images.insert(target);
                if (updated == tag) continue;
                rebuilt += text.mid(cursor, match.capturedStart() - cursor) + updated;
                cursor = match.capturedEnd();
                touched = true;
                ++alt_images;
                changes.append(QJsonObject {
                    { QStringLiteral("rule_id"), QStringLiteral("IMG_ALT_DECISION") },
                    { QStringLiteral("book_path"), rid.value() },
                    { QStringLiteral("line"), lineAt(text, match.capturedStart()) },
                    { QStringLiteral("image"), target },
                    { QStringLiteral("before"), excerpt(tag) },
                    { QStringLiteral("after"), excerpt(updated) } });
            }
            if (touched) {
                rebuilt += text.mid(cursor);
                text = rebuilt;
            }
        }
        for (auto it = image_alts.constBegin(); it != image_alts.constEnd(); ++it) {
            if (!matched_images.contains(it.key())) {
                return ToolResult::failure(QStringLiteral("IMAGE_NOT_REFERENCED"),
                                           QStringLiteral("No <img> in XHTML references %1").arg(it.key()));
            }
        }
    }

    int staged_resources = 0;
    for (const QString &id : order) {
        const QString current = workspace->workingText(id);
        if (texts.value(id) == current) continue;
        const BookOpResult staged = stageWorkingReplace(workspace, id, texts.value(id));
        if (!staged.ok) {
            return ToolResult::failure(staged.code, staged.message, staged.data);
        }
        ++staged_resources;
    }
    m_runId.clear();
    return ToolResult::success(QJsonObject {
        { QStringLiteral("staged"), staged_resources > 0 },
        { QStringLiteral("live_unchanged"), true },
        { QStringLiteral("change_count"), changes.size() },
        { QStringLiteral("alt_images_updated"), alt_images },
        { QStringLiteral("staged_resource_count"), staged_resources },
        { QStringLiteral("changes"), changes },
        { QStringLiteral("next"), QStringLiteral("Read every transaction.preview page, then transaction.commit; afterwards run preflight.run again.") }
    }, false, true);
}

} // namespace SigilAgent
