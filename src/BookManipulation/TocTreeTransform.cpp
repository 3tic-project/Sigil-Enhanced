#include "BookManipulation/TocTreeTransform.h"

#include <algorithm>

#include <QSet>

namespace {

struct PromotionGroup
{
    TocNodeId parent = 0;
    TocNodeId grandparent = 0;
    QList<TocNodeId> selected;
    QList<TocNodeId> remaining;
    QHash<TocNodeId, QList<TocNodeId>> adopted;
};

struct DemotionRange
{
    TocNodeId predecessor = 0;
    QList<TocNodeId> selected;
};

struct DemotionGroup
{
    TocNodeId parent = 0;
    QList<TocNodeId> remaining;
    QList<DemotionRange> ranges;
};

TocTransformResult Failure(const TocEditTree &tree, TocTransformError error,
                           TocNodeId id = 0)
{
    TocTransformResult result;
    result.tree = tree;
    result.error = error;
    result.errorNodeId = id;
    return result;
}

bool IsAncestorOrSelf(const TocEditTree &tree, TocNodeId ancestor,
                      TocNodeId descendant)
{
    TocNodeId current = descendant;
    while (tree.nodes.contains(current)) {
        if (current == ancestor) return true;
        if (current == tree.rootId) return false;
        current = tree.nodes.value(current).parentId;
    }
    return false;
}

TocTransformError NormalizeSelection(
    const TocEditTree &tree, const QList<TocNodeId> &selection,
    QList<TocNodeId> &normalized, TocNodeId &errorNode)
{
    QSet<TocNodeId> selected;
    for (TocNodeId id : selection) {
        if (!tree.nodes.contains(id)) {
            errorNode = id;
            return TocTransformError::UnknownSelection;
        }
        if (id == tree.rootId) {
            errorNode = id;
            return TocTransformError::RootSelected;
        }
        selected.insert(id);
    }

    struct PendingNode {
        TocNodeId id;
        bool hasSelectedAncestor;
    };
    QList<PendingNode> pending;
    const QList<TocNodeId> rootChildren = tree.nodes.value(tree.rootId).children;
    for (auto it = rootChildren.crbegin(); it != rootChildren.crend(); ++it) {
        pending.append({*it, false});
    }
    while (!pending.isEmpty()) {
        const PendingNode current = pending.takeLast();
        const bool isSelected = selected.contains(current.id);
        if (isSelected && !current.hasSelectedAncestor) {
            normalized.append(current.id);
        }
        const bool blocked = current.hasSelectedAncestor || isSelected;
        const QList<TocNodeId> children = tree.nodes.value(current.id).children;
        for (auto it = children.crbegin(); it != children.crend(); ++it) {
            pending.append({*it, blocked});
        }
    }
    return TocTransformError::None;
}

bool PromotionGroupsOverlap(const TocEditTree &tree,
                            const PromotionGroup &first,
                            const PromotionGroup &second)
{
    if (first.parent == second.grandparent
        || second.parent == first.grandparent) {
        return true;
    }
    for (const QList<TocNodeId> &adopted : first.adopted) {
        for (TocNodeId root : adopted) {
            if (IsAncestorOrSelf(tree, root, second.parent)
                || IsAncestorOrSelf(tree, root, second.grandparent)) {
                return true;
            }
        }
    }
    for (const QList<TocNodeId> &adopted : second.adopted) {
        for (TocNodeId root : adopted) {
            if (IsAncestorOrSelf(tree, root, first.parent)
                || IsAncestorOrSelf(tree, root, first.grandparent)) {
                return true;
            }
        }
    }
    return false;
}

bool DemotionGroupsOverlap(const TocEditTree &tree,
                           const DemotionGroup &first,
                           const DemotionGroup &second)
{
    for (const DemotionRange &range : first.ranges) {
        if (IsAncestorOrSelf(tree, range.predecessor, second.parent)) {
            return true;
        }
    }
    for (const DemotionRange &range : second.ranges) {
        if (IsAncestorOrSelf(tree, range.predecessor, first.parent)) {
            return true;
        }
    }
    return false;
}

}

