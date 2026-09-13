#include <cstdlib>
#include <iostream>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "Agent/Core/AgentCancellation.h"
#include "Agent/Execution/MemoryBookWorkspace.h"
#include "Agent/Security/PermissionPolicy.h"
#include "Agent/Tools/DivParagraphTools.h"
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

QString paragraphLeaves(int count)
{
    QString result;
    for (int index = 0; index < count; ++index) {
        result += QStringLiteral(
            "    <div class='para' data-order=\"%1\">　正文%1 "
            "<ruby>字<rp>(</rp><rt>じ</rt><rp>)</rp></ruby>。</div>\n")
                      .arg(index);
    }
    return result;
}

QString chapterSource(const QString &stylesheet)
{
    return QStringLiteral(
        "<!DOCTYPE html><html xmlns=\"http://www.w3.org/1999/xhtml\" class=\"vrtl\">"
        "<head><title>章</title><link rel=\"stylesheet\" href=\"../Styles/%1\"/></head>"
        "<body><div class=\"main\">\n"
        "  <div class=\"content\">\n"
        "    <div class=\"chapter-title\" data-keep=\"yes\">"
        "<!--keep--><a id=\"chapter-1\"></a><h1>第一章</h1></div>\n"
        "    <div class=\"spacer\"><br /></div>\n"
        "%2"
        "  </div>\n"
        "</div></body></html>")
        .arg(stylesheet, paragraphLeaves(12));
}

MemoryResource textResource(const QString &id,
                            const QString &path,
                            const QString &text)
{
    MemoryResource resource;
    resource.id = id;
    resource.bookPath = path;
    resource.kind = QStringLiteral("xhtml");
    resource.mediaType = QStringLiteral("application/xhtml+xml");
    resource.text = text;
    resource.revision = 1;
    return resource;
}

MemoryResource cssResource(const QString &id,
                           const QString &path,
                           const QString &text)
{
    MemoryResource resource;
    resource.id = id;
    resource.bookPath = path;
    resource.kind = QStringLiteral("css");
    resource.mediaType = QStringLiteral("text/css");
    resource.text = text;
    resource.revision = 1;
    return resource;
}

MemoryBookWorkspace sampleBook(bool include_risky = true,
                               bool include_second_safe = false)
{
    MemoryBookWorkspace book;
    book.addResource(cssResource(
        QStringLiteral("safe-css"), QStringLiteral("OEBPS/Styles/safe.css"),
        QStringLiteral("div, p { margin: 0; padding: 0; }")));
    book.addResource(textResource(
        QStringLiteral("safe"), QStringLiteral("OEBPS/Text/safe.xhtml"),
        chapterSource(QStringLiteral("safe.css"))));
    if (include_risky) {
        book.addResource(cssResource(
            QStringLiteral("risk-css"), QStringLiteral("OEBPS/Styles/risk.css"),
            QStringLiteral("div, p { margin: 0; } div.para { line-height: 1.8; }")));
        book.addResource(textResource(
            QStringLiteral("risk"), QStringLiteral("OEBPS/Text/risk.xhtml"),
            chapterSource(QStringLiteral("risk.css"))));
    }
    if (include_second_safe) {
        book.addResource(textResource(
            QStringLiteral("safe-2"), QStringLiteral("OEBPS/Text/safe-2.xhtml"),
            chapterSource(QStringLiteral("safe.css"))));
    }
    return book;
}

ToolResult run(ToolRegistry &registry,
               const QString &name,
               const QJsonObject &arguments = QJsonObject())
{
    IAgentTool *tool = registry.find(name);
    Require(tool != nullptr, "native paragraph tool is missing");
    return tool->execute(arguments);
}

struct PlanBinding {
    QString analysisId;
    QString planId;
    QString digest;
    quint64 revision = 0;
};

