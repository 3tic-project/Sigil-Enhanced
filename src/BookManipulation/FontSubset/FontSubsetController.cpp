#include "BookManipulation/FontSubset/FontSubsetController.h"

#include <QFile>
#include <QObject>

#include "BookManipulation/Book.h"
#include "BookManipulation/FolderKeeper.h"
#include "BookManipulation/FontSubset/FontSubsetTransaction.h"
#include "ResourceObjects/FontResource.h"
#include "ResourceObjects/Resource.h"
#include "ResourceObjects/TextResource.h"

namespace FontSubset
{

BookSnapshot FontSubsetController::CreateSnapshot(Book* book)
{
    BookSnapshot snapshot;
    if (!book || !book->GetFolderKeeper()) {
        snapshot.warnings.append(QObject::tr("No EPUB is currently loaded."));
        return snapshot;
    }

    FolderKeeper* keeper = book->GetFolderKeeper();
    const QList<TextResource*> textResources =
        keeper->GetResourceTypeList<TextResource>(true);
    for (TextResource* resource : textResources) {
        if (!resource) {
            continue;
        }
        resource->InitialLoad();
        UsageSource source;
        source.path = resource->GetRelativePath();
        source.mediaType = resource->GetMediaType();
        source.content = resource->GetText();
        snapshot.textSources.append(source);
    }

    const QList<FontResource*> fontResources =
        keeper->GetResourceTypeList<FontResource>(true);
    for (FontResource* resource : fontResources) {
        if (!resource) {
            continue;
        }

        QFile file(resource->GetFullPath());
        if (!file.open(QIODevice::ReadOnly)) {
            snapshot.warnings.append(
                QObject::tr("Could not read font %1: %2")
                    .arg(resource->GetRelativePath(), file.errorString()));
            continue;
        }

        FontSnapshot font;
        font.identifier = resource->GetIdentifier();
        font.relativePath = resource->GetRelativePath();
        font.fullPath = resource->GetFullPath();
        font.mediaType = resource->GetMediaType();
        font.obfuscationAlgorithm = resource->GetObfuscationAlgorithm();
        font.bytes = file.readAll();
        if (font.bytes.isEmpty()) {
            snapshot.warnings.append(
                QObject::tr("Font %1 is empty and will be skipped.")
                    .arg(font.relativePath));
            continue;
        }
        snapshot.fonts.append(font);
    }
    return snapshot;
}

CommitResult FontSubsetController::Commit(
    Book* book,
    const BatchResult& batch,
    const QSet<QString>& selectedIdentifiers)
{
    CommitResult commitResult;
    if (!book || !book->GetFolderKeeper()) {
        commitResult.error = QObject::tr("No EPUB is currently loaded.");
        return commitResult;
    }
    if (selectedIdentifiers.isEmpty()) {
        commitResult.error = QObject::tr("No subset results were selected.");
        return commitResult;
    }

    FolderKeeper* keeper = book->GetFolderKeeper();
    FontSubsetTransaction transaction;
    QList<FontResource*> changedResources;
    QString stageError;
    for (const FontAnalysis& analysis : batch.fonts) {
        if (!selectedIdentifiers.contains(analysis.font.identifier)) {
            continue;
        }
        if (!analysis.result.success || analysis.result.outputBytes.isEmpty() ||
            analysis.result.newSize >= analysis.result.oldSize) {
            commitResult.error =
                QObject::tr("Font %1 no longer has a valid smaller subset result.")
                    .arg(analysis.font.relativePath);
            return commitResult;
        }

        Resource* resource =
            keeper->GetResourceByIdentifier(analysis.font.identifier);
        FontResource* fontResource = qobject_cast<FontResource*>(resource);
        if (!fontResource || fontResource->GetFullPath() != analysis.font.fullPath ||
            fontResource->GetObfuscationAlgorithm() !=
                analysis.font.obfuscationAlgorithm) {
            commitResult.error =
                QObject::tr("Font resource %1 changed after analysis. Run the analysis again.")
                    .arg(analysis.font.relativePath);
            return commitResult;
        }

        if (!transaction.Stage(analysis.font.fullPath, analysis.font.bytes,
                               analysis.result.outputBytes, &stageError)) {
            commitResult.error = stageError;
            return commitResult;
        }
        changedResources.append(fontResource);
        ++commitResult.fontCount;
        commitResult.oldSize += analysis.result.oldSize;
        commitResult.newSize += analysis.result.newSize;
    }

    if (changedResources.isEmpty()) {
        commitResult.error = QObject::tr("No valid subset results were selected.");
        return commitResult;
    }

    keeper->SuspendWatchingResources();
    QString commitError;
    const bool committed = transaction.Commit(&commitError);
    keeper->ResumeWatchingResources();
    if (!committed) {
        commitResult.error = commitError;
        return commitResult;
    }

    for (FontResource* resource : changedResources) {
        resource->LoadFromDisk();
    }
    book->SetModified();
    commitResult.success = true;
    return commitResult;
}

}
