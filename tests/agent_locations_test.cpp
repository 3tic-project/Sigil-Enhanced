#include <cstdlib>
#include <iostream>

#include <QGuiApplication>
#include <QFont>
#include <QHash>
#include <QRegularExpression>
#include <QTextDocument>

#include "Agent/UI/AgentLocations.h"
#include "Agent/UI/AgentMarkdown.h"

using namespace SigilAgent;

namespace
{

void Require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

class FakeSource final : public AgentLocationSource
{
public:
    QString session = QStringLiteral("book-session-1");
    QList<AgentLocationResource> order;
    QHash<QString, QString> texts;

    void add(const QString &id, const QString &path, const QString &text)
    {
        order.append({ id, path });
        texts.insert(id, text);
    }

    void rename(const QString &id, const QString &path)
    {
        for (AgentLocationResource &resource : order) {
            if (resource.resourceId == id) resource.bookPath = path;
        }
    }

    void remove(const QString &id)
    {
        texts.remove(id);
        order.erase(std::remove_if(order.begin(), order.end(),
                                   [&id](const AgentLocationResource &resource) {
                                       return resource.resourceId == id;
                                   }),
                    order.end());
    }

    QString bookSessionId() const override { return session; }
    QList<AgentLocationResource> textResources() const override { return order; }
    bool resourceText(const QString &resource_id, QString *book_path, QString *text) const override
    {
        if (!texts.contains(resource_id)) return false;
        for (const AgentLocationResource &resource : order) {
            if (resource.resourceId != resource_id) continue;
            if (book_path) *book_path = resource.bookPath;
            if (text) *text = texts.value(resource_id);
            return true;
        }
        return false;
    }
};

QString Lines(int count)
{
    QStringList lines;
    for (int i = 1; i <= count; ++i) lines.append(QStringLiteral("<p>line %1</p>").arg(i));
    return lines.join(QLatin1Char('\n'));
}

QString HrefFor(const QString &markdown, const QString &label)
{
    const QRegularExpression pattern(
        QStringLiteral("\\[%1\\]\\((sigil-agent://location/[0-9a-f]{32})\\)")
            .arg(QRegularExpression::escape(label)));
    return pattern.match(markdown).captured(1);
}

QString VisibleText(const QString &html)
{
    QTextDocument document;
    document.setHtml(html);
    return document.toPlainText();
}

void TestSingleFileAnswer()
{
    FakeSource source;
    source.add(QStringLiteral("ch1"), QStringLiteral("OEBPS/Text/Section001.xhtml"), Lines(30));
    source.add(QStringLiteral("ch2"), QStringLiteral("OEBPS/Text/Section002.xhtml"), Lines(30));
    AgentLocationTable table;
    const QString answer = QString::fromUtf8(
        "**范围**：`OEBPS/Text/Section001.xhtml`（第一章）\n\n"
        "| # | 行 | 原文 |\n|---|---|---|\n"
        "| 1 | L25 | 她**帮忙我**发传单 |\n"
        "| 2 | **L27** | 「暗椿」 |\n\n"
        "7. **L3 / L4**「想像」——L99 越界；HTML5、XL2、L2x 与 L0 都不是行号。\n");
    const AgentLinkifyResult result = table.linkify(answer, source);
    Require(result.fileLinks == 1 && result.lineLinks == 4
                && result.outOfRangeLineRefs == 1 && result.unboundLineRefs == 0,
            "a single-file answer must link its path and every in-range L number only");
    Require(result.markdown.contains(QStringLiteral("[`OEBPS/Text/Section001.xhtml`](sigil-agent://location/")),
            "an inline-code path must be wrapped as a link without losing its code span");
    Require(result.markdown.contains(QStringLiteral("L99")) && HrefFor(result.markdown, QStringLiteral("L99")).isEmpty(),
            "an out-of-range line must stay plain text");
    Require(result.markdown.contains(QStringLiteral("HTML5、XL2、L2x 与 L0")),
            "tokens that only resemble L numbers must stay unchanged");
    const QString l25 = HrefFor(result.markdown, QStringLiteral("L25"));
    const AgentLocation *location = table.find(agentLocationIdFromHref(l25));
    Require(location && location->resourceId == QLatin1String("ch1")
                && location->kind == AgentLocationKind::SourceLine && location->line == 25
                && location->bookSessionId == source.session,
            "an L number must bind to the only file named in the answer");
    const AgentLinkifyResult again = table.linkify(QStringLiteral("`OEBPS/Text/Section001.xhtml` L25"), source);
    Require(HrefFor(again.markdown, QStringLiteral("L25")) == l25,
            "the same unchanged location must reuse its opaque ID");
    QString stripped = result.markdown;
    stripped.remove(QRegularExpression(QStringLiteral("\\]\\(sigil-agent://location/[0-9a-f]{32}\\)")));
    stripped.remove(QRegularExpression(QStringLiteral("\\[(?=L\\d|`OEBPS)")));
    Require(stripped == answer, "linkify must change only the linked tokens");
}

void TestCodeAndProtectedRegions()
{
    FakeSource source;
    source.add(QStringLiteral("ch1"), QStringLiteral("OEBPS/Text/a.xhtml"), Lines(10));
    AgentLocationTable table;
    const QString answer = QStringLiteral(
        "Scope OEBPS/Text/a.xhtml.\n"
        "```\nOEBPS/Text/a.xhtml L2\n```\n"
        "~~~~\nL3\n```\nL4\n~~~~\n"
        "`L5` [see](https://x.test/OEBPS/Text/a.xhtml) https://x.test/L6 <https://x.test/L7> "
        "www.example.com/L8 [L9](sigil-agent://location/00000000000000000000000000000000)\n"
        "xOEBPS/Text/a.xhtml /OEBPS/Text/a.xhtml OEBPS/Text/a.xhtmlx Text/a.xhtml\n"
        "L10");
    const AgentLinkifyResult result = table.linkify(answer, source);
    Require(result.fileLinks == 1 && result.lineLinks == 1,
            "only the plain path and the plain L10 may be linked");
    Require(result.markdown.contains(QStringLiteral("[OEBPS/Text/a.xhtml](sigil-agent://location/"))
                && result.markdown.contains(QStringLiteral("```\nOEBPS/Text/a.xhtml L2\n```"))
                && result.markdown.contains(QStringLiteral("~~~~\nL3\n```\nL4\n~~~~")),
            "fenced code must stay untouched, including a shorter inner fence");
    Require(!HrefFor(result.markdown, QStringLiteral("L10")).isEmpty(),
            "text after a closed fence must be scanned again");
    Require(result.markdown.contains(QStringLiteral("xOEBPS/Text/a.xhtml /OEBPS/Text/a.xhtml OEBPS/Text/a.xhtmlx Text/a.xhtml")),
            "partial or suffix paths must not be treated as book paths");
    Require(result.markdown.contains(QStringLiteral("[L9](sigil-agent://location/00000000000000000000000000000000)")),
            "an existing Markdown link must not be nested");
}

void TestMultiFileBinding()
{
    FakeSource source;
    source.add(QStringLiteral("a"), QStringLiteral("OEBPS/a.xhtml"), Lines(10));
    source.add(QStringLiteral("b"), QStringLiteral("OEBPS/b.xhtml"), Lines(10));
    AgentLocationTable table;
    const QString answer = QStringLiteral(
        "Files `OEBPS/a.xhtml` and `OEBPS/b.xhtml`.\n\nL2 is ambiguous.\n\n"
        "| file | line |\n|---|---|\n"
        "| OEBPS/a.xhtml | L2 |\n"
        "| `OEBPS/b.xhtml` | L3 |\n"
        "| OEBPS/a.xhtml, OEBPS/b.xhtml | L4 |\n"
        "| none | L5 |\n");
    const AgentLinkifyResult result = table.linkify(answer, source);
    Require(result.lineLinks == 2 && result.unboundLineRefs == 3,
            "multi-file answers may bind L numbers only through a one-file table row");
    const AgentLocation *a = table.find(agentLocationIdFromHref(HrefFor(result.markdown, QStringLiteral("L2"))));
    const AgentLocation *b = table.find(agentLocationIdFromHref(HrefFor(result.markdown, QStringLiteral("L3"))));
    Require(a && a->resourceId == QLatin1String("a") && a->line == 2
                && b && b->resourceId == QLatin1String("b") && b->line == 3,
            "each row-bound L number must point to the path in its own row");

    AgentLocationTable no_file;
    const AgentLinkifyResult unbound = no_file.linkify(
        QStringLiteral("L2 and OEBPS/missing.xhtml L3"), source);
    Require(unbound.lineLinks == 0 && unbound.fileLinks == 0 && unbound.unboundLineRefs == 2
                && no_file.size() == 0,
            "without a verified file, L numbers must stay plain");
}

void TestIndentedCodeAndPipeLessTables()
{
    FakeSource source;
    source.add(QStringLiteral("a"), QStringLiteral("OEBPS/a.xhtml"), Lines(10));
    source.add(QStringLiteral("b"), QStringLiteral("OEBPS/b.xhtml"), Lines(10));
    AgentLocationTable code_table;
    const AgentLinkifyResult code = code_table.linkify(
        QStringLiteral("    OEBPS/a.xhtml\nL2"), source);
    Require(code.fileLinks == 0 && code.lineLinks == 0 && code.unboundLineRefs == 1,
            "an indented code block must not supply file context for an outside L number");

    AgentLocationTable table;
    const AgentLinkifyResult rows = table.linkify(QStringLiteral(
        "Files OEBPS/a.xhtml and OEBPS/b.xhtml\n\n"
        "file | line\n--- | ---\n"
        "OEBPS/a.xhtml | L2\nOEBPS/b.xhtml | L3\n"), source);
    const AgentLocation *a = table.find(agentLocationIdFromHref(HrefFor(rows.markdown, QStringLiteral("L2"))));
    const AgentLocation *b = table.find(agentLocationIdFromHref(HrefFor(rows.markdown, QStringLiteral("L3"))));
    Require(rows.lineLinks == 2 && a && b && a->resourceId == QLatin1String("a")
                && b->resourceId == QLatin1String("b"),
            "GitHub tables without leading pipes must bind each L number to its own row");
}

void TestPathEscaping()
{
    FakeSource source;
    source.add(QStringLiteral("odd"), QStringLiteral("OEBPS/Text/a[1].xhtml"), Lines(3));
    AgentLocationTable table;
    const AgentLinkifyResult result = table.linkify(QStringLiteral("See OEBPS/Text/a[1].xhtml L3"), source);
    Require(result.fileLinks == 1 && result.lineLinks == 1
                && result.markdown.contains(QStringLiteral("[OEBPS/Text/a\\[1\\].xhtml](")),
            "link text must escape brackets from a real book path");
    const AgentMarkdownRender render = renderAgentMarkdown(result.markdown, QFont());
    Require(render.rendered && render.allowedLinks == 2
                && VisibleText(render.html).contains(QStringLiteral("OEBPS/Text/a[1].xhtml")),
            "an escaped path must display as the original path");
}

void TestLineCounts()
{
    FakeSource source;
    source.add(QStringLiteral("t"), QStringLiteral("t.xhtml"), QStringLiteral("a\nb\n"));
    AgentLocationTable table;
    const AgentLinkifyResult result = table.linkify(QStringLiteral("t.xhtml L3 L4"), source);
    Require(result.lineLinks == 1 && result.outOfRangeLineRefs == 1,
            "source lines follow Code View blocks, including a final empty line");
}

void TestChecks()
{
    FakeSource source;
    source.add(QStringLiteral("ch1"), QStringLiteral("OEBPS/a.xhtml"), Lines(10));
    source.add(QStringLiteral("ch2"), QStringLiteral("OEBPS/b.xhtml"), Lines(10));
    AgentLocationTable table;
    const AgentLinkifyResult result = table.linkify(QStringLiteral("`OEBPS/a.xhtml` L4"), source);
    const QString line_id = agentLocationIdFromHref(HrefFor(result.markdown, QStringLiteral("L4")));
    const QString file_id = agentLocationIdFromHref(
        HrefFor(result.markdown, QStringLiteral("`OEBPS/a.xhtml`")));
    Require(!line_id.isEmpty() && !file_id.isEmpty(), "linkify must issue file and line IDs");
    Require(table.check(line_id, &source).status == AgentLocationStatus::Exact,
            "an unchanged source line must be exact");

    source.texts[QStringLiteral("ch2")] = Lines(11);
    Require(table.check(line_id, &source).status == AgentLocationStatus::Exact,
            "editing another resource must not invalidate this location");

    const QString original = source.texts.value(QStringLiteral("ch1"));
    source.texts[QStringLiteral("ch1")] = QStringLiteral("<p>inserted</p>\n") + original;
    Require(table.check(line_id, &source).status == AgentLocationStatus::ContentChanged,
            "editing text before the cited line must not keep the old line as exact");
    Require(table.check(file_id, &source).status == AgentLocationStatus::Exact,
            "a file link must still open after its content changes");
    source.texts[QStringLiteral("ch1")] = original;
    Require(table.check(line_id, &source).status == AgentLocationStatus::Exact,
            "undoing the edit must restore the exact location");

    source.rename(QStringLiteral("ch1"), QStringLiteral("OEBPS/renamed.xhtml"));
    const AgentLocationCheck renamed = table.check(line_id, &source);
    Require(renamed.status == AgentLocationStatus::Exact
                && renamed.currentBookPath == QLatin1String("OEBPS/renamed.xhtml")
                && renamed.location.resourceId == QLatin1String("ch1"),
            "a renamed resource must resolve by identity to its current path");

    source.remove(QStringLiteral("ch1"));
    Require(table.check(line_id, &source).status == AgentLocationStatus::ResourceMissing
                && table.check(file_id, &source).status == AgentLocationStatus::ResourceMissing,
            "a deleted resource must not open anything");

    Require(table.check(line_id, nullptr).status == AgentLocationStatus::Unavailable,
            "a missing book source must be explicit");
    Require(table.check(QStringLiteral("0123456789abcdef0123456789abcdef"), &source).status
                == AgentLocationStatus::Unknown,
            "an ID the host did not issue must be unknown");

    source.session = QStringLiteral("book-session-2");
    Require(table.check(file_id, &source).status == AgentLocationStatus::OtherBook,
            "a link from another book session must not open");
    table.revokeOtherBooks(source.session);
    Require(!table.find(file_id) && table.size() == 0
                && table.check(file_id, &source).status == AgentLocationStatus::OtherBook,
            "changing books must drop old locations but still explain the stale link");
    table.clear();
    Require(table.check(file_id, &source).status == AgentLocationStatus::Unknown,
            "a new session must forget every location");
}

void TestMarkdownRender()
{
    const QFont font;
    const QString markdown = QString::fromUtf8(
        "## 一、建议修改\n\n| # | 行 | 原文 |\n|---|---|---|\n| 1 | L25 | 她**帮忙我**发传单 |\n\n"
        "- `code` and *em*\n\n> quote\n\n```\nfenced <b>x</b>\n```\n\n---\n");
    const AgentMarkdownRender render = renderAgentMarkdown(markdown, font);
    const QString visible = VisibleText(render.html);
    Require(render.rendered && render.html.contains(QStringLiteral("<table"))
                && visible.contains(QString::fromUtf8("一、建议修改"))
                && visible.contains(QString::fromUtf8("帮忙我"))
                && visible.contains(QStringLiteral("fenced <b>x</b>"))
                && !visible.contains(QStringLiteral("**")),
            "GitHub Markdown headings, tables, emphasis and fenced code must render");

    const AgentMarkdownRender unsafe = renderAgentMarkdown(QStringLiteral(
        "<b>bold?</b> <img src=\"file:///etc/hosts\"> <script>alert(1)</script>\n\n"
        "![remote](https://example.com/a.png) ![local](file:///etc/hosts)\n\n"
        "[web](https://example.com) [disk](file:///etc/passwd) [js](javascript:alert(1)) "
        "[mail](mailto:a@example.com) https://bare.example.com www.example.org "
        "[escape](sigil-agent://location/../../etc) "
        "[ok](sigil-agent://location/0123456789abcdef0123456789abcdef)\n"), font);
    const QString html = unsafe.html;
    Require(unsafe.rendered && !html.contains(QStringLiteral("<img"), Qt::CaseInsensitive)
                && !html.contains(QStringLiteral("<script"), Qt::CaseInsensitive)
                && !html.contains(QStringLiteral("href=\"http"))
                && !html.contains(QStringLiteral("href=\"file"))
                && !html.contains(QStringLiteral("href=\"javascript"))
                && !html.contains(QStringLiteral("href=\"mailto"))
                && !html.contains(QStringLiteral("href=\"www"))
                && !html.contains(QStringLiteral("location/../"))
                && html.contains(QStringLiteral(
                    "href=\"sigil-agent://location/0123456789abcdef0123456789abcdef\"")),
            "only host-issued location links may survive Markdown rendering");
    Require(unsafe.blockedImages == 2 && unsafe.allowedLinks == 1 && unsafe.blockedLinks >= 7,
            "blocked images and links must be counted");
    const QString unsafe_visible = VisibleText(html);
    Require(unsafe_visible.contains(QStringLiteral("<b>bold?</b>"))
                && unsafe_visible.contains(QStringLiteral("<script>alert(1)</script>"))
                && unsafe_visible.contains(QStringLiteral("[image not loaded]"))
                && unsafe_visible.contains(QStringLiteral("web"))
                && unsafe_visible.contains(QStringLiteral("disk")),
            "raw HTML must stay literal and blocked links must keep a readable label");

    Require(agentLocationIdFromHref(QStringLiteral("sigil-agent://location/0123456789abcdef0123456789abcdef"))
                    == QLatin1String("0123456789abcdef0123456789abcdef")
                && agentLocationIdFromHref(QStringLiteral("sigil-agent://location/0123456789ABCDEF0123456789abcdef")).isEmpty()
                && agentLocationIdFromHref(QStringLiteral("sigil-agent://location/0123456789abcdef0123456789abcdef/x")).isEmpty()
                && agentLocationIdFromHref(QStringLiteral("file:///0123456789abcdef0123456789abcdef")).isEmpty(),
            "the navigation scheme must accept only opaque host IDs");

    const AgentMarkdownRender over = renderAgentMarkdown(
        QString(AGENT_MARKDOWN_RENDER_BUDGET + 1, QLatin1Char('x')), font);
    Require(!over.rendered && over.overBudget && over.html.isEmpty(),
            "messages above the render budget must fall back to plain text");

    QString table = QStringLiteral("| # | 行 | 原文 | 建议 | 依据 |\n|---|---|---|---|---|\n");
    for (int i = 1; i <= 400; ++i) {
        table += QString::fromUtf8("| %1 | L%1 | 她今天早上也**帮忙我**发传单 | **帮我**发传单 | 语病；同章 `L75` |\n").arg(i);
    }
    const AgentMarkdownRender large = renderAgentMarkdown(table, font);
    std::cout << "400-row table: " << table.size() << " UTF-16 units rendered in "
              << large.elapsedMs << " ms\n";
    Require(large.rendered && large.elapsedMs < 2000,
            "a large Markdown table must render within the GUI budget");
}

} // namespace

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    QGuiApplication application(argc, argv);
    TestSingleFileAnswer();
    TestCodeAndProtectedRegions();
    TestMultiFileBinding();
    TestIndentedCodeAndPipeLessTables();
    TestPathEscaping();
    TestLineCounts();
    TestChecks();
    TestMarkdownRender();
    std::cout << "agent locations and Markdown rendering passed\n";
    return EXIT_SUCCESS;
}
