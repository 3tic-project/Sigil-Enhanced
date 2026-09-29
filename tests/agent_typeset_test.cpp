#include <cstdlib>
#include <iostream>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "Agent/Core/AgentSession.h"
#include "Agent/Core/AgentSkills.h"
#include "Agent/Core/PromptAssembler.h"
#include "Agent/Execution/MemoryBookWorkspace.h"
#include "Agent/Tools/BookTools.h"
#include "Agent/Tools/ToolRegistry.h"
#include "Agent/Typeset/ManuscriptParser.h"
#include "Agent/Typeset/TypesetEngine.h"

namespace
{

void Require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

const QString kFixtureTxt = QStringLiteral(
    "［插图：cover］\n"
    "\n"
    "制作信息\n"
    "──────────────\n"
    "中文標題：测试书名\n"
    "作者：测试作者\n"
    "插画：测试插画\n"
    "译者：测试译者\n"
    "图源：测试图源\n"
    "──────────────\n"
    "\n"
    "內容簡介\n"
    "这是简介第一段。\n"
    "第二段说明剧情。\n"
    "\n"
    "［插图：color1］\n"
    "\n"
    "［插图：color2］\n"
    "\n"
    "目錄\n"
    "第一話　春\n"
    "第二話　夏\n"
    "後記\n"
    "\n"
    "第一話　春\n"
    "\n"
    "　春天到了。\n"
    "\n"
    "［插图：p001］\n"
    "\n"
    "　花开了。\n"
    "\n"
    "第二話　夏\n"
    "　夏天很热。\n"
    "\n"
    "後記\n"
    "谢谢。\n");

QString page(const QString &title, const QString &body)
{
    return QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<html xmlns=\"http://www.w3.org/1999/xhtml\" xml:lang=\"zh-CN\">\n"
        "<head><link href=\"../Styles/style.css\" rel=\"stylesheet\" type=\"text/css\"/>"
        "<title>%1</title></head>\n"
        "<body>\n%2</body>\n</html>").arg(title, body);
}

SigilAgent::MemoryResource xhtml(const QString &id, const QString &path, const QString &text)
{
    SigilAgent::MemoryResource resource;
    resource.id = id;
    resource.bookPath = path;
    resource.mediaType = QStringLiteral("application/xhtml+xml");
    resource.kind = QStringLiteral("xhtml");
    resource.text = text;
    return resource;
}

SigilAgent::MemoryResource image(const QString &id, const QString &path)
{
    SigilAgent::MemoryResource resource;
    resource.id = id;
    resource.bookPath = path;
    resource.mediaType = QStringLiteral("image/jpeg");
    resource.kind = QStringLiteral("image");
    resource.binary = QByteArray("fake-jpeg");
    return resource;
}

SigilAgent::MemoryBookWorkspace lnTemplateBook(bool include_manuscript = true)
{
    using namespace SigilAgent;
    MemoryBookWorkspace book;
    book.setEpubVersion(QStringLiteral("2.0"));
    book.setMetadata(QJsonObject {
        { QStringLiteral("title"), QStringLiteral("书名") },
        { QStringLiteral("language"), QStringLiteral("zh") }
    });
    book.addResource(xhtml(QStringLiteral("cover"), QStringLiteral("OEBPS/Text/cover.xhtml"),
        page(QStringLiteral("封面"),
             QStringLiteral("  <div><svg xmlns=\"http://www.w3.org/2000/svg\">"
                            "<image xlink:href=\"../Images/cover.jpg\"/></svg></div>\n"))));
    book.addResource(xhtml(QStringLiteral("start"), QStringLiteral("OEBPS/Text/start.xhtml"),
        page(QStringLiteral("书名页"),
             QStringLiteral("  <div class=\"illus\"><img alt=\"start\" src=\"../Images/start.jpg\"/></div>\n"))));
    book.addResource(xhtml(QStringLiteral("title"), QStringLiteral("OEBPS/Text/title.xhtml"),
        page(QStringLiteral("标题"), QStringLiteral("  <div class=\"title\"></div>\n"))));
    book.addResource(xhtml(QStringLiteral("message"), QStringLiteral("OEBPS/Text/message.xhtml"),
        page(QStringLiteral("制作信息"),
             QStringLiteral("  <div class=\"illus5\"><p class=\"meg\">制作信息</p>"
                            "<p class=\"message\">作者：</p>"
                            "<p class=\"message\">仅供个人学习交流使用，禁作商业用途</p></div>\n"))));
    book.addResource(xhtml(QStringLiteral("summary"), QStringLiteral("OEBPS/Text/summary.xhtml"),
        page(QStringLiteral("简介"),
             QStringLiteral("  <div><p>请先转为书籍视图界面</p><p>全选后将文本粘贴在此处</p></div>\n"))));
    book.addResource(xhtml(QStringLiteral("illus1"), QStringLiteral("OEBPS/Text/illus1.xhtml"),
        page(QStringLiteral("彩页"),
             QStringLiteral("  <div class=\"illus\"><img alt=\"co1\" src=\"../Images/co1.jpg\"/></div>\n"))));
    book.addResource(xhtml(QStringLiteral("illus2"), QStringLiteral("OEBPS/Text/illus2.xhtml"),
        page(QStringLiteral("彩页"),
             QStringLiteral("  <div class=\"illus\"><img alt=\"co2\" src=\"../Images/co2.jpg\"/></div>\n"))));
    book.addResource(xhtml(QStringLiteral("contents"), QStringLiteral("OEBPS/Text/contents.xhtml"),
        page(QStringLiteral("目录"), QStringLiteral("  <div><p>目录</p></div>\n"))));
    book.addResource(xhtml(QStringLiteral("s1"), QStringLiteral("OEBPS/Text/Section001.xhtml"),
        page(QString(), QStringLiteral("  <div><p>请先转为书籍视图界面</p></div>\n"))));
    book.addResource(xhtml(QStringLiteral("s2"), QStringLiteral("OEBPS/Text/Section002.xhtml"),
        page(QString(), QStringLiteral("  <div><p>请先转为书籍视图界面</p></div>\n"))));
    book.addResource(image(QStringLiteral("img-cover"), QStringLiteral("OEBPS/Images/cover.jpg")));
    book.addResource(image(QStringLiteral("img-start"), QStringLiteral("OEBPS/Images/start.jpg")));
    book.addResource(image(QStringLiteral("img-c1"), QStringLiteral("OEBPS/Images/color1.jpg")));
    book.addResource(image(QStringLiteral("img-c2"), QStringLiteral("OEBPS/Images/color2.jpg")));
    book.addResource(image(QStringLiteral("img-p"), QStringLiteral("OEBPS/Images/p001.jpg")));
    if (include_manuscript) {
        MemoryResource txt;
        txt.id = QStringLiteral("ms");
        txt.bookPath = QStringLiteral("OEBPS/Misc/book.txt");
        txt.mediaType = QStringLiteral("text/plain");
        txt.kind = QStringLiteral("text");
        txt.text = kFixtureTxt;
        book.addResource(txt);
    }
    book.setSpine({
        QStringLiteral("cover"), QStringLiteral("title"), QStringLiteral("message"),
        QStringLiteral("summary"), QStringLiteral("illus1"), QStringLiteral("illus2"),
        QStringLiteral("contents"), QStringLiteral("start"),
        QStringLiteral("s1"), QStringLiteral("s2")
    });
    return book;
}

} // namespace

