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

struct ParsedTree {
    bool ok = false;
    QString code;
    QString message;
    TocEditTree tree;
};

struct StoredSnapshot {
    QString id;
    quint64 bookRevision = 0;
    TocEditTree tree;
};

struct StoredPlan {
    QString id;
    QString digest;
    QString snapshotId;
    QString operation;
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
                   quint64 book_revision,
                   const TocEditTree &tree)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    addFramed(hash, domain);
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

ParsedTree parseWorkspaceToc(const QJsonArray &entries,
                             AgentCancellation *cancellation)
{
    ParsedTree parsed;
    parsed.tree.rootId = 0;
    TocEditNode root;
    root.id = 0;
    root.parentId = 0;
    parsed.tree.nodes.insert(0, root);

    QList<TocNodeId> last_at_level;
    TocNodeId next_id = 1;
    for (int index = 0; index < entries.size(); ++index) {
        if (cancellation && cancellation->isCancelled()) {
            parsed.code = QStringLiteral("CANCELLED");
            parsed.message = QStringLiteral("cancelled");
            return parsed;
        }
        if (!entries.at(index).isObject()) {
            parsed.code = QStringLiteral("TOC_INVALID");
            parsed.message = QStringLiteral("TOC entries must be objects");
            return parsed;
        }
        const QJsonObject object = entries.at(index).toObject();
        const int level = object.value(QStringLiteral("level")).toInt(1);
        if (level < 1 || level > last_at_level.size() + 1) {
            parsed.code = QStringLiteral("TOC_INVALID");
            parsed.message = QStringLiteral("TOC level jumps at entry %1").arg(index);
            return parsed;
        }
        const TocNodeId parent = level == 1 ? parsed.tree.rootId
                                             : last_at_level.at(level - 2);
        TocEditNode node;
        node.id = next_id++;
        node.parentId = parent;
        node.label = object.value(QStringLiteral("label")).toString();
        node.target = object.value(QStringLiteral("href")).toString();
        if (node.target.isEmpty()) {
            node.target = object.value(QStringLiteral("target")).toString();
        }
        if (node.target.isEmpty()) {
            node.target = object.value(QStringLiteral("book_path")).toString();
        }
        parsed.tree.nodes.insert(node.id, node);
        parsed.tree.nodes[parent].children.append(node.id);
        while (last_at_level.size() >= level) {
            last_at_level.removeLast();
        }
        last_at_level.append(node.id);
    }

    if (entries.isEmpty()) {
        parsed.code = QStringLiteral("TOC_EMPTY");
        parsed.message = QStringLiteral("The current book has no table of contents entries");
        return parsed;
    }
    QString error;
    if (!TocTreeTransform::Validate(parsed.tree, &error)) {
        parsed.code = QStringLiteral("TOC_INVALID");
        parsed.message = error;
        return parsed;
    }
    parsed.ok = true;
    return parsed;
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
        const int offset = static_cast<int>(offset_integer);
        const int limit = static_cast<int>(limit_integer);
        if (offset < 0 || limit < 1 || limit > kMaxPageSize) {
            return ToolResult::failure(
                QStringLiteral("INVALID_ARGUMENT"),
                QStringLiteral("offset must be non-negative and limit must be between 1 and %1")
                    .arg(kMaxPageSize));
        }
        const ParsedTree parsed = parseWorkspaceToc(m_workspace->toc(), m_cancellation);
        if (!parsed.ok) {
            if (parsed.code == QLatin1String("CANCELLED")) return ToolResult::cancelled();
            return ToolResult::failure(parsed.code, parsed.message);
        }
        StoredSnapshot stored;
        stored.bookRevision = m_workspace->revision();
        stored.tree = parsed.tree;
        stored.id = treeDigest(
            QStringLiteral("sigil-agent-toc-snapshot-v1:") + m_sessionNonce,
            stored.bookRevision, stored.tree);
        m_snapshot = stored;
        m_plan.reset();

        const QList<TocNodeId> preorder = TocTreeTransform::PreorderIds(stored.tree);
        const QHash<TocNodeId, int> node_depths = depths(stored.tree);
        QJsonArray nodes;
        const int end = qMin(preorder.size(), offset + limit);
        for (int index = offset; index < end; ++index) {
            nodes.append(nodeJson(stored.tree, node_depths, preorder.at(index)));
        }
        QJsonObject data {
            { QStringLiteral("snapshot_id"), stored.id },
            { QStringLiteral("book_revision"), static_cast<qint64>(stored.bookRevision) },
            { QStringLiteral("node_count"), preorder.size() },
            { QStringLiteral("offset"), offset },
            { QStringLiteral("limit"), limit },
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
        const ParsedTree current = parseWorkspaceToc(m_workspace->toc(), m_cancellation);
        if (!current.ok) {
            if (current.code == QLatin1String("CANCELLED")) return ToolResult::cancelled();
            return ToolResult::failure(current.code, current.message);
        }
        if (!TocTreeTransform::Equal(current.tree, m_snapshot->tree)) {
            return ToolResult::failure(
                QStringLiteral("TOC_SNAPSHOT_STALE"),
                QStringLiteral("The TOC hierarchy changed after inspection"));
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
        stored.bookRevision = m_snapshot->bookRevision;
        stored.before = m_snapshot->tree;
        stored.after = transform.tree;
        stored.transform = transform;
        stored.id = treeDigest(
            QStringLiteral("sigil-agent-toc-plan-v1:") + operation,
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

private:
    IBookWorkspace *m_workspace = nullptr;
    AgentCancellation *m_cancellation = nullptr;
    const QString m_sessionNonce = QUuid::createUuid().toString(QUuid::WithoutBraces);
    std::optional<StoredSnapshot> m_snapshot;
    std::optional<StoredPlan> m_plan;
};

void addTool(ToolRegistry *registry,
             const QString &name,
             const QString &description,
             const QJsonObject &schema,
             LambdaTool::Function function)
{
    AgentToolDescriptor descriptor;
    descriptor.name = name;
    descriptor.description = description;
    descriptor.risk = ToolRisk::Read;
    descriptor.mutatesBook = false;
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
        QStringLiteral("Inspect the current native Nav/NCX hierarchy with stable node IDs and bounded pagination. Read-only and session-bound; never changes headings or the Book."),
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("offset"), QJsonObject { { QStringLiteral("type"), QStringLiteral("integer") }, { QStringLiteral("minimum"), 0 } } },
                { QStringLiteral("limit"), QJsonObject { { QStringLiteral("type"), QStringLiteral("integer") }, { QStringLiteral("minimum"), 1 }, { QStringLiteral("maximum"), kMaxPageSize } } }
            } }
        },
        [service](const QJsonObject &arguments) { return service->inspect(arguments); });

    addTool(
        registry, QStringLiteral("toc.plan_transform"),
        QStringLiteral("Plan a native promote or demote operation for stable TOC node IDs. Preserves preorder and returns bounded parent/depth changes. Read-only: no Nav, NCX, or XHTML is changed."),
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
}

} // namespace SigilAgent
