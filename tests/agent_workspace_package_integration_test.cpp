#include "EmbedPython/EmbeddedPython.h" // Python must precede Qt's slots macro.

#include <QFileInfo>
#include <QRegularExpression>
#include <QLabel>
#include <QToolButton>
#include <QWebEngineSettings>
#include <QWebEngineUrlScheme>
#include <QXmlStreamReader>
#include <iostream>
#include <stdexcept>

#include "Agent/Execution/SigilBookWorkspace.h"
#include "Agent/Core/PromptAssembler.h"
#include "Agent/UI/AgentDock.h"
#include "BookManipulation/Book.h"
#include "BookManipulation/FolderKeeper.h"
#include "BookManipulation/TocTreeTransform.h"
#include "MainUI/MainApplication.h"
#include "MainUI/MainWindow.h"
#include "Misc/SettingsStore.h"
#include "Misc/WebProfileMgr.h"
#include "ResourceObjects/HTMLResource.h"
#include "ResourceObjects/OPFResource.h"
#include "Tabs/ContentTab.h"

static void Require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

static QString WithoutSpine(QString source)
{
    static const QRegularExpression spine(
        QStringLiteral("<spine\\b[^>]*>[\\s\\S]*?</spine>"));
    const QRegularExpressionMatch match = spine.match(source);
    Require(match.hasMatch(), "Package source has no spine element");
    source.replace(match.capturedStart(), match.capturedLength(), QStringLiteral("<spine-test-placeholder/>"));
    return source;
}

