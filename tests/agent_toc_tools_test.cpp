#include <cstdlib>
#include <iostream>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "Agent/Core/AgentCancellation.h"
#include "Agent/Execution/MemoryBookWorkspace.h"
#include "Agent/Tools/TocTools.h"
#include "Agent/Tools/ToolRegistry.h"

namespace
{

using namespace SigilAgent;

void Require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

QJsonObject entry(const QString &label, const QString &href, int level)
{
    return QJsonObject {
        { QStringLiteral("label"), label },
        { QStringLiteral("href"), href },
        { QStringLiteral("level"), level }
    };
}

MemoryBookWorkspace hierarchyBook()
{
    MemoryBookWorkspace book;
    book.setToc(QJsonArray {
        entry(QStringLiteral("Part A"), QStringLiteral("Text/a.xhtml"), 1),
        entry(QStringLiteral("Chapter B"), QStringLiteral("Text/b.xhtml"), 2),
        entry(QStringLiteral("Chapter C"), QStringLiteral("Text/c.xhtml"), 2),
        entry(QStringLiteral("Section C.1"), QStringLiteral("Text/c.xhtml#one"), 3),
        entry(QStringLiteral("Chapter D"), QStringLiteral("Text/d.xhtml"), 2),
        entry(QStringLiteral("Part E"), QStringLiteral("Text/e.xhtml"), 1)
    });
    return book;
}

ToolResult run(ToolRegistry &registry,
               const QString &name,
               const QJsonObject &arguments = QJsonObject())
{
    IAgentTool *tool = registry.find(name);
    Require(tool != nullptr, "native TOC tool is missing");
    return tool->execute(arguments);
}

} // namespace