PlanBinding analyzeAndPlan(ToolRegistry &registry,
                           const QJsonArray &resource_ids = QJsonArray())
{
    QJsonObject analyze_arguments;
    if (!resource_ids.isEmpty()) {
        analyze_arguments.insert(QStringLiteral("resource_ids"), resource_ids);
    }
    const ToolResult analysis = run(
        registry, QStringLiteral("paragraphs.analyze"), analyze_arguments);
    Require(analysis.ok && analysis.previewOnly && !analysis.applied,
            "paragraph analysis must be read-only");
    const QString analysis_id = analysis.data.value(
        QStringLiteral("analysis_id")).toString();
    Require(analysis_id.size() == 64, "analysis ID must be a full digest");

    const ToolResult plan = run(
        registry, QStringLiteral("paragraphs.plan"),
        QJsonObject { { QStringLiteral("analysis_id"), analysis_id } });
    Require(plan.ok && plan.previewOnly && !plan.applied,
            "paragraph plan must be preview-only");
    Require(plan.data.value(QStringLiteral("local_validation")).toString()
                == QStringLiteral("passed"),
            "paragraph plan must report local validation");
    Require(plan.data.value(QStringLiteral("full_epubcheck")).toObject()
                .value(QStringLiteral("status")).toString()
                == QStringLiteral("not_run"),
            "paragraph plan must not claim EPUBCheck ran");
    Require(!plan.data.value(QStringLiteral("changes_opf")).toBool()
                && !plan.data.value(QStringLiteral("changes_css")).toBool()
                && !plan.data.value(QStringLiteral("adds_resources")).toBool(),
            "paragraph plan must identify its resource boundary");

    PlanBinding binding;
    binding.analysisId = analysis_id;
    binding.planId = plan.data.value(QStringLiteral("plan_id")).toString();
    binding.digest = plan.data.value(QStringLiteral("plan_digest")).toString();
    binding.revision = static_cast<quint64>(
        plan.data.value(QStringLiteral("book_revision")).toInteger());
    Require(binding.planId.size() == 64 && binding.digest.size() == 64,
            "plan identity and approval digest must be full hashes");
    return binding;
}

QJsonObject bindingArguments(const PlanBinding &binding)
{
    return QJsonObject {
        { QStringLiteral("plan_id"), binding.planId },
        { QStringLiteral("plan_digest"), binding.digest },
        { QStringLiteral("expected_book_revision"), static_cast<qint64>(binding.revision) }
    };
}

class FailSecondStageWorkspace final : public MemoryBookWorkspace
{
public:
    BookOpResult replaceText(const QString &resource_id,
                             const QString &text,
                             quint64 expected_resource_revision) override
    {
        ++m_stageCount;
        if (m_stageCount == 2) {
            return BookOpResult::error(
                QStringLiteral("INJECTED_STAGE_FAILURE"),
                QStringLiteral("injected second-stage failure"));
        }
        return MemoryBookWorkspace::replaceText(
            resource_id, text, expected_resource_revision);
    }

private:
    int m_stageCount = 0;
};

void addTwoSafeResources(FailSecondStageWorkspace &book)
{
    book.addResource(cssResource(
        QStringLiteral("safe-css"), QStringLiteral("OEBPS/Styles/safe.css"),
        QStringLiteral("div, p { margin: 0; padding: 0; }")));
    book.addResource(textResource(
        QStringLiteral("safe"), QStringLiteral("OEBPS/Text/safe.xhtml"),
        chapterSource(QStringLiteral("safe.css"))));
    book.addResource(textResource(
        QStringLiteral("safe-2"), QStringLiteral("OEBPS/Text/safe-2.xhtml"),
        chapterSource(QStringLiteral("safe.css"))));
}

} // namespace

