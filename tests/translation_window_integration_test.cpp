#include "EmbedPython/EmbeddedPython.h"
#include <QAction>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QEventLoop>
#include <QPushButton>
#include <QResource>
#include <QTextCursor>
#include <QThread>
#include <QWebEngineUrlScheme>
#include <iostream>
#include <stdexcept>
#include "Agent/Persistence/AgentSettings.h"
#include "Agent/Model/AgentModelCatalog.h"
#include "BookManipulation/Book.h"
#include "BookManipulation/FolderKeeper.h"
#include "Exporters/ExportEPUB.h"
#include "BuiltinPlugins/Translation/TranslationController.h"
#include "BuiltinPlugins/Translation/TranslationPanel.h"
#include "MainUI/MainApplication.h"
#include "MainUI/MainWindow.h"
#include "Misc/SettingsStore.h"
#include "ResourceObjects/OPFResource.h"
#include "ResourceObjects/TextResource.h"
#include "Tabs/ContentTab.h"
#include "ViewEditors/CodeViewEditor.h"

static void require(bool ok, const QString &message) { if (!ok) throw std::runtime_error(message.toStdString()); }
static QByteArray read(const QString &path) { QFile f(path); require(f.open(QIODevice::ReadOnly), "Cannot read fixture"); return f.readAll(); }
static bool waitFor(const std::function<bool()> &condition)
{
    QElapsedTimer timer; timer.start();
    while (!condition() && timer.elapsed() < 20000) { QCoreApplication::processEvents(QEventLoop::AllEvents, 20); QThread::msleep(2); }
    return condition();
}

