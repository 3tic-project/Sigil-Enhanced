#include <cstdlib>
#include <iostream>

#include "BuiltinPlugins/RegexWorkbench/SearchVariableStore.h"

namespace
{

using BuiltinPlugins::RegexWorkbench::SearchVariableStore;
using BuiltinPlugins::RegexWorkbench::VariableScope;
using BuiltinPlugins::RegexWorkbench::VariableStoreLimits;
using BuiltinPlugins::RegexWorkbench::WritePolicy;

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

void TestNameValidationAndResourceFrames()
{
    Require(SearchVariableStore::IsValidName(QStringLiteral("name_1")) &&
                SearchVariableStore::IsValidName(QStringLiteral("_x")) &&
                !SearchVariableStore::IsValidName(QStringLiteral("1name")) &&
                !SearchVariableStore::IsValidName(QStringLiteral("a-b")) &&
                !SearchVariableStore::IsValidName(QString::fromUtf8("a١")) &&
                !SearchVariableStore::IsValidName(QString(65, QLatin1Char('a'))),
            "variable names must use the bounded ASCII identifier grammar");

    SearchVariableStore store;
    QString error;
    Require(!store.set(QStringLiteral("x"), QStringLiteral("missing resource"), &error) &&
                !error.isEmpty(),
            "resource scope must require an active bookpath");
    store.setActiveResource(QStringLiteral("Text/a.xhtml"));
    Require(store.set(QStringLiteral("x"), QStringLiteral("A")),
            "resource variable write failed");
    store.setActiveResource(QStringLiteral("Text/b.xhtml"));
    bool found = true;
    Require(store.get(QStringLiteral("x"), &found).isEmpty() && !found,
            "resource variables must be isolated by bookpath");
    Require(store.set(QStringLiteral("x"), QStringLiteral("B")),
            "second resource variable write failed");
    store.setActiveResource(QStringLiteral("Text/a.xhtml"));
    Require(store.get(QStringLiteral("x")) == QStringLiteral("A"),
            "switching resources must restore that resource's frame");
}

void TestBatchSessionAndClearRunLocals()
{
    SearchVariableStore store;
    store.setScope(VariableScope::Batch);
    Require(store.set(QStringLiteral("batch"), QStringLiteral("value")),
            "batch variable write failed");
    store.setActiveResource(QStringLiteral("Text/other.xhtml"));
    Require(store.get(QStringLiteral("batch")) == QStringLiteral("value"),
            "batch variables must survive active-resource changes");

    store.setScope(VariableScope::Session);
    Require(store.set(QStringLiteral("session"), QStringLiteral("keep")),
            "session variable write failed");
    store.clearRunLocals();
    Require(store.get(QStringLiteral("session")) == QStringLiteral("keep"),
            "clearRunLocals must retain session variables");
    store.setScope(VariableScope::Batch);
    Require(!store.has(QStringLiteral("batch")),
            "clearRunLocals must clear batch variables");
}

void TestWritePoliciesAndLimits()
{
    SearchVariableStore store;
    store.setScope(VariableScope::Batch);
    Require(store.set(QStringLiteral("x"), QStringLiteral("one")) &&
                store.set(QStringLiteral("x"), QStringLiteral("two")) &&
                store.getList(QStringLiteral("x")) == QStringList{QStringLiteral("two")},
            "LastWins must replace the existing value");

    store.setWritePolicy(WritePolicy::FirstOnly);
    Require(store.set(QStringLiteral("x"), QStringLiteral("three")) &&
                store.get(QStringLiteral("x")) == QStringLiteral("two"),
            "FirstOnly must preserve the first stored value");

    store.setWritePolicy(WritePolicy::Append);
    Require(store.set(QStringLiteral("x"), QStringLiteral("three")) &&
                store.getList(QStringLiteral("x")) ==
                    QStringList({QStringLiteral("two"), QStringLiteral("three")}) &&
                store.get(QStringLiteral("x")) == QStringLiteral("three"),
            "Append must retain the list while resolver reads the latest value");

    VariableStoreLimits limits;
    limits.maxValueCodeUnits = 3;
    limits.maxTotalCodeUnits = 5;
    limits.maxVariables = 2;
    SearchVariableStore limited(limits);
    limited.setScope(VariableScope::Batch);
    QString error;
    Require(limited.set(QStringLiteral("a"), QStringLiteral("123")) &&
                !limited.set(QStringLiteral("b"), QStringLiteral("456"), &error) &&
                limited.totalCodeUnits() == 3 && !limited.has(QStringLiteral("b")),
            "store total-limit failure must not publish the rejected value");
    Require(!limited.set(QStringLiteral("a"), QStringLiteral("1234"), &error) &&
                limited.get(QStringLiteral("a")) == QStringLiteral("123"),
            "per-value limit failure must preserve the previous value");
    Require(limited.set(QStringLiteral("b"), QString()) &&
                !limited.set(QStringLiteral("c"), QString(), &error) &&
                !limited.has(QStringLiteral("c")),
            "empty values must still consume the bounded variable count");

    SearchVariableStore resourceLimited(limits);
    resourceLimited.setActiveResource(QStringLiteral("Text/a.xhtml"));
    Require(resourceLimited.set(QStringLiteral("a"), QStringLiteral("123")),
            "resource limit setup write failed");
    resourceLimited.setActiveResource(QStringLiteral("Text/b.xhtml"));
    Require(!resourceLimited.set(QStringLiteral("b"), QStringLiteral("456"), &error),
            "resource total-limit failure fixture did not fail");
    const auto failedSnapshot = resourceLimited.snapshot();
    Require(!failedSnapshot.resourceFrames.contains(QStringLiteral("Text/b.xhtml")),
            "failed resource write must not leave an empty resource frame");
}

void TestCaptureIngestAndTransactionalRollback()
{
    SearchVariableStore store;
    store.setScope(VariableScope::Batch);
    const QHash<QString, int> numbers {
        {QStringLiteral("word"), 1},
        {QStringLiteral("empty"), 2},
        {QStringLiteral("unmatched"), 3}
    };
    const QList<std::pair<int, int>> captures {
        {0, 3}, {0, 3}, {3, 3}, {-1, -1}
    };
    Require(store.ingestNamedCaptures(numbers, QStringLiteral("abc"), captures) &&
                store.get(QStringLiteral("word")) == QStringLiteral("abc") &&
                store.has(QStringLiteral("empty")) &&
                store.get(QStringLiteral("empty")).isEmpty() &&
                !store.has(QStringLiteral("unmatched")),
            "capture ingest must distinguish empty participation from unmatched groups");

    const auto before = store.snapshot();
    QString error;
    Require(!store.ingestNamedCaptures(numbers, QStringLiteral("abc"), captures,
                                       {QStringLiteral("word"), QStringLiteral("missing")},
                                       &error) &&
                store.stateData() == [&]() {
                    SearchVariableStore copy;
                    copy.restore(before);
                    return copy.stateData();
                }(),
            "multi-capture ingest failure must roll back earlier writes in that ingest");
}

void TestSnapshotRestoreAndStableStateData()
{
    SearchVariableStore store;
    store.setScope(VariableScope::Batch);
    store.set(QStringLiteral("b"), QStringLiteral("2"));
    store.set(QStringLiteral("a"), QStringLiteral("1"));
    const auto snapshot = store.snapshot();
    const QByteArray originalState = store.stateData();
    store.set(QStringLiteral("a"), QStringLiteral("changed"));
    Require(store.stateData() != originalState,
            "stateData must change when an active frame changes");
    Require(store.restore(snapshot) && store.stateData() == originalState,
            "snapshot restore must recover all variable frames and metadata");
}

}

int main()
{
    TestNameValidationAndResourceFrames();
    TestBatchSessionAndClearRunLocals();
    TestWritePoliciesAndLimits();
    TestCaptureIngestAndTransactionalRollback();
    TestSnapshotRestoreAndStableStateData();
    std::cout << "regex variable store tests passed\n";
    return 0;
}