int main()
{
    MemoryBookWorkspace book = sampleBook();
    ToolRegistry registry;
    registerDivParagraphTools(&registry, &book);

    Require(registry.find(QStringLiteral("paragraphs_analyze"))
                == registry.find(QStringLiteral("paragraphs.analyze")),
            "wire and dotted paragraph tool names must resolve identically");
    Require(registry.find(QStringLiteral("paragraphs.plan"))->descriptor().risk
                == ToolRisk::Read,
            "planning must remain read-only");
    const AgentToolDescriptor apply_descriptor =
        registry.find(QStringLiteral("paragraphs.apply"))->descriptor();
    Require(apply_descriptor.risk == ToolRisk::Bulk
                && apply_descriptor.mutatesBook
                && apply_descriptor.supportsPreview
                && apply_descriptor.inputSchema.value(QStringLiteral("properties"))
                    .toObject().value(QStringLiteral("selected_resource_ids"))
                    .toObject().value(QStringLiteral("uniqueItems")).toBool(),
            "paragraph apply risk and preview metadata are incorrect");
    PermissionPolicy permission_policy;
    Require(permission_policy.evaluate(AgentMode::Ask, apply_descriptor)
                == PermissionAction::Deny,
            "Ask mode must reject paragraph staging");
    Require(permission_policy.evaluate(AgentMode::Plan, apply_descriptor)
                == PermissionAction::Allow,
            "Plan mode must allow reversible paragraph staging");
    Require(permission_policy.evaluate(AgentMode::Edit, apply_descriptor)
                == PermissionAction::Ask,
            "Edit mode must ask before paragraph staging");
    Require(permission_policy.evaluate(AgentMode::Auto, apply_descriptor)
                == PermissionAction::Allow,
            "Auto mode must allow paragraph staging");

    const QString original = book.resourceText(QStringLiteral("safe"));
    const ToolResult analysis = run(
        registry, QStringLiteral("paragraphs.analyze"),
        QJsonObject { { QStringLiteral("resource_ids"), QJsonArray {
            QStringLiteral("safe"), QStringLiteral("risk")
        } } });
    Require(analysis.ok && analysis.data.value(QStringLiteral("summary")).toObject()
                .value(QStringLiteral("ready_files")).toInt() == 1,
            "analysis must find the one auto-safe resource");
    Require(analysis.data.value(QStringLiteral("summary")).toObject()
                .value(QStringLiteral("review_only_files")).toInt() == 1,
            "CSS-risk resource must remain review-only");
    Require(!book.hasOpenTransaction() && book.resourceText(QStringLiteral("safe")) == original,
            "analysis changed the transaction or live book");

    const QJsonArray files = analysis.data.value(QStringLiteral("files")).toArray();
    Require(files.size() == 2, "analysis must return both requested files");
    bool found_safe = false;
    bool found_risk = false;
    for (const QJsonValue &value : files) {
        const QJsonObject file = value.toObject();
        if (file.value(QStringLiteral("resource_id")) == QStringLiteral("safe")) {
            found_safe = file.value(QStringLiteral("status")) == QStringLiteral("apply")
                && file.value(QStringLiteral("body_candidates")).toInt() == 12
                && file.value(QStringLiteral("candidate_ranges")).toArray().size() == 12
                && file.value(QStringLiteral("protected_count")).toInt() > 0;
        } else if (file.value(QStringLiteral("resource_id")) == QStringLiteral("risk")) {
            found_risk = file.value(QStringLiteral("status")) == QStringLiteral("review")
                && !file.value(QStringLiteral("css_dependencies")).toArray().isEmpty();
        }
    }
    Require(found_safe && found_risk,
            "structured analysis omitted candidates, protected ranges, or CSS risk");

    const QString analysis_id = analysis.data.value(QStringLiteral("analysis_id")).toString();
    const ToolResult unsafe_plan = run(
        registry, QStringLiteral("paragraphs.plan"),
        QJsonObject {
            { QStringLiteral("analysis_id"), analysis_id },
            { QStringLiteral("resource_ids"), QJsonArray { QStringLiteral("risk") } }
        });
    Require(!unsafe_plan.ok && unsafe_plan.code == QStringLiteral("PLAN_SELECTION_UNSAFE"),
            "review-only CSS risk must not enter an apply plan");
    const ToolResult invalid_scope = run(
        registry, QStringLiteral("paragraphs.plan"),
        QJsonObject {
            { QStringLiteral("analysis_id"), analysis_id },
            { QStringLiteral("resource_ids"), QJsonArray {
                QStringLiteral("safe"), 7
            } }
        });
    Require(!invalid_scope.ok && invalid_scope.code == QStringLiteral("INVALID_ARGUMENT"),
            "plan scope must reject non-string resource IDs");

    const ToolResult plan = run(
        registry, QStringLiteral("paragraphs.plan"),
        QJsonObject { { QStringLiteral("analysis_id"), analysis_id } });
    Require(plan.ok && plan.data.value(QStringLiteral("summary")).toObject()
                .value(QStringLiteral("conversion_count")).toInt() == 12,
            "safe plan must contain the native conversions");
    const QJsonObject source_diff = plan.data.value(QStringLiteral("changes")).toArray()
        .first().toObject().value(QStringLiteral("source_diff")).toObject();
    Require(source_diff.value(QStringLiteral("before")).toString().contains(QStringLiteral("<div"))
                && source_diff.value(QStringLiteral("after")).toString().contains(QStringLiteral("<p")),
            "review plan must contain a bounded source diff");
    Require(QJsonDocument(plan.data).toJson(QJsonDocument::Compact).size() < 16384,
            "native plan response must remain bounded");

    MemoryBookWorkspace grouped_book = sampleBook(false, true);
    ToolRegistry grouped_registry;
    registerDivParagraphTools(&grouped_registry, &grouped_book);
    const ToolResult grouped_analysis = run(
        grouped_registry, QStringLiteral("paragraphs.analyze"));
    const ToolResult grouped_plan = run(
        grouped_registry, QStringLiteral("paragraphs.plan"),
        QJsonObject {
            { QStringLiteral("analysis_id"),
              grouped_analysis.data.value(QStringLiteral("analysis_id")) }
        });
    const QJsonArray operation_groups = grouped_plan.data.value(
        QStringLiteral("operation_groups")).toArray();
    Require(grouped_plan.ok
                && grouped_plan.data.value(
                    QStringLiteral("operation_groups_independent")).toBool()
                && operation_groups.size() == 2
                && operation_groups.first().toObject()
                    .value(QStringLiteral("independently_applicable")).toBool()
                && operation_groups.first().toObject()
                    .value(QStringLiteral("resource_ids")).toArray().size() == 1,
            "paragraph plan must declare one independently applicable group per XHTML resource");
    PlanBinding grouped_binding;
    grouped_binding.analysisId = grouped_analysis.data.value(
        QStringLiteral("analysis_id")).toString();
    grouped_binding.planId = grouped_plan.data.value(
        QStringLiteral("plan_id")).toString();
    grouped_binding.digest = grouped_plan.data.value(
        QStringLiteral("plan_digest")).toString();
    grouped_binding.revision = static_cast<quint64>(
        grouped_plan.data.value(QStringLiteral("book_revision")).toInteger());
    QJsonObject empty_group_arguments = bindingArguments(grouped_binding);
    empty_group_arguments.insert(
        QStringLiteral("selected_resource_ids"), QJsonArray());
    const ToolResult empty_groups = run(
        grouped_registry, QStringLiteral("paragraphs.apply"),
        empty_group_arguments);
    Require(!empty_groups.ok
                && empty_groups.code == QStringLiteral("PLAN_GROUP_SELECTION_EMPTY")
                && !grouped_book.hasOpenTransaction(),
            "an empty operation-group selection must fail before transaction creation");
    QJsonObject foreign_group_arguments = bindingArguments(grouped_binding);
    foreign_group_arguments.insert(
        QStringLiteral("selected_resource_ids"),
        QJsonArray { QStringLiteral("not-reviewed") });
    const ToolResult foreign_group = run(
        grouped_registry, QStringLiteral("paragraphs.apply"),
        foreign_group_arguments);
    Require(!foreign_group.ok
                && foreign_group.code == QStringLiteral("PLAN_GROUP_NOT_FOUND")
                && !grouped_book.hasOpenTransaction(),
            "an operation group outside the reviewed plan must fail closed");
    const QString first_group_original = grouped_book.resourceText(
        QStringLiteral("safe"));
    QJsonObject selected_group_arguments = bindingArguments(grouped_binding);
    selected_group_arguments.insert(
        QStringLiteral("selected_resource_ids"),
        QJsonArray { QStringLiteral("safe-2") });
    const ToolResult selected_group = run(
        grouped_registry, QStringLiteral("paragraphs.apply"),
        selected_group_arguments);
    Require(selected_group.ok && selected_group.previewOnly
                && selected_group.data.value(
                    QStringLiteral("available_operation_groups")).toInt() == 2
                && selected_group.data.value(
                    QStringLiteral("selected_operation_groups")).toInt() == 1
                && selected_group.data.value(
                    QStringLiteral("selected_resource_ids")).toArray()
                    == QJsonArray { QStringLiteral("safe-2") },
            "paragraph apply must report the exact approved operation-group subset");
    Require(grouped_book.hasOpenTransaction()
                && grouped_book.workingText(QStringLiteral("safe"))
                    == first_group_original
                && grouped_book.workingText(QStringLiteral("safe-2"))
                    .contains(QStringLiteral("<p class='para'"))
                && grouped_book.previewTransaction().data
                    .value(QStringLiteral("changes")).toArray().size() == 1,
            "selected paragraph groups must stage atomically without touching unchecked XHTML");
    Require(grouped_book.rollbackTransaction().ok,
            "selected operation-group test transaction must roll back cleanly");

    PlanBinding binding;
    binding.analysisId = analysis_id;
    binding.planId = plan.data.value(QStringLiteral("plan_id")).toString();
    binding.digest = plan.data.value(QStringLiteral("plan_digest")).toString();
    binding.revision = static_cast<quint64>(
        plan.data.value(QStringLiteral("book_revision")).toInteger());
    QJsonObject wrong_binding = bindingArguments(binding);
    wrong_binding.insert(QStringLiteral("plan_digest"), QStringLiteral("wrong"));
    const ToolResult rejected = run(
        registry, QStringLiteral("paragraphs.apply"), wrong_binding);
    Require(!rejected.ok && rejected.code == QStringLiteral("PLAN_BINDING_MISMATCH")
                && !book.hasOpenTransaction(),
            "unreviewed digest must be rejected before transaction creation");

    const ToolResult staged = run(
        registry, QStringLiteral("paragraphs.apply"), bindingArguments(binding));
    Require(staged.ok && staged.previewOnly && !staged.applied
                && staged.data.value(QStringLiteral("applied_to_book")) == false
                && staged.data.value(QStringLiteral("requires_transaction_commit")).toBool(),
            "paragraph apply must report staged, not live, state");
    Require(book.hasOpenTransaction() && book.resourceText(QStringLiteral("safe")) == original,
            "staging must leave the live XHTML unchanged");
    const QString staged_text = book.workingText(QStringLiteral("safe"));
    Require(staged_text.contains(QStringLiteral("<p class='para'"))
                && staged_text.contains(QStringLiteral("<ruby>字<rp>(</rp><rt>じ</rt>"))
                && staged_text.contains(QStringLiteral("<div class=\"spacer\"><br /></div>"))
                && staged_text.contains(QStringLiteral("<div class=\"chapter-title\" data-keep=\"yes\">")),
            "native staging did not preserve Ruby, optional blank lines, or heading wrappers");
    Require(book.previewTransaction().data.value(QStringLiteral("changes")).toArray().size() == 1,
            "exclusive paragraph transaction preview must show one changed resource");
    Require(book.commitTransaction(binding.revision).applied,
            "reviewed paragraph transaction did not commit");
    Require(book.resourceText(QStringLiteral("safe")) == staged_text,
            "committed XHTML differs from the reviewed staged output");

    const ToolResult second_analysis = run(
        registry, QStringLiteral("paragraphs.analyze"),
        QJsonObject { { QStringLiteral("resource_ids"), QJsonArray { QStringLiteral("safe") } } });
    Require(second_analysis.ok && second_analysis.data.value(QStringLiteral("summary")).toObject()
                .value(QStringLiteral("ready_files")).toInt() == 0,
            "a second native analysis must be idempotent");

    MemoryBookWorkspace other_book = sampleBook(false);
    ToolRegistry other_registry;
    registerDivParagraphTools(&other_registry, &other_book);
    const PlanBinding other_binding = analyzeAndPlan(
        other_registry, QJsonArray { QStringLiteral("safe") });
    Require(other_binding.planId != binding.planId,
            "identical books in different Agent sessions must have different plan IDs");
    const ToolResult cross_session = run(
        other_registry, QStringLiteral("paragraphs.apply"), bindingArguments(binding));
    Require(!cross_session.ok
                && cross_session.code == QStringLiteral("PLAN_BINDING_MISMATCH"),
            "a plan from another book session must not be accepted");

    MemoryBookWorkspace stale_book = sampleBook(false);
    ToolRegistry stale_registry;
    registerDivParagraphTools(&stale_registry, &stale_book);
    const PlanBinding stale_binding = analyzeAndPlan(
        stale_registry, QJsonArray { QStringLiteral("safe") });
    stale_book.addResource(cssResource(
        QStringLiteral("safe-css"), QStringLiteral("OEBPS/Styles/safe.css"),
        QStringLiteral("div, p { margin: 0; } span { color: black; }")));
    const ToolResult stale_css = run(
        stale_registry, QStringLiteral("paragraphs.apply"),
        bindingArguments(stale_binding));
    Require(!stale_css.ok && stale_css.code == QStringLiteral("PLAN_STALE")
                && !stale_book.hasOpenTransaction(),
            "stylesheet changes must invalidate a reviewed plan before staging");

    AgentCancellation cancellation;
    cancellation.request();
    MemoryBookWorkspace cancelled_book = sampleBook(false);
    ToolRegistry cancelled_registry;
    registerDivParagraphTools(&cancelled_registry, &cancelled_book, &cancellation);
    const ToolResult cancelled = run(
        cancelled_registry, QStringLiteral("paragraphs.analyze"));
    Require(!cancelled.ok && cancelled.code == QStringLiteral("CANCELLED")
                && !cancelled_book.hasOpenTransaction(),
            "cancelled analysis must stop without a transaction");

    FailSecondStageWorkspace failing_book;
    addTwoSafeResources(failing_book);
    ToolRegistry failing_registry;
    registerDivParagraphTools(&failing_registry, &failing_book);
    const QString first_before = failing_book.resourceText(QStringLiteral("safe"));
    const QString second_before = failing_book.resourceText(QStringLiteral("safe-2"));
    const PlanBinding failing_binding = analyzeAndPlan(failing_registry);
    const ToolResult failed_stage = run(
        failing_registry, QStringLiteral("paragraphs.apply"),
        bindingArguments(failing_binding));
    Require(!failed_stage.ok && failed_stage.code == QStringLiteral("STAGING_ROLLED_BACK")
                && !failing_book.hasOpenTransaction(),
            "a mid-batch stage failure must discard the exclusive transaction");
    Require(failing_book.resourceText(QStringLiteral("safe")) == first_before
                && failing_book.resourceText(QStringLiteral("safe-2")) == second_before
                && failing_book.workingText(QStringLiteral("safe")) == first_before,
            "failed paragraph staging left a partial live or working change");

    return EXIT_SUCCESS;
}
