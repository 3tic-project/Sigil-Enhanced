/************************************************************************
**
**  Copyright (C) 2026  Sigil-Enhanced contributors
**
**  This file is part of Sigil.
**
*************************************************************************/

#include "MainUI/SearchBatchCoordinator.h"

#include <QCoreApplication>
#include <QReadLocker>
#include <QSet>
#include <QThread>
#include <QWriteLocker>

#include "BookManipulation/Book.h"
#include "MainUI/MainWindow.h"
#include "ResourceObjects/Resource.h"
#include "ResourceObjects/TextResource.h"
#include "Tabs/ContentTab.h"

SearchBatch::Result SearchBatchCoordinator::Run(
    MainWindow* main_window,
    const QList<SearchBatch::Rule>& rules,
    const QHash<QString, TextResource*>& resources,
    const SearchBatch::ApplyFunction& apply)
{
    SearchBatch::Result failed;
    QStringList orderedPaths;
    QSet<QString> seenPaths;
    for (const SearchBatch::Rule& rule : rules) {
        for (const QString& path : rule.resourcePaths) {
            if (!seenPaths.contains(path)) {
                seenPaths.insert(path);
                orderedPaths.append(path);
            }
        }
    }

    Snapshot snapshot;
    if (!CaptureSnapshot(main_window, orderedPaths, resources, snapshot,
                         &failed.error)) {
        return failed;
    }

    const SearchBatch::Result staged = SearchBatch::Runner::Run(
        rules, snapshot.originalTexts, apply);
    return CommitStagedResult(main_window, resources, snapshot, staged);
}

bool SearchBatchCoordinator::CaptureSnapshot(
    MainWindow* main_window,
    const QStringList& ordered_paths,
    const QHash<QString, TextResource*>& resources,
    Snapshot& snapshot,
    QString* error)
{
    snapshot = Snapshot();
    if (!main_window || !main_window->GetCurrentBook()) {
        if (error) {
            *error = QCoreApplication::translate(
                "RegexWorkbenchCore", "No book is available for the search batch.");
        }
        return false;
    }
    if (QThread::currentThread() != main_window->thread()) {
        if (error) {
            *error = QCoreApplication::translate(
                "RegexWorkbenchCore",
                "Search batch snapshots must be captured on the GUI thread.");
        }
        return false;
    }

    main_window->SaveTabData();
    QSet<QString> seenPaths;
    for (const QString& path : ordered_paths) {
        if (path.isEmpty() || seenPaths.contains(path)) {
            if (error) {
                *error = path.isEmpty()
                             ? QCoreApplication::translate(
                                   "RegexWorkbenchCore",
                                   "Search batch contains an empty target path.")
                             : QCoreApplication::translate(
                                   "RegexWorkbenchCore",
                                   "Search batch contains a duplicate target path: %1")
                                   .arg(path);
            }
            return false;
        }
        seenPaths.insert(path);
        TextResource* resource = resources.value(path, nullptr);
        if (!resource || resource->GetRelativePath() != path) {
            if (error) {
                *error = QCoreApplication::translate(
                             "RegexWorkbenchCore",
                             "Search batch target is no longer available: %1")
                             .arg(path);
            }
            return false;
        }
        resource->InitialLoad();
        QReadLocker locker(&resource->GetLock());
        snapshot.resourcePaths.append(path);
        snapshot.originalTexts.insert(path, resource->GetText());
        snapshot.mediaTypes.insert(path, resource->GetMediaType());
    }
    return true;
}