bool TocTreeTransform::Validate(const TocEditTree &tree, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error) *error = message;
        return false;
    };
    if (!tree.nodes.contains(tree.rootId)) {
        return fail(QStringLiteral("The invisible root is missing"));
    }
    if (tree.nodes.value(tree.rootId).id != tree.rootId
        || tree.nodes.value(tree.rootId).parentId != tree.rootId) {
        return fail(QStringLiteral("The invisible root has an invalid identity"));
    }

    QSet<TocNodeId> referenced;
    for (auto it = tree.nodes.cbegin(); it != tree.nodes.cend(); ++it) {
        const TocEditNode &node = it.value();
        if (it.key() != node.id) {
            return fail(QStringLiteral("A node key does not match its stable identity"));
        }
        QSet<TocNodeId> localChildren;
        for (TocNodeId childId : node.children) {
            if (childId == tree.rootId || !tree.nodes.contains(childId)) {
                return fail(QStringLiteral("A child identity is invalid"));
            }
            if (localChildren.contains(childId) || referenced.contains(childId)) {
                return fail(QStringLiteral("A node is referenced more than once"));
            }
            if (tree.nodes.value(childId).parentId != node.id) {
                return fail(QStringLiteral("A child and parent disagree"));
            }
            localChildren.insert(childId);
            referenced.insert(childId);
        }
    }
    if (referenced.size() + 1 != tree.nodes.size()) {
        return fail(QStringLiteral("The tree has a disconnected node"));
    }

    QSet<TocNodeId> visited;
    QList<TocNodeId> pending = {tree.rootId};
    while (!pending.isEmpty()) {
        const TocNodeId id = pending.takeLast();
        if (visited.contains(id)) {
            return fail(QStringLiteral("The tree contains a cycle"));
        }
        visited.insert(id);
        const QList<TocNodeId> children = tree.nodes.value(id).children;
        for (auto it = children.crbegin(); it != children.crend(); ++it) {
            pending.append(*it);
        }
    }
    if (visited.size() != tree.nodes.size()) {
        return fail(QStringLiteral("The tree has an unreachable node"));
    }
    if (error) error->clear();
    return true;
}

QList<TocNodeId> TocTreeTransform::PreorderIds(const TocEditTree &tree)
{
    if (!Validate(tree)) return {};
    QList<TocNodeId> preorder;
    QList<TocNodeId> pending;
    const QList<TocNodeId> rootChildren = tree.nodes.value(tree.rootId).children;
    for (auto it = rootChildren.crbegin(); it != rootChildren.crend(); ++it) {
        pending.append(*it);
    }
    while (!pending.isEmpty()) {
        const TocNodeId id = pending.takeLast();
        preorder.append(id);
        const QList<TocNodeId> children = tree.nodes.value(id).children;
        for (auto it = children.crbegin(); it != children.crend(); ++it) {
            pending.append(*it);
        }
    }
    return preorder;
}

TocTransformResult TocTreeTransform::Promote(
    const TocEditTree &tree, const QList<TocNodeId> &selection,
    bool adoptFollowingSiblings)
{
    if (!Validate(tree)) return Failure(tree, TocTransformError::InvalidTree);
    QList<TocNodeId> normalized;
    TocNodeId errorNode = 0;
    const TocTransformError selectionError = NormalizeSelection(
        tree, selection, normalized, errorNode);
    if (selectionError != TocTransformError::None) {
        return Failure(tree, selectionError, errorNode);
    }

    QList<PromotionGroup> groups;
    QHash<TocNodeId, int> groupByParent;
    for (TocNodeId id : normalized) {
        const TocNodeId parent = tree.nodes.value(id).parentId;
        if (parent == tree.rootId) {
            return Failure(tree, TocTransformError::AlreadyTopLevel, id);
        }
        if (!groupByParent.contains(parent)) {
            PromotionGroup group;
            group.parent = parent;
            group.grandparent = tree.nodes.value(parent).parentId;
            groupByParent.insert(parent, groups.size());
            groups.append(group);
        }
        groups[groupByParent.value(parent)].selected.append(id);
    }

    const QSet<TocNodeId> selectedSet(normalized.cbegin(), normalized.cend());
    for (PromotionGroup &group : groups) {
        TocNodeId activeSelection = 0;
        for (TocNodeId child : tree.nodes.value(group.parent).children) {
            if (selectedSet.contains(child)) {
                activeSelection = child;
            } else if (activeSelection && adoptFollowingSiblings) {
                group.adopted[activeSelection].append(child);
            } else {
                group.remaining.append(child);
            }
        }
    }
    for (int first = 0; first < groups.size(); ++first) {
        for (int second = first + 1; second < groups.size(); ++second) {
            if (PromotionGroupsOverlap(tree, groups[first], groups[second])) {
                return Failure(tree, TocTransformError::OverlappingPlans);
            }
        }
    }

    TocTransformResult result;
    result.tree = tree;
    result.normalizedSelection = normalized;
    QHash<TocNodeId, QHash<TocNodeId, QList<TocNodeId>>> insertions;
    for (const PromotionGroup &group : groups) {
        result.tree.nodes[group.parent].children = group.remaining;
        insertions[group.grandparent].insert(group.parent, group.selected);
        for (TocNodeId selectedId : group.selected) {
            result.tree.nodes[selectedId].parentId = group.grandparent;
            result.reparentedIds.append(selectedId);
            const QList<TocNodeId> adopted = group.adopted.value(selectedId);
            result.tree.nodes[selectedId].children.append(adopted);
            for (TocNodeId adoptedId : adopted) {
                result.tree.nodes[adoptedId].parentId = selectedId;
                result.reparentedIds.append(adoptedId);
                ++result.adoptedCount;
            }
        }
    }
    for (auto it = insertions.cbegin(); it != insertions.cend(); ++it) {
        QList<TocNodeId> children;
        for (TocNodeId child : tree.nodes.value(it.key()).children) {
            children.append(child);
            children.append(it.value().value(child));
        }
        result.tree.nodes[it.key()].children = children;
    }
    if (!Validate(result.tree)) {
        return Failure(tree, TocTransformError::InvalidTree);
    }
    result.preorderPreserved = PreorderIds(tree) == PreorderIds(result.tree);
    if (adoptFollowingSiblings && !result.preorderPreserved) {
        return Failure(tree, TocTransformError::OverlappingPlans);
    }
    return result;
}

