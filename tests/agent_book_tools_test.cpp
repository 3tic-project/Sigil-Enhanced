#include <cstdlib>
#include <iostream>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "Agent/Execution/MemoryBookWorkspace.h"
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
        { QStringLiteral("start"), 0 },
        { QStringLiteral("end"), 5 },
        { QStringLiteral("text"), QStringLiteral("HELLO") },
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
    Require(book.resourceText(QStringLiteral("ch1")).startsWith(QStringLiteral("HELLO")),
            "commit must write the patched text");
    Require(book.revision() != original_revision, "commit must bump book revision");

    const quint64 after_first = book.revision();
    book.bumpRevision();
    Require(run(QStringLiteral("transaction.begin"), QJsonObject()).ok, "second begin failed");
    run(QStringLiteral("resource.patch_fragment"), QJsonObject {
        { QStringLiteral("resource_id"), QStringLiteral("ch2") },
        { QStringLiteral("start"), 0 },
        { QStringLiteral("end"), 1 },
        { QStringLiteral("text"), QStringLiteral("Z") },
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
        { QStringLiteral("start"), 0 },
        { QStringLiteral("end"), 1 },
        { QStringLiteral("text"), QStringLiteral("Q") },
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
    return EXIT_SUCCESS;
}