SearchBatch::Result SearchBatchCoordinator::CommitStagedResult(
    MainWindow* main_window,
    const QHash<QString, TextResource*>& resources,
    const Snapshot& snapshot,
    const SearchBatch::Result& staged_result)
{
    SearchBatch::Result result = staged_result;
    if (!result.success || result.changedTexts.isEmpty()) {
        return result;
    }
    if (!main_window || !main_window->GetCurrentBook()) {
        result.success = false;
        result.error = QCoreApplication::translate(
            "RegexWorkbenchCore", "No book is available for the search batch commit.");
        return result;
    }
    if (QThread::currentThread() != main_window->thread()) {
        result.success = false;
        result.error = QCoreApplication::translate(
            "RegexWorkbenchCore", "Search batch commits must run on the GUI thread.");
        return result;
    }
    for (auto changed = result.changedTexts.constBegin();
         changed != result.changedTexts.constEnd(); ++changed) {
        if (!snapshot.originalTexts.contains(changed.key()) ||
            !snapshot.resourcePaths.contains(changed.key())) {
            result.success = false;
            result.error = QCoreApplication::translate(
                               "RegexWorkbenchCore",
                               "Staged search result contains an unknown target: %1")
                               .arg(changed.key());
            return result;
        }
    }

    QString conflictPath;
    if (!ResourcesMatchSnapshot(resources, snapshot, &conflictPath)) {
        result.success = false;
        result.error = QCoreApplication::translate(
                           "RegexWorkbenchCore",
                           "Search batch target changed during staging: %1")
                           .arg(conflictPath);
        return result;
    }

    if (!main_window->CreateRecoveryCheckpoint()) {
        result.success = false;
        result.error = QCoreApplication::translate(
            "RegexWorkbenchCore",
            "Could not create the recovery checkpoint; no replacements were written.");
        return result;
    }

    if (!ResourcesMatchSnapshot(resources, snapshot, &conflictPath)) {
        result.success = false;
        result.error = QCoreApplication::translate(
                           "RegexWorkbenchCore",
                           "Search batch target changed while creating the checkpoint: %1")
                           .arg(conflictPath);
        return result;
    }

    QStringList commitOrder;
    for (const QString& path : snapshot.resourcePaths) {
        if (result.changedTexts.contains(path)) {
            commitOrder.append(path);
        }
    }

    QStringList appliedPaths;
    for (const QString& path : commitOrder) {
        TextResource* resource = resources.value(path, nullptr);
        if (!resource) {
            result.success = false;
            result.error = QCoreApplication::translate(
                               "RegexWorkbenchCore",
                               "Saved-search target disappeared before commit: %1")
                               .arg(path);
            break;
        }
        {
            QWriteLocker locker(&resource->GetLock());
            if (resource->GetText() != snapshot.originalTexts.value(path)) {
                result.success = false;
                result.error = QCoreApplication::translate(
                                   "RegexWorkbenchCore",
                                   "Saved-search target changed before commit: %1")
                                   .arg(path);
                break;
            }
            appliedPaths.append(path);
            resource->SetTextAsUndoableEdit(result.changedTexts.value(path));
            if (resource->GetText() != result.changedTexts.value(path)) {
                result.success = false;
                result.error = QCoreApplication::translate(
                                   "RegexWorkbenchCore",
                                   "Saved-search target failed its commit check: %1")
                                   .arg(path);
                break;
            }
        }
    }

    if (!result.success) {
        for (auto it = appliedPaths.crbegin(); it != appliedPaths.crend(); ++it) {
            const QString& path = *it;
            TextResource* resource = resources.value(path, nullptr);
            if (resource) {
                QWriteLocker locker(&resource->GetLock());
                resource->SetText(snapshot.originalTexts.value(path));
            }
        }
        return result;
    }

    main_window->GetCurrentBook()->SetModified(true);
    ContentTab* currentTab = main_window->GetCurrentContentTab();
    Resource* currentResource = currentTab ? currentTab->GetLoadedResource() : nullptr;
    if (currentTab && currentResource &&
        result.changedTexts.contains(currentResource->GetRelativePath())) {
        currentTab->ContentChangedExternally();
    }
    return result;
}

bool SearchBatchCoordinator::ResourcesMatchSnapshot(
    const QHash<QString, TextResource*>& resources,
    const Snapshot& snapshot,
    QString* conflict_path)
{
    for (const QString& path : snapshot.resourcePaths) {
        TextResource* resource = resources.value(path, nullptr);
        if (!resource || resource->GetRelativePath() != path) {
            if (conflict_path) {
                *conflict_path = path;
            }
            return false;
        }
        QReadLocker locker(&resource->GetLock());
        if (resource->GetText() != snapshot.originalTexts.value(path)) {
            if (conflict_path) {
                *conflict_path = path;
            }
            return false;
        }
    }
    return true;
}