int main(int argc, char **argv)
{
    QCoreApplication::setAttribute(Qt::AA_DisableShaderDiskCache);
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QWebEngineUrlScheme scheme("sigil"); scheme.setSyntax(QWebEngineUrlScheme::Syntax::Path);
    scheme.setFlags(QWebEngineUrlScheme::SecureScheme | QWebEngineUrlScheme::LocalScheme | QWebEngineUrlScheme::LocalAccessAllowed
        | QWebEngineUrlScheme::ContentSecurityPolicyIgnored | QWebEngineUrlScheme::FetchApiAllowed);
    QWebEngineUrlScheme::registerScheme(scheme);
    MainApplication app(argc, argv);
    QResource::registerResource(QCoreApplication::applicationDirPath() + "/../Resources/main.rcc");
    try {
        require(argc == 4, "Expected source root, fixture, local mock URL");
        const QString root = QString::fromLocal8Bit(argv[1]), source = QString::fromLocal8Bit(argv[2]);
        const QByteArray original = read(source);
        auto &python = EmbeddedPython::instance();
        python.addToPythonSysPath(qEnvironmentVariable("SIGIL_TEST_PYTHON_ROOT"));
        python.addToPythonSysPath(root + "/src/Resource_Files/plugin_launchers/python"); python.addToPythonSysPath(root + "/src/Resource_Files/python3lib");
        SettingsStore settings; settings.setCleanOn(0); settings.setPreserveOPFSource(true);
        SigilAgent::AgentSettings model;
        model.setProvider("openrouter"); model.setApiKey("synthetic-loopback-key");
        model.setModel("plain-model"); model.setModelSupportsReasoning(false); model.setThinkingEnabled(false);
        SigilAgent::CatalogModel reasoning;
        reasoning.id = "reasoning-translator"; reasoning.reasoning = true; reasoning.reasoningMandatory = true;
        reasoning.supportedReasoningEfforts = {"low", "high"};
        SigilAgent::CatalogResult catalog; catalog.models.append(reasoning);
        model.setCatalogJson(QString::fromUtf8(QJsonDocument(SigilAgent::AgentModelCatalog::toCacheJson(catalog, SigilAgent::AgentProviderKind::OpenRouter)).toJson()));
        model.setTranslationModel(reasoning.id); model.setTranslationThinking("auto");
        auto config = model.translationProviderConfig();
        require(config.model == reasoning.id && config.apiKey == model.providerConfig().apiKey
            && config.baseUrl == model.providerConfig().baseUrl, "Translation must reuse provider connection and credentials");
        require(config.thinking && config.reasoningProtocol == SigilAgent::ReasoningProtocol::OpenRouter && config.reasoningEffort == "low",
            "Mandatory reasoning translator did not restore its own protocol/minimum effort");
        model.setTranslationThinking("high"); config = model.translationProviderConfig();
        require(config.thinking && config.reasoningEffort == "high", "Independent translation effort ignored");
        reasoning.reasoningMandatory = false; catalog.models = {reasoning};
        model.setCatalogJson(QString::fromUtf8(QJsonDocument(SigilAgent::AgentModelCatalog::toCacheJson(catalog, SigilAgent::AgentProviderKind::OpenRouter)).toJson()));
        model.setTranslationThinking("auto");
        require(!model.translationProviderConfig().thinking, "Auto should disable optional reasoning");
        model.setTranslationConcurrency(99); require(model.translationConcurrency() == 8, "Concurrency upper bound missing");
        model.setTranslationConcurrency(0); require(model.translationConcurrency() == 1, "Concurrency lower bound missing");
        model.setProvider("custom"); model.setBaseUrl(QString::fromLocal8Bit(argv[3])); model.setApiKey("synthetic-loopback-key");
        model.setModel("test-planner"); model.setThinkingEnabled(false); model.setTranslationModel("test-translator");
        model.setTranslationThinking("auto"); model.setTranslationConcurrency(4);
        model.setCatalogJson("{}");
        MainWindow window(source);
        auto *controller = window.findChild<BuiltinPlugins::Translation::TranslationController *>();
        auto *panel = window.findChild<BuiltinPlugins::Translation::TranslationPanel *>();
        auto *action = window.findChild<QAction *>("actionTranslateBook");
        require(controller && panel && action, "Native translation menu/panel/controller missing");
        auto *resource = qobject_cast<TextResource *>(window.GetCurrentBook()->GetFolderKeeper()->GetResourceByBookPathNoThrow("OEBPS/a.xhtml"));
        require(resource, "Chapter fixture missing"); window.OpenResourceAndWaitUntilLoaded(resource);
        auto *code = dynamic_cast<CodeViewEditor *>(window.GetCurrentContentTab()->GetSearchableContent());
        require(code, "Code View missing");
        const int at = resource->GetText().indexOf("Original paragraph"); require(at >= 0, "Edit marker missing");
        auto cursor = code->textCursor(); cursor.setPosition(at); cursor.setPosition(at + 18, QTextCursor::KeepAnchor);
        code->setTextCursor(cursor); code->insertPlainText(QStringLiteral("未保存正文😀"));
        action->trigger(); auto *start = panel->findChild<QPushButton *>("translationStart"); require(start, "Start control missing");
        start->click();
        require(waitFor([&]() { return !controller->running(); }), "Native model workflow did not finish");
        const auto status = controller->status();
        require(status.value("paragraphs").toInt() > 100, "Fixture scope not fully read");
        require(status.value("translated").toInt() == status.value("paragraphs").toInt()
            && status.value("scanned").toInt() == status.value("paragraphs").toInt()
            && status.value("reviewed").toInt() == status.value("paragraphs").toInt()
            && status.value("failed").toInt() == 0, QString::fromUtf8(QJsonDocument(status).toJson()));
        require(resource->GetText().contains(QStringLiteral("译：未保存正文😀")), "Live unsaved editor text not translated");
        require(resource->GetText().contains("sigil-translation-images"), "Chinese image preset not applied");
        require(window.GetCurrentBook()->GetOPF()->GetSourceText().contains("page-progression-direction=\"ltr\""), "LTR preset missing");
        require(window.GetCurrentBook()->IsModified() && read(source) == original, "Translation did not remain an unsaved live-book edit");
        const auto saved = controller->saveGlossary(); require(saved.ok, saved.message);
        const QString path = saved.data.value("book_path").toString();
        auto *glossary = qobject_cast<TextResource *>(window.GetCurrentBook()->GetFolderKeeper()->GetResourceByBookPathNoThrow(path));
        require(glossary && glossary->GetText().contains("sigil-translation-glossary-v1"), "Native Misc glossary resource missing");
        const int requests = status.value("requests").toInt();
        require(controller->run({{"stages", QJsonArray {"translate"}}}).ok, "Repeat native run failed");
        require(waitFor([&]() { return !controller->running(); }), "Repeat workflow did not finish");
        require(controller->status().value("requests").toInt() == requests, "Repeat run unnecessarily called the model");
        const QString output = source + QStringLiteral(".translated.epub");
        ExportEPUB(output, window.GetCurrentBook()).WriteBook();
        {
            MainWindow reopened(output);
            auto *newController = reopened.findChild<BuiltinPlugins::Translation::TranslationController *>();
            require(newController && newController->contextId().isEmpty(), "New window unexpectedly restored a translation task");
            auto *savedGlossary = qobject_cast<TextResource *>(reopened.GetCurrentBook()->GetFolderKeeper()->GetResourceByBookPathNoThrow(path));
            require(savedGlossary, "Saved Misc JSON did not reopen as editable text");
            savedGlossary->InitialLoad();
            require(savedGlossary->GetText().contains("sigil-translation-glossary-v1"), "Saved glossary content missing after reopening EPUB");
            reopened.GetCurrentBook()->SetModified(false);
        }
        std::cout << "Native translation window/HTTP integration passed: " << requests << " requests, "
                  << status.value("paragraphs").toInt() << " reading paragraphs\n";
        window.GetCurrentBook()->SetModified(false);
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
    return 0;
}
