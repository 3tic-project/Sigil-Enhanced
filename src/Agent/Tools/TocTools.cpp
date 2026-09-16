/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/Tools/TocTools.h"

#include <functional>
#include <limits>
#include <memory>
#include <optional>

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QUuid>

#include "Agent/Core/AgentCancellation.h"
#include "Agent/Execution/IBookWorkspace.h"
#include "Agent/Tools/IAgentTool.h"
#include "Agent/Tools/ToolRegistry.h"
#include "BookManipulation/TocTreeTransform.h"

namespace SigilAgent
{

namespace
{

constexpr int kDefaultPageSize = 100;
constexpr int kMaxPageSize = 500;
constexpr int kMaxReportedChanges = 128;

class LambdaTool final : public IAgentTool
{
public:
    using Function = std::function<ToolResult(const QJsonObject &)>;

    LambdaTool(AgentToolDescriptor descriptor, Function function) :
        m_descriptor(std::move(descriptor)),
        m_function(std::move(function))
    {
    }

    AgentToolDescriptor descriptor() const override { return m_descriptor; }
    ToolResult execute(const QJsonObject &arguments) override { return m_function(arguments); }

private:
    AgentToolDescriptor m_descriptor;
    Function m_function;
};

struct StoredSnapshot {
    QString id;
    QString sourceIdentity;
    quint64 bookRevision = 0;
    TocEditTree tree;
};

struct StoredPlan {
    QString id;
    QString digest;
    QString snapshotId;
    QString operation;
    QString sourceIdentity;
    quint64 bookRevision = 0;
    TocEditTree before;
    TocEditTree after;
    TocTransformResult transform;
};

void addFramed(QCryptographicHash &hash, const QString &value)
{
    const QByteArray bytes = value.toUtf8();
    hash.addData(QByteArray::number(bytes.size()));
    hash.addData(QByteArrayLiteral(":"));
    hash.addData(bytes);
}

QString treeDigest(const QString &domain,
                   const QString &source_identity,
                   quint64 book_revision,
                   const TocEditTree &tree)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    addFramed(hash, domain);
    addFramed(hash, source_identity);
    addFramed(hash, QString::number(book_revision));
    const QList<TocNodeId> preorder = TocTreeTransform::PreorderIds(tree);
    for (TocNodeId id : preorder) {
        const TocEditNode node = tree.nodes.value(id);
        addFramed(hash, QString::number(node.id));
        addFramed(hash, QString::number(node.parentId));
        addFramed(hash, node.label);
        addFramed(hash, node.target);
        QStringList children;
        for (TocNodeId child : node.children) {
            children.append(QString::number(child));
        }
        addFramed(hash, children.join(QLatin1Char(',')));
    }
    return QString::fromLatin1(hash.result().toHex());
}

QHash<TocNodeId, int> depths(const TocEditTree &tree)
{
    QHash<TocNodeId, int> result;
    result.insert(tree.rootId, 0);
    QList<TocNodeId> pending = tree.nodes.value(tree.rootId).children;
    while (!pending.isEmpty()) {
        const TocNodeId id = pending.takeFirst();
        const TocEditNode node = tree.nodes.value(id);
        result.insert(id, result.value(node.parentId) + 1);
        for (TocNodeId child : node.children) {
            pending.append(child);
        }
    }
    return result;
}

QJsonObject nodeJson(const TocEditTree &tree,
                     const QHash<TocNodeId, int> &node_depths,
                     TocNodeId id)
{
    const TocEditNode node = tree.nodes.value(id);
    return QJsonObject {
        { QStringLiteral("node_id"), static_cast<qint64>(node.id) },
        { QStringLiteral("parent_id"), static_cast<qint64>(node.parentId) },
        { QStringLiteral("depth"), node_depths.value(id) },
        { QStringLiteral("label"), node.label },
        { QStringLiteral("target"), node.target },
        { QStringLiteral("child_count"), node.children.size() }
    };
}

QString transformErrorName(TocTransformError error)
{
    switch (error) {
    case TocTransformError::None: return QStringLiteral("none");
    case TocTransformError::InvalidTree: return QStringLiteral("invalid_tree");
    case TocTransformError::UnknownSelection: return QStringLiteral("unknown_selection");
    case TocTransformError::RootSelected: return QStringLiteral("root_selected");
    case TocTransformError::AlreadyTopLevel: return QStringLiteral("already_top_level");
    case TocTransformError::NoPreviousSibling: return QStringLiteral("no_previous_sibling");
    case TocTransformError::OverlappingPlans: return QStringLiteral("overlapping_plans");
    }
    return QStringLiteral("invalid_tree");
}

QJsonArray idsJson(const QList<TocNodeId> &ids)
{
    QJsonArray array;
    for (TocNodeId id : ids) {
        array.append(static_cast<qint64>(id));
    }
    return array;
}

QJsonObject epubcheckNotRun()
{
    return QJsonObject {
        { QStringLiteral("status"), QStringLiteral("not_run") },
        { QStringLiteral("message"),
          QStringLiteral("Full EPUBCheck was not run; only the native TOC tree invariants were evaluated.") }
    };
}

class TocToolService
{
public:
    TocToolService(IBookWorkspace *workspace, AgentCancellation *cancellation) :
        m_workspace(workspace),
        m_cancellation(cancellation)
    {
    }

