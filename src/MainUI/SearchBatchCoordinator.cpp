/************************************************************************
**
**  Copyright (C) 2026  Sigil-Enhanced contributors
**
**  This file is part of Sigil.
**
*************************************************************************/

#include "MainUI/SearchBatchCoordinator.h"

#include <QReadLocker>
#include <QSet>
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
    if (!main_window || !main_window->GetCurrentBook()) {
        failed.error = QStringLiteral("No book is available for the saved-search batch.");
        return failed;
    }

    main_window->SaveTabData();

    QHash<QString, QString> originalTexts;
    for (auto it = resources.constBegin(); it != resources.constEnd(); ++it) {
        TextResource* resource = it.value();
        if (!resource || resource->GetRelativePath() != it.key()) {
            failed.error = QStringLiteral("Saved-search target is no longer available: %1").arg(it.key());
            return failed;
        }
        resource->InitialLoad();
        QReadLocker locker(&resource->GetLock());
        originalTexts.insert(it.key(), resource->GetText());
    }

    SearchBatch::Result result = SearchBatch::Runner::Run(rules, originalTexts, apply);
    if (!result.success || result.changedTexts.isEmpty()) {
        return result;
    }

    QString conflictPath;
    if (!ResourcesMatchSnapshot(resources, originalTexts, &conflictPath)) {
        result.success = false;
        result.error = QStringLiteral("Saved-search target changed during staging: %1").arg(conflictPath);
        return result;
    }

    if (!main_window->CreateRecoveryCheckpoint()) {
        result.success = false;
        result.error = QStringLiteral("Could not create the recovery checkpoint; no replacements were written.");
        return result;
    }

    if (!ResourcesMatchSnapshot(resources, originalTexts, &conflictPath)) {
        result.success = false;
        result.error = QStringLiteral("Saved-search target changed while creating the checkpoint: %1").arg(conflictPath);
        return result;
    }

    QStringList commitOrder;
    QSet<QString> seenPaths;
    for (const SearchBatch::Rule& rule : rules) {
        for (const QString& path : rule.resourcePaths) {
            if (result.changedTexts.contains(path) && !seenPaths.contains(path)) {
                seenPaths.insert(path);
                commitOrder.append(path);
            }
        }
    }

    QStringList appliedPaths;
    for (const QString& path : commitOrder) {
        TextResource* resource = resources.value(path, nullptr);
        if (!resource) {
            result.success = false;
            result.error = QStringLiteral("Saved-search target disappeared before commit: %1").arg(path);
            break;
        }
        {
            QWriteLocker locker(&resource->GetLock());
            if (resource->GetText() != originalTexts.value(path)) {
                result.success = false;
                result.error = QStringLiteral("Saved-search target changed before commit: %1").arg(path);
                break;
            }
            appliedPaths.append(path);
            resource->SetTextAsUndoableEdit(result.changedTexts.value(path));
            if (resource->GetText() != result.changedTexts.value(path)) {
                result.success = false;
                result.error = QStringLiteral("Saved-search target failed its commit check: %1").arg(path);
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
                resource->SetText(originalTexts.value(path));
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
    const QHash<QString, QString>& original_texts,
    QString* conflict_path)
{
    for (auto it = original_texts.constBegin(); it != original_texts.constEnd(); ++it) {
        TextResource* resource = resources.value(it.key(), nullptr);
        if (!resource || resource->GetRelativePath() != it.key()) {
            if (conflict_path) {
                *conflict_path = it.key();
            }
            return false;
        }
        QReadLocker locker(&resource->GetLock());
        if (resource->GetText() != it.value()) {
            if (conflict_path) {
                *conflict_path = it.key();
            }
            return false;
        }
    }
    return true;
}
