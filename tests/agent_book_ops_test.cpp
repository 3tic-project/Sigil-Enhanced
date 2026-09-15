#include <cstdlib>
#include <iostream>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "Agent/Core/AgentSession.h"
#include "Agent/Execution/ContentOps.h"
#include "Agent/Execution/MemoryBookWorkspace.h"
#include "Agent/Execution/ResourceMutations.h"
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
    const QJsonObject short_hit = searched.data.value(
        QStringLiteral("matches")).toArray().first().toObject();
    Require(short_hit.value(QStringLiteral("captures")).toArray().first().toString()
                    == QStringLiteral("boils")
                && !short_hit.value(QStringLiteral("preview_truncated")).toBool(),
            "short regex matches and captures must remain intact");

    MemoryBookWorkspace bounded_search_book;
    MemoryResource long_page;
    long_page.id = QStringLiteral("long-regex");
    long_page.bookPath = QStringLiteral("OEBPS/Text/long.xhtml");
    long_page.kind = QStringLiteral("xhtml");
    long_page.mediaType = QStringLiteral("application/xhtml+xml");
    long_page.text = QStringLiteral("<html>") + QString(5000, QLatin1Char('x'))
        + QStringLiteral("REGEX-TAIL-SECRET</html>");
    bounded_search_book.addResource(long_page);
    MemoryResource groups_page;
    groups_page.id = QStringLiteral("many-groups");
    groups_page.bookPath = QStringLiteral("OEBPS/Text/groups.xhtml");
    groups_page.kind = QStringLiteral("xhtml");
    groups_page.mediaType = QStringLiteral("application/xhtml+xml");
    groups_page.text = QStringLiteral("abcdefghijkl");
    bounded_search_book.addResource(groups_page);
    MemoryResource hits_page;
    hits_page.id = QStringLiteral("many-hits");
    hits_page.bookPath = QStringLiteral("OEBPS/Text/hits.xhtml");
    hits_page.kind = QStringLiteral("xhtml");
    hits_page.mediaType = QStringLiteral("application/xhtml+xml");
    hits_page.text = QString(200, QLatin1Char('z'));
    bounded_search_book.addResource(hits_page);
    ToolRegistry bounded_search_registry;
    registerBookTools(&bounded_search_registry, &bounded_search_book);
    auto run_bounded_search = [&](const QJsonObject &arguments) {
        return bounded_search_registry.find(
            QStringLiteral("book.search_regex"))->execute(arguments);
    };
    const ToolResult long_search = run_bounded_search(QJsonObject {
        { QStringLiteral("pattern"), QStringLiteral("(<html>([\\s\\S]+)</html>)") },
        { QStringLiteral("resource_id"), long_page.id }
    });
    const QJsonObject long_hit = long_search.data.value(
        QStringLiteral("matches")).toArray().first().toObject();
    const QJsonArray long_captures = long_hit.value(
        QStringLiteral("captures")).toArray();
    const QJsonArray long_capture_lengths = long_hit.value(
        QStringLiteral("capture_lengths")).toArray();
    Require(long_search.ok
                && long_hit.value(QStringLiteral("length")).toInt()
                    == long_page.text.size()
                && long_hit.value(QStringLiteral("match")).toString().size() == 240
                && long_hit.value(QStringLiteral("match_truncated")).toBool()
                && long_hit.value(QStringLiteral("capture_count")).toInt() == 2
                && long_hit.value(QStringLiteral("returned_capture_count")).toInt() == 2
                && long_captures.size() == 2
                && long_captures.at(0).toString().size() == 160
                && long_captures.at(1).toString().size() == 160
                && long_capture_lengths.at(0).toInt() == long_page.text.size()
                && long_hit.value(QStringLiteral("captures_truncated")).toBool()
                && long_hit.value(QStringLiteral("preview_truncated")).toBool()
                && !QJsonDocument(long_search.data).toJson().contains(
                    "REGEX-TAIL-SECRET"),
            "regex search must retain source locations while bounding long match and capture previews");
    const ToolResult grouped_search = run_bounded_search(QJsonObject {
        { QStringLiteral("pattern"), QStringLiteral(
              "(a)(b)(c)(d)(e)(f)(g)(h)(i)(j)(k)(l)") },
        { QStringLiteral("resource_id"), groups_page.id }
    });
    const QJsonObject grouped_hit = grouped_search.data.value(
        QStringLiteral("matches")).toArray().first().toObject();
    Require(grouped_hit.value(QStringLiteral("capture_count")).toInt() == 12
                && grouped_hit.value(
                    QStringLiteral("returned_capture_count")).toInt() == 8
                && grouped_hit.value(QStringLiteral("captures")).toArray().size() == 8
                && grouped_hit.value(QStringLiteral("capture_lengths")).toArray().size() == 8
                && grouped_hit.value(QStringLiteral("captures_truncated")).toBool(),
            "regex search must bound the number of returned capture previews");
    const ToolResult limited_search = run_bounded_search(QJsonObject {
        { QStringLiteral("pattern"), QStringLiteral("z") },
        { QStringLiteral("resource_id"), hits_page.id },
        { QStringLiteral("max_matches"), 999 }
    });
    const QJsonObject search_schema = bounded_search_registry.find(
        QStringLiteral("book.search_regex"))->descriptor().inputSchema
        .value(QStringLiteral("properties")).toObject()
        .value(QStringLiteral("max_matches")).toObject();
    Require(limited_search.data.value(QStringLiteral("match_count")).toInt() == 50
                && limited_search.data.value(QStringLiteral("max_matches")).toInt() == 50
                && limited_search.data.value(
                    QStringLiteral("match_limit_reached")).toBool()
                && search_schema.value(QStringLiteral("minimum")).toInt() == 1
                && search_schema.value(QStringLiteral("default")).toInt() == 40
                && search_schema.value(QStringLiteral("maximum")).toInt() == 50,
            "regex search must clamp and disclose its match-count bound");
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

    AgentSession bounded_session;
    ToolRegistry bounded_session_registry;
    registerBookTools(&bounded_session_registry, &book, &bounded_session);
    auto run_bounded_session = [&](const QString &name,
                                   const QJsonObject &arguments) {
        return bounded_session_registry.find(name)->execute(arguments);
    };
    for (int index = 0; index < MAX_SESSION_MEMORY_ENTRIES; ++index) {
        const ToolResult stored = run_bounded_session(
            QStringLiteral("session.remember"), QJsonObject {
                { QStringLiteral("key"), QStringLiteral("memory-%1").arg(index) },
                { QStringLiteral("value"), QStringLiteral("value-%1").arg(index) }
            });
        Require(stored.ok
                    && stored.data.value(QStringLiteral("memory_count")).toInt()
                        == index + 1
                    && !stored.data.contains(QStringLiteral("memory")),
                "remember must return only the changed bounded note");
    }
    const ToolResult updated_full_memory = run_bounded_session(
        QStringLiteral("session.remember"), QJsonObject {
            { QStringLiteral("key"), QStringLiteral("memory-0") },
            { QStringLiteral("value"), QStringLiteral("updated") }
        });
    const ToolResult overflow_memory = run_bounded_session(
        QStringLiteral("session.remember"), QJsonObject {
            { QStringLiteral("key"), QStringLiteral("memory-overflow") },
            { QStringLiteral("value"), QStringLiteral("overflow") }
        });
    const ToolResult oversized_memory = run_bounded_session(
        QStringLiteral("session.remember"), QJsonObject {
            { QStringLiteral("key"), QStringLiteral("oversized") },
            { QStringLiteral("value"), QString(
                MAX_SESSION_MEMORY_VALUE_LENGTH + 1, QLatin1Char('x')) }
        });
    Require(updated_full_memory.ok
                && updated_full_memory.data.value(
                    QStringLiteral("memory_count")).toInt()
                    == MAX_SESSION_MEMORY_ENTRIES
                && !overflow_memory.ok
                && overflow_memory.code
                    == QStringLiteral("SESSION_MEMORY_LIMIT_REACHED")
                && !oversized_memory.ok
                && oversized_memory.code
                    == QStringLiteral("SESSION_MEMORY_ARGUMENT_INVALID"),
            "session memory must allow updates but reject count and value overflow");
    const ToolResult memory_page = run_bounded_session(
        QStringLiteral("session.recall"), QJsonObject());
    const ToolResult memory_tail = run_bounded_session(
        QStringLiteral("session.recall"), QJsonObject {
            { QStringLiteral("offset"), 32 },
            { QStringLiteral("limit"), 999 }
        });
    const ToolResult exact_memory = run_bounded_session(
        QStringLiteral("session.recall"), QJsonObject {
            { QStringLiteral("key"), QStringLiteral("memory-0") }
        });
    Require(memory_page.data.value(QStringLiteral("memory")).toObject().size() == 16
                && memory_page.data.value(QStringLiteral("total_count")).toInt() == 64
                && memory_page.data.value(QStringLiteral("has_more")).toBool()
                && memory_page.data.value(QStringLiteral("next_offset")).toInt() == 16
                && !memory_page.data.value(QStringLiteral("memory")).toObject()
                    .contains(QStringLiteral("memory-0"))
                && memory_tail.data.value(QStringLiteral("memory")).toObject().size() == 32
                && memory_tail.data.value(QStringLiteral("memory")).toObject()
                    .contains(QStringLiteral("memory-0"))
                && memory_tail.data.value(QStringLiteral("limit")).toInt() == 32
                && !memory_tail.data.value(QStringLiteral("has_more")).toBool()
                && exact_memory.data.value(QStringLiteral("found")).toBool()
                && exact_memory.data.value(QStringLiteral("value")).toString()
                    == QStringLiteral("updated"),
            "session memory listing must paginate while exact recall remains available");

    QString last_task_id;
    for (int index = 0; index < MAX_SESSION_TASKS; ++index) {
        const ToolResult added = run_bounded_session(
            QStringLiteral("session.task_add"), QJsonObject {
                { QStringLiteral("title"), QStringLiteral("task-%1").arg(index) },
                { QStringLiteral("note"), QStringLiteral("note-%1").arg(index) }
            });
        last_task_id = added.data.value(QStringLiteral("id")).toString();
        Require(added.ok && !last_task_id.isEmpty()
                    && added.data.value(QStringLiteral("task_count")).toInt()
                        == index + 1
                    && added.data.value(QStringLiteral("task")).toObject()
                        .value(QStringLiteral("id")).toString() == last_task_id
                    && !added.data.contains(QStringLiteral("tasks")),
                "task add must return only the changed bounded task");
    }
    const ToolResult overflow_task = run_bounded_session(
        QStringLiteral("session.task_add"), QJsonObject {
            { QStringLiteral("title"), QStringLiteral("task-overflow") }
        });
    const ToolResult invalid_task_status = run_bounded_session(
        QStringLiteral("session.task_update"), QJsonObject {
            { QStringLiteral("id"), last_task_id },
            { QStringLiteral("status"), QStringLiteral("mystery") }
        });
    const ToolResult updated_task = run_bounded_session(
        QStringLiteral("session.task_update"), QJsonObject {
            { QStringLiteral("id"), last_task_id },
            { QStringLiteral("status"), QStringLiteral("done") }
        });
    Require(!overflow_task.ok
                && overflow_task.code == QStringLiteral("SESSION_TASK_LIMIT_REACHED")
                && !invalid_task_status.ok
                && invalid_task_status.code
                    == QStringLiteral("SESSION_TASK_ARGUMENT_INVALID")
                && updated_task.ok
                && updated_task.data.value(QStringLiteral("task")).toObject()
                    .value(QStringLiteral("status")).toString()
                    == QStringLiteral("done")
                && !updated_task.data.contains(QStringLiteral("tasks")),
            "session tasks must reject overflow and invalid status without full-list echoes");
    const ToolResult task_page = run_bounded_session(
        QStringLiteral("session.tasks"), QJsonObject());
    const ToolResult task_tail = run_bounded_session(
        QStringLiteral("session.tasks"), QJsonObject {
            { QStringLiteral("offset"), 100 },
            { QStringLiteral("limit"), 999 }
        });
    const QJsonObject recall_schema = bounded_session_registry.find(
        QStringLiteral("session.recall"))->descriptor().inputSchema
        .value(QStringLiteral("properties")).toObject();
    const QJsonObject tasks_schema = bounded_session_registry.find(
        QStringLiteral("session.tasks"))->descriptor().inputSchema
        .value(QStringLiteral("properties")).toObject();
    Require(task_page.data.value(QStringLiteral("tasks")).toArray().size() == 20
                && task_page.data.value(QStringLiteral("total_count")).toInt() == 128
                && task_page.data.value(QStringLiteral("next_offset")).toInt() == 20
                && task_tail.data.value(QStringLiteral("tasks")).toArray().size() == 28
                && task_tail.data.value(QStringLiteral("limit")).toInt() == 50
                && !task_tail.data.value(QStringLiteral("has_more")).toBool()
                && recall_schema.value(QStringLiteral("limit")).toObject()
                    .value(QStringLiteral("default")).toInt() == 16
                && recall_schema.value(QStringLiteral("limit")).toObject()
                    .value(QStringLiteral("maximum")).toInt() == 32
                && tasks_schema.value(QStringLiteral("limit")).toObject()
                    .value(QStringLiteral("default")).toInt() == 20
                && tasks_schema.value(QStringLiteral("limit")).toObject()
                    .value(QStringLiteral("maximum")).toInt() == 50,
            "session task pages and schemas must expose stable bounded traversal");
    bounded_session.clear();
    const QString after_clear_task = bounded_session.addTask(
        QStringLiteral("after-clear"));
    Require(bounded_session.remember(QStringLiteral("after-clear"),
                                     QStringLiteral("value"))
                && !after_clear_task.isEmpty()
                && bounded_session.memoryKeys().size() == 1
                && bounded_session.tasks().size() == 1
                && !bounded_session.remember(
                    QString(MAX_SESSION_MEMORY_KEY_LENGTH + 1, QLatin1Char('k')),
                    QStringLiteral("value"))
                && bounded_session.addTask(
                    QString(MAX_SESSION_TASK_TITLE_LENGTH + 1,
                            QLatin1Char('t'))).isEmpty()
                && !bounded_session.updateTask(
                    after_clear_task, QStringLiteral("invalid"), QString()),
            "new session must reset bounded memory/task storage and insertion order");

    Require(registry.find(QStringLiteral("resource_rename")) != nullptr, "rename registered");
    Require(registry.find(QStringLiteral("spine_sort")) != nullptr, "spine.sort registered");
    Require(registry.find(QStringLiteral("style_link")) != nullptr, "style.link registered");
    Require(registry.find(QStringLiteral("python_run")) != nullptr, "python.run registered");

    Require(resolveRenameTarget(QStringLiteral("OEBPS/Text/ch1.xhtml"), QStringLiteral("Heat"))
                == QStringLiteral("OEBPS/Text/Heat.xhtml"),
            "filename-only rename keeps folder and suffix");
    Require(resolveRenameTarget(QStringLiteral("OEBPS/Text/ch1.xhtml"),
                                QStringLiteral("OEBPS/Misc/ch1.xhtml"))
                == QStringLiteral("OEBPS/Misc/ch1.xhtml"),
            "full-path rename is used as-is");
    Require(rewriteHrefsForMove(QStringLiteral("<link href=\"../Styles/style.css\"/>"),
                                QStringLiteral("OEBPS/Text/ch1.xhtml"),
                                QStringLiteral("OEBPS/Styles/style.css"),
                                QStringLiteral("OEBPS/Styles/theme.css"))
                .contains(QStringLiteral("../Styles/theme.css")),
            "href rewrite follows a stylesheet rename");

    const ToolResult python_missing = run(QStringLiteral("python.run"), QJsonObject {
        { QStringLiteral("script"), QStringLiteral("print(plugin.book.summary())") }
    });
    Require(!python_missing.ok && python_missing.code == QStringLiteral("LIVE_PYTHON_UNAVAILABLE"),
            "Memory workspace cannot snapshot-run Python");

    Require(run(QStringLiteral("transaction.begin"), QJsonObject()).ok, "sort begin");
    Require(run(QStringLiteral("spine.set"), QJsonObject {
        { QStringLiteral("resource_ids"), QJsonArray { QStringLiteral("ch2"), QStringLiteral("ch1") } }
    }).ok, "unsort spine");
    Require(run(QStringLiteral("spine.sort"), QJsonObject()).ok, "spine.sort");
    Require(run(QStringLiteral("transaction.commit"), QJsonObject {
        { QStringLiteral("expected_revision"), static_cast<qint64>(book.revision()) }
    }).applied, "sort commit");
    Require(book.spine().first().toObject().value(QStringLiteral("resource_id")).toString()
                == QStringLiteral("ch1"),
            "numeric path sort puts ch1 before ch2");

    Require(run(QStringLiteral("transaction.begin"), QJsonObject()).ok, "rename begin");
    const ToolResult renamed = run(QStringLiteral("resource.rename"), QJsonObject {
        { QStringLiteral("resource_id"), QStringLiteral("css") },
        { QStringLiteral("book_path"), QStringLiteral("theme.css") }
    });
    Require(renamed.ok, "rename css");
    Require(book.workingText(QStringLiteral("ch1")).contains(QStringLiteral("../Styles/theme.css")),
            "rename rewrites stylesheet hrefs while staged");
    Require(run(QStringLiteral("resource.rename"), QJsonObject {
        { QStringLiteral("resource_id"), QStringLiteral("ch1") },
        { QStringLiteral("book_path"), QStringLiteral("OEBPS/ch1.xhtml") }
    }).ok, "move chapter");
    Require(book.workingText(QStringLiteral("ch1")).contains(QStringLiteral("href=\"Styles/theme.css\"")),
            "move rewrites outgoing hrefs for the new folder");
    const ToolResult previewed = run(QStringLiteral("transaction.preview"), QJsonObject());
    Require(previewed.ok, "rename preview");
    bool saw_renamed = false;
    for (const QJsonValue &value : previewed.data.value(QStringLiteral("changes")).toArray()) {
        if (value.toObject().value(QStringLiteral("renamed")).toBool()) saw_renamed = true;
    }
    Require(saw_renamed, "preview lists renamed resources");
    Require(run(QStringLiteral("transaction.commit"), QJsonObject {
        { QStringLiteral("expected_revision"), static_cast<qint64>(book.revision()) }
    }).applied, "rename commit");
    Require(book.resourceText(QStringLiteral("ch1")).contains(QStringLiteral("Styles/theme.css")),
            "committed move keeps rewritten hrefs");
    QString css_path;
    for (const QJsonValue &value : book.resources()) {
        if (value.toObject().value(QStringLiteral("resource_id")).toString() == QLatin1String("css")) {
            css_path = value.toObject().value(QStringLiteral("book_path")).toString();
        }
    }
    Require(css_path == QStringLiteral("OEBPS/Styles/theme.css"), "css path after rename");

    MemoryResource extra_css;
    extra_css.id = QStringLiteral("css2");
    extra_css.bookPath = QStringLiteral("OEBPS/Styles/extra.css");
    extra_css.kind = QStringLiteral("css");
    extra_css.mediaType = QStringLiteral("text/css");
    extra_css.text = QStringLiteral("p { color: red; }");
    book.addResource(extra_css);
    Require(run(QStringLiteral("transaction.begin"), QJsonObject()).ok, "link begin");
    Require(run(QStringLiteral("style.link"), QJsonObject {
        { QStringLiteral("html_ids"), QJsonArray { QStringLiteral("ch1") } },
        { QStringLiteral("css_ids"), QJsonArray { QStringLiteral("css"), QStringLiteral("css2") } }
    }).ok, "style.link");
    Require(run(QStringLiteral("transaction.commit"), QJsonObject {
        { QStringLiteral("expected_revision"), static_cast<qint64>(book.revision()) }
    }).applied, "link commit");
    const QString linked = book.resourceText(QStringLiteral("ch1"));
    Require(linked.contains(QStringLiteral("Styles/theme.css")), "primary stylesheet linked");
    Require(linked.contains(QStringLiteral("Styles/extra.css")), "second stylesheet linked");

    const ToolResult check_wellformed = run(QStringLiteral("book.check"), QJsonObject());
    Require(check_wellformed.data.contains(QStringLiteral("wellformed")), "book.check wellformed");

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