    ToolResult inspect(const QJsonObject &arguments)
    {
        if (m_workspace->hasOpenTransaction()) {
            return ToolResult::failure(
                QStringLiteral("TRANSACTION_OPEN"),
                QStringLiteral("Preview, commit, or roll back the open transaction before inspecting TOC hierarchy"));
        }
        const QJsonValue offset_value = arguments.value(QStringLiteral("offset"));
        const QJsonValue limit_value = arguments.value(QStringLiteral("limit"));
        if ((!offset_value.isUndefined() && !offset_value.isDouble())
            || (!limit_value.isUndefined() && !limit_value.isDouble())) {
            return ToolResult::failure(
                QStringLiteral("INVALID_ARGUMENT"),
                QStringLiteral("offset and limit must be integers"));
        }
        const qint64 offset_integer = offset_value.isUndefined()
            ? 0 : offset_value.toInteger(-1);
        const qint64 limit_integer = limit_value.isUndefined()
            ? kDefaultPageSize : limit_value.toInteger(-1);
        if (offset_integer > std::numeric_limits<int>::max()
            || limit_integer > std::numeric_limits<int>::max()) {
            return ToolResult::failure(
                QStringLiteral("INVALID_ARGUMENT"),
                QStringLiteral("offset or limit is too large"));
        }
        const int requested_offset = static_cast<int>(offset_integer);
        const int limit = static_cast<int>(limit_integer);
        if (requested_offset < 0 || limit < 1 || limit > kMaxPageSize) {
            return ToolResult::failure(
                QStringLiteral("INVALID_ARGUMENT"),
                QStringLiteral("offset must be non-negative and limit must be between 1 and %1")
                    .arg(kMaxPageSize));
        }
        if (m_cancellation && m_cancellation->isCancelled()) {
            return ToolResult::cancelled();
        }
        const QString source_identity_before = m_workspace->tocHierarchyIdentity();
        const TocEditTree tree = m_workspace->tocHierarchy();
        const QString source_identity_after = m_workspace->tocHierarchyIdentity();
        if (m_cancellation && m_cancellation->isCancelled()) {
            return ToolResult::cancelled();
        }
        if (source_identity_before.isEmpty()
            && source_identity_after.isEmpty()) {
            return ToolResult::failure(
                QStringLiteral("TOC_SOURCE_UNAVAILABLE"),
                QStringLiteral("The writable EPUB 3 Nav or EPUB 2 NCX source is unavailable"));
        }
        if (source_identity_before != source_identity_after) {
            return ToolResult::failure(
                QStringLiteral("TOC_SOURCE_CHANGED"),
                QStringLiteral("The native Nav/NCX source changed while it was being inspected"));
        }
        QString validation_error;
        if (!TocTreeTransform::Validate(tree, &validation_error)) {
            return ToolResult::failure(
                QStringLiteral("TOC_INVALID"), validation_error);
        }
        if (tree.nodes.value(tree.rootId).children.isEmpty()) {
            return ToolResult::failure(
                QStringLiteral("TOC_EMPTY"),
                QStringLiteral("The current book has no table of contents entries"));
        }
        StoredSnapshot stored;
        stored.bookRevision = m_workspace->revision();
        stored.sourceIdentity = source_identity_after;
        stored.tree = tree;
        stored.id = treeDigest(
            QStringLiteral("sigil-agent-toc-snapshot-v1:") + m_sessionNonce,
            stored.sourceIdentity,
            stored.bookRevision, stored.tree);
        m_snapshot = stored;
        m_plan.reset();

        const QList<TocNodeId> preorder = TocTreeTransform::PreorderIds(stored.tree);
        const QHash<TocNodeId, int> node_depths = depths(stored.tree);
        const int offset = qMin(requested_offset, preorder.size());
        QJsonArray nodes;
        const int end = qMin(preorder.size(), offset + limit);
        for (int index = offset; index < end; ++index) {
            nodes.append(nodeJson(stored.tree, node_depths, preorder.at(index)));
        }
        QJsonObject data {
            { QStringLiteral("snapshot_id"), stored.id },
            { QStringLiteral("book_revision"), static_cast<qint64>(stored.bookRevision) },
            { QStringLiteral("node_count"), preorder.size() },
            { QStringLiteral("total_count"), preorder.size() },
            { QStringLiteral("offset"), offset },
            { QStringLiteral("limit"), limit },
            { QStringLiteral("returned_count"), nodes.size() },
            { QStringLiteral("has_more"), end < preorder.size() },
            { QStringLiteral("nodes"), nodes },
            { QStringLiteral("preorder_stable"), true },
            { QStringLiteral("full_epubcheck"), epubcheckNotRun() },
            { QStringLiteral("applied_to_book"), false }
        };
        if (end < preorder.size()) data.insert(QStringLiteral("next_offset"), end);
        return ToolResult::success(data, false, true);
    }