int main(int argc, char **argv)
{
    QCoreApplication::setAttribute(Qt::AA_DisableShaderDiskCache);
    QWebEngineUrlScheme scheme("sigil");
    scheme.setSyntax(QWebEngineUrlScheme::Syntax::Path);
    scheme.setFlags(QWebEngineUrlScheme::SecureScheme | QWebEngineUrlScheme::LocalScheme
                    | QWebEngineUrlScheme::LocalAccessAllowed
                    | QWebEngineUrlScheme::ContentSecurityPolicyIgnored
                    | QWebEngineUrlScheme::FetchApiAllowed);
    QWebEngineUrlScheme::registerScheme(scheme);
    MainApplication app(argc, argv);
    try {
        Require(argc == 3, "Expected source root and EPUB fixture");
        const QString root = QString::fromLocal8Bit(argv[1]);
        auto &python = EmbeddedPython::instance();
        python.addToPythonSysPath(qEnvironmentVariable("SIGIL_TEST_PYTHON_ROOT"));
        python.addToPythonSysPath(root + "/src/Resource_Files/plugin_launchers/python");
        python.addToPythonSysPath(root + "/src/Resource_Files/python3lib");
        SettingsStore settings;
        settings.setPreserveOPFSource(true);
        WebProfileMgr::instance();

        MainWindow window(QString::fromLocal8Bit(argv[2]));
        const QSharedPointer<Book> book = window.GetCurrentBook();
        OPFResource *opf = book->GetOPF();
        auto *chapter = qobject_cast<HTMLResource *>(
            book->GetFolderKeeper()->GetResourceByBookPath("OEBPS/a.xhtml"));
        auto *nav = qobject_cast<HTMLResource *>(
            book->GetFolderKeeper()->GetResourceByBookPath("OEBPS/nav.xhtml"));
        Require(opf && chapter && nav, "Fixture package resources are missing");

        auto *agentDock = window.findChild<SigilAgent::AgentDock *>(
            QStringLiteral("agentDock"));
        auto *bookStatus = agentDock
            ? agentDock->findChild<QLabel *>(QStringLiteral("agentBookStatus"))
            : nullptr;
        Require(agentDock && bookStatus, "Native Agent book status is missing");
        const int resourceCount = book->GetFolderKeeper()->GetResourceList().size();
        Require(bookStatus->property("resourceCount").toInt() == resourceCount
                    && bookStatus->property("bookFileName").toString()
                        == QFileInfo(QString::fromLocal8Bit(argv[2])).fileName()
                    && !bookStatus->property("bookSessionId").toString().isEmpty()
                    && !bookStatus->property("modified").toBool(),
                "Agent book status does not identify the live imported book session");
        book->SetModified(true);
        app.processEvents();
        Require(bookStatus->property("modified").toBool(),
                "Agent book status did not report unsaved changes");
        book->SetModified(false);
        app.processEvents();
        Require(!bookStatus->property("modified").toBool(),
                "Agent book status did not refresh after saving");

        ContentTab *activeTab = window.GetCurrentContentTab();
        Require(activeTab && activeTab->GetLoadedResource()
                    && activeTab->SetSelectionRange(1, 12),
                "Could not create a live editor selection for Agent context");
        app.processEvents();
        auto *selectionChip = agentDock->findChild<QToolButton *>(
            QStringLiteral("agentChipSelection"));
        const QString expectedHandle = QStringLiteral("%1:1-12")
            .arg(activeTab->GetLoadedResource()->GetIdentifier());
        Require(selectionChip && selectionChip->isEnabled()
                    && selectionChip->property("selectionStart").toInt() == 1
                    && selectionChip->property("selectionEnd").toInt() == 12
                    && agentDock->contextHandles().contains(expectedHandle),
                "Live editor selection did not reach the Native Agent context chip");

        SigilAgent::SigilBookWorkspace workspace;
        workspace.setBook(book);
        const QString firstBookSession = workspace.bookSessionId();
        const QJsonObject firstSummary = workspace.summary();
        Require(!firstBookSession.isEmpty()
                    && firstSummary.value(
                           QStringLiteral("book_session_id")).toString()
                        == firstBookSession
                    && firstSummary.value(QStringLiteral("spine_count")).toInt()
                        == workspace.spine().size()
                    && firstSummary.value(QStringLiteral("toc_count")).toInt()
                        == workspace.toc().size()
                    && firstSummary.value(QStringLiteral("title_length")).toInt()
                        == firstSummary.value(QStringLiteral("title")).toString().size()
                    && !firstSummary.value(
                           QStringLiteral("title_truncated")).toBool()
                    && firstSummary.value(QStringLiteral("language_length")).toInt()
                        == firstSummary.value(QStringLiteral("language")).toString().size()
                    && !firstSummary.value(
                           QStringLiteral("language_truncated")).toBool(),
                "Sigil workspace summary did not expose bounded identity and direct counts");
        workspace.setBook(book);
        Require(workspace.bookSessionId() != firstBookSession,
                "rebinding a Sigil workspace must invalidate plans for its previous book session");
        const SigilAgent::BookOpResult firstFragment = workspace.readFragment(
            chapter->GetIdentifier(), 0, 1);
        Require(firstFragment.ok
                    && firstFragment.data.value(QStringLiteral("limit")).toInt() == 1
                    && firstFragment.data.value(QStringLiteral("length")).toInt() == 1
                    && firstFragment.data.value(QStringLiteral("truncated")).toBool()
                    && firstFragment.data.value(QStringLiteral("continuation")).toInt() == 1,
                "Sigil fragment reads did not expose their exact continuation");
        const SigilAgent::BookOpResult pastEndFragment = workspace.readFragment(
            chapter->GetIdentifier(), 999999, 999999);
        Require(pastEndFragment.ok
                    && pastEndFragment.data.value(QStringLiteral("offset")).toInt()
                        == pastEndFragment.data.value(QStringLiteral("total")).toInt()
                    && pastEndFragment.data.value(QStringLiteral("end")).toInt()
                        == pastEndFragment.data.value(QStringLiteral("total")).toInt()
                    && pastEndFragment.data.value(QStringLiteral("limit")).toInt() == 8192
                    && pastEndFragment.data.value(QStringLiteral("length")).toInt() == 0
                    && !pastEndFragment.data.value(QStringLiteral("truncated")).toBool(),
                "Sigil fragment offsets past EOF did not normalize to an empty tail");
        window.SelectResources(QList<Resource *> { chapter, nav });
        app.processEvents();
        auto *selectedFilesChip = agentDock->findChild<QToolButton *>(
            QStringLiteral("agentChipSelectedFiles"));
        Require(selectedFilesChip && selectedFilesChip->isEnabled()
                    && selectedFilesChip->property("resourceIds").toStringList()
                        == QStringList { chapter->GetIdentifier(), nav->GetIdentifier() },
                "Book Browser multi-selection did not reach the Agent scope");
        selectedFilesChip->click();
        const QStringList selectedHandles = agentDock->contextHandles();
        Require(selectedHandles
                    == QStringList { chapter->GetIdentifier(), nav->GetIdentifier() },
                "Agent selected-files scope did not emit the selected resource ids exclusively");
        const QString selectedContext = SigilAgent::PromptAssembler().contextBlock(
            &workspace, selectedHandles);
        Require(selectedContext.contains(QStringLiteral("Original paragraph"))
                    && selectedContext.contains(QStringLiteral("epub:type=\"toc\""))
                    && !selectedContext.contains(QStringLiteral("Resources:\n")),
                "selected-files context did not read current in-memory sources exclusively");
        const QString original = opf->GetSourceText();
        Require(workspace.beginTransaction(QStringLiteral("metadata staged view")).ok,
                "Could not begin staged metadata view transaction");
        Require(workspace.updateMetadata(QJsonObject {
            { QStringLiteral("title"), QStringLiteral("Staged metadata title") },
            { QStringLiteral("creator"), QStringLiteral("Staged creator") },
            { QStringLiteral("_remove"), QJsonArray {
                QStringLiteral("language")
            } }
        }).ok, "Could not stage metadata view update");
        const QJsonObject stagedMetadata = workspace.metadata();
        bool stagedTitleEntry = false;
        bool stagedCreatorEntry = false;
        bool stagedLanguageEntry = false;
        for (const QJsonValue &value : stagedMetadata.value(
                 QStringLiteral("entries")).toArray()) {
            const QJsonObject entry = value.toObject();
            const QString name = entry.value(QStringLiteral("name")).toString();
            if (name.endsWith(QLatin1String("title"))) {
                stagedTitleEntry = stagedTitleEntry
                    || entry.value(QStringLiteral("content")).toString()
                        == QStringLiteral("Staged metadata title");
            } else if (name.endsWith(QLatin1String("creator"))) {
                stagedCreatorEntry = stagedCreatorEntry
                    || entry.value(QStringLiteral("content")).toString()
                        == QStringLiteral("Staged creator");
            } else if (name.endsWith(QLatin1String("language"))) {
                stagedLanguageEntry = true;
            }
        }
        Require(stagedMetadata.value(QStringLiteral("title")).toString()
                    == QStringLiteral("Staged metadata title")
                    && stagedMetadata.value(QStringLiteral("creator")).toString()
                        == QStringLiteral("Staged creator")
                    && !stagedMetadata.contains(QStringLiteral("language"))
                    && stagedTitleEntry && stagedCreatorEntry && !stagedLanguageEntry,
                "Sigil metadata reads did not expose the effective staged values and removals");
        Require(workspace.rollbackTransaction().ok,
                "Could not roll back staged metadata view transaction");
        const quint64 metadataRevision = workspace.revision();
        Require(workspace.beginTransaction(QStringLiteral("metadata source preservation")).ok,
                "Could not begin metadata transaction");
        Require(workspace.updateMetadata(QJsonObject {
            { QStringLiteral("title"), QStringLiteral("Agent title 日本 & more") }
        }).ok, "Could not stage metadata update");
        Require(workspace.previewTransaction().ok, "Could not preview metadata update");
        Require(opf->GetSourceText() == original, "Metadata preview mutated live package source");
        const SigilAgent::BookOpResult metadataCommit = workspace.commitTransaction(metadataRevision);
        Require(metadataCommit.ok && metadataCommit.applied, "Could not commit metadata update");
        const QString metadataExpected = QString(original).replace(
            ">Source regression<", ">Agent title 日本 &amp; more<");
        Require(opf->GetSourceText() == metadataExpected,
                "Agent metadata commit changed unrelated package source");

        const quint64 spineRevision = workspace.revision();
        Require(workspace.beginTransaction(QStringLiteral("spine source preservation")).ok,
                "Could not begin spine transaction");
        Require(workspace.updateSpine(QStringList {
            chapter->GetIdentifier(), nav->GetIdentifier()
        }).ok, "Could not stage spine update");
        Require(workspace.previewTransaction().ok, "Could not preview spine update");
        Require(opf->GetSourceText() == metadataExpected, "Spine preview mutated live package source");
        const SigilAgent::BookOpResult spineCommit = workspace.commitTransaction(spineRevision);
        Require(spineCommit.ok && spineCommit.applied, "Could not commit spine update");
        const QString withSpine = opf->GetSourceText();
        Require(WithoutSpine(withSpine) == WithoutSpine(metadataExpected),
                "Agent spine commit changed source outside the spine element");
        Require(withSpine.contains(QStringLiteral("idref=\"nav\""))
                    || withSpine.contains(QStringLiteral("idref='nav'")),
                "Agent spine commit did not add the requested navigation itemref");
        Require(withSpine.contains(QStringLiteral("<!-- keep source e\u0301 𠮷 -->"))
                    && withSpine.contains(QStringLiteral("<x:extension")),
                "Agent spine commit removed preserved comments or private extensions");

        const quint64 stalePackageRevision = workspace.revision();
        Require(workspace.beginTransaction(QStringLiteral("stale package plan")).ok,
                "Could not begin stale package transaction");
        Require(workspace.updateMetadata(QJsonObject {
            { QStringLiteral("title"), QStringLiteral("Stale agent title") }
        }).ok, "Could not stage stale metadata update");
        Require(workspace.previewTransaction().ok, "Could not preview stale package update");
        opf->SetText(QString(opf->GetSourceText()).replace(
            "Agent title 日本 &amp; more", "Host package edit"));
        const SigilAgent::BookOpResult packageConflict =
            workspace.commitTransaction(stalePackageRevision);
        Require(!packageConflict.ok
                    && packageConflict.code == QStringLiteral("BOOK_REVISION_CONFLICT")
                    && packageConflict.data.value(QStringLiteral("reason")).toString()
                        == QStringLiteral("package_source_changed"),
                "Agent accepted a stale package plan");
        Require(opf->GetSourceText().contains(QStringLiteral(">Host package edit<"))
                    && !opf->GetSourceText().contains(QStringLiteral("Stale agent title")),
                "Rejected package plan overwrote the host edit");
        Require(workspace.rollbackTransaction().ok, "Could not roll back stale package plan");

        chapter->SetText(QString());
        const quint64 emptyRevision = workspace.resourceRevision(chapter->GetIdentifier());
        chapter->SetText(QStringLiteral("host populated an empty resource"));
        Require(workspace.resourceRevision(chapter->GetIdentifier()) > emptyRevision,
                "Empty source initialization hid a live resource revision change");

        const QString chapterBase = QStringLiteral(
            "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>Base</title></head>"
            "<body><p>Agent base</p></body></html>");
        chapter->SetText(chapterBase);
        const quint64 chapterRevision = workspace.resourceRevision(chapter->GetIdentifier());
        const quint64 staleTextBookRevision = workspace.revision();
        Require(workspace.beginTransaction(QStringLiteral("stale text plan")).ok,
                "Could not begin stale text transaction");
        Require(workspace.replaceText(
            chapter->GetIdentifier(), QString(chapterBase).replace("Agent base", "Stale agent body"),
            chapterRevision).ok, "Could not stage text update");
        Require(workspace.previewTransaction().ok, "Could not preview stale text update");
        const QString hostText = QString(chapterBase).replace("Agent base", "Host body edit");
        chapter->SetText(hostText);
        const SigilAgent::BookOpResult textConflict =
            workspace.commitTransaction(staleTextBookRevision);
        Require(!textConflict.ok
                    && textConflict.code == QStringLiteral("BOOK_REVISION_CONFLICT")
                    && textConflict.data.value(QStringLiteral("reason")).toString()
                        == QStringLiteral("resource_source_changed"),
                "Agent accepted a stale text plan");
        Require(chapter->GetText() == hostText, "Rejected text plan overwrote the host edit");
        Require(workspace.rollbackTransaction().ok, "Could not roll back stale text plan");

        const QString taskChapterBefore = chapter->GetText();
        const QString taskNavBefore = nav->GetText();
        const quint64 taskChapterRevision =
            workspace.resourceRevision(chapter->GetIdentifier());
        const quint64 taskNavRevision = workspace.resourceRevision(nav->GetIdentifier());
        Require(workspace.beginTransaction(QStringLiteral("guarded task restore")).ok,
                "Could not begin guarded task transaction");
        Require(workspace.replaceText(
                    chapter->GetIdentifier(),
                    taskChapterBefore + QStringLiteral("\n<!-- agent chapter task -->"),
                    taskChapterRevision).ok
                    && workspace.replaceText(
                        nav->GetIdentifier(),
                        taskNavBefore + QStringLiteral("\n<!-- agent nav task -->"),
                        taskNavRevision).ok,
                "Could not stage guarded task text edits");
        const SigilAgent::BookOpResult taskPoint = workspace.createTaskRestorePoint(
            QStringLiteral("guarded task restore"),
            QStringList { chapter->GetIdentifier(), nav->GetIdentifier() });
        const QString taskPointId = taskPoint.data
            .value(QStringLiteral("checkpoint_id")).toString();
        Require(taskPoint.ok && !taskPointId.isEmpty(),
                "Could not create a guarded task restore point");
        Require(workspace.commitTransaction(workspace.revision()).applied
                    && workspace.sealTaskRestorePoint(taskPointId).ok,
                "Could not commit and seal the guarded task restore point");
        const QString taskChapterAfter = chapter->GetText();
        const QString taskNavAfter = nav->GetText();
        Require(!workspace.restoreCheckpoint(taskPointId).ok,
                "Generic checkpoint restore bypassed the guarded task path");
        chapter->SetText(taskChapterAfter + QStringLiteral("\n<!-- later host edit -->"));
        const QString laterHostChapter = chapter->GetText();
        const SigilAgent::BookOpResult taskConflict =
            workspace.restoreTaskRestorePoint(taskPointId);
        Require(!taskConflict.ok
                    && taskConflict.code == QStringLiteral("TASK_RESTORE_CONFLICT")
                    && taskConflict.data.value(QStringLiteral("live_book_unchanged")).toBool()
                    && chapter->GetText() == laterHostChapter
                    && nav->GetText() == taskNavAfter,
                "Task recovery overwrote a later host edit or partially restored the Book");
        chapter->SetText(taskChapterAfter);
        const SigilAgent::BookOpResult taskRestored =
            workspace.restoreTaskRestorePoint(taskPointId);
        Require(taskRestored.ok && taskRestored.applied
                    && chapter->GetText() == taskChapterBefore
                    && nav->GetText() == taskNavBefore,
                "Guarded task recovery did not restore both reviewed text resources");

        nav->SetText(QStringLiteral(
            "<!DOCTYPE html><html xmlns=\"http://www.w3.org/1999/xhtml\" "
            "xmlns:epub=\"http://www.idpf.org/2007/ops\"><head><title>Contents</title></head>"
            "<body><nav epub:type=\"toc\"><ol>"
            "<li id=\"keep-a\"><a href=\"a.xhtml\"><span>Part A</span></a><ol>"
            "<li data-keep=\"child\"><a href=\"a.xhtml#child\">Child</a></li>"
            "</ol></li><li><a href=\"a.xhtml#next\">Part B</a></li>"
            "</ol></nav><nav epub:type=\"landmarks\"><ol>"
            "<li><a epub:type=\"bodymatter\" href=\"a.xhtml\">Start</a></li>"
            "</ol></nav></body></html>"));
        const QJsonArray tocHierarchy = workspace.toc();
        Require(tocHierarchy.size() == 3,
                "Agent TOC inspection included landmarks or omitted TOC entries");
        Require(tocHierarchy.at(0).toObject().value(QStringLiteral("label")).toString()
                    == QStringLiteral("Part A")
                    && tocHierarchy.at(0).toObject().value(QStringLiteral("level")).toInt() == 1
                    && tocHierarchy.at(1).toObject().value(QStringLiteral("label")).toString()
                    == QStringLiteral("Child")
                    && tocHierarchy.at(1).toObject().value(QStringLiteral("level")).toInt() == 2
                    && tocHierarchy.at(2).toObject().value(QStringLiteral("level")).toInt() == 1,
                "Agent workspace flattened the EPUB 3 Nav hierarchy");

        const QString navBeforeHierarchyCommit = nav->GetText();
        const TocEditTree beforeHierarchy = workspace.tocHierarchy();
        Require(TocTreeTransform::Validate(beforeHierarchy)
                    && TocTreeTransform::PreorderIds(beforeHierarchy)
                        == QList<TocNodeId>({1, 2, 3}),
                "Agent workspace did not expose a valid stable native TOC tree");
        const TocTransformResult promoted = TocTreeTransform::Promote(
            beforeHierarchy, QList<TocNodeId>({2}));
        Require(promoted.succeeded() && promoted.preorderPreserved,
                "Could not prepare the native TOC promotion fixture");
        const quint64 tocRevision = workspace.revision();
        Require(workspace.beginTransaction(
                    QStringLiteral("native TOC source preservation")).ok,
                "Could not begin native TOC hierarchy transaction");
        const SigilAgent::BookOpResult tocStaged = workspace.updateTocHierarchy(
            beforeHierarchy, promoted.tree);
        Require(tocStaged.ok && tocStaged.previewOnly && !tocStaged.applied,
                "Could not stage native TOC hierarchy transform");
        const SigilAgent::BookOpResult tocPreview = workspace.previewTransaction();
        Require(tocPreview.ok && tocPreview.previewOnly
                    && tocPreview.data.value(QStringLiteral("toc_changed")).toBool()
                    && nav->GetText() == navBeforeHierarchyCommit,
                "Native TOC preview mutated the live Nav or omitted its change");
        const SigilAgent::BookOpResult tocCommit =
            workspace.commitTransaction(tocRevision);
        Require(tocCommit.ok && tocCommit.applied,
                "Could not commit native TOC hierarchy transform");
        const QString navAfterHierarchyCommit = nav->GetText();
        QXmlStreamReader navXml(navAfterHierarchyCommit);
        while (!navXml.atEnd()) navXml.readNext();
        const QJsonArray promotedHierarchy = workspace.toc();
        Require(promotedHierarchy.size() == 3
                    && promotedHierarchy.at(0).toObject().value(
                        QStringLiteral("level")).toInt() == 1
                    && promotedHierarchy.at(1).toObject().value(
                        QStringLiteral("level")).toInt() == 1
                    && promotedHierarchy.at(2).toObject().value(
                        QStringLiteral("level")).toInt() == 1,
                "Native TOC commit did not apply the reviewed promotion");
        Require(navAfterHierarchyCommit.contains(QStringLiteral("id=\"keep-a\""))
                    && navAfterHierarchyCommit.contains(
                        QStringLiteral("data-keep=\"child\""))
                    && navAfterHierarchyCommit.contains(
                        QStringLiteral("<span>Part A</span>"))
                    && navAfterHierarchyCommit.contains(
                        QStringLiteral("epub:type=\"landmarks\""))
                    && navAfterHierarchyCommit.contains(
                        QStringLiteral("epub:type=\"bodymatter\"")),
                "Native TOC commit lost Nav attributes, inline markup, or landmarks");
        Require(!navXml.hasError()
                    && navAfterHierarchyCommit.count(
                        QStringLiteral("epub:type=\"landmarks\"")) == 1
                    && navAfterHierarchyCommit.count(
                        QStringLiteral("epub:type=\"bodymatter\"")) == 1,
                "Native TOC commit duplicated a non-TOC Nav region or produced invalid XML");
        Require(nav->GetTextDocumentForWriting().isUndoAvailable(),
                "Native TOC commit was not recorded as an undoable edit");
        nav->GetTextDocumentForWriting().undo();
        Require(nav->GetText() == navBeforeHierarchyCommit,
                "One Nav undo did not restore the exact pre-commit source");
        nav->GetTextDocumentForWriting().redo();
        Require(nav->GetText() == navAfterHierarchyCommit,
                "One Nav redo did not restore the committed hierarchy source");

        const TocEditTree beforeStaleCommit = workspace.tocHierarchy();
        const TocTransformResult demoted = TocTreeTransform::Demote(
            beforeStaleCommit, QList<TocNodeId>({3}));
        Require(demoted.succeeded(),
                "Could not prepare stale native TOC transaction fixture");
        const quint64 staleTocRevision = workspace.revision();
        Require(workspace.beginTransaction(
                    QStringLiteral("stale native TOC source")).ok
                    && workspace.updateTocHierarchy(
                        beforeStaleCommit, demoted.tree).ok,
                "Could not stage stale native TOC hierarchy transaction");
        nav->SetText(nav->GetText() + QStringLiteral("\n<!-- host nav edit -->"));
        const QString hostNavEdit = nav->GetText();
        const SigilAgent::BookOpResult staleTocCommit =
            workspace.commitTransaction(staleTocRevision);
        Require(!staleTocCommit.ok
                    && staleTocCommit.code == QStringLiteral("BOOK_REVISION_CONFLICT")
                    && staleTocCommit.data.value(QStringLiteral("reason")).toString()
                        == QStringLiteral("toc_hierarchy_source_changed")
                    && nav->GetText() == hostNavEdit,
                "Native TOC commit accepted a stale source or overwrote the host edit");
        Require(workspace.rollbackTransaction().ok,
                "Could not roll back stale native TOC hierarchy transaction");

        std::cout << "Native agent package, guarded task recovery, TOC source preservation, undo, and stale-commit checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
