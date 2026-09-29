#include <cstdlib>
#include <iostream>

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

#include "Agent/Execution/MemoryBookWorkspace.h"
#include "Agent/Execution/PreflightAudit.h"
#include "Agent/Tools/BookTools.h"
#include "Agent/Tools/ToolRegistry.h"

namespace
{

void Require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

SigilAgent::MemoryResource text(const QString &id, const QString &path, const QString &kind,
                                const QString &media, const QString &body)
{
    SigilAgent::MemoryResource resource;
    resource.id = id;
    resource.bookPath = path;
    resource.kind = kind;
    resource.mediaType = media;
    resource.text = body;
    return resource;
}

SigilAgent::MemoryResource binary(const QString &id, const QString &path, const QString &kind,
                                  const QString &media)
{
    SigilAgent::MemoryResource resource;
    resource.id = id;
    resource.bookPath = path;
    resource.kind = kind;
    resource.mediaType = media;
    resource.binary = QByteArray("bytes");
    return resource;
}

QString page(const QString &lang, const QString &body)
{
    return QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<html xmlns=\"http://www.w3.org/1999/xhtml\"%1>\n"
        "<head><title>t</title><link href=\"../Styles/style.css\" rel=\"stylesheet\" type=\"text/css\"/></head>\n"
        "<body>\n%2\n</body>\n</html>\n")
        .arg(lang.isEmpty() ? QString() : QStringLiteral(" xml:lang=\"%1\" lang=\"%1\"").arg(lang), body);
}

