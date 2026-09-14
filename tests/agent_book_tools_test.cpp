#include <cstdlib>
#include <iostream>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "Agent/Execution/MemoryBookWorkspace.h"
#include "Agent/Execution/PatchRange.h"
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

} // namespace

int main()
{
    using namespace SigilAgent;
    MemoryBookWorkspace book = MemoryBookWorkspace::samplePhysicsBook();
    ToolRegistry registry;
    registerBookTools(&registry, &book);

    auto run = [&](const QString &name, const QJsonObject &arguments) {
        IAgentTool *tool = registry.find(name);
        Require(tool != nullptr, "shipped tool missing from registry");
        return tool->execute(arguments);
    };

    const QJsonArray schemas = registry.openaiToolSchemas();
    Require(!schemas.isEmpty(), "openai tool schemas must be published");
    for (const QJsonValue &value : schemas) {
        const QString wire = value.toObject()
                                 .value(QStringLiteral("function")).toObject()
                                 .value(QStringLiteral("name")).toString();
        Require(ToolRegistry::isValidWireName(wire),
                "OpenAI/DeepSeek tool names must match ^[a-zA-Z0-9_-]+$");
        Require(!wire.contains(QLatin1Char('.')),
                "dots are invalid in the Chat Completions tool name");
    }
    Require(registry.find(QStringLiteral("book_summary")) == registry.find(QStringLiteral("book.summary")),
            "wire names must resolve to the same shipped tool as dotted names");
    Require(ToolRegistry::toWireName(QStringLiteral("book.summary")) == QStringLiteral("book_summary"),
            "book.summary must be sent as book_summary");
    const QJsonArray read_only_schemas = registry.openaiToolSchemas(
        [](const AgentToolDescriptor &descriptor) {
            return !descriptor.mutatesBook;
        });
    Require(!read_only_schemas.isEmpty()
                && read_only_schemas.size() < schemas.size(),
            "schema filters must publish a strict read-only subset");
    for (const QJsonValue &value : read_only_schemas) {
        const QString wire = value.toObject()
                                 .value(QStringLiteral("function")).toObject()
                                 .value(QStringLiteral("name")).toString();
        Require(registry.find(wire) != nullptr
                    && !registry.find(wire)->descriptor().mutatesBook,
                "filtered schemas must retain only accepted descriptors");
    }

    const QString paragraph_impact = humanReadableImpact(
        QStringLiteral("paragraphs.apply"),
        QJsonObject {
            { QStringLiteral("plan_id"), QStringLiteral("plan-123") },
            { QStringLiteral("plan_digest"), QStringLiteral("digest-456") },
            { QStringLiteral("expected_book_revision"), 17 }
        });
    Require(paragraph_impact.contains(QStringLiteral("plan-123"))
                && paragraph_impact.contains(QStringLiteral("digest-456"))
                && paragraph_impact.contains(QStringLiteral("17"))
                && paragraph_impact.contains(QStringLiteral("unchanged")),
            "paragraph approval impact must identify the reviewed plan and staging boundary");

    const QString toc_impact = humanReadableImpact(
        QStringLiteral("toc.apply_transform"),
        QJsonObject {
            { QStringLiteral("plan_id"), QStringLiteral("toc-plan-123") },
            { QStringLiteral("plan_digest"), QStringLiteral("toc-digest-456") },
            { QStringLiteral("expected_book_revision"), 19 }
        });
    Require(toc_impact.contains(QStringLiteral("toc-plan-123"))
                && toc_impact.contains(QStringLiteral("toc-digest-456"))
                && toc_impact.contains(QStringLiteral("19"))
                && toc_impact.contains(QStringLiteral("XHTML headings"))
                && toc_impact.contains(QStringLiteral("unchanged")),
            "TOC approval impact must identify the plan and navigation-only staging boundary");

    const ToolResult summary = run(QStringLiteral("book.summary"), QJsonObject());
    Require(summary.ok && summary.data.value(QStringLiteral("title")).toString() == QStringLiteral("Junior Physics"),
            "book.summary must return the fixture title");
    Require(summary.data.value(QStringLiteral("spine_count")).toInt() == 2,
            "book.summary must report real spine count");

    const ToolResult resources = run(QStringLiteral("book.resources"), QJsonObject());
    Require(resources.data.value(QStringLiteral("resources")).toArray().size() >= 4,
            "book.resources must list fixture files");

    const ToolResult spine = run(QStringLiteral("book.spine"), QJsonObject());
    Require(spine.data.value(QStringLiteral("spine")).toArray().size() == 2,
            "book.spine must list both chapters");

    const ToolResult toc = run(QStringLiteral("book.toc"), QJsonObject());
    Require(toc.data.value(QStringLiteral("toc")).toArray().first().toObject()
                .value(QStringLiteral("label")).toString().contains(QStringLiteral("Heat")),
            "book.toc must return real labels");

    const ToolResult metadata = run(QStringLiteral("book.metadata"), QJsonObject());
    Require(metadata.data.value(QStringLiteral("language")).toString() == QStringLiteral("zh-CN"),
            "book.metadata must return language");

    const ToolResult search = run(QStringLiteral("book.search"), QJsonObject {
        { QStringLiteral("query"), QStringLiteral("boils") }
    });
    Require(search.data.value(QStringLiteral("matches")).toArray().size() == 1,
            "book.search must find the fixture sentence");

    const ToolResult fragment = run(QStringLiteral("resource.read_fragment"), QJsonObject {
        { QStringLiteral("resource_id"), QStringLiteral("ch1") },
        { QStringLiteral("offset"), 0 },
        { QStringLiteral("limit"), 80 }
    });
    Require(fragment.ok && fragment.data.value(QStringLiteral("text")).toString().contains(QStringLiteral("<html")),
            "resource.read_fragment must return real XHTML text");
    Require(fragment.data.value(QStringLiteral("truncated")).toBool(),
            "bounded fragment must truncate the fixture chapter");
    const ToolResult heat = run(QStringLiteral("resource.read_fragment"), QJsonObject {
        { QStringLiteral("resource_id"), QStringLiteral("ch1") },
        { QStringLiteral("offset"), 0 },
        { QStringLiteral("limit"), 400 }
    });
    Require(heat.data.value(QStringLiteral("text")).toString().contains(QStringLiteral("Heat")),
            "a larger bounded read must include the chapter heading");
    Require(!fragment.data.value(QStringLiteral("text")).toString().contains(QStringLiteral("OTTO-FAKE-FONT")),
            "fragment must not include font bytes");

    const ToolResult fonts = run(QStringLiteral("font.inventory"), QJsonObject());
    Require(fonts.data.value(QStringLiteral("embedded_fonts")).toArray().size() == 1,
            "font.inventory must list the embedded font");
    Require(!QJsonDocument(fonts.data).toJson().contains("OTTO-FAKE-FONT"),
            "font.inventory must never return font bytes");

    const ToolResult css = run(QStringLiteral("style.stylesheets"), QJsonObject());
    Require(css.data.value(QStringLiteral("stylesheets")).toArray().first().toObject()
                .value(QStringLiteral("text")).toString().contains(QStringLiteral("SerifFace")),
            "style.stylesheets must return CSS text");

    const ToolResult validate = run(QStringLiteral("book.validate"), QJsonObject());
    Require(validate.data.value(QStringLiteral("ok")).toBool(),
            "valid fixture must pass book.validate");

    const QString original = book.resourceText(QStringLiteral("ch1"));
    const quint64 original_revision = book.revision();
    Require(run(QStringLiteral("transaction.begin"), QJsonObject()).ok, "transaction.begin failed");
    const ToolResult patched = run(QStringLiteral("resource.patch_fragment"), QJsonObject {
        { QStringLiteral("resource_id"), QStringLiteral("ch1") },
        { QStringLiteral("expected_text"), QStringLiteral("<title>Heat</title>") },
        { QStringLiteral("text"), QStringLiteral("<title>HELLO</title>") },
        { QStringLiteral("expected_revision"), static_cast<qint64>(book.resourceRevision(QStringLiteral("ch1"))) }
    });
    Require(patched.ok && patched.previewOnly, "patch must stage a preview");
    Require(book.resourceText(QStringLiteral("ch1")) == original,
            "staged patch must not change the live book");

    const ToolResult preview = run(QStringLiteral("transaction.preview"), QJsonObject());
    Require(preview.ok && preview.previewOnly, "preview must not apply");
    Require(book.resourceText(QStringLiteral("ch1")) == original,
            "preview must leave live book unchanged");

    const ToolResult committed = run(QStringLiteral("transaction.commit"), QJsonObject {
        { QStringLiteral("expected_revision"), static_cast<qint64>(original_revision) }
    });
    Require(committed.ok && committed.applied, "commit must apply staged patch");
    Require(book.resourceText(QStringLiteral("ch1")).contains(QStringLiteral("<title>HELLO</title>")),
            "commit must write the patched text");
    Require(book.revision() != original_revision, "commit must bump book revision");

    MemoryBookWorkspace recovery_book = MemoryBookWorkspace::samplePhysicsBook();
    const QString recovery_ch1_before = recovery_book.resourceText(QStringLiteral("ch1"));
    const QString recovery_ch2_before = recovery_book.resourceText(QStringLiteral("ch2"));
    Require(recovery_book.beginTransaction(QStringLiteral("two chapter edit")).ok,
            "recovery transaction begin failed");
    Require(recovery_book.patchFragment(
                QStringLiteral("ch1"), -1, -1, QStringLiteral("<title>RECOVER ONE</title>"),
                recovery_book.resourceRevision(QStringLiteral("ch1")),
                QStringLiteral("<title>Heat</title>")).ok,
            "first recovery patch failed");
    Require(recovery_book.patchFragment(
                QStringLiteral("ch2"), -1, -1, QStringLiteral("<title>RECOVER TWO</title>"),
                recovery_book.resourceRevision(QStringLiteral("ch2")),
                QStringLiteral("<title>Light</title>")).ok,
            "second recovery patch failed");
    const BookOpResult recovery_created = recovery_book.createTaskRestorePoint(
        QStringLiteral("two chapter edit"),
        QStringList { QStringLiteral("ch1"), QStringLiteral("ch2") });
    Require(recovery_created.ok, "guarded recovery point creation failed");
    const QString recovery_id = recovery_created.data
        .value(QStringLiteral("checkpoint_id")).toString();
    Require(recovery_book.commitTransaction(recovery_book.revision()).applied,
            "recovery transaction commit failed");
    Require(recovery_book.sealTaskRestorePoint(recovery_id).ok,
            "guarded recovery point sealing failed");
    Require(!recovery_book.restoreCheckpoint(recovery_id).ok,
            "generic checkpoint restore must not bypass task conflict guards");
    const BookOpResult recovered = recovery_book.restoreTaskRestorePoint(recovery_id);
    Require(recovered.ok && recovered.applied,
            "unchanged post-task resources must be restorable");
    Require(recovery_book.resourceText(QStringLiteral("ch1")) == recovery_ch1_before
                && recovery_book.resourceText(QStringLiteral("ch2")) == recovery_ch2_before,
            "task restore must restore every affected text resource");
    Require(recovery_book.restoreTaskRestorePoint(recovery_id).code
                == QStringLiteral("TASK_ALREADY_RESTORED"),
            "task restore points must be one-shot");

    MemoryBookWorkspace conflict_book = MemoryBookWorkspace::samplePhysicsBook();
    Require(conflict_book.beginTransaction(QStringLiteral("conflicting task")).ok,
            "conflict transaction begin failed");
    Require(conflict_book.patchFragment(
                QStringLiteral("ch1"), -1, -1, QStringLiteral("<title>TASK ONE</title>"),
                conflict_book.resourceRevision(QStringLiteral("ch1")),
                QStringLiteral("<title>Heat</title>")).ok,
            "conflict first task patch failed");
    Require(conflict_book.patchFragment(
                QStringLiteral("ch2"), -1, -1, QStringLiteral("<title>TASK TWO</title>"),
                conflict_book.resourceRevision(QStringLiteral("ch2")),
                QStringLiteral("<title>Light</title>")).ok,
            "conflict second task patch failed");
    const QString conflict_id = conflict_book.createTaskRestorePoint(
        QStringLiteral("conflicting task"),
        QStringList { QStringLiteral("ch1"), QStringLiteral("ch2") })
        .data.value(QStringLiteral("checkpoint_id")).toString();
    Require(conflict_book.commitTransaction(conflict_book.revision()).applied,
            "conflicting task commit failed");
    Require(conflict_book.sealTaskRestorePoint(conflict_id).ok,
            "conflicting task seal failed");
    const QString committed_ch1 = conflict_book.resourceText(QStringLiteral("ch1"));
    Require(conflict_book.beginTransaction(QStringLiteral("later manual edit")).ok,
            "later edit begin failed");
    Require(conflict_book.patchFragment(
                QStringLiteral("ch2"), -1, -1, QStringLiteral("<title>MANUAL</title>"),
                conflict_book.resourceRevision(QStringLiteral("ch2")),
                QStringLiteral("<title>TASK TWO</title>")).ok,
            "later manual edit failed");
    Require(conflict_book.commitTransaction(conflict_book.revision()).applied,
            "later manual edit commit failed");
    const QString manual_ch2 = conflict_book.resourceText(QStringLiteral("ch2"));
    const BookOpResult conflicted = conflict_book.restoreTaskRestorePoint(conflict_id);
    Require(!conflicted.ok && conflicted.code == QStringLiteral("TASK_RESTORE_CONFLICT")
                && conflicted.data.value(QStringLiteral("live_book_unchanged")).toBool(),
            "later edits to an affected resource must block the whole restore");
    Require(conflict_book.resourceText(QStringLiteral("ch1")) == committed_ch1
                && conflict_book.resourceText(QStringLiteral("ch2")) == manual_ch2,
            "a conflicted restore must not partially mutate any resource");

    const quint64 after_first = book.revision();
    book.bumpRevision();
    Require(run(QStringLiteral("transaction.begin"), QJsonObject()).ok, "second begin failed");
    run(QStringLiteral("resource.patch_fragment"), QJsonObject {
        { QStringLiteral("resource_id"), QStringLiteral("ch2") },
        { QStringLiteral("expected_text"), QStringLiteral("<title>Light</title>") },
        { QStringLiteral("text"), QStringLiteral("<title>Z</title>") },
        { QStringLiteral("expected_revision"), static_cast<qint64>(book.resourceRevision(QStringLiteral("ch2"))) }
    });
    const QString ch2_before = book.resourceText(QStringLiteral("ch2"));
    const ToolResult stale = run(QStringLiteral("transaction.commit"), QJsonObject {
        { QStringLiteral("expected_revision"), static_cast<qint64>(after_first) }
    });
    Require(!stale.ok && stale.code == QStringLiteral("BOOK_REVISION_CONFLICT"),
            "stale expected revision must be rejected");
    Require(book.resourceText(QStringLiteral("ch2")) == ch2_before,
            "revision conflict must leave the live book unchanged");
    run(QStringLiteral("transaction.rollback"), QJsonObject());

    MemoryBookWorkspace failing = MemoryBookWorkspace::samplePhysicsBook();
    ToolRegistry failing_registry;
    registerBookTools(&failing_registry, &failing);
    auto run_failing = [&](const QString &name, const QJsonObject &arguments) {
        return failing_registry.find(name)->execute(arguments);
    };
    const QString ch1_live = failing.resourceText(QStringLiteral("ch1"));
    const QString css_live = failing.resourceText(QStringLiteral("css"));
    failing.setInjectedFailureIndex(1);
    run_failing(QStringLiteral("transaction.begin"), QJsonObject());
    run_failing(QStringLiteral("resource.patch_fragment"), QJsonObject {
        { QStringLiteral("resource_id"), QStringLiteral("ch1") },
        { QStringLiteral("expected_text"), QStringLiteral("<title>Heat</title>") },
        { QStringLiteral("text"), QStringLiteral("<title>Q</title>") },
        { QStringLiteral("expected_revision"), static_cast<qint64>(failing.resourceRevision(QStringLiteral("ch1"))) }
    });
    run_failing(QStringLiteral("css.update_rules"), QJsonObject {
        { QStringLiteral("resource_id"), QStringLiteral("css") },
        { QStringLiteral("text"), QStringLiteral("body{color:red}") },
        { QStringLiteral("expected_revision"), static_cast<qint64>(failing.resourceRevision(QStringLiteral("css"))) }
    });
    const ToolResult rolled = run_failing(QStringLiteral("transaction.commit"), QJsonObject {
        { QStringLiteral("expected_revision"), static_cast<qint64>(failing.revision()) }
    });
    Require(!rolled.ok && rolled.code == QStringLiteral("TRANSACTION_ROLLED_BACK"),
            "injected mid-batch failure must roll back");
    Require(failing.resourceText(QStringLiteral("ch1")) == ch1_live
                && failing.resourceText(QStringLiteral("css")) == css_live,
            "rollback after injected failure must restore every resource");
    Require(!failing.hasOpenTransaction(), "failed commit must not leave an open transaction");

    const QString skeleton = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<!DOCTYPE html>\n\n"
        "<html xmlns=\"http://www.w3.org/1999/xhtml\" xmlns:epub=\"http://www.idpf.org/2007/ops\">\n"
        "<head>\n"
        "  <title></title>\n"
        "</head>\n\n"
        "<body>\n"
        "  <p>&#160;</p>\n"
        "</body>\n"
        "</html>");
    MemoryBookWorkspace title_book;
    MemoryResource page;
    page.id = QStringLiteral("s1");
    page.bookPath = QStringLiteral("OEBPS/Text/Section0001.xhtml");
    page.mediaType = QStringLiteral("application/xhtml+xml");
    page.kind = QStringLiteral("xhtml");
    page.text = skeleton;
    title_book.addResource(page);
    title_book.setSpine({ QStringLiteral("s1") });
    ToolRegistry title_registry;
    registerBookTools(&title_registry, &title_book);
    auto run_title = [&](const QString &name, const QJsonObject &arguments) {
        return title_registry.find(name)->execute(arguments);
    };
    Require(run_title(QStringLiteral("transaction.begin"), QJsonObject()).ok, "title book begin failed");

    const ToolResult titled_fragment = run_title(QStringLiteral("resource.read_fragment"), QJsonObject {
        { QStringLiteral("resource_id"), QStringLiteral("s1") },
        { QStringLiteral("offset"), 0 },
        { QStringLiteral("limit"), 400 }
    });
    Require(titled_fragment.data.value(QStringLiteral("text")).toString().contains(QStringLiteral("<title></title>")),
            "read_fragment text must be copyable expected_text without line prefixes");
    Require(titled_fragment.data.value(QStringLiteral("start_line")).toInt() == 1,
            "read_fragment must report 1-based start_line");
    Require(!titled_fragment.data.value(QStringLiteral("lines")).toArray().isEmpty(),
            "read_fragment must list line identity for start_line");

    MemoryBookWorkspace unique_book;
    MemoryResource unique_page = page;
    unique_book.addResource(unique_page);
    unique_book.setSpine({ QStringLiteral("s1") });
    ToolRegistry unique_registry;
    registerBookTools(&unique_registry, &unique_book);
    auto run_unique = [&](const QString &name, const QJsonObject &arguments) {
        return unique_registry.find(name)->execute(arguments);
    };
    Require(run_unique(QStringLiteral("transaction.begin"), QJsonObject()).ok, "unique-text book begin failed");
    const ToolResult no_offsets = run_unique(QStringLiteral("resource.patch_fragment"), QJsonObject {
        { QStringLiteral("resource_id"), QStringLiteral("s1") },
        { QStringLiteral("expected_text"), QStringLiteral("<title></title>") },
        { QStringLiteral("text"), QStringLiteral("<title>测试</title>") },
        { QStringLiteral("expected_revision"), static_cast<qint64>(unique_book.resourceRevision(QStringLiteral("s1"))) }
    });
    Require(no_offsets.ok, "unique expected_text must locate the title without start/end offsets");
    Require(run_unique(QStringLiteral("transaction.commit"), QJsonObject {
        { QStringLiteral("expected_revision"), static_cast<qint64>(unique_book.revision()) }
    }).applied, "unique-text patch must commit");
    Require(unique_book.resourceText(QStringLiteral("s1")).contains(QStringLiteral("<title>测试</title>")),
            "committed title must be 测试 when located only by expected_text");
    Require(!unique_book.resourceText(QStringLiteral("s1")).contains(QStringLiteral("</title>e>")),
            "unique-text commit must not leave </title>e>");

    const ToolResult missing = run_title(QStringLiteral("resource.patch_fragment"), QJsonObject {
        { QStringLiteral("resource_id"), QStringLiteral("s1") },
        { QStringLiteral("start"), 149 },
        { QStringLiteral("end"), 164 },
        { QStringLiteral("text"), QStringLiteral("<title>测试</title>") },
        { QStringLiteral("expected_revision"), static_cast<qint64>(title_book.resourceRevision(QStringLiteral("s1"))) }
    });
    Require(!missing.ok && missing.code == QStringLiteral("PATCH_EXPECTED_TEXT_REQUIRED"),
            "a patch without expected_text must be rejected");
    Require(title_book.resourceText(QStringLiteral("s1")) == skeleton,
            "rejected patch must not change the live book");

    const ToolResult split = run_title(QStringLiteral("resource.patch_fragment"), QJsonObject {
        { QStringLiteral("resource_id"), QStringLiteral("s1") },
        { QStringLiteral("start"), 149 },
        { QStringLiteral("end"), 164 },
        { QStringLiteral("expected_text"), skeleton.mid(149, 15) },
        { QStringLiteral("text"), QStringLiteral("<title>测试</title>") },
        { QStringLiteral("expected_revision"), static_cast<qint64>(title_book.resourceRevision(QStringLiteral("s1"))) }
    });
    Require(!split.ok && split.code == QStringLiteral("PATCH_SPLITS_MARKUP"),
            "a range that cuts through </title> must be rejected");

    const ToolResult corrected = run_title(QStringLiteral("resource.patch_fragment"), QJsonObject {
        { QStringLiteral("resource_id"), QStringLiteral("s1") },
        { QStringLiteral("start"), 149 },
        { QStringLiteral("end"), 164 },
        { QStringLiteral("expected_text"), QStringLiteral("<title></title>") },
        { QStringLiteral("text"), QStringLiteral("<title>测试</title>") },
        { QStringLiteral("expected_revision"), static_cast<qint64>(title_book.resourceRevision(QStringLiteral("s1"))) }
    });
    Require(corrected.ok && corrected.data.value(QStringLiteral("range_corrected")).toBool(),
            "wrong offsets with a unique expected_text must be corrected");
    Require(corrected.data.value(QStringLiteral("start")).toInt() == skeleton.indexOf(QStringLiteral("<title></title>")),
            "corrected start must be the unique <title></title> occurrence");
    const ToolResult staged_read = run_title(QStringLiteral("resource.read_fragment"), QJsonObject {
        { QStringLiteral("resource_id"), QStringLiteral("s1") },
        { QStringLiteral("offset"), 0 },
        { QStringLiteral("limit"), 400 }
    });
    const QString staged_text = staged_read.data.value(QStringLiteral("text")).toString();
    Require(staged_text.contains(QStringLiteral("<title>测试</title>")),
            "staged fragment must contain the full replacement title");
    Require(!staged_text.contains(QStringLiteral("</title>e>")),
            "corrected patch must not leave a split </title>");
    const ToolResult previewed = run_title(QStringLiteral("transaction.preview"), QJsonObject());
    Require(previewed.ok && previewed.previewOnly, "preview after corrected patch must succeed");
    Require(!previewed.data.value(QStringLiteral("changes")).toArray().isEmpty(),
            "preview must list the staged resource");
    Require(run_title(QStringLiteral("transaction.commit"), QJsonObject {
        { QStringLiteral("expected_revision"), static_cast<qint64>(title_book.revision()) }
    }).applied, "corrected title patch must commit");
    Require(title_book.resourceText(QStringLiteral("s1")).contains(QStringLiteral("<title>测试</title>")),
            "committed title must be 测试");
    Require(!title_book.resourceText(QStringLiteral("s1")).contains(QStringLiteral("</title>e>")),
            "committed XHTML must not contain the leftover e>");

    Require(rangeSplitsMarkup(skeleton, 149, 164), "the live off-by-two title range must be detected as a tag split");
    const PatchRangeResolution resolved = resolvePatchRange(skeleton, 149, 164, QStringLiteral("<title></title>"));
    Require(resolved.ok && resolved.rangeCorrected, "resolvePatchRange must unique-match <title></title>");
    const PatchRangeResolution by_text_only = resolvePatchRange(
        skeleton, -1, -1, QStringLiteral("<title></title>"));
    Require(by_text_only.ok, "resolvePatchRange must work with no character offsets");

    MemoryBookWorkspace lines_book;
    MemoryResource twice;
    twice.id = QStringLiteral("s2");
    twice.bookPath = QStringLiteral("OEBPS/Text/twice.xhtml");
    twice.mediaType = QStringLiteral("application/xhtml+xml");
    twice.kind = QStringLiteral("xhtml");
    twice.text = QStringLiteral(
        "<html><body>\n<p>alpha</p>\n<p>alpha</p>\n</body></html>");
    lines_book.addResource(twice);
    lines_book.setSpine({ QStringLiteral("s2") });
    ToolRegistry lines_registry;
    registerBookTools(&lines_registry, &lines_book);
    auto run_lines = [&](const QString &name, const QJsonObject &arguments) {
        return lines_registry.find(name)->execute(arguments);
    };
    Require(run_lines(QStringLiteral("transaction.begin"), QJsonObject()).ok, "line-disambiguation begin failed");
    const ToolResult ambiguous = run_lines(QStringLiteral("resource.patch_fragment"), QJsonObject {
        { QStringLiteral("resource_id"), QStringLiteral("s2") },
        { QStringLiteral("expected_text"), QStringLiteral("<p>alpha</p>") },
        { QStringLiteral("text"), QStringLiteral("<p>beta</p>") },
        { QStringLiteral("expected_revision"), static_cast<qint64>(lines_book.resourceRevision(QStringLiteral("s2"))) }
    });
    Require(!ambiguous.ok && ambiguous.code == QStringLiteral("PATCH_TEXT_AMBIGUOUS"),
            "repeated expected_text without start_line must be ambiguous");
    const ToolResult lined = run_lines(QStringLiteral("resource.patch_fragment"), QJsonObject {
        { QStringLiteral("resource_id"), QStringLiteral("s2") },
        { QStringLiteral("expected_text"), QStringLiteral("<p>alpha</p>") },
        { QStringLiteral("start_line"), 3 },
        { QStringLiteral("text"), QStringLiteral("<p>beta</p>") },
        { QStringLiteral("expected_revision"), static_cast<qint64>(lines_book.resourceRevision(QStringLiteral("s2"))) }
    });
    Require(lined.ok, "start_line must pick one of several identical substrings");
    Require(run_lines(QStringLiteral("transaction.commit"), QJsonObject {
        { QStringLiteral("expected_revision"), static_cast<qint64>(lines_book.revision()) }
    }).applied, "line-disambiguated patch must commit");
    const QString lined_text = lines_book.resourceText(QStringLiteral("s2"));
    Require(lined_text.contains(QStringLiteral("<p>alpha</p>")) && lined_text.contains(QStringLiteral("<p>beta</p>")),
            "start_line must change only the chosen occurrence");

    MemoryBookWorkspace copy_book = MemoryBookWorkspace::samplePhysicsBook();
    ToolRegistry copy_registry;
    registerBookTools(&copy_registry, &copy_book);
    auto run_copy = [&](const QString &name, const QJsonObject &arguments) {
        return copy_registry.find(name)->execute(arguments);
    };
    Require(run_copy(QStringLiteral("transaction.begin"), QJsonObject()).ok, "copy begin failed");
    const ToolResult copied = run_copy(QStringLiteral("resource.copy"), QJsonObject {
        { QStringLiteral("resource_id"), QStringLiteral("ch1") }
    });
    Require(copied.ok && copied.previewOnly, "resource.copy must stage a new file");
    Require(copied.data.value(QStringLiteral("book_path")).toString()
                == QStringLiteral("OEBPS/Text/ch3.xhtml"),
            "copy of ch1.xhtml must pick the next unique sibling path");
    const QString staging_id = copied.data.value(QStringLiteral("resource_id")).toString();
    Require(copy_book.resourceText(QStringLiteral("ch1")).contains(QStringLiteral("Heat")),
            "copy must not change the live source before commit");
    Require(run_copy(QStringLiteral("transaction.commit"), QJsonObject {
        { QStringLiteral("expected_revision"), static_cast<qint64>(copy_book.revision()) }
    }).applied, "copy commit must apply");
    Require(copy_book.resourceText(staging_id).contains(QStringLiteral("Heat")),
            "committed copy must contain the source XHTML");
    bool in_spine = false;
    for (const QJsonValue &value : copy_book.spine()) {
        if (value.toObject().value(QStringLiteral("resource_id")).toString() == staging_id) {
            in_spine = true;
        }
    }
    Require(in_spine, "copied XHTML must be inserted in the spine");

    return EXIT_SUCCESS;
}
