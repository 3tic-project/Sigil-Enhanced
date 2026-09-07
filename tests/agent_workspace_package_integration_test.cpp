#include "EmbedPython/EmbeddedPython.h" // Python must precede Qt's slots macro.

#include <QRegularExpression>
#include <QWebEngineSettings>
#include <QWebEngineUrlScheme>
#include <iostream>
#include <stdexcept>

#include "Agent/Execution/SigilBookWorkspace.h"
#include "BookManipulation/Book.h"
#include "BookManipulation/FolderKeeper.h"
#include "MainUI/MainApplication.h"
#include "MainUI/MainWindow.h"
#include "Misc/SettingsStore.h"
#include "Misc/WebProfileMgr.h"
#include "ResourceObjects/HTMLResource.h"
#include "ResourceObjects/OPFResource.h"

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

        SigilAgent::SigilBookWorkspace workspace;
        workspace.setBook(book);
        const QString original = opf->GetSourceText();
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

        std::cout << "Native agent package preservation and stale-commit checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