    ToolResult planTransform(const QJsonObject &arguments)
    {
        if (!m_snapshot) {
            return ToolResult::failure(
                QStringLiteral("TOC_SNAPSHOT_NOT_FOUND"),
                QStringLiteral("Run toc.inspect_hierarchy before planning a TOC transform"));
        }
        if (arguments.value(QStringLiteral("snapshot_id")).toString() != m_snapshot->id) {
            return ToolResult::failure(
                QStringLiteral("TOC_SNAPSHOT_BINDING_MISMATCH"),
                QStringLiteral("The TOC snapshot ID is not current for this book session"));
        }
        if (m_workspace->hasOpenTransaction()) {
            return ToolResult::failure(
                QStringLiteral("TRANSACTION_OPEN"),
                QStringLiteral("Preview, commit, or roll back the open transaction before planning a TOC transform"));
        }
        if (m_cancellation && m_cancellation->isCancelled()) return ToolResult::cancelled();
        if (m_workspace->revision() != m_snapshot->bookRevision) {
            return ToolResult::failure(
                QStringLiteral("TOC_SNAPSHOT_STALE"),
                QStringLiteral("The book revision changed after TOC inspection"));
        }
        const QString current_identity_before = m_workspace->tocHierarchyIdentity();
        const TocEditTree current = m_workspace->tocHierarchy();
        const QString current_identity_after = m_workspace->tocHierarchyIdentity();
        if (m_cancellation && m_cancellation->isCancelled()) {
            return ToolResult::cancelled();
        }
        if (current_identity_before != m_snapshot->sourceIdentity
            || current_identity_after != m_snapshot->sourceIdentity
            || !TocTreeTransform::Equal(current, m_snapshot->tree)) {
            return ToolResult::failure(
                QStringLiteral("TOC_SNAPSHOT_STALE"),
                QStringLiteral("The TOC source or hierarchy changed after inspection"));
        }

        const QString operation = arguments.value(QStringLiteral("operation")).toString();
        if (operation != QLatin1String("promote") && operation != QLatin1String("demote")) {
            return ToolResult::failure(
                QStringLiteral("INVALID_ARGUMENT"),
                QStringLiteral("operation must be promote or demote"));
        }
        const QJsonValue selected_value = arguments.value(QStringLiteral("node_ids"));
        if (!selected_value.isArray() || selected_value.toArray().isEmpty()) {
            return ToolResult::failure(
                QStringLiteral("INVALID_ARGUMENT"),
                QStringLiteral("node_ids must be a non-empty array of positive integers"));
        }
        QList<TocNodeId> selected;
        QSet<TocNodeId> seen;
        for (const QJsonValue &value : selected_value.toArray()) {
            const qint64 signed_id = value.toInteger(-1);
            if (!value.isDouble() || signed_id <= 0 || seen.contains(static_cast<TocNodeId>(signed_id))) {
                return ToolResult::failure(
                    QStringLiteral("INVALID_ARGUMENT"),
                    QStringLiteral("node_ids must contain unique positive integers"));
            }
            seen.insert(static_cast<TocNodeId>(signed_id));
            selected.append(static_cast<TocNodeId>(signed_id));
        }

        const QJsonValue adopt_value = arguments.value(
            QStringLiteral("adopt_following_siblings"));
        if (!adopt_value.isUndefined() && !adopt_value.isBool()) {
            return ToolResult::failure(
                QStringLiteral("INVALID_ARGUMENT"),
                QStringLiteral("adopt_following_siblings must be a boolean"));
        }
        const bool adopt = adopt_value.isUndefined() ? true : adopt_value.toBool();
        const TocTransformResult transform = operation == QLatin1String("promote")
            ? TocTreeTransform::Promote(m_snapshot->tree, selected, adopt)
            : TocTreeTransform::Demote(m_snapshot->tree, selected);
        if (!transform.succeeded()) {
            return ToolResult::failure(
                QStringLiteral("TOC_TRANSFORM_REJECTED"),
                QStringLiteral("The native TOC transform rejected this selection"),
                QJsonObject {
                    { QStringLiteral("reason"), transformErrorName(transform.error) },
                    { QStringLiteral("error_node_id"), static_cast<qint64>(transform.errorNodeId) }
                });
        }
        if (!transform.preorderPreserved) {
            return ToolResult::failure(
                QStringLiteral("TOC_PREORDER_CHANGED"),
                QStringLiteral("The requested transform did not preserve preorder"));
        }

        StoredPlan stored;
        stored.snapshotId = m_snapshot->id;
        stored.operation = operation;
        stored.sourceIdentity = m_snapshot->sourceIdentity;
        stored.bookRevision = m_snapshot->bookRevision;
        stored.before = m_snapshot->tree;
        stored.after = transform.tree;
        stored.transform = transform;
        stored.id = treeDigest(
            QStringLiteral("sigil-agent-toc-plan-v1:") + m_sessionNonce
                + QLatin1Char(':') + stored.snapshotId + QLatin1Char(':') + operation,
            stored.sourceIdentity,
            stored.bookRevision, stored.after);
        QCryptographicHash digest(QCryptographicHash::Sha256);
        addFramed(digest, QStringLiteral("sigil-agent-toc-plan-digest-v1"));
        addFramed(digest, stored.snapshotId);
        addFramed(digest, stored.id);
        addFramed(digest, QString::number(stored.bookRevision));
        addFramed(digest, operation);
        addFramed(digest, adopt ? QStringLiteral("adopt") : QStringLiteral("no-adopt"));
        stored.digest = QString::fromLatin1(digest.result().toHex());
        m_plan = stored;

        const QHash<TocNodeId, int> before_depths = depths(stored.before);
        const QHash<TocNodeId, int> after_depths = depths(stored.after);
        QJsonArray changes;
        const int change_count = stored.transform.reparentedIds.size();
        const int report_count = qMin(change_count, kMaxReportedChanges);
        for (int index = 0; index < report_count; ++index) {
            const TocNodeId id = stored.transform.reparentedIds.at(index);
            const TocEditNode before = stored.before.nodes.value(id);
            const TocEditNode after = stored.after.nodes.value(id);
            changes.append(QJsonObject {
                { QStringLiteral("node_id"), static_cast<qint64>(id) },
                { QStringLiteral("label"), before.label },
                { QStringLiteral("target"), before.target },
                { QStringLiteral("from_parent_id"), static_cast<qint64>(before.parentId) },
                { QStringLiteral("to_parent_id"), static_cast<qint64>(after.parentId) },
                { QStringLiteral("from_depth"), before_depths.value(id) },
                { QStringLiteral("to_depth"), after_depths.value(id) }
            });
        }
        return ToolResult::success(QJsonObject {
            { QStringLiteral("plan_id"), stored.id },
            { QStringLiteral("plan_digest"), stored.digest },
            { QStringLiteral("snapshot_id"), stored.snapshotId },
            { QStringLiteral("book_revision"), static_cast<qint64>(stored.bookRevision) },
            { QStringLiteral("operation"), stored.operation },
            { QStringLiteral("normalized_selection"), idsJson(transform.normalizedSelection) },
            { QStringLiteral("preorder_preserved"), transform.preorderPreserved },
            { QStringLiteral("adopted_count"), transform.adoptedCount },
            { QStringLiteral("affected_count"), change_count },
            { QStringLiteral("changes"), changes },
            { QStringLiteral("changes_truncated"), change_count > report_count },
            { QStringLiteral("changes_navigation"), true },
            { QStringLiteral("changes_xhtml_headings"), false },
            { QStringLiteral("applied_to_book"), false },
            { QStringLiteral("local_validation"), QStringLiteral("passed") },
            { QStringLiteral("full_epubcheck"), epubcheckNotRun() }
        }, false, true);
    }

