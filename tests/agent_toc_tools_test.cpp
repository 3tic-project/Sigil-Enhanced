#include <cstdlib>
#include <iostream>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "Agent/Core/AgentCancellation.h"
#include "Agent/Execution/MemoryBookWorkspace.h"
#include "Agent/Security/PermissionPolicy.h"
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

QJsonObject planBinding(const ToolResult &plan)
{
    return QJsonObject {
        { QStringLiteral("plan_id"),
          plan.data.value(QStringLiteral("plan_id")).toString() },
        { QStringLiteral("plan_digest"),
          plan.data.value(QStringLiteral("plan_digest")).toString() },
        { QStringLiteral("expected_book_revision"),
          plan.data.value(QStringLiteral("book_revision")).toInteger() }
    };
}

ToolResult inspectAndPlan(ToolRegistry &registry,
                          const QString &operation,
                          const QJsonArray &node_ids)
{
    const ToolResult inspected = run(
        registry, QStringLiteral("toc.inspect_hierarchy"));
    Require(inspected.ok, "TOC inspection failed while preparing a plan");
    const ToolResult planned = run(
        registry, QStringLiteral("toc.plan_transform"),
        QJsonObject {
            { QStringLiteral("snapshot_id"),
              inspected.data.value(QStringLiteral("snapshot_id")) },
            { QStringLiteral("operation"), operation },
            { QStringLiteral("node_ids"), node_ids }
        });
    Require(planned.ok, "TOC transform planning failed");
    return planned;
}

