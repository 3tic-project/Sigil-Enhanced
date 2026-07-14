#include <QCoreApplication>
#include <QElapsedTimer>
#include <QList>
#include <QStandardItemModel>

#include <memory>

namespace
{

struct Fixture {
    QStandardItemModel model;
    QList<QStandardItem*> folders;
};

struct Measurement {
    qint64 elapsedNs = 0;
    int rowsRemovedSignals = 0;
    int rowsInsertedSignals = 0;
};

bool expect(bool condition, const char* message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        return false;
    }
    return true;
}

Fixture* createFixture(int resourceCount)
{
    auto* fixture = new Fixture;
    for (int i = 0; i < 7; ++i) {
        auto* folder = new QStandardItem(QStringLiteral("Folder %1").arg(i));
        fixture->folders.append(folder);
        fixture->model.appendRow(folder);
    }
    for (int i = 0; i < resourceCount; ++i) {
        fixture->folders.at(i % fixture->folders.size())
            ->appendRow(new QStandardItem(QStringLiteral("Resource %1").arg(i)));
    }
    fixture->model.appendRow(new QStandardItem(QStringLiteral("content.opf")));
    fixture->model.appendRow(new QStandardItem(QStringLiteral("toc.ncx")));
    return fixture;
}

bool fixtureRebuilt(const Fixture& fixture)
{
    if (fixture.model.rowCount() != fixture.folders.size() + 2) {
        return false;
    }
    int resourceCount = 0;
    for (QStandardItem* folder : fixture.folders) {
        if (folder->model() != &fixture.model) {
            return false;
        }
        resourceCount += folder->rowCount();
    }
    return resourceCount > 0;
}

Measurement clearOneRowAtATime(Fixture& fixture)
{
    Measurement result;
    QObject::connect(&fixture.model, &QStandardItemModel::rowsRemoved,
                     &fixture.model, [&](const QModelIndex&, int, int) {
                         ++result.rowsRemovedSignals;
                     });
    QObject::connect(&fixture.model, &QStandardItemModel::rowsInserted,
                     &fixture.model, [&](const QModelIndex&, int, int) {
                         ++result.rowsInsertedSignals;
                     });
    int resourceCount = 0;
    for (QStandardItem* folder : fixture.folders) {
        resourceCount += folder->rowCount();
    }
    QElapsedTimer timer;
    timer.start();
    for (QStandardItem* folder : fixture.folders) {
        while (folder->rowCount() > 0) {
            folder->removeRow(0);
        }
    }
    while (fixture.model.rowCount() > fixture.folders.size()) {
        fixture.model.removeRow(fixture.folders.size());
    }
    for (int i = 0; i < resourceCount; ++i) {
        fixture.folders.at(i % fixture.folders.size())
            ->appendRow(new QStandardItem(QStringLiteral("Rebuilt %1").arg(i)));
    }
    fixture.model.appendRow(new QStandardItem(QStringLiteral("content.opf")));
    fixture.model.appendRow(new QStandardItem(QStringLiteral("toc.ncx")));
    result.elapsedNs = timer.nsecsElapsed();
    return result;
}

Measurement clearInBatches(Fixture& fixture)
{
    Measurement result;
    QObject::connect(&fixture.model, &QStandardItemModel::rowsRemoved,
                     &fixture.model, [&](const QModelIndex&, int, int) {
                         ++result.rowsRemovedSignals;
                     });
    QObject::connect(&fixture.model, &QStandardItemModel::rowsInserted,
                     &fixture.model, [&](const QModelIndex&, int, int) {
                         ++result.rowsInsertedSignals;
                     });
    int resourceCount = 0;
    for (QStandardItem* folder : fixture.folders) {
        resourceCount += folder->rowCount();
    }
    QElapsedTimer timer;
    timer.start();
    for (QStandardItem* folder : fixture.folders) {
        const int rows = folder->rowCount();
        if (rows > 0) {
            folder->removeRows(0, rows);
        }
    }
    const int extraRows = fixture.model.rowCount() - fixture.folders.size();
    if (extraRows > 0) {
        fixture.model.removeRows(fixture.folders.size(), extraRows);
    }
    QList<QList<QStandardItem*>> itemsByFolder(fixture.folders.size());
    for (int i = 0; i < resourceCount; ++i) {
        itemsByFolder[i % fixture.folders.size()]
            .append(new QStandardItem(QStringLiteral("Rebuilt %1").arg(i)));
    }
    for (int i = 0; i < fixture.folders.size(); ++i) {
        fixture.folders.at(i)->appendRows(itemsByFolder.at(i));
    }
    QList<QStandardItem*> rootItems;
    rootItems.append(new QStandardItem(QStringLiteral("content.opf")));
    rootItems.append(new QStandardItem(QStringLiteral("toc.ncx")));
    fixture.model.invisibleRootItem()->appendRows(rootItems);
    result.elapsedNs = timer.nsecsElapsed();
    return result;
}

bool runSample(int resourceCount)
{
    std::unique_ptr<Fixture> legacy(createFixture(resourceCount));
    std::unique_ptr<Fixture> batched(createFixture(resourceCount));
    const Measurement legacyMeasurement = clearOneRowAtATime(*legacy);
    const Measurement batchedMeasurement = clearInBatches(*batched);

    fprintf(stdout,
            "OPF_CLEAR_BENCH resources=%d legacy_ms=%.3f batched_ms=%.3f "
            "legacy_remove_signals=%d batched_remove_signals=%d "
            "legacy_insert_signals=%d batched_insert_signals=%d\n",
            resourceCount,
            legacyMeasurement.elapsedNs / 1000000.0,
            batchedMeasurement.elapsedNs / 1000000.0,
            legacyMeasurement.rowsRemovedSignals,
            batchedMeasurement.rowsRemovedSignals,
            legacyMeasurement.rowsInsertedSignals,
            batchedMeasurement.rowsInsertedSignals);

    return expect(fixtureRebuilt(*legacy), "legacy fixture did not rebuild") &&
           expect(fixtureRebuilt(*batched), "batched fixture did not rebuild") &&
           expect(legacyMeasurement.rowsRemovedSignals == resourceCount + 2,
                  "legacy signal baseline changed") &&
           expect(batchedMeasurement.rowsRemovedSignals <= 8,
                  "batched clear emitted intermediate row signals") &&
           expect(legacyMeasurement.rowsInsertedSignals == resourceCount + 2,
                  "legacy insertion signal baseline changed") &&
           expect(batchedMeasurement.rowsInsertedSignals <= 8,
                  "batched rebuild emitted intermediate row signals") &&
           expect(resourceCount != 5000 ||
                      batchedMeasurement.elapsedNs < 500LL * 1000 * 1000,
                  "5000-node batched clear exceeded 500ms");
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    const bool ok = runSample(5) && runSample(100) && runSample(1000) && runSample(5000);
    return ok ? 0 : 1;
}