TocTransformResult TocTreeTransform::Demote(
    const TocEditTree &tree, const QList<TocNodeId> &selection)
{
    if (!Validate(tree)) return Failure(tree, TocTransformError::InvalidTree);
    QList<TocNodeId> normalized;
    TocNodeId errorNode = 0;
    const TocTransformError selectionError = NormalizeSelection(
        tree, selection, normalized, errorNode);
    if (selectionError != TocTransformError::None) {
        return Failure(tree, selectionError, errorNode);
    }

    QList<DemotionGroup> groups;
    QHash<TocNodeId, int> groupByParent;
    for (TocNodeId id : normalized) {
        const TocNodeId parent = tree.nodes.value(id).parentId;
        if (!groupByParent.contains(parent)) {
            DemotionGroup group;
            group.parent = parent;
            groupByParent.insert(parent, groups.size());
            groups.append(group);
        }
    }
    const QSet<TocNodeId> selectedSet(normalized.cbegin(), normalized.cend());
    for (DemotionGroup &group : groups) {
        const QList<TocNodeId> children = tree.nodes.value(group.parent).children;
        for (int index = 0; index < children.size();) {
            if (!selectedSet.contains(children[index])) {
                group.remaining.append(children[index++]);
                continue;
            }
            if (index == 0) {
                return Failure(tree, TocTransformError::NoPreviousSibling,
                               children[index]);
            }
            DemotionRange range;
            range.predecessor = children[index - 1];
            while (index < children.size() && selectedSet.contains(children[index])) {
                range.selected.append(children[index++]);
            }
            group.ranges.append(range);
        }
    }
    for (int first = 0; first < groups.size(); ++first) {
        for (int second = first + 1; second < groups.size(); ++second) {
            if (DemotionGroupsOverlap(tree, groups[first], groups[second])) {
                return Failure(tree, TocTransformError::OverlappingPlans);
            }
        }
    }

    TocTransformResult result;
    result.tree = tree;
    result.normalizedSelection = normalized;
    for (const DemotionGroup &group : groups) {
        result.tree.nodes[group.parent].children = group.remaining;
        for (const DemotionRange &range : group.ranges) {
            result.tree.nodes[range.predecessor].children.append(range.selected);
            for (TocNodeId id : range.selected) {
                result.tree.nodes[id].parentId = range.predecessor;
                result.reparentedIds.append(id);
            }
        }
    }
    if (!Validate(result.tree)) {
        return Failure(tree, TocTransformError::InvalidTree);
    }
    result.preorderPreserved = PreorderIds(tree) == PreorderIds(result.tree);
    if (!result.preorderPreserved) {
        return Failure(tree, TocTransformError::OverlappingPlans);
    }
    return result;
}