int main()
{
    using namespace SigilAgent;

    const AgentSkill folded_skill = parseSkillMarkdown(QStringLiteral(
        "---\nname: folded\ndescription: >\n"
        "  Inspect the current book.\n  Keep changes staged.\n"
        "metadata:\n  sigil.category: typesetting\n"
        "allowed-tools: >\n  book.summary manuscript.parse\n"
        "  transaction.preview transaction.commit\n---\n# Body\n"));
    Require(folded_skill.description == QStringLiteral(
                "Inspect the current book. Keep changes staged."),
            "folded description must not absorb metadata or tool declarations");
    Require(folded_skill.allowedTools == QStringLiteral(
                "book.summary manuscript.parse transaction.preview transaction.commit"),
            "folded allowed-tools must retain every continuation line");
    Require(folded_skill.body == QStringLiteral("# Body"),
            "skill body must exclude frontmatter");
        Require(folded_skill.metadataError.isEmpty() && folded_skill.toolNames.size() == 4,
            "folded frontmatter must produce structured tools without errors");
        const AgentSkill structured_skill = parseSkillMarkdown(QStringLiteral(
        "---\r\nname: 'custom-skill'\r\nversion: \"1\"\r\n"
        "description: \"Inspect: current book\"\r\n"
        "triggers: [\"release, preflight\", release preflight]\r\n"
        "requires_capabilities:\r\n  - book.check\r\n  - book.validate\r\n"
        "allowed-tools: [book.summary, 'book.check', book.summary]\r\n"
        "requires_book: true\r\nauto_trigger: false\r\n---\r\nBody"));
        Require(structured_skill.metadataError.isEmpty()
            && structured_skill.name == QStringLiteral("custom-skill")
            && structured_skill.version == QStringLiteral("1")
            && structured_skill.requiresBook && !structured_skill.autoTrigger,
            "quoted scalars, CRLF, versions and flags must parse");
        Require(structured_skill.toolNames.size() == 2
            && structured_skill.requiredCapabilities == QStringList {
                QStringLiteral("book.check"), QStringLiteral("book.validate") }
            && structured_skill.triggers == QStringList {
                QStringLiteral("release, preflight"), QStringLiteral("release preflight") },
            "flow and block lists must preserve phrases and deduplicate tools");
        Require(!parseSkillMarkdown(QStringLiteral("---\nname: broken\nBody")).metadataError.isEmpty(),
            "unterminated frontmatter must not silently load as a valid skill");
        Require(!parseSkillMarkdown(QStringLiteral(
            "---\nname: first\nname: second\n---\nBody")).metadataError.isEmpty(),
            "duplicate metadata must be rejected");
        Require(!parseSkillMarkdown(QStringLiteral(
            "---\nallowed-tools: [book.check\n---\nBody")).metadataError.isEmpty(),
            "malformed tool lists must not silently disable capability checks");
        MemoryBookWorkspace routing_book;
        const QStringList inspect_tools {
            QStringLiteral("book.summary"), QStringLiteral("book.check"), QStringLiteral("book.validate") };
        const AgentSkillSelection disabled_selection = selectAgentSkills(
            { structured_skill }, QStringLiteral("release preflight"), &routing_book, inspect_tools);
        Require(disabled_selection.bodies.isEmpty()
                    && disabled_selection.availableSkills.size() == 1,
                "auto_trigger=false must preserve catalog discovery without automatic injection");
        const AgentSkillSelection named_selection = selectAgentSkills(
            { structured_skill }, QStringLiteral("use custom-skill"), &routing_book, inspect_tools);
        Require(named_selection.bodies.contains(QStringLiteral("# Skill: custom-skill"))
                    && named_selection.decisions.first().toObject().value(QStringLiteral("reason"))
                        == QStringLiteral("explicit_name"),
                "explicit skill names must override disabled automatic triggering");
        Require(selectAgentSkills({ structured_skill }, QStringLiteral("custom-skill"),
                    nullptr, inspect_tools).availableSkills.isEmpty(),
                "book-dependent skills must not be advertised without a book");
        AgentSkill automatic_skill = structured_skill;
        automatic_skill.name = QStringLiteral("automatic-skill");
        automatic_skill.autoTrigger = true;
        const AgentSkillSelection trigger_selection = selectAgentSkills(
            { automatic_skill }, QStringLiteral("release preflight"), &routing_book, inspect_tools);
        Require(!trigger_selection.bodies.isEmpty()
                    && trigger_selection.decisions.first().toObject().value(QStringLiteral("trigger"))
                        == QStringLiteral("release preflight"),
                "declared triggers must record the exact routing evidence");
        const AgentSkillSelection explicit_override = selectAgentSkills(
            { structured_skill, automatic_skill }, QStringLiteral("custom-skill release preflight"),
            &routing_book, inspect_tools);
        Require(!explicit_override.bodies.contains(QStringLiteral("# Skill: automatic-skill")),
                "explicit selection must suppress unrelated automatic skill injection");
        Require(selectAgentSkills({ structured_skill }, QStringLiteral("custom-skill-extra"),
                    &routing_book, inspect_tools).bodies.isEmpty(),
                "skill names must not match prefixes of different names");
        automatic_skill.version = QStringLiteral("2");
        Require(selectAgentSkills({ automatic_skill }, QStringLiteral("automatic-skill"),
                    &routing_book, inspect_tools).availableSkills.isEmpty(),
                "unsupported skill versions must not be injected");
        const AgentSkill forged_skill = parseSkillMarkdown(QStringLiteral(
            "---\nname: forged\nallowed-tools: [python.run]\n---\nCall python.run"));
        const AgentSkillSelection forged_selection = selectAgentSkills(
            { forged_skill }, QStringLiteral("forged"), &routing_book, inspect_tools);
        Require(forged_selection.bodies.isEmpty() && forged_selection.availableSkills.isEmpty()
                    && forged_selection.decisions.first().toObject()
                        .value(QStringLiteral("missing_capabilities")).toArray().contains(QStringLiteral("python.run")),
                "forged allowed-tools must not add a denied or unregistered tool");

    Require(lineLooksLikeChapterHeading(QStringLiteral("第一話　春")), "第一話 must be a heading");
    Require(lineLooksLikeChapterHeading(QStringLiteral("第１話　光屬性的朝日同學")), "fullwidth 第１話 must be a heading");
    Require(lineLooksLikeChapterHeading(QStringLiteral("後記")), "後記 must be a heading");
    Require(!lineLooksLikeChapterHeading(QStringLiteral("春天到了。")), "body line must not be a heading");
    for (const QString &special : {
             QStringLiteral("序　章"), QStringLiteral("幕　間　托露特的視角"), QStringLiteral("終　章"),
             QStringLiteral("後　記"), QStringLiteral("間章"), QStringLiteral("楔子"), QStringLiteral("プロローグ") }) {
        Require(lineLooksLikeChapterHeading(special), "spaced special headings must be recognized");
    }
    Require(!lineLooksLikeChapterHeading(QStringLiteral("後頭傳來類似哀嚎的聲音。")),
            "a body sentence starting with 後 must not be a heading");
    Require(!lineLooksLikeChapterHeading(QStringLiteral("序章的內容很短。但這是正文裡的一句話，不是標題。她笑了笑。我也笑了。")),
            "a long body sentence starting with 序章 must not be a heading");
    const ParsedManuscript spaced = parseManuscriptText(QStringLiteral(
        "目錄\n序　章\n第一章　邂逅\n幕　間　托露特的視角\n終　章\n後　記\n\n"
        "序　章\n開場。\n第一章　邂逅\n後頭傳來聲音。\n"
        "幕　間　托露特的視角\n她想。\n終　章\n結束。\n後　記\n謝謝。\n"));
    Require(spaced.chapters.size() == 5, "spaced special headings must yield every chapter once");
    Require(spaced.chapters.at(0).numberLabel == QStringLiteral("序　章")
                && spaced.chapters.at(2).numberLabel == QStringLiteral("幕　間")
                && spaced.chapters.at(2).title == QStringLiteral("托露特的視角"),
            "spaced special headings must split label and title at the heading word");
    Require(spaced.toc.size() == 5 && spaced.chapters.at(1).body.contains(QStringLiteral("後頭")),
            "front-matter TOC must be separated from body chapters");
    const ParsedManuscript listed = parseManuscriptText(QStringLiteral(
        "目錄\n序章\n1.玲琳，互相交換\n2.玲琳，面臨處刑\n特別篇　她與化妝\n序章\n開場。\n＊＊＊\n"
        "1.玲琳，互相交換\n正文一。\n＊＊＊\n2.玲琳，面臨處刑\n正文二。\n特別篇　她與化妝\n正文三。\n"));
    Require(listed.chapters.size() == 4
                && listed.chapters.at(1).heading == QStringLiteral("1.玲琳，互相交換")
                && listed.chapters.at(3).heading == QStringLiteral("特別篇　她與化妝")
                && listed.chapters.at(0).body.contains(QStringLiteral("＊＊＊")),
            "TOC-listed headings with custom numbering must split chapters without adopting scene breaks");
    Require(chapterNumberFromHeading(QStringLiteral("第１０話　x")) == 10, "fullwidth 10");
    Require(chineseTalkLabel(1) == QStringLiteral("第一话"), "chineseTalkLabel 1");
    Require(chineseTalkLabel(11) == QStringLiteral("第十一话"), "chineseTalkLabel 11");
    Require(illustrationNameInLine(QStringLiteral("［插图：color1］")) == QStringLiteral("color1"),
            "illustration marker");
    Require(illustrationNameInLine(QStringLiteral("（插圖001）")) == QStringLiteral("001")
                && illustrationNameInLine(QStringLiteral("（插图：p012）")) == QStringLiteral("p012")
                && illustrationNameInLine(QStringLiteral("[插圖 007]")) == QStringLiteral("007")
                && illustrationNameInLine(QStringLiteral("【插图：kuchie-001】")) == QStringLiteral("kuchie-001"),
            "fullwidth parenthesis and colon-less illustration markers must be recognized");
    Require(illustrationNameInLine(QStringLiteral("這張插圖很漂亮。")).isEmpty()
                && illustrationNameInLine(QStringLiteral("（插圖）")).isEmpty(),
            "prose mentioning illustrations must not become markers");

    const ParsedManuscript parsed = parseManuscriptText(kFixtureTxt, QStringLiteral("hint"));
    Require(parsed.chapters.size() == 3, "fixture must parse three chapters");
    Require(parsed.chapters.at(0).heading.contains(QStringLiteral("春")), "first chapter heading");
    Require(parsed.chapters.at(2).heading.contains(QStringLiteral("後記")), "last chapter is 後記");
    Require(parsed.title == QStringLiteral("测试书名"), "title from 中文標題");
    Require(parsed.credits.author == QStringLiteral("测试作者"), "author credit");
    Require(parsed.credits.illustrator == QStringLiteral("测试插画"), "illustrator credit");
    Require(parsed.synopsis.contains(QStringLiteral("简介第一段")), "synopsis");
    Require(parsed.frontIllustrationNames.contains(QStringLiteral("color1")), "front color1");
    Require(parsed.chapters.at(0).illustrationNames.contains(QStringLiteral("p001")), "in-chapter p001");
    const QJsonObject summary = manuscriptSummaryJson(parsed);
    Require(!QJsonDocument(summary).toJson().contains("春天到了"),
            "summary JSON must not include chapter bodies");
    Require(summary.value(QStringLiteral("chapter_count")).toInt() == 3, "summary chapter_count");

    const QString imported_html = QStringLiteral("<html><body>\n")
        + QStringLiteral("<p>［插图：cover］</p>\n<p>制作信息</p>\n<p>作者：甲</p>\n")
        + QStringLiteral("<p>目錄</p>\n<p>第一話　一</p>\n<p>第一話　一</p>\n<p>正文一行</p>\n</body></html>");
    const ParsedManuscript from_html = parseManuscriptText(imported_html);
    Require(from_html.chapters.size() == 1, "ImportTXT HTML must parse as one chapter");
    Require(from_html.credits.author == QStringLiteral("甲"), "credits from stripped HTML");

    MemoryBookWorkspace book = lnTemplateBook();
    ToolRegistry registry;
    registerBookTools(&registry, &book);
    auto run = [&](const QString &name, const QJsonObject &arguments) {
        IAgentTool *tool = registry.find(name);
        Require(tool != nullptr, "tool missing");
        return tool->execute(arguments);
    };

    Require(registry.find(QStringLiteral("content_typeset_from_manuscript"))
                == registry.find(QStringLiteral("content.typeset_from_manuscript")),
            "typeset wire name must resolve");
    Require(ToolRegistry::isValidWireName(ToolRegistry::toWireName(QStringLiteral("manuscript.parse"))),
            "manuscript.parse must be a valid wire name");

    const ToolResult parsed_tool = run(QStringLiteral("manuscript.parse"), QJsonObject());
    Require(parsed_tool.ok, "manuscript.parse must succeed");
    Require(parsed_tool.data.value(QStringLiteral("chapter_count")).toInt() == 3,
            "parse tool chapter_count");
    Require(parsed_tool.data.value(QStringLiteral("total_counts")).toObject()
                    .value(QStringLiteral("chapters")).toInt() == 3
                && !parsed_tool.data.value(QStringLiteral("has_more")).toBool(),
            "small parsed manuscripts must remain complete with pagination metadata");
    Require(parsed_tool.data.value(QStringLiteral("template")).toObject()
                .value(QStringLiteral("detected")).toBool(),
            "template must be detected");
    Require(!QJsonDocument(parsed_tool.data).toJson().contains("春天到了"),
            "parse tool must omit chapter bodies");

    MemoryBookWorkspace large_manuscript_book;
    MemoryResource large_manuscript;
    large_manuscript.id = QStringLiteral("large-manuscript");
    large_manuscript.bookPath = QStringLiteral("OEBPS/Misc/large.txt");
    large_manuscript.mediaType = QStringLiteral("text/plain");
    large_manuscript.kind = QStringLiteral("text");
    QStringList manuscript_lines;
    for (int index = 0; index < 135; ++index) {
        manuscript_lines.append(
            QStringLiteral("第%1話　章节 %1").arg(index + 1));
        manuscript_lines.append(QStringLiteral("正文 %1").arg(index + 1));
        manuscript_lines.append(
            QStringLiteral("［插图：image-%1］").arg(index + 1));
    }
    large_manuscript.text = manuscript_lines.join(QLatin1Char('\n'));
    large_manuscript_book.addResource(large_manuscript);
    for (int index = 0; index < 135; ++index) {
        large_manuscript_book.addResource(image(
            QStringLiteral("large-image-%1").arg(index),
            QStringLiteral("OEBPS/Images/image-%1.jpg").arg(index + 1)));
    }
    for (int index = 0; index < 105; ++index) {
        large_manuscript_book.addResource(xhtml(
            QStringLiteral("large-illus-page-%1").arg(index),
            QStringLiteral("OEBPS/Text/illus%1.xhtml").arg(index + 1),
            QStringLiteral("<html><body/></html>")));
        large_manuscript_book.addResource(xhtml(
            QStringLiteral("large-section-page-%1").arg(index),
            QStringLiteral("OEBPS/Text/Section%1.xhtml").arg(index + 1),
            QStringLiteral("<html><body/></html>")));
    }
    ToolRegistry large_manuscript_registry;
    registerBookTools(&large_manuscript_registry, &large_manuscript_book);
    auto parse_large_manuscript = [&](const QJsonObject &arguments) {
        return large_manuscript_registry.find(
            QStringLiteral("manuscript.parse"))->execute(arguments);
    };
    const ToolResult manuscript_page = parse_large_manuscript(QJsonObject {
        { QStringLiteral("manuscript_id"), large_manuscript.id }
    });
    const ToolResult manuscript_page_again = parse_large_manuscript(QJsonObject {
        { QStringLiteral("manuscript_id"), large_manuscript.id }
    });
    const ToolResult manuscript_tail = parse_large_manuscript(QJsonObject {
        { QStringLiteral("manuscript_id"), large_manuscript.id },
        { QStringLiteral("offset"), 100 },
        { QStringLiteral("limit"), 999 }
    });
    const QJsonObject manuscript_totals = manuscript_page.data.value(
        QStringLiteral("total_counts")).toObject();
    const QJsonObject manuscript_returned = manuscript_tail.data.value(
        QStringLiteral("returned_counts")).toObject();
    const QJsonObject manuscript_schema = large_manuscript_registry.find(
        QStringLiteral("manuscript.parse"))->descriptor().inputSchema
        .value(QStringLiteral("properties")).toObject();
    Require(manuscript_page.ok
                && manuscript_page.data.value(QStringLiteral("chapter_count")).toInt()
                    == 135
                && manuscript_page.data.value(QStringLiteral("chapters")).toArray().size()
                    == 40
                && manuscript_page.data.value(QStringLiteral("toc")).toArray().size()
                    == 40
                && manuscript_page.data.value(QStringLiteral("illustrations")).toArray().size()
                    == 40
                && manuscript_page.data.value(QStringLiteral("images_in_book")).toArray().size()
                    == 40
                && manuscript_page.data.value(QStringLiteral("images_in_book")).toArray()
                    == manuscript_page_again.data.value(
                        QStringLiteral("images_in_book")).toArray()
                && manuscript_page.data.value(QStringLiteral("resolved_images")).toArray().size()
                    == 40
                && manuscript_page.data.value(QStringLiteral("template")).toObject()
                    .value(QStringLiteral("illustrations")).toArray().size() == 40
                && manuscript_page.data.value(QStringLiteral("template")).toObject()
                    .value(QStringLiteral("chapters")).toArray().size() == 40
                && manuscript_totals.value(QStringLiteral("chapters")).toInt() == 135
                && manuscript_totals.value(QStringLiteral("template.chapters")).toInt()
                    == 105
                && manuscript_page.data.value(QStringLiteral("has_more")).toBool()
                && manuscript_page.data.value(QStringLiteral("next_offset")).toInt() == 40
                && manuscript_tail.data.value(QStringLiteral("limit")).toInt() == 100
                && manuscript_tail.data.value(QStringLiteral("chapters")).toArray().size()
                    == 35
                && manuscript_returned.value(QStringLiteral("illustrations")).toInt()
                    == 35
                && manuscript_returned.value(QStringLiteral("template.illustrations")).toInt()
                    == 5
                && !manuscript_tail.data.value(QStringLiteral("has_more")).toBool()
                && manuscript_schema.value(QStringLiteral("limit")).toObject()
                    .value(QStringLiteral("default")).toInt() == 40
                && manuscript_schema.value(QStringLiteral("limit")).toObject()
                    .value(QStringLiteral("maximum")).toInt() == 100,
            "manuscript.parse must page every top-level and template collection together");

    Require(run(QStringLiteral("transaction.begin"), QJsonObject()).ok, "begin");
    const ToolResult typeset = run(QStringLiteral("content.typeset_from_manuscript"), QJsonObject());
    Require(typeset.ok && typeset.previewOnly, "typeset must stage");
    Require(typeset.data.value(QStringLiteral("chapters_filled")).toInt() == 3, "three chapters filled");
    Require(typeset.data.value(QStringLiteral("sections_copied")).toInt() == 1, "one extra section copied");
    Require(typeset.data.value(QStringLiteral("toc_entries_staged")).toInt() == 3,
            "chapter navigation staged");
    Require(book.resourceText(QStringLiteral("s1")).contains(QStringLiteral("请先转为书籍视图界面")),
            "live Section001 must stay placeholder until commit");

    const ToolResult preview = run(QStringLiteral("transaction.preview"), QJsonObject());
    Require(preview.ok && preview.previewOnly, "preview after typeset");
    Require(run(QStringLiteral("transaction.commit"), QJsonObject {
        { QStringLiteral("expected_revision"), static_cast<qint64>(book.revision()) }
    }).applied, "typeset commit");

    const QString ch1 = book.resourceText(QStringLiteral("s1"));
    Require(ch1.contains(QStringLiteral("<h1>第一話　春</h1>")), "chapter 1 heading");
    Require(ch1.contains(QStringLiteral("<p>春天到了。</p>")), "chapter 1 body without leading indent");
    Require(ch1.contains(QStringLiteral("src=\"../Images/p001.jpg\"")), "in-chapter illustration");
    Require(ch1.contains(QStringLiteral("<img alt=\"\" src=\"../Images/p001.jpg\""))
                && !ch1.contains(QStringLiteral("alt=\"p001\"")),
            "generated body illustrations must not use the file name as alt text");
    Require(typeset.data.value(QStringLiteral("alt_review_required")).toObject()
                .value(QStringLiteral("image_count")).toInt() > 0,
            "typeset must report images whose alt text needs editor review");
    Require(ch1.contains(QStringLiteral("class=\"illus")), "illustration wrapper class");
    Require(!ch1.contains(QStringLiteral("请先转为书籍视图界面")), "placeholder must be gone");

    const QString ch2 = book.resourceText(QStringLiteral("s2"));
    Require(ch2.contains(QStringLiteral("夏天很热")), "chapter 2 body");

    bool found_afterword = false;
    QString afterword_id;
    for (const QJsonValue &value : book.spine()) {
        const QString id = value.toObject().value(QStringLiteral("resource_id")).toString();
        if (book.resourceText(id).contains(QStringLiteral("谢谢。"))) {
            found_afterword = true;
            afterword_id = id;
        }
    }
    Require(found_afterword, "copied section must contain 後記 body");
    Require(afterword_id != QStringLiteral("s1") && afterword_id != QStringLiteral("s2"),
            "後記 must be a new section");

    Require(book.resourceText(QStringLiteral("illus1")).contains(QStringLiteral("color1.jpg")),
            "illus1 must rewrite co1.jpg to color1.jpg");
    Require(book.resourceText(QStringLiteral("illus1")).contains(QStringLiteral("alt=\"\""))
                && !book.resourceText(QStringLiteral("illus1")).contains(QStringLiteral("alt=\"co1\"")),
            "color pages must drop template file-name alt text");
    Require(book.resourceText(QStringLiteral("illus2")).contains(QStringLiteral("color2.jpg")),
            "illus2 must rewrite to color2.jpg");
    Require(book.resourceText(QStringLiteral("message")).contains(QStringLiteral("测试作者")),
            "credits page must include author");
    Require(book.resourceText(QStringLiteral("summary")).contains(QStringLiteral("简介第一段")),
            "synopsis page filled");
    Require(book.resourceText(QStringLiteral("contents")).contains(QStringLiteral("Section001.xhtml")),
            "TOC must link Section001");
    Require(book.toc().size() == 3
                && book.toc().first().toObject().value(QStringLiteral("label")).toString()
                    == QStringLiteral("第一話　春")
                && book.toc().last().toObject().value(QStringLiteral("label")).toString()
                    == QStringLiteral("後記"),
            "navigation TOC must use the manuscript chapter headings");
    Require(book.resourceText(QStringLiteral("title")).contains(QStringLiteral("测试书名")),
            "title page must use 中文標題");
    Require(book.metadata().value(QStringLiteral("creator")).toString() == QStringLiteral("测试作者"),
            "metadata creator");
    Require(book.metadata().value(QStringLiteral("title")).toString() == QStringLiteral("测试书名"),
            "metadata title from 中文標題");

    MemoryBookWorkspace custom_book = lnTemplateBook(false);
    MemoryResource custom_manuscript;
    custom_manuscript.id = QStringLiteral("custom-ms");
    custom_manuscript.bookPath = QStringLiteral("OEBPS/Misc/custom.txt");
    custom_manuscript.kind = QStringLiteral("text");
    custom_manuscript.mediaType = QStringLiteral("text/plain");
    custom_manuscript.text = QStringLiteral(
        "中文標題：自定义书名\n目錄\n序　章\n第一話　春\n終　章\n\n"
        "序　章\n序文。\n（插圖001）\n第一話　春\n春天。\n終　章\n完结。\n");
    custom_book.addResource(custom_manuscript);
    custom_book.addResource(image(QStringLiteral("img-001"),
                                  QStringLiteral("OEBPS/Images/001.jpg")));
    custom_book.setToc(QJsonArray {
        QJsonObject {
            { QStringLiteral("label"), QStringLiteral("书名") },
            { QStringLiteral("href"), QStringLiteral("Text/title.xhtml") }
        },
        QJsonObject {
            { QStringLiteral("label"), QStringLiteral("第一话") },
            { QStringLiteral("href"), QStringLiteral("Text/Section001.xhtml") }
        }
    });
    ToolRegistry custom_registry;
    registerBookTools(&custom_registry, &custom_book);
    auto run_custom = [&](const QString &name, const QJsonObject &arguments) {
        return custom_registry.find(name)->execute(arguments);
    };
    const QString custom_headings = QStringLiteral("^(序　章|第一話　春|終　章)$");
    const QString custom_illustrations = QStringLiteral("^（插圖([0-9]{3})）$");
    const QJsonObject custom_patterns {
        { QStringLiteral("heading_pattern"), custom_headings },
        { QStringLiteral("illustration_pattern"), custom_illustrations }
    };
    const ToolResult invalid_pattern = run_custom(QStringLiteral("manuscript.parse"),
        QJsonObject { { QStringLiteral("heading_pattern"), QStringLiteral("[") } });
    Require(!invalid_pattern.ok && invalid_pattern.code == QStringLiteral("REGEX_INVALID"),
            "invalid custom manuscript regex must be reported");
    const ToolResult custom_parsed = run_custom(QStringLiteral("manuscript.parse"),
                                                custom_patterns);
    Require(custom_parsed.ok
                && custom_parsed.data.value(QStringLiteral("chapter_count")).toInt() == 3,
            "custom manuscript parse finds prologue and finale");
    Require(run_custom(QStringLiteral("transaction.begin"), QJsonObject()).ok,
            "custom typeset begin");
    const ToolResult custom_typeset = run_custom(
        QStringLiteral("content.typeset_from_manuscript"), custom_patterns);
    Require(custom_typeset.ok
                && custom_typeset.data.value(QStringLiteral("chapters_filled")).toInt() == 3,
            "custom parse patterns carry into typesetting");
    Require(run_custom(QStringLiteral("transaction.commit"), QJsonObject {
        { QStringLiteral("expected_revision"), static_cast<qint64>(custom_book.revision()) }
    }).applied, "custom typeset commit");
    Require(custom_book.resourceText(QStringLiteral("s1")).contains(
                QStringLiteral("<h1>序　章</h1>")),
            "custom prologue heading appears in chapter");
    Require(custom_book.resourceText(QStringLiteral("s1")).contains(
                QStringLiteral("../Images/001.jpg")),
            "custom illustration marker resolves in chapter");
    Require(custom_book.toc().size() == 4
                && custom_book.toc().first().toObject().value(
                    QStringLiteral("label")).toString() == QStringLiteral("自定义书名")
                && custom_book.toc().last().toObject().value(
                    QStringLiteral("label")).toString() == QStringLiteral("終　章"),
            "custom chapter navigation matches parsed headings");

    Require(run(QStringLiteral("transaction.begin"), QJsonObject()).ok, "replace begin");
    const ToolResult replaced = run(QStringLiteral("resource.replace_text"), QJsonObject {
        { QStringLiteral("resource_id"), QStringLiteral("title") },
        { QStringLiteral("text"), QStringLiteral("<html><body><p>短页</p></body></html>") },
        { QStringLiteral("expected_revision"), static_cast<qint64>(book.resourceRevision(QStringLiteral("title"))) }
    });
    Require(replaced.ok && replaced.previewOnly, "replace_text stages");
    Require(run(QStringLiteral("transaction.commit"), QJsonObject {
        { QStringLiteral("expected_revision"), static_cast<qint64>(book.revision()) }
    }).applied, "replace_text commit");
    Require(book.resourceText(QStringLiteral("title")).contains(QStringLiteral("短页")),
            "replace_text must write the full file");

    Require(run(QStringLiteral("transaction.begin"), QJsonObject()).ok, "cap begin");
    const ToolResult too_large = run(QStringLiteral("resource.replace_text"), QJsonObject {
        { QStringLiteral("resource_id"), QStringLiteral("s1") },
        { QStringLiteral("text"), QString(70000, QLatin1Char('x')) },
        { QStringLiteral("expected_revision"), static_cast<qint64>(book.resourceRevision(QStringLiteral("s1"))) }
    });
    Require(!too_large.ok && too_large.code == QStringLiteral("REPLACE_TOO_LARGE"),
            "model-facing replace_text must reject chapter-sized payloads");
    run(QStringLiteral("transaction.rollback"), QJsonObject());

    AgentSession session;
    session.append(AgentEventType::UserMessage, QJsonObject {
        { QStringLiteral("text"), QStringLiteral("按模板排版") }
    });
    ToolRegistry prompt_tools;
    registerBookTools(&prompt_tools, &book);
    PromptAssembler assembler;
    const ModelRequest request = assembler.build(
        session, &book, prompt_tools, AgentMode::Auto,
        QStringLiteral("test"), false, QString(), QStringList());
    Require(!request.messages.isEmpty(), "prompt must have a system message");
    const QString system = request.messages.first().content;
    Require(system.contains(QStringLiteral("content.typeset_from_manuscript")),
            "system prompt must name the typeset tool");
    Require(system.contains(QStringLiteral("ln-template-typeset")),
            "matched skill must be injected");
    Require(request.toolContext.value(QStringLiteral("skills")).toObject()
                .value(QStringLiteral("available_count")).toInt() > 0,
            "request diagnostics must record available skills");
    Require(system.contains(QStringLiteral("Never paste chapter bodies")),
            "playbook must forbid pasting chapter bodies");
    const QString context = request.messages.at(1).content;
    Require(context.contains(QStringLiteral("OEBPS/Misc/book.txt")),
            "context must list the dropped manuscript path");
    Require(context.contains(QStringLiteral("color1.jpg")),
            "context must list dropped images");

    const QList<AgentSkill> skills = loadAgentSkills();
    bool have_skill = false;
    for (const AgentSkill &skill : skills) {
        if (skill.name == QLatin1String("ln-template-typeset")) have_skill = true;
    }
    Require(have_skill, "ln-template-typeset skill must load");
    Require(looksLikeLightNovelTemplate(&book), "fixture must look like the LN template");

    AgentSession paragraph_session;
    paragraph_session.append(AgentEventType::UserMessage, QJsonObject {
        { QStringLiteral("text"), QStringLiteral("整理全书伪段落 DIV 结构") }
    });
    const ModelRequest paragraph_request = assembler.build(
        paragraph_session, &book, prompt_tools, AgentMode::Edit,
        QStringLiteral("test"), false, QString(), QStringList());
    const QString paragraph_system = paragraph_request.messages.first().content;
        Require(!paragraph_system.contains(QStringLiteral("# Skill: paragraph-normalization")),
            "a registry without paragraph tools must not inject their workflow");

    AgentSession toc_session;
    toc_session.append(AgentEventType::UserMessage, QJsonObject {
        { QStringLiteral("text"), QStringLiteral("把目录中的子章节提升一级，别改正文标题") }
    });
    const ModelRequest toc_request = assembler.build(
        toc_session, &book, prompt_tools, AgentMode::Edit,
        QStringLiteral("test"), false, QString(), QStringList());
    const QString toc_system = toc_request.messages.first().content;
    Require(!toc_system.contains(QStringLiteral("# Skill: book-structure")),
            "a registry without TOC and Python tools must not inject a complete structure workflow");
    QStringList all_skill_tools;
    for (const AgentSkill &skill : skills) {
        Require(skill.metadataError.isEmpty() && skill.version == QStringLiteral("1")
                    && !skill.toolNames.isEmpty() && skill.requiresBook,
                "every bundled skill must have valid, structured version-1 metadata");
        all_skill_tools.append(skill.toolNames);
    }
    const AgentSkillSelection paragraph_selection = selectAgentSkills(
        skills, QStringLiteral("整理全书伪段落 DIV 结构"), &book, all_skill_tools);
    Require(paragraph_selection.bodies.contains(QStringLiteral("# Skill: paragraph-normalization"))
                && paragraph_selection.bodies.contains(QStringLiteral("`transaction.begin` first"))
                && paragraph_selection.bodies.contains(QStringLiteral("full EPUBCheck as not run")),
            "a capable host must retain the original paragraph workflow and its safeguards");
    const AgentSkillSelection toc_selection = selectAgentSkills(
        skills, QStringLiteral("把目录中的子章节提升一级，别改正文标题"), &book, all_skill_tools);
    Require(toc_selection.bodies.contains(QStringLiteral("# Skill: book-structure"))
                && toc_selection.bodies.contains(QStringLiteral("`toc.inspect_hierarchy`"))
                && toc_selection.bodies.contains(QStringLiteral("`toc.plan_transform`"))
                && toc_selection.bodies.contains(QStringLiteral("`toc.apply_transform`")),
            "a capable host must retain the native TOC workflow");
    Require(selectAgentSkills(skills, QStringLiteral("dividend"), &book, all_skill_tools).bodies.isEmpty(),
            "DIV trigger must not match an unrelated English word");
    for (const AgentMode mode : { AgentMode::Ask, AgentMode::Plan }) {
        const ModelRequest limited_request = assembler.build(
            session, &book, prompt_tools, mode, QStringLiteral("test"), false, QString(), QStringList());
        Require(!limited_request.messages.first().content.contains(QStringLiteral("# Skill:")),
                "Ask and Plan must not inject complete mutation recipes requiring denied tools");
        Require(limited_request.tools.size() < request.tools.size(),
                "skill declarations must not expand the policy-filtered tool schemas");
    }

    const QString saintess = QStringLiteral(
        "/Users/parsle/Downloads/聖女老師的前衛魔法課！ 1/聖女老師的前衛魔法課！ 1.txt");
    if (QFile::exists(saintess)) {
        QFile file(saintess);
        Require(file.open(QIODevice::ReadOnly), "open saintess sample");
        const ParsedManuscript live = parseManuscriptText(QString::fromUtf8(file.readAll()));
        Require(live.chapters.size() == 9, "real sample must parse prologue, interlude, finale and afterword");
        Require(live.chapters.first().heading == QStringLiteral("序　章")
                    && live.chapters.last().heading == QStringLiteral("後　記"),
                "real sample chapter bounds");
        int body_illustrations = 0;
        for (const ManuscriptChapter &chapter : live.chapters) body_illustrations += chapter.illustrationNames.size();
        Require(live.frontIllustrationNames.size() == 5 && body_illustrations == 12,
                "real sample （插圖NNN） markers must map front and body illustrations");
    }

    const QString asahi = QStringLiteral(
        "/Users/parsle/Code/sigil-modified/todo/Sigil-Enhanced-Native-Agent-PRD/test/"
        "[新人][关于光属性美少女朝日同学为何每周末都泡在我房间这件事][01]/"
        "關於光屬性美少女朝日同學為何每週末都泡在我房間這件事(01).txt");
    if (QFile::exists(asahi)) {
        QFile file(asahi);
        Require(file.open(QIODevice::ReadOnly), "open asahi fixture");
        const ParsedManuscript live = parseManuscriptText(QString::fromUtf8(file.readAll()));
        Require(live.chapters.size() >= 11, "asahi manuscript must yield 11+ chapters");
        Require(live.credits.author.contains(QStringLiteral("新人")), "asahi author");
        Require(live.frontIllustrationNames.contains(QStringLiteral("color1")), "asahi color1");
        Require(live.synopsis.contains(QStringLiteral("陽光系少女"))
                    || live.synopsis.contains(QStringLiteral("朝日")),
                "asahi synopsis");
    }

    return EXIT_SUCCESS;
}