    ToolResult applyTransform(const QJsonObject &arguments)
    {
        if (!m_plan) {
            return ToolResult::failure(
                QStringLiteral("TOC_PLAN_NOT_FOUND"),
                QStringLiteral("Create a TOC transform plan in this book session before applying it"));
        }
        const QJsonValue plan_id_value = arguments.value(QStringLiteral("plan_id"));
        const QJsonValue digest_value = arguments.value(QStringLiteral("plan_digest"));
        const QJsonValue revision_value = arguments.value(
            QStringLiteral("expected_book_revision"));
        const qint64 signed_revision = revision_value.toInteger(-1);
        if (!plan_id_value.isString() || !digest_value.isString()
            || !revision_value.isDouble() || signed_revision < 0) {
            return ToolResult::failure(
                QStringLiteral("INVALID_ARGUMENT"),
                QStringLiteral("plan_id and plan_digest must be strings and expected_book_revision must be a non-negative integer"));
        }
        const quint64 expected_revision = static_cast<quint64>(signed_revision);
        if (plan_id_value.toString() != m_plan->id
            || digest_value.toString() != m_plan->digest
            || expected_revision != m_plan->bookRevision) {
            return ToolResult::failure(
                QStringLiteral("TOC_PLAN_BINDING_MISMATCH"),
                QStringLiteral("plan_id, plan_digest, and expected_book_revision must match the reviewed TOC plan"));
        }
        if (m_workspace->hasOpenTransaction()) {
            return ToolResult::failure(
                QStringLiteral("TRANSACTION_OPEN"),
                QStringLiteral("The reviewed TOC plan requires an exclusive new transaction"));
        }
        if (m_cancellation && m_cancellation->isCancelled()) {
            return ToolResult::cancelled();
        }
        if (m_workspace->revision() != m_plan->bookRevision) {
            return ToolResult::failure(
                QStringLiteral("BOOK_REVISION_CONFLICT"),
                QStringLiteral("The book revision changed after TOC planning"));
        }
        const QString current_identity_before = m_workspace->tocHierarchyIdentity();
        const TocEditTree current = m_workspace->tocHierarchy();
        const QString current_identity_after = m_workspace->tocHierarchyIdentity();
        if (m_cancellation && m_cancellation->isCancelled()) {
            return ToolResult::cancelled();
        }
        if (current_identity_before != m_plan->sourceIdentity
            || current_identity_after != m_plan->sourceIdentity
            || !TocTreeTransform::Equal(current, m_plan->before)) {
            return ToolResult::failure(
                QStringLiteral("TOC_PLAN_STALE"),
                QStringLiteral("The native TOC source or hierarchy changed after planning"));
        }

        const BookOpResult begun = m_workspace->beginTransaction(
            QStringLiteral("Apply TOC hierarchy plan (%1)").arg(m_plan->id.left(12)));
        if (!begun.ok) {
            return ToolResult::failure(begun.code, begun.message, begun.data);
        }
        if (m_cancellation && m_cancellation->isCancelled()) {
            return rollbackFailure(ToolResult::cancelled());
        }
        const BookOpResult staged = m_workspace->updateTocHierarchy(
            m_plan->before, m_plan->after);
        if (!staged.ok) {
            return rollbackFailure(ToolResult::failure(
                staged.code, staged.message, staged.data));
        }
        if (m_cancellation && m_cancellation->isCancelled()) {
            return rollbackFailure(ToolResult::cancelled());
        }

        QJsonObject data = begun.data;
        data.insert(QStringLiteral("plan_id"), m_plan->id);
        data.insert(QStringLiteral("plan_digest"), m_plan->digest);
        data.insert(QStringLiteral("book_revision"),
                    static_cast<qint64>(m_plan->bookRevision));
        data.insert(QStringLiteral("staged_nodes"),
                    m_plan->transform.reparentedIds.size());
        data.insert(QStringLiteral("staged_entries"), m_plan->after.nodes.size() - 1);
        data.insert(QStringLiteral("preorder_preserved"), true);
        data.insert(QStringLiteral("changes_navigation"), true);
        data.insert(QStringLiteral("changes_xhtml_headings"), false);
        data.insert(QStringLiteral("requires_transaction_preview"), true);
        data.insert(QStringLiteral("requires_transaction_commit"), true);
        data.insert(QStringLiteral("applied_to_book"), false);
        data.insert(QStringLiteral("save_status"), QStringLiteral("not_applied"));
        data.insert(QStringLiteral("local_validation"), QStringLiteral("passed"));
        data.insert(QStringLiteral("full_epubcheck"), epubcheckNotRun());
        return ToolResult::success(data, false, true);
    }

private:
    ToolResult rollbackFailure(const ToolResult &failure)
    {
        const BookOpResult rolled_back = m_workspace->rollbackTransaction();
        if (!rolled_back.ok) {
            return ToolResult::failure(
                QStringLiteral("STAGING_ROLLBACK_FAILED"),
                QStringLiteral("TOC staging failed and the transaction could not be rolled back: %1")
                    .arg(rolled_back.message),
                QJsonObject {
                    { QStringLiteral("original_code"), failure.code },
                    { QStringLiteral("original_message"), failure.message }
                });
        }
        if (failure.code == QLatin1String("CANCELLED")) return failure;
        return ToolResult::failure(
            QStringLiteral("STAGING_ROLLED_BACK"),
            QStringLiteral("TOC staging failed; the exclusive transaction was rolled back: %1")
                .arg(failure.message),
            QJsonObject { { QStringLiteral("original_code"), failure.code } });
    }

