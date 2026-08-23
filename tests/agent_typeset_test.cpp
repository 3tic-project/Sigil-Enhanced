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

    Require(lineLooksLikeChapterHeading(QStringLiteral("第一話　春")), "第一話 must be a heading");
    Require(lineLooksLikeChapterHeading(QStringLiteral("第１話　光屬性的朝日同學")), "fullwidth 第１話 must be a heading");
    Require(lineLooksLikeChapterHeading(QStringLiteral("後記")), "後記 must be a heading");
    Require(!lineLooksLikeChapterHeading(QStringLiteral("春天到了。")), "body line must not be a heading");
    Require(chapterNumberFromHeading(QStringLiteral("第１０話　x")) == 10, "fullwidth 10");
    Require(chineseTalkLabel(1) == QStringLiteral("第一话"), "chineseTalkLabel 1");
    Require(chineseTalkLabel(11) == QStringLiteral("第十一话"), "chineseTalkLabel 11");
    Require(illustrationNameInLine(QStringLiteral("［插图：color1］")) == QStringLiteral("color1"),
            "illustration marker");

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
    Require(parsed_tool.data.value(QStringLiteral("template")).toObject()
                .value(QStringLiteral("detected")).toBool(),
            "template must be detected");
    Require(!QJsonDocument(parsed_tool.data).toJson().contains("春天到了"),
            "parse tool must omit chapter bodies");

    Require(run(QStringLiteral("transaction.begin"), QJsonObject()).ok, "begin");
    const ToolResult typeset = run(QStringLiteral("content.typeset_from_manuscript"), QJsonObject());
    Require(typeset.ok && typeset.previewOnly, "typeset must stage");
    Require(typeset.data.value(QStringLiteral("chapters_filled")).toInt() == 3, "three chapters filled");
    Require(typeset.data.value(QStringLiteral("sections_copied")).toInt() == 1, "one extra section copied");
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
    Require(book.resourceText(QStringLiteral("illus2")).contains(QStringLiteral("color2.jpg")),
            "illus2 must rewrite to color2.jpg");
    Require(book.resourceText(QStringLiteral("message")).contains(QStringLiteral("测试作者")),
            "credits page must include author");
    Require(book.resourceText(QStringLiteral("summary")).contains(QStringLiteral("简介第一段")),
            "synopsis page filled");
    Require(book.resourceText(QStringLiteral("contents")).contains(QStringLiteral("Section001.xhtml")),
            "TOC must link Section001");
    Require(book.resourceText(QStringLiteral("title")).contains(QStringLiteral("测试书名")),
            "title page must use 中文標題");
    Require(book.metadata().value(QStringLiteral("creator")).toString() == QStringLiteral("测试作者"),
            "metadata creator");
    Require(book.metadata().value(QStringLiteral("title")).toString() == QStringLiteral("测试书名"),
            "metadata title from 中文標題");

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
