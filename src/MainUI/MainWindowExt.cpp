#include <QApplication>
#include <QHash>
#include <QMessageBox>
#include <QObject>
#include <QWriteLocker>

#include "MainUI/MainWindow.h"
#include "MainUI/BookBrowser.h"
#include "MainUI/TableOfContents.h"
#include "MainUI/ValidationResultsView.h"
#include "BuiltinPlugins/EpubStructureNormalizer.h"
#include "BuiltinPlugins/FormatterEnhancer.h"
#include "BuiltinPlugins/BrParagraphNormalizer.h"
#include "BuiltinPlugins/KfxParagraphNormalizer.h"
#include "BookManipulation/FolderKeeper.h"
#include "ResourceObjects/HTMLResource.h"
#include "ResourceObjects/Resource.h"
#include "Tabs/ContentTab.h"
#include "Tabs/FlowTab.h"
#include "Tabs/CSSTab.h"
#include "BookManipulation/EpubVersionConv.h" // modified: Epub3ToEpub2 Epub2ToEpub3
#include "Misc/ResourceInsertion.h"
#include "Misc/SettingsStoreExtend.h"
#include "Misc/Utility.h"

namespace
{

QHash<Resource*, QString> CaptureResourcePaths(Book* book)
{
    QHash<Resource*, QString> paths;
    if (!book || !book->GetFolderKeeper()) {
        return paths;
    }

    foreach(Resource* resource, book->GetFolderKeeper()->GetResourceList()) {
        if (resource) {
            paths.insert(resource, resource->GetRelativePath());
        }
    }
    return paths;
}

QHash<QString, QString> BuildResourcePathMap(Book* book, const QHash<Resource*, QString>& old_paths)
{
    QHash<QString, QString> path_map;
    if (!book || !book->GetFolderKeeper()) {
        return path_map;
    }

    foreach(Resource* resource, book->GetFolderKeeper()->GetResourceList()) {
        if (!resource || !old_paths.contains(resource)) {
            continue;
        }
        const QString old_path = old_paths.value(resource);
        const QString new_path = resource->GetRelativePath();
        if (old_path != new_path) {
            path_map.insert(old_path, new_path);
        }
    }
    return path_map;
}

QList<ValidationResult> RebaseValidationResultPaths(const QList<ValidationResult>& results,
                                                    const QHash<QString, QString>& path_map)
{
    if (path_map.isEmpty()) {
        return results;
    }

    QList<ValidationResult> rebased;
    foreach(ValidationResult result, results) {
        QString bookpath = result.BookPath();
        if (path_map.contains(bookpath)) {
            bookpath = path_map.value(bookpath);
        }
        rebased << ValidationResult(result.Type(), bookpath, result.LineNumber(),
                                    result.CharOffset(), result.Message());
    }
    return rebased;
}

void ApplyKfxPostFormat(HTMLResource* resource,
                        QString& text,
                        QList<ValidationResult>& results,
                        const QString& bookpath)
{
    if (!resource) {
        return;
    }

    SettingsStoreExtend settings;
    const BuiltinPlugins::FormatterEnhancer::FormatResult format_result =
        BuiltinPlugins::FormatterEnhancer::formatXhtmlText(text, resource->GetEpubVersion(), settings.getXhtmlFormat());

    if (!format_result.ok) {
        results << ValidationResult(ValidationResult::ResType_Warn, bookpath, -1, -1,
                                    QObject::tr("KFX paragraph normalization: automatic XHTML formatting failed; writing the normalized XHTML without formatter changes. %1")
                                        .arg(format_result.messages.join(QStringLiteral("; "))));
        return;
    }

    if (format_result.changed) {
        text = format_result.text;
        results << ValidationResult(ValidationResult::ResType_Info, bookpath, -1, -1,
                                    QObject::tr("KFX paragraph normalization: automatic XHTML formatting was applied."));
    } else {
        results << ValidationResult(ValidationResult::ResType_Info, bookpath, -1, -1,
                                    QObject::tr("KFX paragraph normalization: automatic XHTML formatting found no further changes."));
    }
}

}

//-----modified: Epub3ToEpub2------
void MainWindow::Epub3ToEpub2()
{
    QString epubversion = m_Book->GetConstOPF()->GetEpubVersion();
    if (epubversion.startsWith("2")) {
        QMessageBox::warning(this, tr("Sigil"),
            tr("This Epub is already the version 2.0 !"), QMessageBox::Ok);
        return;
    }
    if (!StandardizeEpub()) return;
    GenerateNCXGuideFromNav();
    EpubVersionConv* evc = new EpubVersionConv(m_Book);
    evc->convert_to_epub2();
    m_TableOfContents->SetBook(m_Book); // set the TOCModel's m_EpubVersion to 2.0
    ResourcesAddedOrDeletedOrMoved(); // Change the main window's title to show 2.0 version
    m_BookBrowser->Refresh();
}

//-----modified: Epub2ToEpub3------
void MainWindow::Epub2ToEpub3()
{
    QString epubversion = m_Book->GetConstOPF()->GetEpubVersion();
    if (epubversion.startsWith("3")) {
        QMessageBox::warning(this, tr("Sigil"),
            tr("This Epub is already the version 3.0 !"), QMessageBox::Ok);
        return;
    }
    if (!StandardizeEpub()) return;
    EpubVersionConv* evc = new EpubVersionConv(m_Book);
    evc->convert_to_epub3();
    RemoveNCXGuideFromEpub3();
    m_TableOfContents->SetBook(m_Book); // set the TOCModel's m_EpubVersion to 3.0
    ResourcesAddedOrDeletedOrMoved(); // Change the main window's title to show 3.0 version
    m_BookBrowser->Refresh();
}