    IBookWorkspace *m_workspace = nullptr;
    AgentCancellation *m_cancellation = nullptr;
    const QString m_sessionNonce = QUuid::createUuid().toString(QUuid::WithoutBraces);
    std::optional<StoredSnapshot> m_snapshot;
    std::optional<StoredPlan> m_plan;
};

void addTool(ToolRegistry *registry,
             const QString &name,
             const QString &description,
             ToolRisk risk,
             bool mutates,
             const QJsonObject &schema,
             LambdaTool::Function function)
{
    AgentToolDescriptor descriptor;
    descriptor.name = name;
    descriptor.description = description;
    descriptor.risk = risk;
    descriptor.mutatesBook = mutates;
    descriptor.supportsPreview = true;
    descriptor.inputSchema = schema;
    registry->add(std::make_unique<LambdaTool>(descriptor, std::move(function)));
}

} // namespace

void registerTocTools(ToolRegistry *registry,
                      IBookWorkspace *workspace,
                      AgentCancellation *cancellation)
{
    if (!registry || !workspace) return;
    auto service = std::make_shared<TocToolService>(workspace, cancellation);

    addTool(
        registry, QStringLiteral("toc.inspect_hierarchy"),
        QStringLiteral("Inspect a bounded page of the current native Nav/NCX hierarchy with stable node IDs. Use next_offset while has_more is true. Read-only and session-bound; never changes headings or the Book."),
        ToolRisk::Read, false,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("offset"), QJsonObject { { QStringLiteral("type"), QStringLiteral("integer") }, { QStringLiteral("minimum"), 0 } } },
                { QStringLiteral("limit"), QJsonObject { { QStringLiteral("type"), QStringLiteral("integer") }, { QStringLiteral("minimum"), 1 }, { QStringLiteral("maximum"), kMaxPageSize }, { QStringLiteral("default"), kDefaultPageSize } } }
            } }
        },
        [service](const QJsonObject &arguments) { return service->inspect(arguments); });

    addTool(
        registry, QStringLiteral("toc.plan_transform"),
        QStringLiteral("Plan a native promote or demote operation for stable TOC node IDs. Preserves preorder and returns bounded parent/depth changes. Read-only: no Nav, NCX, or XHTML is changed."),
        ToolRisk::Read, false,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("snapshot_id"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("operation"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") }, { QStringLiteral("enum"), QJsonArray { QStringLiteral("promote"), QStringLiteral("demote") } } } },
                { QStringLiteral("node_ids"), QJsonObject { { QStringLiteral("type"), QStringLiteral("array") }, { QStringLiteral("items"), QJsonObject { { QStringLiteral("type"), QStringLiteral("integer") }, { QStringLiteral("minimum"), 1 } } }, { QStringLiteral("minItems"), 1 }, { QStringLiteral("uniqueItems"), true } } },
                { QStringLiteral("adopt_following_siblings"), QJsonObject { { QStringLiteral("type"), QStringLiteral("boolean") } } }
            } },
            { QStringLiteral("required"), QJsonArray {
                QStringLiteral("snapshot_id"), QStringLiteral("operation"), QStringLiteral("node_ids")
            } }
        },
        [service](const QJsonObject &arguments) { return service->planTransform(arguments); });

    addTool(
        registry, QStringLiteral("toc.apply_transform"),
        QStringLiteral("Revalidate and stage exactly the reviewed native TOC hierarchy plan in an exclusive transaction. Preserves labels, targets, source attributes, inline markup, and preorder. The live Book remains unchanged until transaction.commit."),
        ToolRisk::Bulk, true,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("plan_id"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("plan_digest"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("expected_book_revision"), QJsonObject { { QStringLiteral("type"), QStringLiteral("integer") }, { QStringLiteral("minimum"), 0 } } }
            } },
            { QStringLiteral("required"), QJsonArray {
                QStringLiteral("plan_id"),
                QStringLiteral("plan_digest"),
                QStringLiteral("expected_book_revision")
            } }
        },
        [service](const QJsonObject &arguments) { return service->applyTransform(arguments); });
}

} // namespace SigilAgent
