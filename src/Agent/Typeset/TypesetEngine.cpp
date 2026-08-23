/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/Typeset/TypesetEngine.h"

#include <algorithm>

#include <QCollator>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

#include "Agent/Execution/ResourceMutations.h"

namespace SigilAgent
{

namespace
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

QString stripLeadingIndent(QString line)
{
    while (!line.isEmpty()) {
        const QChar ch = line.at(0);
        if (ch == QChar(0x3000) || ch == QLatin1Char(' ') || ch == QChar(0x00A0) || ch == QChar('\t')) {
            line = line.mid(1);
            continue;
        }
        break;
    }
    return line;
}

bool isSceneBreak(const QString &line)
{
    QString compact = line;
    compact.remove(QLatin1Char(' '));
    compact.remove(QChar(0x3000));
    return compact == QStringLiteral("＊＊＊") || compact == QStringLiteral("***")
        || compact == QStringLiteral("＊") || compact == QStringLiteral("※")
        || compact.contains(QStringLiteral("＊＊＊"));
}

QString fileBaseName(const QString &book_path)
{
    return QFileInfo(book_path).completeBaseName();
}

QString fileName(const QString &book_path)
{
    return QFileInfo(book_path).fileName();
}

int trailingNumber(const QString &stem)
{
    QRegularExpression re(QStringLiteral("(\\d+)$"));
    const QRegularExpressionMatch match = re.match(stem);
    if (!match.hasMatch()) return -1;
    return match.captured(1).toInt();
}

QString imageHrefFromBookPath(const QString &book_path)
{
    const QString name = fileName(book_path);
    if (book_path.contains(QLatin1String("/Images/"), Qt::CaseInsensitive)
        || book_path.startsWith(QLatin1String("Images/"), Qt::CaseInsensitive)) {
        return QStringLiteral("../Images/") + name;
    }
    return QStringLiteral("../Images/") + name;
}

QHash<QString, QString> imageIndex(IBookWorkspace *workspace)
{
    QHash<QString, QString> map;
    if (!workspace) return map;
    const QJsonArray resources = workspace->resources();
    for (const QJsonValue &value : resources) {
        const QJsonObject object = value.toObject();
        if (object.value(QStringLiteral("kind")).toString() != QLatin1String("image")) continue;
        const QString path = object.value(QStringLiteral("book_path")).toString();
        map.insert(fileName(path).toLower(), path);
        map.insert(fileBaseName(path).toLower(), path);
    }
    return map;
}

QString resolveImage(const QHash<QString, QString> &index, const QString &name)
{
    if (name.isEmpty()) return QString();
    const QString lower = name.toLower();
    if (index.contains(lower)) return index.value(lower);
    const QFileInfo info(name);
    const QString base = info.completeBaseName().toLower();
    if (index.contains(base)) return index.value(base);
    const QString with_jpg = base + QStringLiteral(".jpg");
    if (index.contains(with_jpg)) return index.value(with_jpg);
    return QString();
}

bool looksLikeTemplatePage(const QString &role)
{
    return role == QLatin1String("cover") || role == QLatin1String("start")
        || role == QLatin1String("title") || role == QLatin1String("credits")
        || role == QLatin1String("synopsis") || role == QLatin1String("contents")
        || role == QLatin1String("illustration") || role == QLatin1String("chapter");
}

QString currentTextOf(IBookWorkspace *workspace, const QString &resource_id)
{
    if (!workspace) return QString();
    QString live = workspace->resourceText(resource_id);
    if (!live.isEmpty()) return live;
    const BookOpResult fragment = workspace->readFragment(resource_id, 0, 8192);
    if (fragment.ok) return workspace->resourceText(resource_id);
    return live;
}

QString setTitleAndBody(const QString &skeleton, const QString &title, const QString &body_inner)
{
    QString out = skeleton;
    if (out.isEmpty()) {
        out = defaultXhtmlTemplate();
    }
    QRegularExpression title_re(QStringLiteral("<title>[\\s\\S]*?</title>"),
                                QRegularExpression::CaseInsensitiveOption);
    if (title_re.match(out).hasMatch()) {
        out.replace(title_re, QStringLiteral("<title>") + xmlEscape(title) + QStringLiteral("</title>"));
    }
    QRegularExpression body_open(QStringLiteral("<body\\b[^>]*>"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch open = body_open.match(out);
    const int close = out.lastIndexOf(QStringLiteral("</body>"), -1, Qt::CaseInsensitive);
    if (!open.hasMatch() || close < 0) {
        return defaultXhtmlTemplate()
            .replace(QStringLiteral("<title></title>"),
                     QStringLiteral("<title>") + xmlEscape(title) + QStringLiteral("</title>"))
            .replace(QStringLiteral("  <p>&#160;</p>\n"), body_inner);
    }
    const int open_end = open.capturedEnd();
    return out.left(open_end) + QLatin1Char('\n') + body_inner + out.mid(close);
}

QString rewriteFirstImage(const QString &xhtml, const QString &href, const QString &alt)
{
    QString out = xhtml;
    QRegularExpression img(QStringLiteral("(<img\\b[^>]*src=\")[^\"]+(\")"),
                           QRegularExpression::CaseInsensitiveOption);
    if (img.match(out).hasMatch()) {
        out.replace(img, QStringLiteral("\\1") + href + QStringLiteral("\\2"));
    }
    QRegularExpression xlink(QStringLiteral("(xlink:href=\")[^\"]+(\")"),
                             QRegularExpression::CaseInsensitiveOption);
    if (xlink.match(out).hasMatch()) {
        out.replace(xlink, QStringLiteral("\\1") + href + QStringLiteral("\\2"));
    }
    QRegularExpression alt_re(QStringLiteral("(<img\\b[^>]*alt=\")[^\"]+(\")"),
                              QRegularExpression::CaseInsensitiveOption);
    if (alt_re.match(out).hasMatch() && !alt.isEmpty()) {
        out.replace(alt_re, QStringLiteral("\\1") + xmlEscape(alt) + QStringLiteral("\\2"));
    }
    return out;
}

bool lineIsOnlyIllustration(const QString &raw)
{
    const QString name = illustrationNameInLine(raw);
    if (name.isEmpty()) return false;
    QString t = raw.trimmed();
    t.remove(QChar(0x3000));
    t.remove(QLatin1Char(' '));
    QRegularExpression marker(
        QStringLiteral("［插[图圖]：[^］]+］|\\[插[图圖]：[^\\]]+\\]|【插[图圖]：[^】]+】"),
        QRegularExpression::UseUnicodePropertiesOption);
    t.replace(marker, QString());
    return t.isEmpty();
}

QString chapterBodyHtml(const ManuscriptChapter &chapter, const QHash<QString, QString> &images)
{
    QString html = QStringLiteral("  <div>\n    <h1>%1</h1>\n").arg(xmlEscape(chapter.heading));
    const QStringList lines = chapter.body.split(QLatin1Char('\n'));
    bool pending_blank = false;
    for (const QString &raw : lines) {
        if (lineIsOnlyIllustration(raw)) {
            pending_blank = false;
            const QString illus = illustrationNameInLine(raw);
            const QString path = resolveImage(images, illus);
            const QString href = path.isEmpty()
                ? QStringLiteral("../Images/%1.jpg").arg(illus)
                : imageHrefFromBookPath(path);
            html += QStringLiteral("    <div class=\"illus duokan-image-single\"><img alt=\"%1\" src=\"%2\"/></div>\n")
                        .arg(xmlEscape(illus), xmlEscape(href));
            continue;
        }
        const QString line = stripLeadingIndent(raw);
        if (line.trimmed().isEmpty()) {
            pending_blank = true;
            continue;
        }
        if (pending_blank) {
            html += QStringLiteral("    <p><br/></p>\n");
            pending_blank = false;
        }
        if (isSceneBreak(line)) {
            html += QStringLiteral("    <p class=\"ph4\">%1</p>\n").arg(xmlEscape(line.trimmed()));
            continue;
        }
        html += QStringLiteral("    <p>%1</p>\n").arg(xmlEscape(line));
    }
    html += QStringLiteral("  </div>\n");
    return html;
}

QString creditsBodyHtml(const ParsedManuscript &parsed, const QString &skeleton)
{
    QStringList extra;
    QRegularExpression p_re(QStringLiteral("<p class=\"message\"[^>]*>([\\s\\S]*?)</p>"),
                            QRegularExpression::CaseInsensitiveOption);
    auto it = p_re.globalMatch(skeleton);
    while (it.hasNext()) {
        QString inner = it.next().captured(1);
        inner.replace(QRegularExpression(QStringLiteral("<[^>]+>")), QString());
        inner = inner.trimmed();
        if (inner.startsWith(QStringLiteral("作者")) || inner.startsWith(QStringLiteral("插画"))
            || inner.startsWith(QStringLiteral("插畫")) || inner.startsWith(QStringLiteral("译者"))
            || inner.startsWith(QStringLiteral("譯者")) || inner.startsWith(QStringLiteral("图源"))
            || inner.startsWith(QStringLiteral("圖源")) || inner.startsWith(QStringLiteral("录入"))
            || inner.startsWith(QStringLiteral("錄入")) || inner.startsWith(QStringLiteral("翻译"))
            || inner.startsWith(QStringLiteral("翻譯")) || inner.startsWith(QStringLiteral("发布"))
            || inner.startsWith(QStringLiteral("發布"))) {
            continue;
        }
        if (!inner.isEmpty()) extra.append(inner);
    }
    QString html = QStringLiteral("  <div class=\"illus5\">\n    <p class=\"meg\">制作信息</p>\n");
    html += QStringLiteral("    <p class=\"message\">≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡</p>\n");
    auto add = [&](const QString &label, const QString &value) {
        if (value.isEmpty()) return;
        html += QStringLiteral("    <p class=\"message\">%1：%2</p>\n")
                    .arg(label, xmlEscape(value));
    };
    add(QStringLiteral("作者"), parsed.credits.author);
    add(QStringLiteral("插画"), parsed.credits.illustrator);
    add(QStringLiteral("译者"), parsed.credits.translator);
    add(QStringLiteral("图源"), parsed.credits.source);
    add(QStringLiteral("录入"), parsed.credits.transcriber);
    add(QStringLiteral("发布"), parsed.credits.publisher);
    if (!parsed.credits.forum.isEmpty()) {
        html += QStringLiteral("    <p class=\"message\">论坛：%1</p>\n").arg(xmlEscape(parsed.credits.forum));
    }
    if (extra.isEmpty()) {
        extra << QStringLiteral("仅供个人学习交流使用，禁作商业用途")
              << QStringLiteral("下载后请在24小时内删除，如果您喜欢本作品，请支持购买正版")
              << QStringLiteral("请尊重图源、翻译、扫图、录入、校对的辛勤劳动，未经允许严禁转载！");
    }
    for (const QString &line : extra) {
        html += QStringLiteral("    <p class=\"message\">%1</p>\n").arg(xmlEscape(line));
    }
    html += QStringLiteral("  </div>\n");
    return html;
}

QString synopsisBodyHtml(const ParsedManuscript &parsed)
{
    QString html = QStringLiteral("  <div>\n");
    const QStringList lines = parsed.synopsis.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (lines.isEmpty()) {
        html += QStringLiteral("    <p></p>\n");
    } else {
        for (const QString &line : lines) {
            html += QStringLiteral("    <p>%1</p>\n").arg(xmlEscape(line.trimmed()));
        }
    }
    html += QStringLiteral("  </div>\n");
    return html;
}

QString titleBodyHtml(const ParsedManuscript &parsed)
{
    QString html = QStringLiteral("  <div class=\"title\" style=\"margin: 1em 0 0 0;\">\n");
    html += QStringLiteral("    <p>%1</p>\n").arg(xmlEscape(parsed.title));
    if (!parsed.originalTitle.isEmpty() && parsed.originalTitle != parsed.title) {
        html += QStringLiteral("    <p>%1</p>\n").arg(xmlEscape(parsed.originalTitle));
    }
    html += QStringLiteral("  </div>\n");
    return html;
}

QString contentsBodyHtml(const ParsedManuscript &parsed, const QList<TemplatePage> &chapters)
{
    QString html = QStringLiteral(
        "  <div class=\"bold\" style=\"margin:8% 1%; width:24em;\">\n"
        "    <p class=\"zin em16\" style=\"margin:-0.5em 0 0 0\"><b>目录</b>"
        "<span class=\"em06\">Contents</span></p>\n"
        "    <table class=\"bc cline2\" style=\"margin:0 0 0 2em\">\n"
        "      <tbody><tr>\n        <td>\n");
    for (int i = 0; i < parsed.chapters.size(); ++i) {
        const ManuscriptChapter &chapter = parsed.chapters.at(i);
        const QString href = (i < chapters.size())
            ? QStringLiteral("../Text/") + fileName(chapters.at(i).bookPath)
            : QString();
        const int number = chapterNumberFromHeading(chapter.heading);
        QString box = chapter.numberLabel;
        if (number > 0) box = chineseTalkLabel(number);
        else if (box.contains(QChar(0x8A71))) box.replace(QChar(0x8A71), QChar(0x8BDD));
        const QString label = chapter.title.isEmpty() ? chapter.heading : chapter.title;
        html += QStringLiteral("          <p class=\"em07\"><br/></p>\n");
        html += QStringLiteral("          <p class=\"mulu2\"><span class=\"cbox1\">%1</span></p>\n")
                    .arg(xmlEscape(box));
        if (!href.isEmpty()) {
            html += QStringLiteral(
                "          <p class=\"mulu\"><a class=\"no-d\" href=\"%1\"><span class=\"co0\">%2</span></a></p>\n")
                        .arg(xmlEscape(href), xmlEscape(label));
        } else {
            html += QStringLiteral("          <p class=\"mulu\"><span class=\"co0\">%1</span></p>\n")
                        .arg(xmlEscape(label));
        }
    }
    html += QStringLiteral("        </td>\n      </tr>\n    </tbody>\n    </table>\n  </div>\n");
    return html;
}

QString stubSourceBody()
{
    return QStringLiteral("  <div>\n    <p>文稿已按模板排入章节页，此导入页可删除。</p>\n  </div>\n");
}

QJsonObject resourceObject(IBookWorkspace *workspace, const QString &id)
{
    const QJsonArray resources = workspace->resources();
    for (const QJsonValue &value : resources) {
        const QJsonObject object = value.toObject();
        if (object.value(QStringLiteral("resource_id")).toString() == id
            || object.value(QStringLiteral("book_path")).toString() == id) {
            return object;
        }
    }
    return QJsonObject();
}

QString hintTitleFromPath(const QString &book_path)
{
    return QFileInfo(book_path).completeBaseName();
}

int manuscriptScore(const QJsonObject &resource, IBookWorkspace *workspace)
{
    const QString kind = resource.value(QStringLiteral("kind")).toString();
    const QString path = resource.value(QStringLiteral("book_path")).toString();
    const QString media = resource.value(QStringLiteral("media_type")).toString();
    const int length = resource.value(QStringLiteral("text_length")).toInt();
    const QString role = templatePageRole(path);
    if (looksLikeTemplatePage(role) && role != QLatin1String("chapter")) return -1000;
    int score = 0;
    if (kind == QLatin1String("text") || media == QLatin1String("text/plain")
        || path.endsWith(QLatin1String(".txt"), Qt::CaseInsensitive)) {
        score += 80;
    }
    if (length > 4000) score += 20;
    if (length > 40000) score += 20;
    if (looksLikeTemplatePage(role) && role == QLatin1String("chapter") && length < 4000) score -= 50;
    const BookOpResult fragment = workspace->readFragment(
        resource.value(QStringLiteral("resource_id")).toString(), 0, 2500);
    if (fragment.ok) {
        const QString sample = fragment.data.value(QStringLiteral("text")).toString();
        if (sample.contains(QStringLiteral("［插图")) || sample.contains(QStringLiteral("［插圖"))
            || sample.contains(QStringLiteral("制作信息")) || sample.contains(QStringLiteral("第")) ) {
            score += 40;
        }
        if (lineLooksLikeChapterHeading(manuscriptPlainText(sample).section(QLatin1Char('\n'), 0, 40))) {
            score += 10;
        }
    }
    return score;
}

QList<QString> unusedImageBases(const QHash<QString, QString> &index,
                                const QStringList &used_names)
{
    QSet<QString> used;
    for (const QString &name : used_names) used.insert(name.toLower());
    QList<QString> unused;
    QSet<QString> seen;
    for (auto it = index.constBegin(); it != index.constEnd(); ++it) {
        const QString base = fileBaseName(it.value()).toLower();
        if (seen.contains(base)) continue;
        seen.insert(base);
        if (used.contains(base) || used.contains(fileName(it.value()).toLower())) continue;
        unused.append(base);
    }
    QCollator collator;
    collator.setNumericMode(true);
    std::sort(unused.begin(), unused.end(), [&](const QString &a, const QString &b) {
        return collator.compare(a, b) < 0;
    });
    return unused;
}

BookOpResult stageReplace(IBookWorkspace *workspace, const QString &resource_id, const QString &text)
{
    quint64 revision = workspace->resourceRevision(resource_id);
    if (revision == 0) revision = 1;
    return workspace->replaceText(resource_id, text, revision);
}

} // namespace

QString templatePageRole(const QString &book_path)
{
    const QString stem = fileBaseName(book_path).toLower();
    if (stem == QLatin1String("cover")) return QStringLiteral("cover");
    if (stem == QLatin1String("start")) return QStringLiteral("start");
    if (stem == QLatin1String("title")) return QStringLiteral("title");
    if (stem == QLatin1String("contents") || stem == QLatin1String("toc")
        || stem == QLatin1String("nav")) {
        return QStringLiteral("contents");
    }
    if (stem == QLatin1String("message") || stem == QLatin1String("colophon")
        || stem == QLatin1String("credits")) {
        return QStringLiteral("credits");
    }
    if (stem == QLatin1String("summary") || stem == QLatin1String("intro")
        || stem == QLatin1String("synopsis")) {
        return QStringLiteral("synopsis");
    }
    if (stem.startsWith(QLatin1String("illus"))) return QStringLiteral("illustration");
    if (stem.startsWith(QLatin1String("section"))) return QStringLiteral("chapter");
    return QString();
}

TemplateMap detectTemplateMap(IBookWorkspace *workspace)
{
    TemplateMap map;
    if (!workspace) return map;
    const QJsonArray resources = workspace->resources();
    for (const QJsonValue &value : resources) {
        const QJsonObject object = value.toObject();
        if (object.value(QStringLiteral("kind")).toString() != QLatin1String("xhtml")) continue;
        TemplatePage page;
        page.resourceId = object.value(QStringLiteral("resource_id")).toString();
        page.bookPath = object.value(QStringLiteral("book_path")).toString();
        page.role = templatePageRole(page.bookPath);
        page.number = trailingNumber(fileBaseName(page.bookPath));
        if (page.role == QLatin1String("cover")) map.cover = page;
        else if (page.role == QLatin1String("start")) map.start = page;
        else if (page.role == QLatin1String("title")) map.title = page;
        else if (page.role == QLatin1String("credits")) map.credits = page;
        else if (page.role == QLatin1String("synopsis")) map.synopsis = page;
        else if (page.role == QLatin1String("contents")) map.contents = page;
        else if (page.role == QLatin1String("illustration")) map.illustrations.append(page);
        else if (page.role == QLatin1String("chapter")) map.chapters.append(page);
    }
    std::sort(map.illustrations.begin(), map.illustrations.end(),
              [](const TemplatePage &a, const TemplatePage &b) { return a.number < b.number; });
    std::sort(map.chapters.begin(), map.chapters.end(),
              [](const TemplatePage &a, const TemplatePage &b) { return a.number < b.number; });
    map.detected = !map.cover.resourceId.isEmpty() && !map.chapters.isEmpty()
        && (!map.credits.resourceId.isEmpty() || !map.synopsis.resourceId.isEmpty());
    return map;
}

QString findManuscriptResourceId(IBookWorkspace *workspace, const QString &explicit_id)
{
    if (!workspace) return QString();
    if (!explicit_id.isEmpty()) {
        if (!resourceObject(workspace, explicit_id).isEmpty()) return explicit_id;
        return QString();
    }
    QString best;
    int best_score = 0;
    const QJsonArray resources = workspace->resources();
    for (const QJsonValue &value : resources) {
        const QJsonObject object = value.toObject();
        const QString kind = object.value(QStringLiteral("kind")).toString();
        if (kind == QLatin1String("image") || kind == QLatin1String("font")
            || kind == QLatin1String("css") || kind == QLatin1String("opf")
            || kind == QLatin1String("ncx")) {
            continue;
        }
        const int score = manuscriptScore(object, workspace);
        if (score > best_score) {
            best_score = score;
            best = object.value(QStringLiteral("resource_id")).toString();
        }
    }
    return best;
}

QJsonObject parseManuscriptInBook(IBookWorkspace *workspace, const QString &manuscript_id)
{
    if (!workspace) {
        return QJsonObject {
            { QStringLiteral("ok"), false },
            { QStringLiteral("code"), QStringLiteral("NO_BOOK") }
        };
    }
    const QString id = findManuscriptResourceId(workspace, manuscript_id);
    if (id.isEmpty()) {
        return QJsonObject {
            { QStringLiteral("ok"), false },
            { QStringLiteral("code"), QStringLiteral("MANUSCRIPT_NOT_FOUND") },
            { QStringLiteral("message"),
              QStringLiteral("No TXT/imported manuscript found. Drop the book folder into Book Browser first.") }
        };
    }
    const QJsonObject resource = resourceObject(workspace, id);
    const QString path = resource.value(QStringLiteral("book_path")).toString();
    const QString text = workspace->resourceText(id);
    const ParsedManuscript parsed = parseManuscriptText(text, hintTitleFromPath(path));
    QJsonObject summary = manuscriptSummaryJson(parsed);
    summary.insert(QStringLiteral("ok"), true);
    summary.insert(QStringLiteral("resource_id"), id);
    summary.insert(QStringLiteral("book_path"), path);
    const TemplateMap tmpl = detectTemplateMap(workspace);
    QJsonArray illus;
    for (const TemplatePage &page : tmpl.illustrations) {
        illus.append(QJsonObject {
            { QStringLiteral("resource_id"), page.resourceId },
            { QStringLiteral("book_path"), page.bookPath }
        });
    }
    QJsonArray chapters;
    for (const TemplatePage &page : tmpl.chapters) {
        chapters.append(QJsonObject {
            { QStringLiteral("resource_id"), page.resourceId },
            { QStringLiteral("book_path"), page.bookPath }
        });
    }
    QJsonArray images;
    const QHash<QString, QString> index = imageIndex(workspace);
    QSet<QString> seen;
    for (auto it = index.constBegin(); it != index.constEnd(); ++it) {
        const QString path_value = it.value();
        if (seen.contains(path_value)) continue;
        seen.insert(path_value);
        images.append(QJsonObject {
            { QStringLiteral("name"), fileBaseName(path_value) },
            { QStringLiteral("book_path"), path_value }
        });
    }
    QJsonArray resolved;
    for (const ManuscriptIllustration &marker : parsed.illustrations) {
        const QString found = resolveImage(index, marker.name);
        resolved.append(QJsonObject {
            { QStringLiteral("name"), marker.name },
            { QStringLiteral("book_path"), found },
            { QStringLiteral("missing"), found.isEmpty() }
        });
    }
    summary.insert(QStringLiteral("images_in_book"), images);
    summary.insert(QStringLiteral("resolved_images"), resolved);
    summary.insert(QStringLiteral("template"), QJsonObject {
        { QStringLiteral("detected"), tmpl.detected },
        { QStringLiteral("cover"), tmpl.cover.resourceId },
        { QStringLiteral("start"), tmpl.start.resourceId },
        { QStringLiteral("title"), tmpl.title.resourceId },
        { QStringLiteral("credits"), tmpl.credits.resourceId },
        { QStringLiteral("synopsis"), tmpl.synopsis.resourceId },
        { QStringLiteral("contents"), tmpl.contents.resourceId },
        { QStringLiteral("illustrations"), illus },
        { QStringLiteral("chapters"), chapters }
    });
    return summary;
}

BookOpResult fillTemplateSection(IBookWorkspace *workspace,
                                 const QString &resource_id,
                                 const QString &role,
                                 const QString &manuscript_id,
                                 int chapter_index,
                                 const QString &image_name)
{
    if (!workspace) {
        return BookOpResult::error(QStringLiteral("NO_BOOK"), QStringLiteral("No book is open"));
    }
    const QJsonObject parsed_json = parseManuscriptInBook(workspace, manuscript_id);
    if (!parsed_json.value(QStringLiteral("ok")).toBool()) {
        return BookOpResult::error(parsed_json.value(QStringLiteral("code")).toString(),
                                   parsed_json.value(QStringLiteral("message")).toString(),
                                   parsed_json);
    }
    const QString source_id = parsed_json.value(QStringLiteral("resource_id")).toString();
    const ParsedManuscript parsed = parseManuscriptText(
        workspace->resourceText(source_id),
        parsed_json.value(QStringLiteral("title")).toString());
    const QHash<QString, QString> images = imageIndex(workspace);
    const QString skeleton = currentTextOf(workspace, resource_id);
    QString filled;
    QString used_role = role;
    if (used_role.isEmpty()) used_role = templatePageRole(resourceObject(workspace, resource_id)
                                                              .value(QStringLiteral("book_path")).toString());
    if (used_role == QLatin1String("chapter")) {
        if (chapter_index < 0 || chapter_index >= parsed.chapters.size()) {
            return BookOpResult::error(QStringLiteral("CHAPTER_OUT_OF_RANGE"),
                                       QStringLiteral("chapter_index is out of range"));
        }
        filled = setTitleAndBody(skeleton, parsed.chapters.at(chapter_index).heading,
                                 chapterBodyHtml(parsed.chapters.at(chapter_index), images));
    } else if (used_role == QLatin1String("credits")) {
        filled = setTitleAndBody(skeleton, QStringLiteral("制作信息"), creditsBodyHtml(parsed, skeleton));
    } else if (used_role == QLatin1String("synopsis")) {
        filled = setTitleAndBody(skeleton, QStringLiteral("简介"), synopsisBodyHtml(parsed));
    } else if (used_role == QLatin1String("title")) {
        filled = setTitleAndBody(skeleton, parsed.title, titleBodyHtml(parsed));
    } else if (used_role == QLatin1String("contents")) {
        const TemplateMap tmpl = detectTemplateMap(workspace);
        filled = setTitleAndBody(skeleton, QStringLiteral("目录"), contentsBodyHtml(parsed, tmpl.chapters));
    } else if (used_role == QLatin1String("illustration") || used_role == QLatin1String("cover")
               || used_role == QLatin1String("start")) {
        const QString resolved = resolveImage(images, image_name);
        if (resolved.isEmpty()) {
            return BookOpResult::error(QStringLiteral("IMAGE_NOT_FOUND"),
                                       QStringLiteral("No image named %1").arg(image_name));
        }
        filled = rewriteFirstImage(skeleton, imageHrefFromBookPath(resolved), image_name);
    } else {
        return BookOpResult::error(QStringLiteral("UNKNOWN_ROLE"),
                                   QStringLiteral("Unknown fill role %1").arg(used_role));
    }
    const BookOpResult replaced = stageReplace(workspace, resource_id, filled);
    if (!replaced.ok) return replaced;
    QJsonObject data = replaced.data;
    data.insert(QStringLiteral("role"), used_role);
    data.insert(QStringLiteral("heading"),
                used_role == QLatin1String("chapter") && chapter_index >= 0
                    ? parsed.chapters.at(chapter_index).heading : QString());
    return BookOpResult::success(data, false, true);
}

BookOpResult typesetFromManuscript(IBookWorkspace *workspace, const TypesetOptions &options)
{
    if (!workspace) {
        return BookOpResult::error(QStringLiteral("NO_BOOK"), QStringLiteral("No book is open"));
    }
    TemplateMap tmpl = detectTemplateMap(workspace);
    if (tmpl.chapters.isEmpty()) {
        return BookOpResult::error(
            QStringLiteral("TEMPLATE_NOT_DETECTED"),
            QStringLiteral("Open 轻小说模板.epub first. Need cover/start/title/message/summary/illus/Section pages."));
    }
    const QString manuscript_id = findManuscriptResourceId(workspace, options.manuscriptId);
    if (manuscript_id.isEmpty()) {
        return BookOpResult::error(
            QStringLiteral("MANUSCRIPT_NOT_FOUND"),
            QStringLiteral("Drop the book's TXT and images into Book Browser, then retry."));
    }
    const QJsonObject resource = resourceObject(workspace, manuscript_id);
    const ParsedManuscript parsed = parseManuscriptText(
        workspace->resourceText(manuscript_id), hintTitleFromPath(resource.value(QStringLiteral("book_path")).toString()));
    if (parsed.chapters.isEmpty()) {
        return BookOpResult::error(
            QStringLiteral("NO_CHAPTERS"),
            QStringLiteral("The manuscript was found but no 第N話/后记 headings were detected."),
            QJsonObject { { QStringLiteral("resource_id"), manuscript_id } });
    }

    const BookOpResult ensured = workspace->hasOpenTransaction()
        ? BookOpResult::success(QJsonObject())
        : workspace->beginTransaction(QStringLiteral("typeset from manuscript"));
    if (!ensured.ok) return ensured;

    QJsonArray copied;
    QList<TemplatePage> chapter_pages = tmpl.chapters;
    while (chapter_pages.size() < parsed.chapters.size()) {
        const TemplatePage &source = chapter_pages.last();
        const BookOpResult copy = workspace->copyResource(source.resourceId, QString(), true);
        if (!copy.ok) return copy;
        TemplatePage added;
        added.resourceId = copy.data.value(QStringLiteral("resource_id")).toString();
        added.bookPath = copy.data.value(QStringLiteral("book_path")).toString();
        added.role = QStringLiteral("chapter");
        added.number = trailingNumber(fileBaseName(added.bookPath));
        chapter_pages.append(added);
        copied.append(copy.data);
    }
    tmpl.chapters = chapter_pages;

    const QHash<QString, QString> images = imageIndex(workspace);
    QJsonArray filled;
    QJsonArray image_map;
    QJsonArray missing;
    QJsonArray warnings;
    QStringList used_images;

    auto map_image_page = [&](const TemplatePage &page, const QString &wanted, const QString &fallback_alt) {
        if (page.resourceId.isEmpty()) return;
        QString name = wanted;
        QString path = resolveImage(images, name);
        if (path.isEmpty() && name.isEmpty()) return;
        if (path.isEmpty()) {
            missing.append(name);
            warnings.append(QStringLiteral("%1: image %2 not in the book").arg(page.bookPath, name));
            return;
        }
        used_images.append(fileBaseName(path));
        const QString skeleton = currentTextOf(workspace, page.resourceId);
        const QString href = imageHrefFromBookPath(path);
        const QString next = rewriteFirstImage(skeleton.isEmpty() ? workspace->resourceText(page.resourceId) : skeleton,
                                               href, fallback_alt.isEmpty() ? name : fallback_alt);
        const BookOpResult replaced = stageReplace(workspace, page.resourceId, next);
        if (!replaced.ok) {
            warnings.append(replaced.message);
            return;
        }
        filled.append(QJsonObject {
            { QStringLiteral("resource_id"), page.resourceId },
            { QStringLiteral("book_path"), page.bookPath },
            { QStringLiteral("role"), page.role }
        });
        image_map.append(QJsonObject {
            { QStringLiteral("page"), fileBaseName(page.bookPath) },
            { QStringLiteral("from_marker"), name },
            { QStringLiteral("to"), fileName(path) }
        });
    };

    QStringList front = parsed.frontIllustrationNames;
    QString cover_name;
    QString start_name;
    QStringList color_names;
    for (const QString &name : front) {
        const QString lower = name.toLower();
        if (lower == QLatin1String("cover") && cover_name.isEmpty()) cover_name = name;
        else if (lower == QLatin1String("start") && start_name.isEmpty()) start_name = name;
        else color_names.append(name);
    }
    if (cover_name.isEmpty() && resolveImage(images, QStringLiteral("cover")).isEmpty() == false) {
        cover_name = QStringLiteral("cover");
    }
    if (start_name.isEmpty() && !resolveImage(images, QStringLiteral("start")).isEmpty()) {
        start_name = QStringLiteral("start");
    }
    if (color_names.isEmpty()) {
        const QList<QString> leftover = unusedImageBases(images, QStringList {
            QStringLiteral("cover"), QStringLiteral("start"), QStringLiteral("note")
        });
        for (const QString &base : leftover) {
            if (base == QLatin1String("cover") || base == QLatin1String("start")
                || base == QLatin1String("note")) {
                continue;
            }
            color_names.append(base);
        }
    }

    map_image_page(tmpl.cover, cover_name, QStringLiteral("cover"));
    map_image_page(tmpl.start, start_name, QStringLiteral("start"));
    for (int i = 0; i < tmpl.illustrations.size(); ++i) {
        const QString name = i < color_names.size() ? color_names.at(i) : QString();
        if (name.isEmpty()) {
            warnings.append(QStringLiteral("%1 left as a placeholder (no matching color page)")
                                .arg(tmpl.illustrations.at(i).bookPath));
            continue;
        }
        map_image_page(tmpl.illustrations.at(i), name, name);
    }

    if (!tmpl.title.resourceId.isEmpty()) {
        const QString skeleton = currentTextOf(workspace, tmpl.title.resourceId);
        const BookOpResult replaced = stageReplace(
            workspace, tmpl.title.resourceId,
            setTitleAndBody(skeleton, parsed.title, titleBodyHtml(parsed)));
        if (replaced.ok) {
            filled.append(QJsonObject {
                { QStringLiteral("resource_id"), tmpl.title.resourceId },
                { QStringLiteral("role"), QStringLiteral("title") }
            });
        }
    }
    if (!tmpl.credits.resourceId.isEmpty()) {
        const QString skeleton = currentTextOf(workspace, tmpl.credits.resourceId);
        const BookOpResult replaced = stageReplace(
            workspace, tmpl.credits.resourceId,
            setTitleAndBody(skeleton, QStringLiteral("制作信息"), creditsBodyHtml(parsed, skeleton)));
        if (replaced.ok) {
            filled.append(QJsonObject {
                { QStringLiteral("resource_id"), tmpl.credits.resourceId },
                { QStringLiteral("role"), QStringLiteral("credits") }
            });
        }
    }
    if (!tmpl.synopsis.resourceId.isEmpty()) {
        const QString skeleton = currentTextOf(workspace, tmpl.synopsis.resourceId);
        const BookOpResult replaced = stageReplace(
            workspace, tmpl.synopsis.resourceId,
            setTitleAndBody(skeleton, QStringLiteral("简介"), synopsisBodyHtml(parsed)));
        if (replaced.ok) {
            filled.append(QJsonObject {
                { QStringLiteral("resource_id"), tmpl.synopsis.resourceId },
                { QStringLiteral("role"), QStringLiteral("synopsis") }
            });
        }
    }
    if (!tmpl.contents.resourceId.isEmpty()) {
        const QString skeleton = currentTextOf(workspace, tmpl.contents.resourceId);
        const BookOpResult replaced = stageReplace(
            workspace, tmpl.contents.resourceId,
            setTitleAndBody(skeleton, QStringLiteral("目录"), contentsBodyHtml(parsed, chapter_pages)));
        if (replaced.ok) {
            filled.append(QJsonObject {
                { QStringLiteral("resource_id"), tmpl.contents.resourceId },
                { QStringLiteral("role"), QStringLiteral("contents") }
            });
        }
    }

    const QString chapter_skeleton = currentTextOf(workspace, tmpl.chapters.first().resourceId);
    for (int i = 0; i < parsed.chapters.size(); ++i) {
        const ManuscriptChapter &chapter = parsed.chapters.at(i);
        const TemplatePage &page = chapter_pages.at(i);
        const QString filled_text = setTitleAndBody(
            chapter_skeleton, chapter.heading, chapterBodyHtml(chapter, images));
        const BookOpResult replaced = stageReplace(workspace, page.resourceId, filled_text);
        if (!replaced.ok) return replaced;
        for (const QString &name : chapter.illustrationNames) {
            const QString path = resolveImage(images, name);
            if (path.isEmpty()) missing.append(name);
            else used_images.append(fileBaseName(path));
        }
        filled.append(QJsonObject {
            { QStringLiteral("resource_id"), page.resourceId },
            { QStringLiteral("book_path"), page.bookPath },
            { QStringLiteral("role"), QStringLiteral("chapter") },
            { QStringLiteral("heading"), chapter.heading },
            { QStringLiteral("chars"), chapter.charCount }
        });
    }

    bool retired = false;
    const QString source_role = templatePageRole(resource.value(QStringLiteral("book_path")).toString());
    const bool source_is_template = looksLikeTemplatePage(source_role);
    if (options.retireSource && !source_is_template
        && resource.value(QStringLiteral("kind")).toString() == QLatin1String("xhtml")) {
        const QString skeleton = currentTextOf(workspace, manuscript_id);
        const BookOpResult replaced = stageReplace(
            workspace, manuscript_id,
            setTitleAndBody(skeleton, QStringLiteral("已导入文稿"), stubSourceBody()));
        retired = replaced.ok;
    }

    if (options.updateMetadata) {
        QJsonObject patch;
        if (!parsed.title.isEmpty()) patch.insert(QStringLiteral("title"), parsed.title);
        if (!parsed.credits.author.isEmpty()) patch.insert(QStringLiteral("creator"), parsed.credits.author);
        patch.insert(QStringLiteral("language"), QStringLiteral("zh-CN"));
        workspace->updateMetadata(patch);
    }

    QJsonArray unused;
    const QList<QString> leftover = unusedImageBases(images, used_images);
    for (const QString &base : leftover) unused.append(base);

    return BookOpResult::success(QJsonObject {
        { QStringLiteral("staged"), true },
        { QStringLiteral("live_unchanged"), true },
        { QStringLiteral("manuscript_id"), manuscript_id },
        { QStringLiteral("title"), parsed.title },
        { QStringLiteral("chapters_filled"), parsed.chapters.size() },
        { QStringLiteral("sections_copied"), copied.size() },
        { QStringLiteral("copied"), copied },
        { QStringLiteral("filled"), filled },
        { QStringLiteral("image_map"), image_map },
        { QStringLiteral("missing_images"), missing },
        { QStringLiteral("unplaced_images"), unused },
        { QStringLiteral("retired_source"), retired },
        { QStringLiteral("warnings"), warnings }
    }, false, true);
}

} // namespace SigilAgent