bool MainWindow::NormalizeEpubStructure()
{
    SaveTabData();

    QMessageBox::StandardButton button_pressed;
    button_pressed = Utility::warning(
        this,
        tr("Sigil-Enhanced"),
        tr("Normalize this EPUB structure?\n\n"
           "This will repair OPF manifest issues, correct internal link path casing, "
           "and move resources to Sigil's standard folder layout."),
        QMessageBox::Ok | QMessageBox::Cancel);
    if (button_pressed != QMessageBox::Ok) {
        return false;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    if (!CanStandardizeEpubLayout(tr("Normalize EPUB structure"))) {
        QApplication::restoreOverrideCursor();
        return false;
    }

    BuiltinPlugins::EpubStructureNormalizer normalizer(m_Book.data());
    BuiltinPlugins::EpubStructureNormalizer::Result result = normalizer.normalize();
    QHash<Resource*, QString> paths_before_layout = CaptureResourcePaths(m_Book.data());
    QList<ValidationResult> layout_results;
    bool layout_modified = ApplyStandardEpubLayout(&layout_results);
    result.validationResults = RebaseValidationResultPaths(
        result.validationResults,
        BuildResourcePathMap(m_Book.data(), paths_before_layout));
    foreach(const ValidationResult& validation_result, layout_results) {
        result.validationResults.append(validation_result);
    }
    QApplication::restoreOverrideCursor();

    if (result.bookBrowserRefreshRequired && !layout_modified) {
        m_BookBrowser->Refresh();
    }
    if (result.modified && !layout_modified) {
        m_Book->SetModified();
    }

    bool modified = result.modified || layout_modified;
    m_ValidationResultsView->LoadResults(result.validationResults);
    ShowMessageOnStatusBar(modified ?
                           tr("EPUB structure normalization completed.") :
                           tr("No EPUB structure changes needed."));
    return true;
}

bool MainWindow::EnhanceSourceFormatting()
{
    SaveTabData();

    QMessageBox::StandardButton button_pressed;
    button_pressed = Utility::warning(
        this,
        tr("Sigil-Enhanced"),
        tr("Enhance source formatting for all XHTML and CSS resources?\n\n"
           "This uses the built-in EPUB-safe formatter backend. XHTML files that are not well-formed "
           "and CSS files with parser errors will be skipped and reported in Validation Results."),
        QMessageBox::Ok | QMessageBox::Cancel);
    if (button_pressed != QMessageBox::Ok) {
        return false;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    BuiltinPlugins::FormatterEnhancer formatter(m_Book.data());
    BuiltinPlugins::FormatterEnhancer::Options options;
    BuiltinPlugins::FormatterEnhancer::Result result = formatter.format(options);
    QApplication::restoreOverrideCursor();

    if (result.modified) {
        m_Book->SetModified();
    }

    m_ValidationResultsView->LoadResults(result.validationResults);
    ShowMessageOnStatusBar(result.modified ?
                           tr("Source formatting enhancement completed.") :
                           tr("No source formatting changes needed."));
    return true;
}

bool MainWindow::AnalyzeBrParagraphs()
{
    SaveTabData();

    QApplication::setOverrideCursor(Qt::WaitCursor);
    QList<ValidationResult> results;
    int checked = 0;
    int auto_candidates = 0;
    int manual_candidates = 0;
    int skipped = 0;
    int total_paragraphs = 0;

    if (!m_Book || !m_Book->GetFolderKeeper()) {
        results << ValidationResult(ValidationResult::ResType_Error, QString(), -1, -1,
                                    tr("BR paragraph analysis: no EPUB is currently loaded."));
        QApplication::restoreOverrideCursor();
        m_ValidationResultsView->LoadResults(results);
        return false;
    }

    const QList<HTMLResource*> html_resources =
        m_Book->GetFolderKeeper()->GetResourceTypeList<HTMLResource>(true);
    foreach(HTMLResource* resource, html_resources) {
        if (!resource) {
            continue;
        }

        checked++;
        resource->InitialLoad();
        const QString bookpath = resource->GetRelativePath();
        const BuiltinPlugins::BrParagraphNormalizer::Analysis analysis =
            BuiltinPlugins::BrParagraphNormalizer::analyzeXhtmlText(resource->GetText());

        if (analysis.safeToNormalize) {
            auto_candidates++;
            total_paragraphs += analysis.candidateParagraphs;
            results << ValidationResult(ValidationResult::ResType_Warn, bookpath, -1, -1, analysis.message);
        } else if (analysis.candidate) {
            manual_candidates++;
            results << ValidationResult(ValidationResult::ResType_Warn, bookpath, -1, -1, analysis.message);
        } else {
            skipped++;
            results << ValidationResult(ValidationResult::ResType_Info, bookpath, -1, -1, analysis.message);
        }
    }

    results << ValidationResult(ValidationResult::ResType_Info, QString(), -1, -1,
                                tr("BR paragraph analysis completed. Checked %1 XHTML files, found %2 auto-safe candidate files, %3 manual-review candidate files, skipped %4 files, estimated %5 auto-safe paragraphs.")
                                    .arg(checked)
                                    .arg(auto_candidates)
                                    .arg(manual_candidates)
                                    .arg(skipped)
                                    .arg(total_paragraphs));
    QApplication::restoreOverrideCursor();

    m_ValidationResultsView->LoadResults(results);
    ShowMessageOnStatusBar((auto_candidates + manual_candidates) > 0 ?
                           tr("BR paragraph candidates found. See Validation Results.") :
                           tr("No BR paragraph candidates found."));
    return true;
}

bool MainWindow::NormalizeCurrentBrParagraphs()
{
    SaveTabData();

    ContentTab* tab = GetCurrentContentTab();
    HTMLResource* resource = tab ? qobject_cast<HTMLResource*>(tab->GetLoadedResource()) : nullptr;
    QList<ValidationResult> results;

    if (!resource) {
        results << ValidationResult(ValidationResult::ResType_Error, QString(), -1, -1,
                                    tr("BR paragraph normalization: current tab is not an XHTML resource."));
        m_ValidationResultsView->LoadResults(results);
        Utility::warning(this, tr("Sigil-Enhanced"), tr("The current tab is not an XHTML file."));
        return false;
    }

    resource->InitialLoad();
    const QString bookpath = resource->GetRelativePath();
    const BuiltinPlugins::BrParagraphNormalizer::NormalizeResult normalize_result =
        BuiltinPlugins::BrParagraphNormalizer::normalizeXhtmlText(resource->GetText(), true);

    results << ValidationResult((normalize_result.before.candidate || normalize_result.before.safeToNormalize) ?
                                    ValidationResult::ResType_Warn :
                                    ValidationResult::ResType_Info,
                                bookpath, -1, -1, normalize_result.before.message);
    foreach(const QString& message, normalize_result.messages) {
        results << ValidationResult(normalize_result.ok ?
                                        ValidationResult::ResType_Info :
                                        ValidationResult::ResType_Error,
                                    bookpath, -1, -1, message);
    }

    if (!normalize_result.before.candidate) {
        m_ValidationResultsView->LoadResults(results);
        Utility::warning(this, tr("Sigil-Enhanced"),
                         tr("The current XHTML file is not a BR paragraph candidate. See Validation Results."));
        return false;
    }

    if (!normalize_result.ok) {
        m_ValidationResultsView->LoadResults(results);
        Utility::warning(this, tr("Sigil-Enhanced"),
                         tr("BR paragraph normalization failed safety checks. See Validation Results."));
        return false;
    }

    if (!normalize_result.changed) {
        results << ValidationResult(ValidationResult::ResType_Info, bookpath, -1, -1,
                                    tr("BR paragraph normalization: no changes needed."));
        m_ValidationResultsView->LoadResults(results);
        ShowMessageOnStatusBar(tr("No BR paragraph changes needed."));
        return true;
    }

    const QString review_note = normalize_result.before.safeToNormalize ?
        tr("This file is an auto-safe BR paragraph candidate.") :
        tr("This file requires manual review and is skipped by full-book normalization. Continue only if you inspected it.");

    QMessageBox::StandardButton button_pressed;
    button_pressed = Utility::warning(
        this,
        tr("Sigil-Enhanced"),
        tr("Normalize BR paragraphs in the current XHTML file?\n\n"
           "%1\n\n"
           "This will convert body-level BR-separated text into p elements. "
           "Safety checks have confirmed visible text, id/name, and href/src values are preserved.\n\n"
           "Estimated paragraphs: %2")
            .arg(review_note)
            .arg(normalize_result.before.candidateParagraphs),
        QMessageBox::Ok | QMessageBox::Cancel);
    if (button_pressed != QMessageBox::Ok) {
        m_ValidationResultsView->LoadResults(results);
        return false;
    }

    FlowTab* flowtab = qobject_cast<FlowTab*>(tab);
    if (flowtab) {
        const int cursor_position = flowtab->GetCursorPosition();
        flowtab->ReplaceDocumentText(normalize_result.text);
        flowtab->ScrollToPosition(qMin(cursor_position, normalize_result.text.length()));
    } else {
        QWriteLocker locker(&resource->GetLock());
        resource->SetText(normalize_result.text);
        if (tab) {
            tab->ContentChangedExternally();
        }
    }
    if (m_Book) {
        m_Book->SetModified();
    }

    results << ValidationResult(ValidationResult::ResType_Info, bookpath, -1, -1,
                                tr("BR paragraph normalization: current XHTML file was updated."));
    m_ValidationResultsView->LoadResults(results);
    ShowMessageOnStatusBar(tr("Current XHTML BR paragraphs normalized."));
    return true;
}

bool MainWindow::NormalizeAllBrParagraphs()
{
    SaveTabData();

    QList<ValidationResult> results;
    if (!m_Book || !m_Book->GetFolderKeeper()) {
        results << ValidationResult(ValidationResult::ResType_Error, QString(), -1, -1,
                                    tr("BR paragraph normalization: no EPUB is currently loaded."));
        m_ValidationResultsView->LoadResults(results);
        return false;
    }

    struct PlanEntry {
        HTMLResource* resource = nullptr;
        QString bookpath;
        BuiltinPlugins::BrParagraphNormalizer::Analysis analysis;
    };

    QList<PlanEntry> plan;
    int checked = 0;
    int manual_candidates = 0;
    int skipped = 0;
    int total_paragraphs = 0;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QList<HTMLResource*> html_resources =
        m_Book->GetFolderKeeper()->GetResourceTypeList<HTMLResource>(true);
    foreach(HTMLResource* resource, html_resources) {
        if (!resource) {
            continue;
        }

        checked++;
        resource->InitialLoad();
        const QString bookpath = resource->GetRelativePath();
        const BuiltinPlugins::BrParagraphNormalizer::Analysis analysis =
            BuiltinPlugins::BrParagraphNormalizer::analyzeXhtmlText(resource->GetText());

        if (analysis.safeToNormalize) {
            plan << PlanEntry{resource, bookpath, analysis};
            total_paragraphs += analysis.candidateParagraphs;
            results << ValidationResult(ValidationResult::ResType_Warn, bookpath, -1, -1, analysis.message);
        } else if (analysis.candidate) {
            manual_candidates++;
            results << ValidationResult(ValidationResult::ResType_Warn, bookpath, -1, -1, analysis.message);
        } else {
            skipped++;
            results << ValidationResult(ValidationResult::ResType_Info, bookpath, -1, -1, analysis.message);
        }
    }
    QApplication::restoreOverrideCursor();

    results << ValidationResult(ValidationResult::ResType_Info, QString(), -1, -1,
                                tr("BR paragraph normalization dry-run completed. Checked %1 XHTML files, %2 files are auto-safe, %3 files require manual review, %4 files were skipped, estimated %5 auto-safe paragraphs.")
                                    .arg(checked)
                                    .arg(plan.count())
                                    .arg(manual_candidates)
                                    .arg(skipped)
                                    .arg(total_paragraphs));

    if (plan.isEmpty()) {
        m_ValidationResultsView->LoadResults(results);
        Utility::warning(this, tr("Sigil-Enhanced"),
                         tr("No auto-safe BR paragraph files were found. See Validation Results."));
        return false;
    }

    QMessageBox::StandardButton button_pressed;
    button_pressed = Utility::warning(
        this,
        tr("Sigil-Enhanced"),
        tr("Normalize BR paragraphs in %1 auto-safe XHTML files?\n\n"
           "%2 files require manual review and will be skipped. %3 non-candidate files will be skipped.\n\n"
           "Visible text, id/name, and href/src safety checks will be run for every changed file.\n\n"
           "Estimated paragraphs: %4")
            .arg(plan.count())
            .arg(manual_candidates)
            .arg(skipped)
            .arg(total_paragraphs),
        QMessageBox::Ok | QMessageBox::Cancel);
    if (button_pressed != QMessageBox::Ok) {
        m_ValidationResultsView->LoadResults(results);
        return false;
    }

    ShowMessageOnStatusBar(tr("Creating checkpoint before BR paragraph normalization..."));
    if (!RepoCommit()) {
        results << ValidationResult(ValidationResult::ResType_Error, QString(), -1, -1,
                                    tr("BR paragraph normalization cancelled: checkpoint failed. No XHTML files were changed."));
        m_ValidationResultsView->LoadResults(results);
        Utility::warning(this, tr("Sigil-Enhanced"),
                         tr("Checkpoint creation failed. BR paragraph normalization was cancelled."));
        return false;
    }
    results << ValidationResult(ValidationResult::ResType_Info, QString(), -1, -1,
                                tr("BR paragraph normalization: checkpoint saved before batch changes. Use Checkpoints to restore; batch resource writes are not available in Code View undo."));

    int changed = 0;
    int unchanged = 0;
    int failed = 0;
    ContentTab* current_tab = GetCurrentContentTab();
    Resource* current_resource = current_tab ? current_tab->GetLoadedResource() : nullptr;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    foreach(const PlanEntry& entry, plan) {
        HTMLResource* resource = entry.resource;
        if (!resource) {
            continue;
        }

        resource->InitialLoad();
        const BuiltinPlugins::BrParagraphNormalizer::NormalizeResult normalize_result =
            BuiltinPlugins::BrParagraphNormalizer::normalizeXhtmlText(resource->GetText());

        foreach(const QString& message, normalize_result.messages) {
            results << ValidationResult(normalize_result.ok ?
                                            ValidationResult::ResType_Info :
                                            ValidationResult::ResType_Error,
                                        entry.bookpath, -1, -1, message);
        }

        if (!normalize_result.ok) {
            failed++;
            continue;
        }

        if (!normalize_result.changed) {
            unchanged++;
            results << ValidationResult(ValidationResult::ResType_Info, entry.bookpath, -1, -1,
                                        tr("BR paragraph normalization: no changes needed."));
            continue;
        }

        {
            QWriteLocker locker(&resource->GetLock());
            resource->SetText(normalize_result.text);
        }
        if (current_resource == resource && current_tab) {
            current_tab->ContentChangedExternally();
        }

        changed++;
        results << ValidationResult(ValidationResult::ResType_Info, entry.bookpath, -1, -1,
                                    tr("BR paragraph normalization: XHTML file was updated."));
    }
    QApplication::restoreOverrideCursor();

    if (changed > 0 && m_Book) {
        m_Book->SetModified();
    }

    results << ValidationResult(failed > 0 ? ValidationResult::ResType_Warn : ValidationResult::ResType_Info,
                                QString(), -1, -1,
                                tr("BR paragraph normalization completed. Updated %1 files, left %2 unchanged, failed %3 files, skipped %4 manual-review candidates.")
                                    .arg(changed)
                                    .arg(unchanged)
                                    .arg(failed)
                                    .arg(manual_candidates));
    m_ValidationResultsView->LoadResults(results);
    ShowMessageOnStatusBar(changed > 0 ?
                           tr("BR paragraph normalization completed.") :
                           tr("No BR paragraph files were changed."));
    return failed == 0;
}

bool MainWindow::AnalyzeKfxParagraphs()
{
    SaveTabData();

    QApplication::setOverrideCursor(Qt::WaitCursor);
    QList<ValidationResult> results;
    int checked = 0;
    int auto_candidates = 0;
    int manual_candidates = 0;
    int skipped = 0;
    int total_paragraphs = 0;

    if (!m_Book || !m_Book->GetFolderKeeper()) {
        results << ValidationResult(ValidationResult::ResType_Error, QString(), -1, -1,
                                    tr("KFX paragraph analysis: no EPUB is currently loaded."));
        QApplication::restoreOverrideCursor();
        m_ValidationResultsView->LoadResults(results);
        return false;
    }

    const QList<HTMLResource*> html_resources =
        m_Book->GetFolderKeeper()->GetResourceTypeList<HTMLResource>(true);
    foreach(HTMLResource* resource, html_resources) {
        if (!resource) {
            continue;
        }

        checked++;
        resource->InitialLoad();
        const QString bookpath = resource->GetRelativePath();
        const BuiltinPlugins::KfxParagraphNormalizer::Analysis analysis =
            BuiltinPlugins::KfxParagraphNormalizer::analyzeXhtmlText(resource->GetText());

        if (analysis.safeToNormalize) {
            auto_candidates++;
            total_paragraphs += analysis.candidateParagraphs;
            results << ValidationResult(ValidationResult::ResType_Warn, bookpath, -1, -1, analysis.message);
        } else if (analysis.candidate) {
            manual_candidates++;
            results << ValidationResult(ValidationResult::ResType_Warn, bookpath, -1, -1, analysis.message);
        } else {
            skipped++;
            results << ValidationResult(ValidationResult::ResType_Info, bookpath, -1, -1, analysis.message);
        }
    }

    results << ValidationResult(ValidationResult::ResType_Info, QString(), -1, -1,
                                tr("KFX paragraph analysis completed. Checked %1 XHTML files, found %2 auto-safe candidate files, %3 manual-review candidate files, skipped %4 files, estimated %5 auto-safe paragraphs.")
                                    .arg(checked)
                                    .arg(auto_candidates)
                                    .arg(manual_candidates)
                                    .arg(skipped)
                                    .arg(total_paragraphs));
    QApplication::restoreOverrideCursor();

    m_ValidationResultsView->LoadResults(results);
    ShowMessageOnStatusBar((auto_candidates + manual_candidates) > 0 ?
                           tr("KFX paragraph candidates found. See Validation Results.") :
                           tr("No KFX paragraph candidates found."));
    return true;
}

bool MainWindow::NormalizeCurrentKfxParagraphs()
{
    SaveTabData();

    ContentTab* tab = GetCurrentContentTab();
    HTMLResource* resource = tab ? qobject_cast<HTMLResource*>(tab->GetLoadedResource()) : nullptr;
    QList<ValidationResult> results;

    if (!resource) {
        results << ValidationResult(ValidationResult::ResType_Error, QString(), -1, -1,
                                    tr("KFX paragraph normalization: current tab is not an XHTML resource."));
        m_ValidationResultsView->LoadResults(results);
        Utility::warning(this, tr("Sigil-Enhanced"), tr("The current tab is not an XHTML file."));
        return false;
    }

    resource->InitialLoad();
    const QString bookpath = resource->GetRelativePath();
    const BuiltinPlugins::KfxParagraphNormalizer::NormalizeResult normalize_result =
        BuiltinPlugins::KfxParagraphNormalizer::normalizeXhtmlText(resource->GetText(), true);

    results << ValidationResult((normalize_result.before.candidate || normalize_result.before.safeToNormalize) ?
                                    ValidationResult::ResType_Warn :
                                    ValidationResult::ResType_Info,
                                bookpath, -1, -1, normalize_result.before.message);
    foreach(const QString& message, normalize_result.messages) {
        results << ValidationResult(normalize_result.ok ?
                                        ValidationResult::ResType_Info :
                                        ValidationResult::ResType_Error,
                                    bookpath, -1, -1, message);
    }

    if (!normalize_result.before.candidate) {
        m_ValidationResultsView->LoadResults(results);
        Utility::warning(this, tr("Sigil-Enhanced"),
                         tr("The current XHTML file is not a KFX paragraph candidate. See Validation Results."));
        return false;
    }

    if (!normalize_result.ok) {
        m_ValidationResultsView->LoadResults(results);
        Utility::warning(this, tr("Sigil-Enhanced"),
                         tr("KFX paragraph normalization failed safety checks. See Validation Results."));
        return false;
    }

    if (!normalize_result.changed) {
        results << ValidationResult(ValidationResult::ResType_Info, bookpath, -1, -1,
                                    tr("KFX paragraph normalization: no changes needed."));
        m_ValidationResultsView->LoadResults(results);
        ShowMessageOnStatusBar(tr("No KFX paragraph changes needed."));
        return true;
    }

    const QString review_note = normalize_result.before.safeToNormalize ?
        tr("This file is an auto-safe KFX paragraph candidate.") :
        tr("This file requires manual review and is skipped by full-book normalization. Continue only if you inspected it.");

    QMessageBox::StandardButton button_pressed;
    button_pressed = Utility::warning(
        this,
        tr("Sigil-Enhanced"),
        tr("Normalize KFX paragraphs in the current XHTML file?\n\n"
           "%1\n\n"
           "This will wrap body-level raw text and inline/media runs into p elements, remove 0-height spacer p elements, "
           "and preserve non-zero spacer p elements for visual spacing. "
           "The result will be formatted once before writing. "
           "Safety checks have confirmed visible text, id/name, and href/src values are preserved.\n\n"
           "Estimated paragraphs: %2")
            .arg(review_note)
            .arg(normalize_result.before.candidateParagraphs),
        QMessageBox::Ok | QMessageBox::Cancel);
    if (button_pressed != QMessageBox::Ok) {
        m_ValidationResultsView->LoadResults(results);
        return false;
    }

    QString text_to_write = normalize_result.text;
    ApplyKfxPostFormat(resource, text_to_write, results, bookpath);

    FlowTab* flowtab = qobject_cast<FlowTab*>(tab);
    if (flowtab) {
        const int cursor_position = flowtab->GetCursorPosition();
        flowtab->ReplaceDocumentText(text_to_write);
        flowtab->ScrollToPosition(qMin(cursor_position, text_to_write.length()));
    } else {
        QWriteLocker locker(&resource->GetLock());
        resource->SetText(text_to_write);
        if (tab) {
            tab->ContentChangedExternally();
        }
    }
    if (m_Book) {
        m_Book->SetModified();
    }

    results << ValidationResult(ValidationResult::ResType_Info, bookpath, -1, -1,
                                tr("KFX paragraph normalization: current XHTML file was updated."));
    m_ValidationResultsView->LoadResults(results);
    ShowMessageOnStatusBar(tr("Current XHTML KFX paragraphs normalized."));
    return true;
}

bool MainWindow::NormalizeAllKfxParagraphs()
{
    SaveTabData();

    QList<ValidationResult> results;
    if (!m_Book || !m_Book->GetFolderKeeper()) {
        results << ValidationResult(ValidationResult::ResType_Error, QString(), -1, -1,
                                    tr("KFX paragraph normalization: no EPUB is currently loaded."));
        m_ValidationResultsView->LoadResults(results);
        return false;
    }

    struct PlanEntry {
        HTMLResource* resource = nullptr;
        QString bookpath;
        BuiltinPlugins::KfxParagraphNormalizer::Analysis analysis;
    };

    QList<PlanEntry> plan;
    int checked = 0;
    int manual_candidates = 0;
    int skipped = 0;
    int total_paragraphs = 0;
    int total_zero_spacers = 0;
    int total_gap_spacers = 0;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QList<HTMLResource*> html_resources =
        m_Book->GetFolderKeeper()->GetResourceTypeList<HTMLResource>(true);
    foreach(HTMLResource* resource, html_resources) {
        if (!resource) {
            continue;
        }

        checked++;
        resource->InitialLoad();
        const QString bookpath = resource->GetRelativePath();
        const BuiltinPlugins::KfxParagraphNormalizer::Analysis analysis =
            BuiltinPlugins::KfxParagraphNormalizer::analyzeXhtmlText(resource->GetText());

        if (analysis.safeToNormalize) {
            plan << PlanEntry{resource, bookpath, analysis};
            total_paragraphs += analysis.candidateParagraphs;
            total_zero_spacers += analysis.zeroHeightSpacers;
            total_gap_spacers += analysis.gapSpacers;
            results << ValidationResult(ValidationResult::ResType_Warn, bookpath, -1, -1, analysis.message);
        } else if (analysis.candidate) {
            manual_candidates++;
            results << ValidationResult(ValidationResult::ResType_Warn, bookpath, -1, -1, analysis.message);
        } else {
            skipped++;
            results << ValidationResult(ValidationResult::ResType_Info, bookpath, -1, -1, analysis.message);
        }
    }
    QApplication::restoreOverrideCursor();

    results << ValidationResult(ValidationResult::ResType_Info, QString(), -1, -1,
                                tr("KFX paragraph normalization dry-run completed. Checked %1 XHTML files, %2 files are auto-safe, %3 files require manual review, %4 files were skipped, estimated %5 auto-safe paragraphs. Will remove %6 0-height spacer p elements and preserve %7 spacing p elements.")
                                    .arg(checked)
                                    .arg(plan.count())
                                    .arg(manual_candidates)
                                    .arg(skipped)
                                    .arg(total_paragraphs)
                                    .arg(total_zero_spacers)
                                    .arg(total_gap_spacers));

    if (plan.isEmpty()) {
        m_ValidationResultsView->LoadResults(results);
        Utility::warning(this, tr("Sigil-Enhanced"),
                         tr("No auto-safe KFX paragraph files were found. See Validation Results."));
        return false;
    }

    QMessageBox::StandardButton button_pressed;
    button_pressed = Utility::warning(
        this,
        tr("Sigil-Enhanced"),
        tr("Normalize KFX paragraphs in %1 auto-safe XHTML files?\n\n"
           "%2 files require manual review and will be skipped. %3 non-candidate files will be skipped.\n\n"
           "The operation wraps body-level raw text into p elements, removes 0-height spacer p elements, "
           "preserves non-zero spacing p elements, formats each changed XHTML once, and runs safety checks for every changed file.\n\n"
           "Estimated paragraphs: %4\n"
           "0-height spacer p elements to remove: %5\n"
           "Spacing p elements to preserve: %6")
            .arg(plan.count())
            .arg(manual_candidates)
            .arg(skipped)
            .arg(total_paragraphs)
            .arg(total_zero_spacers)
            .arg(total_gap_spacers),
        QMessageBox::Ok | QMessageBox::Cancel);
    if (button_pressed != QMessageBox::Ok) {
        m_ValidationResultsView->LoadResults(results);
        return false;
    }

    ShowMessageOnStatusBar(tr("Creating checkpoint before KFX paragraph normalization..."));
    if (!RepoCommit()) {
        results << ValidationResult(ValidationResult::ResType_Error, QString(), -1, -1,
                                    tr("KFX paragraph normalization cancelled: checkpoint failed. No XHTML files were changed."));
        m_ValidationResultsView->LoadResults(results);
        Utility::warning(this, tr("Sigil-Enhanced"),
                         tr("Checkpoint creation failed. KFX paragraph normalization was cancelled."));
        return false;
    }
    results << ValidationResult(ValidationResult::ResType_Info, QString(), -1, -1,
                                tr("KFX paragraph normalization: checkpoint saved before batch changes. Use Checkpoints to restore; batch resource writes are not available in Code View undo."));

    int changed = 0;
    int unchanged = 0;
    int failed = 0;
    ContentTab* current_tab = GetCurrentContentTab();
    Resource* current_resource = current_tab ? current_tab->GetLoadedResource() : nullptr;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    foreach(const PlanEntry& entry, plan) {
        HTMLResource* resource = entry.resource;
        if (!resource) {
            continue;
        }

        resource->InitialLoad();
        const BuiltinPlugins::KfxParagraphNormalizer::NormalizeResult normalize_result =
            BuiltinPlugins::KfxParagraphNormalizer::normalizeXhtmlText(resource->GetText());

        foreach(const QString& message, normalize_result.messages) {
            results << ValidationResult(normalize_result.ok ?
                                            ValidationResult::ResType_Info :
                                            ValidationResult::ResType_Error,
                                        entry.bookpath, -1, -1, message);
        }

        if (!normalize_result.ok) {
            failed++;
            continue;
        }

        if (!normalize_result.changed) {
            unchanged++;
            results << ValidationResult(ValidationResult::ResType_Info, entry.bookpath, -1, -1,
                                        tr("KFX paragraph normalization: no changes needed."));
            continue;
        }

        QString text_to_write = normalize_result.text;
        ApplyKfxPostFormat(resource, text_to_write, results, entry.bookpath);

        {
            QWriteLocker locker(&resource->GetLock());
            resource->SetText(text_to_write);
        }
        if (current_resource == resource && current_tab) {
            current_tab->ContentChangedExternally();
        }

        changed++;
        results << ValidationResult(ValidationResult::ResType_Info, entry.bookpath, -1, -1,
                                    tr("KFX paragraph normalization: XHTML file was updated."));
    }
    QApplication::restoreOverrideCursor();

    if (changed > 0 && m_Book) {
        m_Book->SetModified();
    }

    results << ValidationResult(failed > 0 ? ValidationResult::ResType_Warn : ValidationResult::ResType_Info,
                                QString(), -1, -1,
                                tr("KFX paragraph normalization completed. Updated %1 files, left %2 unchanged, failed %3 files, skipped %4 manual-review candidates.")
                                    .arg(changed)
                                    .arg(unchanged)
                                    .arg(failed)
                                    .arg(manual_candidates));
    m_ValidationResultsView->LoadResults(results);
    ShowMessageOnStatusBar(changed > 0 ?
                           tr("KFX paragraph normalization completed.") :
                           tr("No KFX paragraph files were changed."));
    return failed == 0;
}

//modified: insertFileToEditor
void MainWindow::InsertFileFromBookBrowser()
{
    Resource* res = m_BookBrowser->GetCurrentResource();

    ContentTab* tab = GetCurrentContentTab();
    if (!res || !tab) {
        QMessageBox::warning(this, tr("Sigil"), tr("You cannot insert a file at this position."));
        return;
    }

    Resource* tab_res = tab->GetLoadedResource();
    ResourceInsertion::Context context;
    if (!ResourceInsertion::ContextFromTargetResource(tab_res, context) ||
        !ResourceInsertion::CanInsertResource(res, context)) {
        QMessageBox::warning(this, tr("Sigil"), tr("You cannot insert a file at this position."));
        return;
    }

    QString insert_text = ResourceInsertion::TextForResource(res, tab_res, context);
    if (insert_text.isEmpty()) {
        QMessageBox::warning(this, tr("Sigil"), tr("You cannot insert a file at this position."));
        return;
    }

    if (context == ResourceInsertion::Context::CSS) {
        CSSTab* csstab = qobject_cast<CSSTab*>(tab);
        if (csstab) {
            csstab->InsertFile(insert_text);
            return;
        }
    } else {
        FlowTab* flowtab = qobject_cast<FlowTab*>(tab);
        if (flowtab && flowtab->InsertFileEnabled()) {
            flowtab->InsertFile(insert_text);
            return;
        }
    }

    QMessageBox::warning(this, tr("Sigil"), tr("You cannot insert a file at this position."));
}

//modified: Add Lables On Multiple Lines
void MainWindow::ApplyHeadingStyleToTab_Plus(QAction* act)
{
    FlowTab* flow_tab = GetCurrentFlowTab();

    QString heading_type;
    QString name = act->objectName();
    if (name == "actionHeadingNormal") {
        heading_type = "Normal";
    }
    else if (name == "actionHeadingDivision") {
        heading_type = "Division";
    }
    else {
        heading_type = name[name.length() - 1];
    }

    if (flow_tab) {
        flow_tab->HeadingStylePlus(heading_type, m_preserveHeadingAttributes);
    }
}

//modified: FindReplacePlus
MainWindow::FindReplaceMode MainWindow::GetFindReplaceMode()
{
    return m_findReplaceMode;
}

//modified: FindReplacePlus
QList<SearchEditorModelPlus::searchEntry*> MainWindow::SearchEditorGetCurrentEntriesPlus()
{
    return m_SearchEditorPlus->GetCurrentEntries();
}

//modified: FindReplacePlus
void MainWindow::SearchEditorRecordEntryAsCompletedPlus(SearchEditorModelPlus::searchEntry* entry)
{
    m_SearchEditorPlus->RecordEntryAsCompleted(entry);
}

//modified: FindReplacePlus
void MainWindow::changeFindReplaceMode()
{
    m_findReplaceMode = m_findReplaceMode == EnhancedMode ? OriginalMode : EnhancedMode;

    if (m_findReplaceMode == FindReplaceMode::EnhancedMode) {
        bool isShowed = m_FindReplace->isVisible();
        m_FindReplace->HideFindReplace();
        if (isShowed)
            m_FindReplacePlus->show();
        delete m_SearchEditor;
        m_SearchEditorPlus = new SearchEditorPlus(this);
        ConnectSignalsToSearchEditor();
        ConnectSignalsToFindReplace();
    }
    else if (m_findReplaceMode == FindReplaceMode::OriginalMode) {
        bool isShowed = m_FindReplacePlus->isVisible();
        m_FindReplacePlus->HideFindReplace();
        if (isShowed)
            m_FindReplace->show();
        delete m_SearchEditorPlus;
        m_SearchEditor = new SearchEditor(this);
        ConnectSignalsToSearchEditor();
        ConnectSignalsToFindReplace();
    }
}

//modified: SavedSearchPlus
void MainWindow::SearchEditorDialogPlus(SearchEditorModelPlus::searchEntry* search_entry)
{
    // non-modal dialog
    m_SearchEditorPlus->show();
    m_SearchEditorPlus->raise();
    m_SearchEditorPlus->activateWindow();

    if (search_entry) {
        m_SearchEditorPlus->AddEntry(search_entry->is_group, search_entry, false);
    }
}

void MainWindow::ConnectSignalsToSearchEditor()
{
    QObject* findReplace;
    QObject* searchEditor;
    if (m_findReplaceMode == FindReplaceMode::EnhancedMode) {
        findReplace = qobject_cast<QObject*>(m_FindReplacePlus);
        searchEditor = qobject_cast<QObject*>(m_SearchEditorPlus);
        connect(findReplace,SIGNAL(AskWhyGetEmptyEntries()), m_SearchEditorPlus,SLOT(WhyEntriesEmpty()));
        disconnect(ui.actionSearchEditor, SIGNAL(triggered()), this, SLOT(SearchEditorDialog()));
        connect(ui.actionSearchEditor, SIGNAL(triggered()), this, SLOT(SearchEditorDialogPlus()));
        connect(m_SearchEditorPlus, SIGNAL(LoadSelectedSearchRequest(SearchEditorModelPlus::searchEntry*)),
                m_FindReplacePlus, SLOT(LoadSearch(SearchEditorModelPlus::searchEntry*)));
    } else {
        findReplace = qobject_cast<QObject*>(m_FindReplace);
        searchEditor = qobject_cast<QObject*>(m_SearchEditor);
        connect(m_SearchEditor, SIGNAL(CountsReportCountRequest(SearchEditorModel::searchEntry*, int&)),
                findReplace, SLOT(CountsReportCount(SearchEditorModel::searchEntry*, int&)));
        disconnect(ui.actionSearchEditor, SIGNAL(triggered()), this, SLOT(SearchEditorDialogPlus()));
        connect(ui.actionSearchEditor, SIGNAL(triggered()), this, SLOT(SearchEditorDialog()));
        connect(m_SearchEditor, SIGNAL(LoadSelectedSearchRequest(SearchEditorModel::searchEntry*)),
                m_FindReplace, SLOT(LoadSearch(SearchEditorModel::searchEntry*)));
    }
    connect(findReplace, SIGNAL(ShowMessageRequest(const QString&)), searchEditor, SLOT(ShowMessage(const QString&)));
    connect(searchEditor, SIGNAL(ShowStatusMessageRequest(const QString&)), this, SLOT(ShowMessageOnStatusBar(const QString&)));
    connect(searchEditor, SIGNAL(FindSelectedSearchRequest()), findReplace, SLOT(FindSearch()));
    connect(searchEditor, SIGNAL(ReplaceCurrentSelectedSearchRequest()), findReplace, SLOT(ReplaceCurrentSearch()));
    connect(searchEditor, SIGNAL(ReplaceSelectedSearchRequest()), findReplace, SLOT(ReplaceSearch()));
    connect(searchEditor, SIGNAL(CountAllSelectedSearchRequest()), findReplace, SLOT(CountAllSearch()));
    connect(searchEditor, SIGNAL(ReplaceAllSelectedSearchRequest()), findReplace, SLOT(ReplaceAllSearch()));
    connect(searchEditor, SIGNAL(RestartSearch()), findReplace, SLOT(DoRestart()));

}

void MainWindow::ConnectSignalsToFindReplace()
{
    QObject* findReplace;
    QObject* hiddenFindReplace;
    if (m_findReplaceMode == FindReplaceMode::EnhancedMode) {
        findReplace = qobject_cast<QObject*>(m_FindReplacePlus);
        hiddenFindReplace = qobject_cast<QObject*>(m_FindReplace);
        connect(this, SIGNAL(UpdateSearchStateRequest()), m_FindReplacePlus, SLOT(DoRestart()));
        connect(m_FindReplacePlus, SIGNAL(OpenSearchEditorRequest(SearchEditorModelPlus::searchEntry*)),
                this, SLOT(SearchEditorDialogPlus(SearchEditorModelPlus::searchEntry*)));
        disconnect(m_FindReplace, SIGNAL(OpenSearchEditorRequest(SearchEditorModel::searchEntry*)),
                this, SLOT(SearchEditorDialog(SearchEditorModel::searchEntry*)));
    }
    else {
        findReplace = qobject_cast<QObject*>(m_FindReplace);
        hiddenFindReplace = qobject_cast<QObject*>(m_FindReplacePlus);
        disconnect(this, SIGNAL(UpdateSearchStateRequest()), m_FindReplacePlus, SLOT(DoRestart()));
        disconnect(m_FindReplacePlus, SIGNAL(OpenSearchEditorRequest(SearchEditorModelPlus::searchEntry*)),
                   this, SLOT(SearchEditorDialogPlus(SearchEditorModelPlus::searchEntry*)));
        connect(m_FindReplace, SIGNAL(OpenSearchEditorRequest(SearchEditorModel::searchEntry*)),
                   this, SLOT(SearchEditorDialog(SearchEditorModel::searchEntry*)));
    }

    disconnect(ui.actionFind, SIGNAL(triggered()), this, SLOT(Find()));
    disconnect(ui.actionFindNext, SIGNAL(triggered()), hiddenFindReplace, SLOT(DoFindNext()));
    disconnect(ui.actionFindPrevious, SIGNAL(triggered()), hiddenFindReplace, SLOT(DoFindPrevious()));
    disconnect(ui.actionReplaceNext, SIGNAL(triggered()), hiddenFindReplace, SLOT(DoReplaceNext()));
    disconnect(ui.actionReplacePrevious, SIGNAL(triggered()), hiddenFindReplace, SLOT(DoReplacePrevious()));
    disconnect(ui.actionReplaceCurrent, SIGNAL(triggered()), hiddenFindReplace, SLOT(ReplaceCurrent()));
    disconnect(ui.actionReplaceAll, SIGNAL(triggered()), hiddenFindReplace, SLOT(ReplaceAll()));
    disconnect(ui.actionCount, SIGNAL(triggered()), hiddenFindReplace, SLOT(Count()));
    disconnect(ui.actionDryRun, SIGNAL(triggered()), hiddenFindReplace, SLOT(PerformDryRunReplace()));
    disconnect(ui.actionFilterReplaceAll, SIGNAL(triggered()), hiddenFindReplace, SLOT(ChooseReplacements()));
    disconnect(ui.actionFindNextInFile, SIGNAL(triggered()), hiddenFindReplace, SLOT(FindNextInFile()));
    disconnect(ui.actionReplaceNextInFile, SIGNAL(triggered()), hiddenFindReplace, SLOT(ReplaceNextInFile()));
    disconnect(ui.actionReplaceAllInFile, SIGNAL(triggered()), hiddenFindReplace, SLOT(ReplaceAllInFile()));
    disconnect(ui.actionCountInFile, SIGNAL(triggered()), hiddenFindReplace, SLOT(CountInFile()));
    disconnect(hiddenFindReplace, SIGNAL(FROpenFileRequest(QString, int, int)), this, SLOT(OpenFile(QString, int, int)));
    disconnect(hiddenFindReplace, SIGNAL(ClipboardSaveRequest()), m_ClipboardHistorySelector, SLOT(SaveClipboardState()));
    disconnect(hiddenFindReplace, SIGNAL(ClipboardRestoreRequest()), m_ClipboardHistorySelector, SLOT(RestoreClipboardState()));

    connect(ui.actionFind, SIGNAL(triggered()), this, SLOT(Find()));
    connect(ui.actionFindNext, SIGNAL(triggered()), findReplace, SLOT(DoFindNext()));
    connect(ui.actionFindPrevious, SIGNAL(triggered()), findReplace, SLOT(DoFindPrevious()));
    connect(ui.actionReplaceNext, SIGNAL(triggered()), findReplace, SLOT(DoReplaceNext()));
    connect(ui.actionReplacePrevious, SIGNAL(triggered()), findReplace, SLOT(DoReplacePrevious()));
    connect(ui.actionReplaceCurrent, SIGNAL(triggered()), findReplace, SLOT(ReplaceCurrent()));
    connect(ui.actionReplaceAll, SIGNAL(triggered()), findReplace, SLOT(ReplaceAll()));
    connect(ui.actionCount, SIGNAL(triggered()), findReplace, SLOT(Count()));
    connect(ui.actionDryRun, SIGNAL(triggered()), findReplace, SLOT(PerformDryRunReplace()));
    connect(ui.actionFilterReplaceAll, SIGNAL(triggered()), findReplace, SLOT(ChooseReplacements()));
    connect(ui.actionFindNextInFile, SIGNAL(triggered()), findReplace, SLOT(FindNextInFile()));
    connect(ui.actionReplaceNextInFile, SIGNAL(triggered()), findReplace, SLOT(ReplaceNextInFile()));
    connect(ui.actionReplaceAllInFile, SIGNAL(triggered()), findReplace, SLOT(ReplaceAllInFile()));
    connect(ui.actionCountInFile, SIGNAL(triggered()), findReplace, SLOT(CountInFile()));
    connect(findReplace, SIGNAL(FROpenFileRequest(QString, int, int)), this, SLOT(OpenFile(QString, int, int)));
    connect(findReplace, SIGNAL(ClipboardSaveRequest()), m_ClipboardHistorySelector, SLOT(SaveClipboardState()));
    connect(findReplace, SIGNAL(ClipboardRestoreRequest()), m_ClipboardHistorySelector, SLOT(RestoreClipboardState()));
}