class FailHierarchyStageWorkspace final : public MemoryBookWorkspace
{
public:
    BookOpResult updateTocHierarchy(const TocEditTree &,
                                    const TocEditTree &) override
    {
        return BookOpResult::error(
            QStringLiteral("INJECTED_STAGE_FAILURE"),
            QStringLiteral("injected TOC stage failure"));
    }
};

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
    const AgentToolDescriptor apply_descriptor =
        registry.find(QStringLiteral("toc.apply_transform"))->descriptor();
    Require(apply_descriptor.risk == ToolRisk::Bulk
                && apply_descriptor.mutatesBook
                && apply_descriptor.supportsPreview,
            "TOC apply risk and preview metadata are incorrect");
    PermissionPolicy permission_policy;
    Require(permission_policy.evaluate(AgentMode::Ask, apply_descriptor)
                == PermissionAction::Deny
                && permission_policy.evaluate(AgentMode::Plan, apply_descriptor)
                    == PermissionAction::Allow
                && permission_policy.evaluate(AgentMode::Edit, apply_descriptor)
                    == PermissionAction::Ask
                && permission_policy.evaluate(AgentMode::Auto, apply_descriptor)
                    == PermissionAction::Allow,
            "TOC apply permissions do not match staged bulk-edit policy");
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

    QJsonObject wrong_binding = planBinding(promote);
    wrong_binding.insert(QStringLiteral("plan_digest"), QStringLiteral("wrong"));
    const ToolResult rejected_apply = run(
        registry, QStringLiteral("toc.apply_transform"), wrong_binding);
    Require(!rejected_apply.ok
                && rejected_apply.code == QStringLiteral("TOC_PLAN_BINDING_MISMATCH")
                && !book.hasOpenTransaction(),
            "an unreviewed TOC plan digest must be rejected before staging");

    const ToolResult staged = run(
        registry, QStringLiteral("toc.apply_transform"), planBinding(promote));
    Require(staged.ok && staged.previewOnly && !staged.applied
                && !staged.data.value(QStringLiteral("applied_to_book")).toBool()
                && staged.data.value(QStringLiteral("requires_transaction_commit")).toBool()
                && staged.data.value(QStringLiteral("changes_xhtml_headings")).toBool() == false,
            "TOC apply must report staged navigation-only state");
    Require(book.hasOpenTransaction() && book.toc() == original,
            "TOC staging changed the live Book");
    const SigilAgent::BookOpResult preview = book.previewTransaction();
    Require(preview.ok && preview.previewOnly
                && preview.data.value(QStringLiteral("toc_changed")).toBool(),
            "TOC transaction preview omitted the staged hierarchy change");
    const quint64 plan_revision = static_cast<quint64>(
        promote.data.value(QStringLiteral("book_revision")).toInteger());
    Require(book.commitTransaction(plan_revision).applied,
            "reviewed TOC hierarchy transaction did not commit");
    const QJsonArray promoted_toc = book.toc();
    Require(promoted_toc.size() == original.size()
                && promoted_toc.at(0).toObject().value(QStringLiteral("level")).toInt() == 1
                && promoted_toc.at(1).toObject().value(QStringLiteral("level")).toInt() == 2
                && promoted_toc.at(2).toObject().value(QStringLiteral("level")).toInt() == 1
                && promoted_toc.at(3).toObject().value(QStringLiteral("level")).toInt() == 2
                && promoted_toc.at(4).toObject().value(QStringLiteral("level")).toInt() == 2
                && promoted_toc.at(5).toObject().value(QStringLiteral("level")).toInt() == 1,
            "committed TOC promotion did not preserve preorder and adoption semantics");
    for (int index = 0; index < original.size(); ++index) {
        Require(promoted_toc.at(index).toObject().value(QStringLiteral("label"))
                    == original.at(index).toObject().value(QStringLiteral("label"))
                    && promoted_toc.at(index).toObject().value(QStringLiteral("href"))
                        == original.at(index).toObject().value(QStringLiteral("href")),
                "TOC hierarchy commit changed a label, target, or preorder position");
    }

    MemoryBookWorkspace other_book = hierarchyBook();
    ToolRegistry other_registry;
    registerTocTools(&other_registry, &other_book);
    const ToolResult other_inspect = run(
        other_registry, QStringLiteral("toc.inspect_hierarchy"));
    Require(other_inspect.ok
                && other_inspect.data.value(QStringLiteral("snapshot_id")).toString()
                    != snapshot_id,
            "identical books in different Agent sessions need distinct TOC snapshots");
    const ToolResult other_plan = run(
        other_registry, QStringLiteral("toc.plan_transform"),
        QJsonObject {
            { QStringLiteral("snapshot_id"),
              other_inspect.data.value(QStringLiteral("snapshot_id")) },
            { QStringLiteral("operation"), QStringLiteral("promote") },
            { QStringLiteral("node_ids"), QJsonArray { 3 } }
        });
    Require(other_plan.ok
                && other_plan.data.value(QStringLiteral("plan_id"))
                    != promote.data.value(QStringLiteral("plan_id")),
            "identical books in different Agent sessions need distinct TOC plan IDs");
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
    const ToolResult cross_session_apply = run(
        other_registry, QStringLiteral("toc.apply_transform"), planBinding(promote));
    Require(!cross_session_apply.ok
                && cross_session_apply.code == QStringLiteral("TOC_PLAN_BINDING_MISMATCH")
                && !other_book.hasOpenTransaction(),
            "a TOC plan from another Agent session must not be applicable");

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

    MemoryBookWorkspace stale_source_book = hierarchyBook();
    ToolRegistry stale_source_registry;
    registerTocTools(&stale_source_registry, &stale_source_book);
    const ToolResult stale_source_plan = inspectAndPlan(
        stale_source_registry, QStringLiteral("promote"), QJsonArray { 3 });
    QJsonArray host_toc = stale_source_book.toc();
    QJsonObject host_entry = host_toc.at(1).toObject();
    host_entry.insert(QStringLiteral("label"), QStringLiteral("Host renamed B"));
    host_toc.replace(1, host_entry);
    stale_source_book.setToc(host_toc);
    const ToolResult stale_source_apply = run(
        stale_source_registry, QStringLiteral("toc.apply_transform"),
        planBinding(stale_source_plan));
    Require(!stale_source_apply.ok
                && stale_source_apply.code == QStringLiteral("TOC_PLAN_STALE")
                && !stale_source_book.hasOpenTransaction()
                && stale_source_book.toc() == host_toc,
            "a source-only TOC change must invalidate the plan before staging");

    MemoryBookWorkspace staged_conflict_book = hierarchyBook();
    ToolRegistry staged_conflict_registry;
    registerTocTools(&staged_conflict_registry, &staged_conflict_book);
    const ToolResult staged_conflict_plan = inspectAndPlan(
        staged_conflict_registry, QStringLiteral("promote"), QJsonArray { 3 });
    Require(run(staged_conflict_registry, QStringLiteral("toc.apply_transform"),
                planBinding(staged_conflict_plan)).ok,
            "could not stage TOC plan for commit-conflict test");
    QJsonArray staged_host_toc = staged_conflict_book.toc();
    staged_host_toc.append(entry(
        QStringLiteral("Host appendix"), QStringLiteral("Text/host.xhtml"), 1));
    staged_conflict_book.setToc(staged_host_toc);
    const SigilAgent::BookOpResult commit_conflict =
        staged_conflict_book.commitTransaction(staged_conflict_book.revision());
    Require(!commit_conflict.ok
                && commit_conflict.code == QStringLiteral("BOOK_REVISION_CONFLICT")
                && staged_conflict_book.toc() == staged_host_toc
                && staged_conflict_book.hasOpenTransaction(),
            "commit must reject a host TOC edit made after staging");
    Require(staged_conflict_book.rollbackTransaction().ok
                && !staged_conflict_book.hasOpenTransaction(),
            "stale staged TOC transaction did not roll back");

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

    AgentCancellation apply_cancellation;
    MemoryBookWorkspace cancelled_apply_book = hierarchyBook();
    ToolRegistry cancelled_apply_registry;
    registerTocTools(
        &cancelled_apply_registry, &cancelled_apply_book, &apply_cancellation);
    const ToolResult cancelled_apply_plan = inspectAndPlan(
        cancelled_apply_registry, QStringLiteral("promote"), QJsonArray { 3 });
    apply_cancellation.request();
    const ToolResult cancelled_apply = run(
        cancelled_apply_registry, QStringLiteral("toc.apply_transform"),
        planBinding(cancelled_apply_plan));
    Require(!cancelled_apply.ok
                && cancelled_apply.code == QStringLiteral("CANCELLED")
                && !cancelled_apply_book.hasOpenTransaction(),
            "cancelled TOC apply must not leave a transaction open");

    FailHierarchyStageWorkspace failing_book;
    failing_book.setToc(hierarchyBook().toc());
    ToolRegistry failing_registry;
    registerTocTools(&failing_registry, &failing_book);
    const ToolResult failing_plan = inspectAndPlan(
        failing_registry, QStringLiteral("promote"), QJsonArray { 3 });
    const ToolResult failed_stage = run(
        failing_registry, QStringLiteral("toc.apply_transform"),
        planBinding(failing_plan));
    Require(!failed_stage.ok
                && failed_stage.code == QStringLiteral("STAGING_ROLLED_BACK")
                && failed_stage.data.value(QStringLiteral("original_code")).toString()
                    == QStringLiteral("INJECTED_STAGE_FAILURE")
                && !failing_book.hasOpenTransaction(),
            "failed TOC staging must roll back the exclusive transaction");

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
