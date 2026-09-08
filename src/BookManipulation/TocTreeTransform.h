#pragma once

#include <QHash>
#include <QList>
#include <QString>

using TocNodeId = quint64;

struct TocEditNode
{
    TocNodeId id = 0;
    TocNodeId parentId = 0;
    QString label;
    QString target;
    QList<TocNodeId> children;
};

struct TocEditTree
{
    TocNodeId rootId = 0;
    QHash<TocNodeId, TocEditNode> nodes;
};

enum class TocTransformError
{
    None,
    InvalidTree,
    UnknownSelection,
    RootSelected,
    AlreadyTopLevel,
    NoPreviousSibling,
    OverlappingPlans
};

struct TocTransformResult
{
    TocEditTree tree;
    QList<TocNodeId> normalizedSelection;
    QList<TocNodeId> reparentedIds;
    TocTransformError error = TocTransformError::None;
    TocNodeId errorNodeId = 0;
    int adoptedCount = 0;
    bool preorderPreserved = false;

    bool succeeded() const { return error == TocTransformError::None; }
};

class TocTreeTransform
{
public:
    static TocTransformResult Promote(
        const TocEditTree &tree, const QList<TocNodeId> &selection,
        bool adoptFollowingSiblings = true);
    static TocTransformResult Demote(
        const TocEditTree &tree, const QList<TocNodeId> &selection);

    static bool Validate(const TocEditTree &tree, QString *error = nullptr);
    static QList<TocNodeId> PreorderIds(const TocEditTree &tree);
};
