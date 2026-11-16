#include "EmbedPython/EmbeddedPython.h"

#include <QComboBox>
#include <QDialog>
#include <QEventLoop>
#include <QPointer>
#include <QResource>
#include <QSignalMapper>
#include <QTimer>
#include <QWebEngineUrlScheme>
#include <iostream>
#include <memory>
#include <stdexcept>

#include "BookManipulation/Book.h"
#include "BookManipulation/FolderKeeper.h"
#include "ChineseConversion/ChineseConversionSettings.h"
#include "Dialogs/ChineseConversionDialog.h"
#include "Dialogs/ChineseConversionPreviewDialog.h"
#include "Importers/ImportEPUB.h"
#include "MainUI/BookBrowser.h"
#include "MainUI/FindReplace.h"
#include "MainUI/FindReplacePlus.h"
#include "MainUI/MainApplication.h"
#include "MainUI/MainWindow.h"
#include "Misc/SettingsStore.h"
#include "Misc/SettingsStoreExtend.h"
#include "ResourceObjects/CSSResource.h"
#include "ResourceObjects/FontResource.h"
#include "ResourceObjects/HTMLResource.h"
#include "ResourceObjects/OPFResource.h"
#include "ResourceObjects/SVGResource.h"
#include "Tabs/CSSTab.h"
#include "Tabs/ContentTab.h"
#include "Tabs/FlowTab.h"
#include "Tabs/TabManager.h"
#include "sigil_constants.h"