SigilAgent::MemoryBookWorkspace defectiveBook()
{
    using namespace SigilAgent;
    MemoryBookWorkspace book;
    book.setEpubVersion(QStringLiteral("3.0"));
    book.setMetadata(QJsonObject {
        { QStringLiteral("title"), QStringLiteral("预检样书") },
        { QStringLiteral("language"), QStringLiteral("zh-CN") }
    });
    book.addResource(text(QStringLiteral("opf"), QStringLiteral("OEBPS/content.opf"), QStringLiteral("opf"),
        QStringLiteral("application/oebps-package+xml"),
        QStringLiteral("<package version=\"3.0\"><manifest>\n"
                       "<item id=\"nav\" href=\"nav.xhtml\" properties=\"nav\" media-type=\"application/xhtml+xml\"/>\n"
                       "<item id=\"c1\" href=\"Text/ch1.xhtml\" media-type=\"application/xhtml+xml\"/>\n"
                       "<item id=\"ghost\" href=\"Text/ghost.xhtml\" media-type=\"application/xhtml+xml\"/>\n"
                       "<item id=\"cover\" href=\"Images/cover.jpg\" properties=\"cover-image\" media-type=\"image/jpeg\"/>\n"
                       "</manifest></package>")));
    book.addResource(text(QStringLiteral("nav"), QStringLiteral("OEBPS/nav.xhtml"), QStringLiteral("xhtml"),
        QStringLiteral("application/xhtml+xml"),
        page(QStringLiteral("zh-CN"),
             QStringLiteral("<nav epub:type=\"toc\" xmlns:epub=\"http://www.idpf.org/2007/ops\"><ol>"
                            "<li><a href=\"Text/ch1.xhtml\">一</a></li></ol></nav>"))));
    book.addResource(text(QStringLiteral("ch1"), QStringLiteral("OEBPS/Text/ch1.xhtml"), QStringLiteral("xhtml"),
        QStringLiteral("application/xhtml+xml"),
        page(QStringLiteral("zh-CN"),
             QStringLiteral("<h1 id=\"top\">第一章</h1>\n"
                            "<h3>小节</h3>\n"
                            "<p><img src=\"../Images/p001.jpg\"/></p>\n"
                            "<p><img alt=\"p002\" src=\"../Images/p002.jpg\"/></p>\n"
                            "<p><a href=\"ch2.xhtml#note-9\">注</a> <a href=\"Missing.xhtml\">失效</a>"
                            " <a href=\"#top\">回到顶部</a> <a href=\"https://example.com\">外链</a></p>\n"
                            "<p><img alt=\"\" src=\"../images/Deco.png\"/></p>"))));
    book.addResource(text(QStringLiteral("ch2"), QStringLiteral("OEBPS/Text/ch2.xhtml"), QStringLiteral("xhtml"),
        QStringLiteral("application/xhtml+xml"),
        page(QString(), QStringLiteral("<h1 id=\"note-1\">第二章</h1>\n<p>&nbsp;空格</p>"))));
    book.addResource(text(QStringLiteral("ch3"), QStringLiteral("OEBPS/Text/ch3.xhtml"), QStringLiteral("xhtml"),
        QStringLiteral("application/xhtml+xml"),
        page(QStringLiteral("zh-CN"), QStringLiteral("<h1>第三章</h1>\n<p>未闭合<p>"))));
    book.addResource(text(QStringLiteral("css"), QStringLiteral("OEBPS/Styles/style.css"), QStringLiteral("css"),
        QStringLiteral("text/css"),
        QStringLiteral("@font-face { font-family: Kai; src: url(\"../Fonts/kai.ttf\"); }\n"
                       "@font-face { font-family: Hei; src: url(../Fonts/missing.otf); }\n"
                       "body { background: url(data:image/png;base64,AAAA); }\n")));
    book.addResource(binary(QStringLiteral("cover"), QStringLiteral("OEBPS/Images/cover.jpg"), QStringLiteral("image"), QStringLiteral("image/jpeg")));
    book.addResource(binary(QStringLiteral("p001"), QStringLiteral("OEBPS/Images/p001.jpg"), QStringLiteral("image"), QStringLiteral("image/jpeg")));
    book.addResource(binary(QStringLiteral("p002"), QStringLiteral("OEBPS/Images/p002.jpg"), QStringLiteral("image"), QStringLiteral("image/jpeg")));
    book.addResource(binary(QStringLiteral("deco"), QStringLiteral("OEBPS/Images/Deco.png"), QStringLiteral("image"), QStringLiteral("image/png")));
    book.addResource(binary(QStringLiteral("unused"), QStringLiteral("OEBPS/Images/unused.jpg"), QStringLiteral("image"), QStringLiteral("image/jpeg")));
    book.addResource(binary(QStringLiteral("kai"), QStringLiteral("OEBPS/Fonts/kai.ttf"), QStringLiteral("font"), QStringLiteral("font/ttf")));
    book.setSpine({ QStringLiteral("ch1"), QStringLiteral("ch2"), QStringLiteral("ch3") });
    book.setToc(QJsonArray {
        QJsonObject { { QStringLiteral("label"), QStringLiteral("一") }, { QStringLiteral("href"), QStringLiteral("Text/ch1.xhtml#top") } },
        QJsonObject { { QStringLiteral("label"), QStringLiteral("二") }, { QStringLiteral("href"), QStringLiteral("Text/ch2.xhtml#gone") } },
        QJsonObject { { QStringLiteral("label"), QStringLiteral("四") }, { QStringLiteral("href"), QStringLiteral("Text/ch4.xhtml") } }
    });
    return book;
}

QSet<QString> rulesIn(const QJsonArray &issues)
{
    QSet<QString> rules;
    for (const QJsonValue &value : issues) rules.insert(value.toObject().value(QStringLiteral("rule_id")).toString());
    return rules;
}

const QJsonObject *findIssue(const QJsonArray &issues, const QString &rule, const QString &needle, QJsonObject *out)
{
    for (const QJsonValue &value : issues) {
        const QJsonObject issue = value.toObject();
        if (issue.value(QStringLiteral("rule_id")).toString() != rule) continue;
        if (!needle.isEmpty() && !issue.value(QStringLiteral("evidence")).toString().contains(needle)
            && !issue.value(QStringLiteral("book_path")).toString().contains(needle)) {
            continue;
        }
        *out = issue;
        return out;
    }
    return nullptr;
}

} // namespace