int main()
{
    MemoryBookWorkspace book = hierarchyBook();
    const QJsonArray original = book.toc();
    ToolRegistry registry;
    registerTocTools(&registry, &book);

    Require(registry.find(QStringLiteral("toc_inspect_hierarchy"))
                == registry.find(QStringLiteral("toc.inspect_hierarchy")),
            "wire and dotted TOC tool names must resolve identically");
    Require(registry.find(QStringLiteral("toc.plan_transform"))->descriptor().risk
                == ToolRisk::Read
                && !registry.find(QStringLiteral("toc.plan_transform"))->descriptor().mutatesBook,
            "TOC planning must remain read-only");
    const ToolResult invalid_page = run(
        registry, QStringLiteral("toc.inspect_hierarchy"),
        QJsonObject { { QStringLiteral("limit"), QStringLiteral("100") } });
    Require(!invalid_page.ok && invalid_page.code == QStringLiteral("INVALID_ARGUMENT"),
            "TOC pagination must reject string limits");

    const ToolResult first_page = run(
        registry, QStringLiteral("toc.inspect_hierarchy"),
        QJsonObject {
            { QStringLiteral("offset"), 0 },
            { QStringLiteral("limit"), 3 }
        });
    Require(first_page.ok && first_page.previewOnly && !first_page.applied,
            "TOC inspection must be read-only");
    Require(first_page.data.value(QStringLiteral("snapshot_id")).toString().size() == 64
                && first_page.data.value(QStringLiteral("node_count")).toInt() == 6
                && first_page.data.value(QStringLiteral("nodes")).toArray().size() == 3
                && first_page.data.value(QStringLiteral("next_offset")).toInt() == 3,
            "TOC inspection must publish a stable paginated snapshot");
    const QJsonArray nodes = first_page.data.value(QStringLiteral("nodes")).toArray();
    Require(nodes.at(0).toObject().value(QStringLiteral("node_id")).toInteger() == 1
                && nodes.at(0).toObject().value(QStringLiteral("depth")).toInt() == 1
                && nodes.at(1).toObject().value(QStringLiteral("parent_id")).toInteger() == 1
                && nodes.at(2).toObject().value(QStringLiteral("label")).toString()
                    == QStringLiteral("Chapter C"),
            "TOC inspection lost hierarchy, identity, or labels");

    const QString snapshot_id = first_page.data.value(
        QStringLiteral("snapshot_id")).toString();
    const ToolResult promote = run(
        registry, QStringLiteral("toc.plan_transform"),
        QJsonObject {
            { QStringLiteral("snapshot_id"), snapshot_id },
            { QStringLiteral("operation"), QStringLiteral("promote") },
            { QStringLiteral("node_ids"), QJsonArray { 3 } }
        });
    Require(promote.ok && promote.previewOnly && !promote.applied,
            "native TOC promotion plan failed");
    Require(promote.data.value(QStringLiteral("plan_id")).toString().size() == 64
                && promote.data.value(QStringLiteral("plan_digest")).toString().size() == 64
                && promote.data.value(QStringLiteral("preorder_preserved")).toBool()
                && promote.data.value(QStringLiteral("adopted_count")).toInt() == 1
                && promote.data.value(QStringLiteral("affected_count")).toInt() == 2,
            "TOC promotion plan omitted binding or adoption evidence");
    const QJsonArray promote_changes = promote.data.value(
        QStringLiteral("changes")).toArray();
    Require(promote_changes.at(0).toObject().value(
                QStringLiteral("node_id")).toInteger() == 3
                && promote_changes.at(0).toObject().value(
                    QStringLiteral("from_parent_id")).toInteger() == 1
                && promote_changes.at(0).toObject().value(
                    QStringLiteral("to_parent_id")).toInteger() == 0,
            "promotion plan did not describe the selected node reparenting");
    Require(book.toc() == original && !book.hasOpenTransaction(),
            "TOC planning changed the Book or opened a transaction");

    const ToolResult duplicate = run(
        registry, QStringLiteral("toc.plan_transform"),
        QJsonObject {
            { QStringLiteral("snapshot_id"), snapshot_id },
            { QStringLiteral("operation"), QStringLiteral("promote") },
            { QStringLiteral("node_ids"), QJsonArray { 3, 3 } }
        });
    Require(!duplicate.ok && duplicate.code == QStringLiteral("INVALID_ARGUMENT"),
            "duplicate TOC node IDs must be rejected");
    const ToolResult invalid_adopt = run(
        registry, QStringLiteral("toc.plan_transform"),
        QJsonObject {
            { QStringLiteral("snapshot_id"), snapshot_id },
            { QStringLiteral("operation"), QStringLiteral("promote") },
            { QStringLiteral("node_ids"), QJsonArray { 3 } },
            { QStringLiteral("adopt_following_siblings"), QStringLiteral("true") }
        });
    Require(!invalid_adopt.ok && invalid_adopt.code == QStringLiteral("INVALID_ARGUMENT"),
            "TOC adoption policy must reject non-boolean values");
    const ToolResult top_level = run(
        registry, QStringLiteral("toc.plan_transform"),
        QJsonObject {
            { QStringLiteral("snapshot_id"), snapshot_id },
            { QStringLiteral("operation"), QStringLiteral("promote") },
            { QStringLiteral("node_ids"), QJsonArray { 1 } }
        });
    Require(!top_level.ok && top_level.code == QStringLiteral("TOC_TRANSFORM_REJECTED")
                && top_level.data.value(QStringLiteral("reason")).toString()
                    == QStringLiteral("already_top_level"),
            "native TOC boundary failure must be structured");

    MemoryBookWorkspace other_book = hierarchyBook();
    ToolRegistry other_registry;
    registerTocTools(&other_registry, &other_book);
    const ToolResult other_inspect = run(
        other_registry, QStringLiteral("toc.inspect_hierarchy"));
    Require(other_inspect.ok
                && other_inspect.data.value(QStringLiteral("snapshot_id")).toString()
                    != snapshot_id,
            "identical books in different Agent sessions need distinct TOC snapshots");
    const ToolResult cross_session = run(
        other_registry, QStringLiteral("toc.plan_transform"),
        QJsonObject {
            { QStringLiteral("snapshot_id"), snapshot_id },
            { QStringLiteral("operation"), QStringLiteral("promote") },
            { QStringLiteral("node_ids"), QJsonArray { 3 } }
        });
    Require(!cross_session.ok
                && cross_session.code == QStringLiteral("TOC_SNAPSHOT_BINDING_MISMATCH"),
            "a TOC snapshot from another Agent session must not be usable");

    book.bumpRevision();
    const ToolResult stale = run(
        registry, QStringLiteral("toc.plan_transform"),
        QJsonObject {
            { QStringLiteral("snapshot_id"), snapshot_id },
            { QStringLiteral("operation"), QStringLiteral("demote") },
            { QStringLiteral("node_ids"), QJsonArray { 6 } }
        });
    Require(!stale.ok && stale.code == QStringLiteral("TOC_SNAPSHOT_STALE"),
            "book revision changes must invalidate a TOC snapshot");

    MemoryBookWorkspace demote_book = hierarchyBook();
    ToolRegistry demote_registry;
    registerTocTools(&demote_registry, &demote_book);
    const ToolResult demote_inspect = run(
        demote_registry, QStringLiteral("toc.inspect_hierarchy"));
    const ToolResult demote = run(
        demote_registry, QStringLiteral("toc.plan_transform"),
        QJsonObject {
            { QStringLiteral("snapshot_id"),
              demote_inspect.data.value(QStringLiteral("snapshot_id")) },
            { QStringLiteral("operation"), QStringLiteral("demote") },
            { QStringLiteral("node_ids"), QJsonArray { 6 } }
        });
    Require(demote.ok && demote.data.value(
                QStringLiteral("preorder_preserved")).toBool()
                && demote.data.value(QStringLiteral("adopted_count")).toInt() == 0
                && demote.data.value(QStringLiteral("changes")).toArray().first().toObject()
                    .value(QStringLiteral("to_parent_id")).toInteger() == 1,
            "native TOC demotion plan is incorrect");

    MemoryBookWorkspace invalid_book;
    invalid_book.setToc(QJsonArray {
        entry(QStringLiteral("Broken"), QStringLiteral("Text/broken.xhtml"), 3)
    });
    ToolRegistry invalid_registry;
    registerTocTools(&invalid_registry, &invalid_book);
    const ToolResult invalid = run(
        invalid_registry, QStringLiteral("toc.inspect_hierarchy"));
    Require(!invalid.ok && invalid.code == QStringLiteral("TOC_INVALID"),
            "invalid TOC depth jumps must be rejected");

    AgentCancellation cancellation;
    cancellation.request();
    MemoryBookWorkspace cancelled_book = hierarchyBook();
    ToolRegistry cancelled_registry;
    registerTocTools(&cancelled_registry, &cancelled_book, &cancellation);
    const ToolResult cancelled = run(
        cancelled_registry, QStringLiteral("toc.inspect_hierarchy"));
    Require(!cancelled.ok && cancelled.code == QStringLiteral("CANCELLED")
                && cancelled_book.toc() == original,
            "cancelled TOC inspection must remain read-only");

    return EXIT_SUCCESS;
}