namespace {
void require(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

void settle()
{
    QEventLoop loop;
    QTimer::singleShot(50, &loop, &QEventLoop::quit);
    loop.exec();
}

QSharedPointer<Book> loadBook(const QString& input)
{
    ImportEPUB importer(input);
    return importer.GetBook();
}

void closeEditors(MainWindow& window)
{
    auto* tabs = window.findChild<TabManager*>();
    require(tabs, "Missing editor tab manager");
    tabs->CloseAllTabs(true);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    require(!window.GetCurrentContentTab(), "Editor tabs did not close");
}

void browserActions(MainWindow& window, const QString& source, const QByteArray& scenario)
{
    auto book = window.GetCurrentBook();
    auto* browser = window.findChild<BookBrowser*>();
    auto* chapter = book->GetFolderKeeper()->GetResourceByBookPath("OEBPS/a.xhtml");
    require(browser && chapter, "Missing browser fixtures");
    auto* css = book->CreateEmptyCSSFile();
    browser->Refresh();
    if (scenario == "copy-html") browser->UpdateSelection(css);
    else browser->UpdateSelection(chapter);
    const QString before = book->GetOPF()->GetText();
    const int resources = book->GetAllResources().size();
    int modified = 0;
    QObject observer;
    QObject::connect(browser, &BookBrowser::BookContentModified, &observer, [&]() { ++modified; });
    if (scenario == "copy-html") browser->CopyHTML();
    else if (scenario == "copy-css") browser->CopyCSS();
    else {
        for (const char* method : {"NoObfuscationMethod", "AdobesObfuscationMethod", "IdpfsObfuscationMethod"}) {
            require(QMetaObject::invokeMethod(browser, method, Qt::DirectConnection), "Cannot invoke font action");
        }
    }
    require(book->GetAllResources().size() == resources && book->GetOPF()->GetText() == before && modified == 0,
            "Wrong-type action partially modified the book");
    if (scenario == "font-actions") {
        auto* font = qobject_cast<FontResource*>(book->GetFolderKeeper()->AddContentFileToFolder(
            source + "/tests/fixtures/fonts/OpenSans-Test.ttf"));
        require(font, "Cannot load real font fixture");
        browser->Refresh();
        browser->UpdateSelection(font);
        require(QMetaObject::invokeMethod(browser, "IdpfsObfuscationMethod", Qt::DirectConnection), "Cannot obfuscate font");
        require(font->GetObfuscationAlgorithm() == IDPF_FONT_ALGO_ID, "Valid font action stopped working");
        require(QMetaObject::invokeMethod(browser, "NoObfuscationMethod", Qt::DirectConnection), "Cannot clear font obfuscation");
        require(font->GetObfuscationAlgorithm().isEmpty(), "Cannot clear valid font obfuscation");
    }
}

void searchWithoutEditor(MainWindow& window, bool enhanced)
{
    QWidget* search = enhanced ? static_cast<QWidget*>(window.findChild<FindReplacePlus*>())
                               : static_cast<QWidget*>(window.findChild<FindReplace*>());
    require(search, "Missing requested search panel");
    auto* find = search->findChild<QComboBox*>("cbFind");
    auto* replacement = search->findChild<QComboBox*>("cbReplace");
    auto* scope = search->findChild<QComboBox*>("cbLookWhere");
    require(find && replacement && scope, "Missing search fields");
    if (enhanced) {
        find->clearFocus();
        require(QMetaObject::invokeMethod(search, "TokeniseSelection", Qt::DirectConnection), "Cannot invoke tokenization without focus");
    }
    find->setEditText("Original paragraph");
    replacement->setEditText("Replaced with no editor open");
    scope->setCurrentIndex(scope->findData(1)); // All HTML in both modes.
    closeEditors(window);
    int count = -1;
    require(QMetaObject::invokeMethod(search, "Count", Qt::DirectConnection, Q_RETURN_ARG(int, count)), "Cannot count matches");
    require(count == 1, "Book-wide count requires an open editor");
    require(QMetaObject::invokeMethod(search, "ReplaceAll", Qt::DirectConnection, Q_RETURN_ARG(int, count)), "Cannot replace matches");
    require(count == 1, "Book-wide replacement requires an open editor");
    auto* text = qobject_cast<HTMLResource*>(window.GetCurrentBook()->GetFolderKeeper()->GetResourceByBookPath("OEBPS/a.xhtml"));
    require(text->GetText().contains("Replaced with no editor open"), "Replacement did not reach the book");
    scope->setCurrentIndex(scope->findData(0));
    require(QMetaObject::invokeMethod(search, "Count", Qt::DirectConnection, Q_RETURN_ARG(int, count)), "Cannot count empty current editor");
    require(count == 0, "Missing current editor should have no matches");
}

void closedWhileLoading(MainWindow& window)
{
    auto* target = window.GetCurrentBook()->CreateEmptyHTMLFile();
    bool closed = false;
    QTimer::singleShot(0, &window, [&]() {
        closeEditors(window);
        closed = true;
    });
    window.OpenResourceAndWaitUntilLoaded(target);
    require(closed && !window.GetCurrentContentTab(), "Loading wait did not tolerate its editor closing");
    window.OpenResourceAndWaitUntilLoaded(nullptr);
}

void conversionWhileClosing(MainWindow& window, const QByteArray& scenario)
{
    const bool valid = scenario.endsWith("apply");
    const bool selection = scenario == "conversion-selection-apply";
    const bool duringPreview = scenario == "conversion-preview" || selection;
    TextResource* chapter = nullptr;
    if (scenario == "conversion-svg-apply") {
        chapter = window.GetCurrentBook()->CreateEmptySVGFile();
        chapter->SetText(QString::fromUtf8("<svg xmlns='http://www.w3.org/2000/svg'><text>汉语测试</text></svg>"));
    } else {
        chapter = qobject_cast<HTMLResource*>(window.GetCurrentBook()->GetFolderKeeper()->GetResourceByBookPath("OEBPS/a.xhtml"));
        chapter->SetText(QString(chapter->GetText()).replace("Original paragraph", QString::fromUtf8("汉语测试")));
    }
    window.OpenResourceAndWaitUntilLoaded(chapter);
    const QString before = chapter->GetText();
    if (selection) {
        const int start = before.indexOf(QString::fromUtf8("汉语测试"));
        require(start >= 0 && window.GetCurrentContentTab()->SetSelectionRange(start, start + 4), "Cannot select Chinese text");
    }
    ChineseConversionOptions options;
    options.mode = ChineseConversionMode::S2T;
    options.scope = selection ? ChineseConversionScope::CurrentSelection : ChineseConversionScope::CurrentFile;
    options.previewBeforeApply = duringPreview;
    ChineseConversionSettings::Save(options);
    bool optionsHandled = false;
    bool previewHandled = false;
    QTimer drive;
    QObject::connect(&drive, &QTimer::timeout, &window, [&]() {
        for (auto* widget : QApplication::topLevelWidgets()) {
            if (auto* dialog = qobject_cast<ChineseConversionDialog*>(widget)) {
                if (!dialog->isVisible() || optionsHandled) continue;
                optionsHandled = true;
                if (!duringPreview && !valid) closeEditors(window);
                require(QMetaObject::invokeMethod(dialog, "RequestConvert", Qt::DirectConnection), "Cannot confirm conversion options");
            } else if (auto* preview = qobject_cast<ChineseConversionPreviewDialog*>(widget)) {
                if (!preview->isVisible() || previewHandled) continue;
                previewHandled = true;
                if (!valid) closeEditors(window);
                preview->accept();
            }
        }
    });
    drive.start(10);
    bool applied = true;
    require(QMetaObject::invokeMethod(&window, "ConvertChineseText", Qt::DirectConnection, Q_RETURN_ARG(bool, applied)), "Cannot start conversion");
    require(optionsHandled && previewHandled == duringPreview && applied == valid,
            "Conversion did not respect editor lifetime");
    require(valid ? chapter->GetText().contains(QString::fromUtf8("漢語測試")) : chapter->GetText() == before,
            "Conversion wrote unexpected content");
}

void resourceLifetime(MainWindow& window, const QString& input, const QByteArray& scenario)
{
    auto book = loadBook(input);
    std::unique_ptr<Resource> directResource;
    QPointer<ContentTab> tab;
    if (scenario.contains("xhtml")) {
        auto* resource = qobject_cast<HTMLResource*>(book->GetFolderKeeper()->GetResourceByBookPath("OEBPS/a.xhtml"));
        if (scenario.endsWith("direct")) {
            auto* transient = new HTMLResource(resource->GetFullPathToBookFolder(), resource->GetFullPath(), book->GetFolderKeeper());
            transient->SetText(resource->GetText());
            directResource.reset(transient);
            resource = transient;
        }
        tab = new FlowTab(resource, QUrl(), -1, -1, QString(), false, &window);
    } else {
        auto* resource = book->CreateEmptyCSSFile();
        if (scenario.endsWith("direct")) {
            auto* transient = new CSSResource(resource->GetFullPathToBookFolder(), resource->GetFullPath());
            transient->SetText(resource->GetText());
            directResource.reset(transient);
            resource = transient;
        }
        tab = new CSSTab(resource, -1, -1, &window);
    }
    if (scenario.endsWith("loaded")) settle();
    int closeRequests = 0;
    QObject observer;
    QObject::connect(tab, &ContentTab::DeleteMe, &observer, [&]() { ++closeRequests; });
    if (directResource) directResource.reset(); // No Deleted signal.
    else book.clear();
    require(tab->GetResourceWasDeleted() && closeRequests == 1, "Editor did not notice resource destruction");
    // Normal deletion queues the resource's QObject destruction. Complete
    // that phase without running the tab's pending initialization first.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    require(closeRequests == 1, "Resource teardown emitted duplicate close requests");
    require(!tab->GetLoadedResource() && tab->GetFilename().isEmpty() && tab->GetShortPathName().isEmpty() && tab->GetIcon().isNull(),
            "Closed resource metadata remained accessible");
    tab->SaveTabContent();
    settle(); // Deliver any initialization/refresh queued before destruction.
    delete tab;
    require(!tab, "Orphaned editor did not close");
}
}

int main(int argc, char** argv)
{
    QCoreApplication::setAttribute(Qt::AA_DisableShaderDiskCache);
    QWebEngineUrlScheme scheme("sigil");
    scheme.setSyntax(QWebEngineUrlScheme::Syntax::Path);
    scheme.setFlags(QWebEngineUrlScheme::SecureScheme | QWebEngineUrlScheme::LocalScheme |
                    QWebEngineUrlScheme::LocalAccessAllowed | QWebEngineUrlScheme::ContentSecurityPolicyIgnored |
                    QWebEngineUrlScheme::FetchApiAllowed);
    QWebEngineUrlScheme::registerScheme(scheme);
    MainApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    QResource::registerResource(QCoreApplication::applicationDirPath() + "/../Resources/main.rcc");
    try {
        require(argc == 4, "Expected source root, EPUB and scenario");
        const QString root = QString::fromLocal8Bit(argv[1]);
        const QString input = QString::fromLocal8Bit(argv[2]);
        const QByteArray scenario(argv[3]);
        auto& python = EmbeddedPython::instance();
        python.addToPythonSysPath(qEnvironmentVariable("SIGIL_TEST_PYTHON_ROOT"));
        python.addToPythonSysPath(root + "/src/Resource_Files/plugin_launchers/python");
        python.addToPythonSysPath(root + "/src/Resource_Files/python3lib");
        SettingsStore settings;
        settings.setCleanOn(0);
        settings.setPreserveOPFSource(true);
        SettingsStoreExtend().setFindReplaceEnhancedMode(scenario == "search-plus");
        MainWindow window(input);
        window.setAttribute(Qt::WA_DeleteOnClose, false);
        window.showNormal();
        settle();
        std::cout << "Running " << scenario.constData() << '\n' << std::flush;
        if (scenario.startsWith("copy-") || scenario == "font-actions") browserActions(window, root, scenario);
        else if (scenario.startsWith("search-")) searchWithoutEditor(window, scenario == "search-plus");
        else if (scenario == "loading-close") closedWhileLoading(window);
        else if (scenario.startsWith("conversion-")) conversionWhileClosing(window, scenario);
        else if (scenario.startsWith("resource-")) resourceLifetime(window, input, scenario);
        else throw std::runtime_error("Unknown scenario");
        window.GetCurrentBook()->SetModified(false);
        std::cout << scenario.constData() << " passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
