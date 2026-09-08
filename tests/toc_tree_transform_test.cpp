#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>

#include "BookManipulation/TocTreeTransform.h"

namespace {

void Require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

TocEditTree EmptyTree()
{
    TocEditTree tree;
    TocEditNode root;
    root.id = 0;
    root.parentId = 0;
    root.label = QStringLiteral("invisible root");
    tree.nodes.insert(0, root);
    return tree;
}

void Add(TocEditTree &tree, TocNodeId parent, TocNodeId id,
         const QString &label = QString(), const QString &target = QString())
{
    TocEditNode node;
    node.id = id;
    node.parentId = parent;
    node.label = label.isEmpty() ? QStringLiteral("Node %1").arg(id) : label;
    node.target = target.isEmpty() ? QStringLiteral("chapter.xhtml#%1").arg(id) : target;
    tree.nodes.insert(id, node);
    tree.nodes[parent].children.append(id);
}

QList<TocNodeId> Children(const TocEditTree &tree, TocNodeId id)
{
    return tree.nodes.value(id).children;
}

void RequireMetadataUnchanged(const TocEditTree &before, const TocEditTree &after)
{
    Require(before.nodes.size() == after.nodes.size(),
            "A transform added or removed a stable node identity");
    for (auto it = before.nodes.cbegin(); it != before.nodes.cend(); ++it) {
        Require(after.nodes.contains(it.key())
                    && after.nodes.value(it.key()).label == it.value().label
                    && after.nodes.value(it.key()).target == it.value().target,
                "A transform changed a node label or target");
    }
}

void TestSinglePromotionAndExistingChildren()
{
    TocEditTree tree = EmptyTree();
    Add(tree, 0, 1, QStringLiteral("A"));
    Add(tree, 1, 2, QStringLiteral("B"));
    Add(tree, 1, 3, QStringLiteral("C"));
    Add(tree, 3, 4, QStringLiteral("C1"));
    Add(tree, 1, 5, QStringLiteral("D"));
    Add(tree, 0, 6, QStringLiteral("X"));
    const QList<TocNodeId> before = TocTreeTransform::PreorderIds(tree);

    const TocTransformResult result = TocTreeTransform::Promote(tree, {3});
    Require(result.succeeded() && result.preorderPreserved
                && result.adoptedCount == 1,
            "A middle promotion did not preserve preorder");
    Require(Children(result.tree, 0) == QList<TocNodeId>({1, 3, 6})
                && Children(result.tree, 1) == QList<TocNodeId>({2})
                && Children(result.tree, 3) == QList<TocNodeId>({4, 5})
                && result.tree.nodes.value(5).parentId == 3,
            "A promoted node did not adopt following siblings after existing children");
    Require(TocTreeTransform::PreorderIds(result.tree) == before,
            "Single promotion changed reading order");
    RequireMetadataUnchanged(tree, result.tree);
}

void TestPromotionSelectionSemantics()
{
    TocEditTree tree = EmptyTree();
    Add(tree, 0, 1, QStringLiteral("Same"), QStringLiteral("a.xhtml#one"));
    Add(tree, 1, 2, QStringLiteral("B"));
    Add(tree, 1, 3, QStringLiteral("Same"), QStringLiteral("a.xhtml#three"));
    Add(tree, 3, 4, QStringLiteral("Same"), QStringLiteral("a.xhtml#four"));
    Add(tree, 1, 5, QStringLiteral("D"));
    Add(tree, 1, 6, QStringLiteral("E"));
    Add(tree, 1, 7, QStringLiteral("F"));

    TocTransformResult result = TocTreeTransform::Promote(tree, {3, 6});
    Require(result.succeeded() && result.normalizedSelection == QList<TocNodeId>({3, 6})
                && Children(result.tree, 0) == QList<TocNodeId>({1, 3, 6})
                && Children(result.tree, 1) == QList<TocNodeId>({2})
                && Children(result.tree, 3) == QList<TocNodeId>({4, 5})
                && Children(result.tree, 6) == QList<TocNodeId>({7}),
            "Non-contiguous same-parent promotion did not partition following siblings");
    RequireMetadataUnchanged(tree, result.tree);

    result = TocTreeTransform::Promote(tree, {3, 5});
    Require(result.succeeded()
                && Children(result.tree, 0) == QList<TocNodeId>({1, 3, 5})
                && Children(result.tree, 1) == QList<TocNodeId>({2})
                && Children(result.tree, 3) == QList<TocNodeId>({4})
                && Children(result.tree, 5) == QList<TocNodeId>({6, 7}),
            "A continuous promotion selection incorrectly nested selected siblings");

    result = TocTreeTransform::Promote(tree, {3, 4});
    Require(result.succeeded() && result.normalizedSelection == QList<TocNodeId>({3})
                && Children(result.tree, 3) == QList<TocNodeId>({4, 5, 6, 7}),
            "A selected descendant was promoted twice");

    result = TocTreeTransform::Promote(tree, {3}, false);
    Require(result.succeeded()
                && Children(result.tree, 1) == QList<TocNodeId>({2, 5, 6, 7})
                && Children(result.tree, 3) == QList<TocNodeId>({4})
                && !result.preorderPreserved,
            "The explicit legacy promotion mode did not retain following siblings");

    result = TocTreeTransform::Promote(tree, {2});
    Require(result.succeeded() && Children(result.tree, 1).isEmpty()
                && Children(result.tree, 2) == QList<TocNodeId>({3, 5, 6, 7}),
            "Promoting the first child did not retain its parent and adopt the tail");

    TocEditTree onlyChild = EmptyTree();
    Add(onlyChild, 0, 10);
    Add(onlyChild, 10, 11);
    result = TocTreeTransform::Promote(onlyChild, {11});
    Require(result.succeeded() && Children(result.tree, 0) == QList<TocNodeId>({10, 11})
                && Children(result.tree, 10).isEmpty(),
            "Promoting an only child did not leave an empty parent in place");
}

void TestPromotionBoundariesAndCrossParents()
{
    TocEditTree tree = EmptyTree();
    Add(tree, 0, 1, QStringLiteral("A"));
    Add(tree, 1, 2, QStringLiteral("B"));
    Add(tree, 1, 3, QStringLiteral("C"));
    Add(tree, 0, 4, QStringLiteral("X"));
    Add(tree, 4, 5, QStringLiteral("Y"));
    Add(tree, 4, 6, QStringLiteral("Z"));

    TocTransformResult result = TocTreeTransform::Promote(tree, {3, 6});
    Require(result.succeeded() && result.preorderPreserved
                && Children(result.tree, 0) == QList<TocNodeId>({1, 3, 4, 6}),
            "Disjoint parents were not promoted from the same snapshot");

    result = TocTreeTransform::Promote(tree, {1, 3});
    Require(result.error == TocTransformError::AlreadyTopLevel
                && result.tree.nodes.value(1).children == tree.nodes.value(1).children,
            "A mixed root-level selection was partially promoted");

    Add(tree, 3, 7, QStringLiteral("Nested"));
    result = TocTreeTransform::Promote(tree, {2, 7});
    Require(result.error == TocTransformError::OverlappingPlans
                && TocTreeTransform::PreorderIds(result.tree)
                    == TocTreeTransform::PreorderIds(tree),
            "Overlapping cross-parent promotion plans were not rejected atomically");
}

void TestDemotionRanges()
{
    TocEditTree tree = EmptyTree();
    for (TocNodeId id = 1; id <= 5; ++id) Add(tree, 0, id);
    Add(tree, 1, 6, QStringLiteral("existing child"));
    const QList<TocNodeId> before = TocTreeTransform::PreorderIds(tree);

    TocTransformResult result = TocTreeTransform::Demote(tree, {2, 3, 5});
    Require(result.succeeded() && result.preorderPreserved
                && Children(result.tree, 0) == QList<TocNodeId>({1, 4})
                && Children(result.tree, 1) == QList<TocNodeId>({6, 2, 3})
                && Children(result.tree, 4) == QList<TocNodeId>({5}),
            "Contiguous and non-contiguous demotion ranges used the wrong predecessors");
    Require(TocTreeTransform::PreorderIds(result.tree) == before,
            "Demotion changed reading order");
    RequireMetadataUnchanged(tree, result.tree);

    result = TocTreeTransform::Demote(tree, {1, 3});
    Require(result.error == TocTransformError::NoPreviousSibling
                && Children(result.tree, 0) == Children(tree, 0),
            "A demotion with a boundary item partially changed the tree");

    result = TocTreeTransform::Demote(tree, {1, 6});
    Require(result.normalizedSelection == QList<TocNodeId>()
                && result.error == TocTransformError::NoPreviousSibling,
            "A selected descendant bypassed an ancestor boundary failure");
}

void TestInvalidInputs()
{
    TocEditTree tree = EmptyTree();
    Add(tree, 0, 1);
    Add(tree, 1, 2);
    TocTransformResult result = TocTreeTransform::Promote(tree, {99});
    Require(result.error == TocTransformError::UnknownSelection
                && result.errorNodeId == 99,
            "An unknown stable selection identity was accepted");
    result = TocTreeTransform::Demote(tree, {0});
    Require(result.error == TocTransformError::RootSelected,
            "The invisible root was accepted as a selection");

    TocEditTree invalid = tree;
    invalid.nodes[2].parentId = 0;
    Require(!TocTreeTransform::Validate(invalid),
            "A mismatched parent relation passed validation");
    result = TocTreeTransform::Promote(invalid, {2});
    Require(result.error == TocTransformError::InvalidTree,
            "A transform ran against an invalid tree");
}

TocEditTree MakeWideTree(int count)
{
    TocEditTree tree = EmptyTree();
    TocNodeId id = 1;
    while (id <= static_cast<TocNodeId>(count)) {
        const TocNodeId parent = id++;
        Add(tree, 0, parent);
        for (int child = 0; child < 99
             && id <= static_cast<TocNodeId>(count); ++child) {
            Add(tree, parent, id++);
        }
    }
    return tree;
}

qint64 Percentile95(QList<qint64> samples)
{
    std::sort(samples.begin(), samples.end());
    return samples.at((samples.size() * 95 - 1) / 100);
}

void TestPerformance()
{
    const TocEditTree thousand = MakeWideTree(1000);
    QList<qint64> samples;
    for (int run = 0; run < 20; ++run) {
        const auto start = std::chrono::steady_clock::now();
        const TocTransformResult result = TocTreeTransform::Promote(thousand, {51});
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count();
        Require(result.succeeded(), "The 1,000-node performance transform failed");
        samples.append(elapsed);
    }
    Require(Percentile95(samples) <= 100000,
            "The 1,000-node promotion P95 exceeded 100 ms");
    const qint64 thousandP95 = Percentile95(samples);

    const TocEditTree tenThousand = MakeWideTree(10000);
    const auto start = std::chrono::steady_clock::now();
    const TocTransformResult result = TocTreeTransform::Promote(tenThousand, {51});
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    Require(result.succeeded() && elapsed <= 300,
            "The 10,000-node promotion exceeded 300 ms");
    std::cout << "TOC transform timing: 1,000-node P95 " << thousandP95
              << " us; 10,000-node " << elapsed << " ms\n";
}

}

int main()
{
    TestSinglePromotionAndExistingChildren();
    TestPromotionSelectionSemantics();
    TestPromotionBoundariesAndCrossParents();
    TestDemotionRanges();
    TestInvalidInputs();
    TestPerformance();
    return EXIT_SUCCESS;
}
