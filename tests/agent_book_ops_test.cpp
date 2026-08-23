#include <cstdlib>
#include <iostream>

#include <QJsonArray>
#include <QJsonObject>

#include "Agent/Core/AgentSession.h"
#include "Agent/Execution/ContentOps.h"
#include "Agent/Execution/MemoryBookWorkspace.h"
#include "Agent/Tools/BookTools.h"
#include "Agent/Tools/ToolRegistry.h"
#include "Agent/Typeset/ManuscriptParser.h"

namespace
{

void Require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main()
{
    using namespace SigilAgent;

    Require(xmlEscape(QStringLiteral("a<b")) == QStringLiteral("a&lt;b"), "xmlEscape");
    Require(relativeBookHref(QStringLiteral("OEBPS/Text/ch.xhtml"),
                             QStringLiteral("OEBPS/Images/cover.jpg"))
                == QStringLiteral("../Images/cover.jpg"),
            "relative href");
    const QString wrapped = wrapPlainText(
        QStringLiteral("Title line\n\nHello.\n[pic:foo]\n"),
        QJsonObject {
            { QStringLiteral("heading_pattern"), QStringLiteral("^Title") },
            { QStringLiteral("illustration_pattern"), QStringLiteral("\\[pic:([^\\]]+)\\]") },
            { QStringLiteral("illustration_html"),
              QStringLiteral("<div class=\"pic\"><img alt=\"$1\" src=\"../Images/$1.jpg\"/></div>") }
        });
    Require(wrapped.contains(QStringLiteral("<h1>Title line</h1>")), "wrap_plain heading");
    Require(wrapped.contains(QStringLiteral("<p>Hello.</p>")), "wrap_plain paragraph");
    Require(wrapped.contains(QStringLiteral("src=\"../Images/foo.jpg\"")), "wrap_plain illustration $1");

    ParseOptions options;
    options.headingRegex = QStringLiteral("^Chapter \\d+");
    options.illustrationRegex = QStringLiteral("\\{img:([^}]+)\\}");
    const ParsedManuscript custom = parseManuscriptText(
        QStringLiteral("Chapter 1 One\nChapter 1 One\n{img:a}\nbody one\nChapter 2 Two\nbody two\n"),
        QString(), options);
    Require(custom.chapters.size() == 2, "custom heading regex chapters");
    Require(custom.chapters.at(0).illustrationNames.contains(QStringLiteral("a")),
            "custom illustration regex");

    MemoryBookWorkspace book = MemoryBookWorkspace::samplePhysicsBook();
    MemoryResource img;
    img.id = QStringLiteral("img1");
    img.bookPath = QStringLiteral("OEBPS/Images/heat.jpg");
    img.kind = QStringLiteral("image");
    img.mediaType = QStringLiteral("image/jpeg");
    img.binary = QByteArray("x");
    book.addResource(img);

    AgentSession session;
    ToolRegistry registry;
    registerBookTools(&registry, &book, &session);
    auto run = [&](const QString &name, const QJsonObject &arguments) {
        IAgentTool *tool = registry.find(name);
        Require(tool != nullptr, "missing tool");
        return tool->execute(arguments);
    };

    Require(registry.find(QStringLiteral("book_search_regex")) != nullptr, "search_regex registered");
    Require(registry.find(QStringLiteral("session_task_add")) != nullptr, "session tools registered");

    Require(run(QStringLiteral("transaction.begin"), QJsonObject()).ok, "begin");

    const ToolResult wrapped_res = run(QStringLiteral("content.wrap"), QJsonObject {
        { QStringLiteral("resource_id"), QStringLiteral("ch1") },
        { QStringLiteral("pattern"), QStringLiteral("100 degrees") },
        { QStringLiteral("open"), QStringLiteral("<em>") },
        { QStringLiteral("close"), QStringLiteral("</em>") }
    });
    Require(wrapped_res.ok, "wrap");
    Require(run(QStringLiteral("transaction.commit"), QJsonObject {
        { QStringLiteral("expected_revision"), static_cast<qint64>(book.revision()) }
    }).applied, "wrap commit");
    Require(book.resourceText(QStringLiteral("ch1")).contains(QStringLiteral("<em>100 degrees</em>")),
            "wrap applied");

    Require(run(QStringLiteral("transaction.begin"), QJsonObject()).ok, "regex begin");
    Require(run(QStringLiteral("content.replace_regex"), QJsonObject {
        { QStringLiteral("resource_id"), QStringLiteral("ch1") },
        { QStringLiteral("pattern"), QStringLiteral("HELLO") },
        { QStringLiteral("replacement"), QStringLiteral("Heat") }
    }).ok, "regex no-op still ok");
    const ToolResult searched = run(QStringLiteral("book.search_regex"), QJsonObject {
        { QStringLiteral("pattern"), QStringLiteral("Water (\\w+)") },
        { QStringLiteral("resource_id"), QStringLiteral("ch1") }
    });
    Require(searched.ok && searched.data.value(QStringLiteral("match_count")).toInt() >= 1,
            "regex search");
    Require(run(QStringLiteral("content.replace_regex"), QJsonObject {
        { QStringLiteral("resource_id"), QStringLiteral("ch1") },
        { QStringLiteral("pattern"), QStringLiteral("<em>([^<]+)</em>") },
        { QStringLiteral("replacement"), QStringLiteral("<strong>$1</strong>") }
    }).ok, "regex replace");
    Require(run(QStringLiteral("transaction.commit"), QJsonObject {
        { QStringLiteral("expected_revision"), static_cast<qint64>(book.revision()) }
    }).applied, "regex commit");
    Require(book.resourceText(QStringLiteral("ch1")).contains(QStringLiteral("<strong>100 degrees</strong>")),
            "capture replace");

    Require(run(QStringLiteral("transaction.begin"), QJsonObject()).ok, "split begin");
    Require(run(QStringLiteral("resource.replace_text"), QJsonObject {
        { QStringLiteral("resource_id"), QStringLiteral("ch2") },
        { QStringLiteral("text"), QStringLiteral(
            "<html><head><title>t</title></head><body>"
            "<h1>A</h1><p>one</p><h1>B</h1><p>two</p></body></html>") },
        { QStringLiteral("expected_revision"), static_cast<qint64>(book.resourceRevision(QStringLiteral("ch2"))) }
    }).ok, "seed split source");
    const ToolResult split = run(QStringLiteral("content.split"), QJsonObject {
        { QStringLiteral("resource_id"), QStringLiteral("ch2") }
    });
    Require(split.ok, "split");
    Require(split.data.value(QStringLiteral("section_count")).toInt() == 2, "two sections");
    Require(run(QStringLiteral("transaction.commit"), QJsonObject {
        { QStringLiteral("expected_revision"), static_cast<qint64>(book.revision()) }
    }).applied, "split commit");
    Require(book.resourceText(QStringLiteral("ch2")).contains(QStringLiteral("<h1>A</h1>")),
            "first section kept");
    Require(!book.resourceText(QStringLiteral("ch2")).contains(QStringLiteral("<h1>B</h1>")),
            "second heading moved");
    const QJsonArray created = split.data.value(QStringLiteral("created")).toArray();
    Require(!created.isEmpty(), "created copy");
    const QString copy_id = created.first().toObject().value(QStringLiteral("resource_id")).toString();
    Require(book.resourceText(copy_id).contains(QStringLiteral("<h1>B</h1>")), "second file has B");

    Require(run(QStringLiteral("transaction.begin"), QJsonObject()).ok, "image begin");
    Require(run(QStringLiteral("image.insert"), QJsonObject {
        { QStringLiteral("page_id"), QStringLiteral("ch1") },
        { QStringLiteral("image_id"), QStringLiteral("img1") },
        { QStringLiteral("anchor"), QStringLiteral("</h1>") }
    }).ok, "insert image");
    Require(run(QStringLiteral("transaction.commit"), QJsonObject {
        { QStringLiteral("expected_revision"), static_cast<qint64>(book.revision()) }
    }).applied, "image commit");
    Require(book.resourceText(QStringLiteral("ch1")).contains(QStringLiteral("../Images/heat.jpg")),
            "image href");

    Require(run(QStringLiteral("transaction.begin"), QJsonObject()).ok, "body begin");
    Require(run(QStringLiteral("content.replace_body"), QJsonObject {
        { QStringLiteral("resource_id"), QStringLiteral("ch1") },
        { QStringLiteral("source_resource_id"), QStringLiteral("ch2") }
    }).ok, "replace_body from source");
    Require(run(QStringLiteral("transaction.commit"), QJsonObject {
        { QStringLiteral("expected_revision"), static_cast<qint64>(book.revision()) }
    }).applied, "body commit");
    Require(book.resourceText(QStringLiteral("ch1")).contains(QStringLiteral("<h1>A</h1>")),
            "body copied from ch2 without model dump");

    Require(run(QStringLiteral("transaction.begin"), QJsonObject()).ok, "merge begin");
    Require(run(QStringLiteral("content.merge"), QJsonObject {
        { QStringLiteral("resource_ids"), QJsonArray { copy_id, QStringLiteral("ch2") } },
        { QStringLiteral("delete_sources"), false }
    }).ok, "merge");
    Require(run(QStringLiteral("toc.generate"), QJsonObject()).ok, "toc generate");
    Require(run(QStringLiteral("metadata.update"), QJsonObject {
        { QStringLiteral("patch"), QJsonObject {
            { QStringLiteral("publisher"), QStringLiteral("Test Press") },
            { QStringLiteral("_remove"), QJsonArray { QStringLiteral("creator") } }
        } }
    }).ok, "metadata crud");
    Require(run(QStringLiteral("spine.set"), QJsonObject {
        { QStringLiteral("resource_ids"), QJsonArray {
            QStringLiteral("ch1"), QStringLiteral("ch2"), copy_id
        } }
    }).ok, "spine set");
    Require(run(QStringLiteral("resource.delete"), QJsonObject {
        { QStringLiteral("resource_id"), copy_id }
    }).ok, "delete staged");
    Require(run(QStringLiteral("transaction.commit"), QJsonObject {
        { QStringLiteral("expected_revision"), static_cast<qint64>(book.revision()) }
    }).applied, "structure commit");
    Require(book.metadata().value(QStringLiteral("publisher")).toString() == QStringLiteral("Test Press"),
            "publisher set");
    Require(!book.metadata().contains(QStringLiteral("creator")), "creator removed");
    bool copy_gone = true;
    for (const QJsonValue &value : book.resources()) {
        if (value.toObject().value(QStringLiteral("resource_id")).toString() == copy_id) copy_gone = false;
    }
    Require(copy_gone, "deleted resource is gone");

    const ToolResult check = run(QStringLiteral("book.check"), QJsonObject());
    Require(check.ok, "book.check");

    const ToolResult remembered = run(QStringLiteral("session.remember"), QJsonObject {
        { QStringLiteral("key"), QStringLiteral("heading") },
        { QStringLiteral("value"), QStringLiteral("^Chapter") }
    });
    Require(remembered.ok, "remember");
    const ToolResult recalled = run(QStringLiteral("session.recall"), QJsonObject {
        { QStringLiteral("key"), QStringLiteral("heading") }
    });
    Require(recalled.data.value(QStringLiteral("value")).toString() == QStringLiteral("^Chapter"),
            "recall");
    const ToolResult task = run(QStringLiteral("session.task_add"), QJsonObject {
        { QStringLiteral("title"), QStringLiteral("Wrap paragraphs") }
    });
    Require(task.ok && !task.data.value(QStringLiteral("id")).toString().isEmpty(), "task add");
    Require(run(QStringLiteral("session.task_update"), QJsonObject {
        { QStringLiteral("id"), task.data.value(QStringLiteral("id")).toString() },
        { QStringLiteral("status"), QStringLiteral("done") }
    }).ok, "task update");
    Require(run(QStringLiteral("session.tasks"), QJsonObject())
                .data.value(QStringLiteral("tasks")).toArray().first().toObject()
                .value(QStringLiteral("status")).toString() == QStringLiteral("done"),
            "task done");

    Require(run(QStringLiteral("transaction.begin"), QJsonObject()).ok, "plain begin");
    MemoryResource txt;
    txt.id = QStringLiteral("plain");
    txt.bookPath = QStringLiteral("OEBPS/Misc/plain.txt");
    txt.kind = QStringLiteral("text");
    txt.mediaType = QStringLiteral("text/plain");
    txt.text = QStringLiteral("HEAD\nbody line");
    book.addResource(txt);
    Require(run(QStringLiteral("content.wrap_plain"), QJsonObject {
        { QStringLiteral("source_resource_id"), QStringLiteral("plain") },
        { QStringLiteral("target_id"), QStringLiteral("ch1") },
        { QStringLiteral("rules"), QJsonObject {
            { QStringLiteral("heading_pattern"), QStringLiteral("^HEAD") }
        } }
    }).ok, "wrap_plain tool");
    Require(run(QStringLiteral("transaction.commit"), QJsonObject {
        { QStringLiteral("expected_revision"), static_cast<qint64>(book.revision()) }
    }).applied, "wrap_plain commit");
    Require(book.resourceText(QStringLiteral("ch1")).contains(QStringLiteral("<h1>HEAD</h1>")),
            "plain wrapped into existing xhtml");

    return EXIT_SUCCESS;
}