int main()
{
    using namespace SigilAgent;
    MemoryBookWorkspace book = defectiveBook();
    const quint64 revision = book.revision();
    const QString ch1_before = book.resourceText(QStringLiteral("ch1"));

    const QJsonArray all = preflightIssues(&book, QStringLiteral("release-basic"));
    const QSet<QString> rules = rulesIn(all);
    for (const QString &expected : {
             QStringLiteral("LINK_TARGET_MISSING"), QStringLiteral("LINK_FRAGMENT_MISSING"),
             QStringLiteral("LINK_PATH_CASE_MISMATCH"), QStringLiteral("CSS_URL_MISSING"),
             QStringLiteral("TOC_TARGET_MISSING"), QStringLiteral("MANIFEST_HREF_MISSING"),
             QStringLiteral("METADATA_REQUIRED_MISSING"), QStringLiteral("XHTML_NOT_WELLFORMED"),
             QStringLiteral("XHTML_UNDECLARED_ENTITY"), QStringLiteral("XHTML_LANG_MISSING"),
             QStringLiteral("IMG_ALT_MISSING"), QStringLiteral("IMG_ALT_FILENAME"),
             QStringLiteral("HEADING_LEVEL_SKIP"), QStringLiteral("RESOURCE_UNREFERENCED") }) {
        if (!rules.contains(expected)) std::cerr << "missing rule " << expected.toStdString() << '\n';
        Require(rules.contains(expected), "defective fixture must trigger every seeded rule");
    }

    QJsonObject issue;
    Require(findIssue(all, QStringLiteral("LINK_FRAGMENT_MISSING"), QStringLiteral("note-9"), &issue)
                && issue.value(QStringLiteral("book_path")).toString() == QStringLiteral("OEBPS/Text/ch1.xhtml")
                && issue.value(QStringLiteral("line")).toInt() == 9,
            "fragment issue must locate the source file and 1-based line");
    Require(findIssue(all, QStringLiteral("LINK_FRAGMENT_MISSING"), QStringLiteral("#gone"), &issue),
            "TOC fragments must be resolved against the target document");
    Require(!findIssue(all, QStringLiteral("LINK_FRAGMENT_MISSING"), QStringLiteral("#top"), &issue),
            "same-document fragments that exist must not be reported");
    Require(findIssue(all, QStringLiteral("LINK_PATH_CASE_MISMATCH"), QStringLiteral("images/Deco.png"), &issue)
                && issue.value(QStringLiteral("fix_kind")).toString() == QStringLiteral("mechanical"),
            "case-only path mismatches must be separated as mechanical fixes");
    Require(!findIssue(all, QStringLiteral("IMG_ALT_MISSING"), QStringLiteral("Deco"), &issue),
            "an explicit empty alt marks a decorative image and must not be reported missing");
    Require(findIssue(all, QStringLiteral("IMG_ALT_MISSING"), QStringLiteral("p001"), &issue)
                && issue.value(QStringLiteral("manual_required")).toBool(),
            "missing alt text must require a human decision");
    Require(findIssue(all, QStringLiteral("CSS_URL_MISSING"), QStringLiteral("missing.otf"), &issue)
                && issue.value(QStringLiteral("line")).toInt() == 2,
            "CSS url() must be checked with a line number");
    Require(!findIssue(all, QStringLiteral("CSS_URL_MISSING"), QStringLiteral("data:"), &issue),
            "data URLs are not external resources");
    Require(!findIssue(all, QStringLiteral("LINK_TARGET_MISSING"), QStringLiteral("example.com"), &issue),
            "external links are not resolved against the book");
    Require(findIssue(all, QStringLiteral("METADATA_REQUIRED_MISSING"), QStringLiteral("identifier"), &issue),
            "missing dc:identifier must be reported");
    Require(findIssue(all, QStringLiteral("XHTML_LANG_MISSING"), QStringLiteral("ch2"), &issue)
                && !findIssue(all, QStringLiteral("XHTML_LANG_MISSING"), QStringLiteral("nav.xhtml"), &issue),
            "spine documents need lang, while the navigation document is exempt");
    Require(findIssue(all, QStringLiteral("XHTML_UNDECLARED_ENTITY"), QStringLiteral("nbsp"), &issue)
                && issue.value(QStringLiteral("severity")).toString() == QStringLiteral("error"),
            "undeclared named entities are errors in EPUB 3");
    Require(findIssue(all, QStringLiteral("RESOURCE_UNREFERENCED"), QStringLiteral("unused.jpg"), &issue)
                && !findIssue(all, QStringLiteral("RESOURCE_UNREFERENCED"), QStringLiteral("cover.jpg"), &issue)
                && !findIssue(all, QStringLiteral("RESOURCE_UNREFERENCED"), QStringLiteral("kai.ttf"), &issue),
            "cover-image and CSS-referenced fonts count as referenced");
    Require(all.first().toObject().value(QStringLiteral("severity")).toString() == QStringLiteral("error")
                && all.last().toObject().value(QStringLiteral("severity")).toString() == QStringLiteral("info"),
            "issues must be ordered by severity");

    const QJsonArray a11y = preflightIssues(&book, QStringLiteral("a11y-basic"));
    Require(!rulesIn(a11y).contains(QStringLiteral("LINK_TARGET_MISSING"))
                && rulesIn(a11y).contains(QStringLiteral("IMG_ALT_MISSING")),
            "profiles must select their own rules");

    ToolRegistry registry;
    registerBookTools(&registry, &book);
    auto call = [&](const QString &name, const QJsonObject &arguments) {
        IAgentTool *tool = registry.find(name);
        Require(tool && !tool->descriptor().mutatesBook, "preflight tools must be registered as read-only");
        return tool->execute(arguments);
    };
    const ToolResult profiles = call(QStringLiteral("preflight.profiles"), QJsonObject());
    Require(profiles.ok && profiles.data.value(QStringLiteral("profiles")).toArray().size() == 3,
            "profiles must be listed");
    Require(!call(QStringLiteral("preflight.run"), QJsonObject {
                { QStringLiteral("profile_id"), QStringLiteral("store-apple") } }).ok,
            "unknown profiles must be rejected, not silently replaced");

    const ToolResult run = call(QStringLiteral("preflight.run"), QJsonObject { { QStringLiteral("limit"), 5 } });
    Require(run.ok, "preflight.run must succeed");
    const QString run_id = run.data.value(QStringLiteral("run_id")).toString();
    const QJsonObject status = run.data.value(QStringLiteral("validation_status")).toObject();
    Require(status.value(QStringLiteral("epubcheck")).toObject().value(QStringLiteral("status")).toString()
                    == QStringLiteral("not_run")
                && status.value(QStringLiteral("ace")).toObject().value(QStringLiteral("status")).toString()
                    == QStringLiteral("not_run")
                && status.value(QStringLiteral("local")).toString() == QStringLiteral("errors_found")
                && !run.data.value(QStringLiteral("saved_epub_checked")).toBool(),
            "external validators must be reported as not_run and local status must reflect errors");
    Require(run.data.value(QStringLiteral("total_count")).toInt() == all.size()
                && run.data.value(QStringLiteral("returned_count")).toInt() == 5
                && run.data.value(QStringLiteral("has_more")).toBool(),
            "run must return an exact total and the first bounded page");

    int collected = run.data.value(QStringLiteral("returned_count")).toInt();
    int offset = run.data.value(QStringLiteral("next_offset")).toInt();
    QSet<QString> ids;
    for (const QJsonValue &value : run.data.value(QStringLiteral("issues")).toArray()) {
        ids.insert(value.toObject().value(QStringLiteral("issue_id")).toString());
    }
    while (true) {
        const ToolResult next = call(QStringLiteral("preflight.issues"), QJsonObject {
            { QStringLiteral("run_id"), run_id }, { QStringLiteral("offset"), offset }, { QStringLiteral("limit"), 5 } });
        Require(next.ok, "issue continuation must succeed");
        for (const QJsonValue &value : next.data.value(QStringLiteral("issues")).toArray()) {
            ids.insert(value.toObject().value(QStringLiteral("issue_id")).toString());
        }
        collected += next.data.value(QStringLiteral("returned_count")).toInt();
        if (!next.data.value(QStringLiteral("has_more")).toBool()) break;
        offset = next.data.value(QStringLiteral("next_offset")).toInt();
    }
    Require(collected == all.size() && ids.size() == all.size(), "paging must return every issue exactly once");
    const ToolResult errors_only = call(QStringLiteral("preflight.issues"), QJsonObject {
        { QStringLiteral("run_id"), run_id }, { QStringLiteral("severity"), QStringLiteral("error") },
        { QStringLiteral("limit"), 200 } });
    Require(errors_only.ok && rulesIn(errors_only.data.value(QStringLiteral("issues")).toArray())
                .contains(QStringLiteral("XHTML_NOT_WELLFORMED"))
                && !rulesIn(errors_only.data.value(QStringLiteral("issues")).toArray())
                    .contains(QStringLiteral("RESOURCE_UNREFERENCED")),
            "severity filter must apply");
    Require(book.revision() == revision && book.resourceText(QStringLiteral("ch1")) == ch1_before
                && !book.hasOpenTransaction(),
            "preflight must not change the book or open a transaction");

    Require(registry.find(QStringLiteral("transaction.begin"))->execute(QJsonObject()).ok, "begin");
    const QString fixed = book.workingText(QStringLiteral("ch1")).replace(QStringLiteral("Missing.xhtml"), QStringLiteral("ch2.xhtml"));
    Require(book.replaceText(QStringLiteral("ch1"), fixed, book.resourceRevision(QStringLiteral("ch1"))).ok, "stage fix");
    const ToolResult stale = call(QStringLiteral("preflight.issues"), QJsonObject { { QStringLiteral("run_id"), run_id } });
    Require(!stale.ok && stale.code == QStringLiteral("PREFLIGHT_STALE"),
            "a staged edit must invalidate the previous run");
    const ToolResult rerun = call(QStringLiteral("preflight.run"), QJsonObject { { QStringLiteral("limit"), 200 } });
    Require(rerun.ok && rerun.data.value(QStringLiteral("includes_staged_changes")).toBool()
                && !findIssue(rerun.data.value(QStringLiteral("issues")).toArray(), QStringLiteral("LINK_TARGET_MISSING"),
                              QStringLiteral("Missing.xhtml"), &issue)
                && rerun.data.value(QStringLiteral("total_count")).toInt() == all.size() - 1,
            "a new run must evaluate staged text");
    Require(!call(QStringLiteral("preflight.issues"), QJsonObject { { QStringLiteral("run_id"), run_id } }).ok,
            "a superseded run_id must be rejected");

    MemoryBookWorkspace clean = MemoryBookWorkspace::samplePhysicsBook();
    const QJsonArray clean_issues = preflightIssues(&clean, QStringLiteral("epub-core"));
    for (const QJsonValue &value : clean_issues) {
        Require(value.toObject().value(QStringLiteral("severity")).toString() != QStringLiteral("error")
                    || value.toObject().value(QStringLiteral("rule_id")).toString()
                        == QStringLiteral("METADATA_REQUIRED_MISSING"),
                "the clean sample must only lack its fixture identifier");
    }
    std::cout << "preflight issues=" << all.size() << " rules=" << rules.size() << '\n';

    MemoryBookWorkspace novel;
    novel.setEpubVersion(QStringLiteral("2.0"));
    novel.setMetadata(QJsonObject { { QStringLiteral("title"), QStringLiteral("书") }, { QStringLiteral("language"), QStringLiteral("zh") } });
    novel.addResource(text(QStringLiteral("cover"), QStringLiteral("OEBPS/Text/cover.xhtml"), QStringLiteral("xhtml"),
        QStringLiteral("application/xhtml+xml"), page(QStringLiteral("zh"), QStringLiteral("<div><img alt=\"封面\" src=\"../Images/cover.jpg\"/></div>"))));
    novel.addResource(text(QStringLiteral("contents"), QStringLiteral("OEBPS/Text/contents.xhtml"), QStringLiteral("xhtml"),
        QStringLiteral("application/xhtml+xml"), page(QStringLiteral("zh"), QStringLiteral("<h2>目录</h2><p><a href=\"Section001.xhtml\">序章</a></p>"))));
    novel.addResource(text(QStringLiteral("s1"), QStringLiteral("OEBPS/Text/Section001.xhtml"), QStringLiteral("xhtml"),
        QStringLiteral("application/xhtml+xml"),
        page(QStringLiteral("zh"), QStringLiteral("<h1><ruby>序<rt>じょ</rt></ruby>　章</h1>\n<p>%1</p>").arg(QString(300, QChar(0x6587))))));
    novel.addResource(text(QStringLiteral("s2"), QStringLiteral("OEBPS/Text/Section002.xhtml"), QStringLiteral("xhtml"),
        QStringLiteral("application/xhtml+xml"),
        page(QStringLiteral("zh"), QStringLiteral("<h1>第一章　邂逅</h1>\n<p>%1</p>").arg(QString(300, QChar(0x6587))))));
    novel.addResource(text(QStringLiteral("s3"), QStringLiteral("OEBPS/Text/Section003.xhtml"), QStringLiteral("xhtml"),
        QStringLiteral("application/xhtml+xml"),
        page(QStringLiteral("zh"), QStringLiteral("<h1>第二章　邊境</h1>\n<p>%1</p>").arg(QString(300, QChar(0x6587))))));
    novel.addResource(text(QStringLiteral("s4"), QStringLiteral("OEBPS/Text/Section004.xhtml"), QStringLiteral("xhtml"),
        QStringLiteral("application/xhtml+xml"),
        page(QStringLiteral("zh"), QStringLiteral("<p>%1</p>").arg(QString(600, QChar(0x6587))))));
    novel.setSpine({ QStringLiteral("cover"), QStringLiteral("contents"), QStringLiteral("s1"), QStringLiteral("s2"),
                     QStringLiteral("s3"), QStringLiteral("s4") });
    novel.setToc(QJsonArray {
        QJsonObject { { QStringLiteral("label"), QStringLiteral("封面") }, { QStringLiteral("href"), QStringLiteral("OEBPS/Text/cover.xhtml") } },
        QJsonObject { { QStringLiteral("label"), QStringLiteral("第一话 相遇") }, { QStringLiteral("href"), QStringLiteral("OEBPS/Text/Section002.xhtml") } },
        QJsonObject { { QStringLiteral("label"), QStringLiteral("序章") }, { QStringLiteral("href"), QStringLiteral("OEBPS/Text/Section001.xhtml") } },
        QJsonObject { { QStringLiteral("label"), QStringLiteral("后记") }, { QStringLiteral("href"), QStringLiteral("OEBPS/Text/Section004.xhtml") } }
    });
    ToolRegistry novel_tools;
    registerBookTools(&novel_tools, &novel);
    const ToolResult structure = novel_tools.find(QStringLiteral("structure.inspect"))->execute(QJsonObject());
    Require(structure.ok && !novel_tools.find(QStringLiteral("structure.inspect"))->descriptor().mutatesBook,
            "structure.inspect must be a read-only tool");
    const QJsonArray pages = structure.data.value(QStringLiteral("pages")).toArray();
    Require(pages.size() == 6, "every spine page must be mapped");
    auto page_at = [&](int index) { return pages.at(index).toObject(); };
    Require(page_at(0).value(QStringLiteral("role")).toString() == QStringLiteral("cover")
                && page_at(1).value(QStringLiteral("role")).toString() == QStringLiteral("toc")
                && page_at(2).value(QStringLiteral("role")).toString() == QStringLiteral("prologue")
                && page_at(3).value(QStringLiteral("role")).toString() == QStringLiteral("chapter")
                && page_at(5).value(QStringLiteral("role")).toString() == QStringLiteral("afterword"),
            "roles must follow image layout and heading text");
    Require(page_at(2).value(QStringLiteral("heading")).toObject().value(QStringLiteral("text")).toString()
                == QStringLiteral("序　章"),
            "ruby annotations must not leak into heading text");
    Require(!page_at(0).value(QStringLiteral("evidence")).toArray().isEmpty()
                && !page_at(0).value(QStringLiteral("confidence")).toString().isEmpty(),
            "every role must carry evidence and confidence");
    QSet<QString> kinds;
    for (const QJsonValue &value : structure.data.value(QStringLiteral("differences")).toArray()) {
        const QJsonObject difference = value.toObject();
        kinds.insert(difference.value(QStringLiteral("kind")).toString()
                     + QLatin1Char('@') + difference.value(QStringLiteral("book_path")).toString().section(QLatin1Char('/'), -1));
    }
    for (const QString &expected : {
             QStringLiteral("toc_label_mismatch@Section002.xhtml"),
             QStringLiteral("heading_without_toc@Section003.xhtml"),
             QStringLiteral("page_without_heading@Section004.xhtml"),
             QStringLiteral("toc_order_differs_from_spine@Section002.xhtml") }) {
        if (!kinds.contains(expected)) std::cerr << "missing difference " << expected.toStdString() << '\n';
        Require(kinds.contains(expected), "heading/TOC differences must be reported per page");
    }
    Require(!kinds.contains(QStringLiteral("toc_label_mismatch@Section001.xhtml")),
            "spacing-only differences between TOC label and heading are not mismatches");
    Require(novel.revision() == 0 || !novel.hasOpenTransaction(), "structure inspection must not open a transaction");
    return EXIT_SUCCESS;
}
