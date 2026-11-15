#include "EmbedPython/EmbeddedPython.h"
#include <QAction>
#include <QDockWidget>
#include <atomic>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QPushButton>
#include <QProgressBar>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QResource>
#include <QTextCursor>
#include <QThread>
#include <QWebEngineUrlScheme>
#include <iostream>
#include <stdexcept>
#include "Agent/Persistence/AgentSettings.h"
#include "Agent/Model/AgentModelCatalog.h"
#include "Agent/UI/AgentDock.h"
#include "Dialogs/PreferenceWidgets/AgentSettingsWidget.h"
#include "BookManipulation/Book.h"
#include "BookManipulation/FolderKeeper.h"
#include "Exporters/ExportEPUB.h"
#include "BuiltinPlugins/Translation/TranslationController.h"
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
        model.setDefaultMode(SigilAgent::AgentMode::Auto); model.setModelSupportsTools(true);
        model.setCatalogJson("{}");
        {
            AgentSettingsWidget preferences;
            auto *batch = preferences.findChild<QSpinBox *>("agentTranslationBatchCharacters");
            auto *spacing = preferences.findChild<QDoubleSpinBox *>("agentTranslationSpacing");
            auto *review = preferences.findChild<QCheckBox *>("agentTranslationReviewWithAgent");
            require(batch && spacing && review && preferences.findChild<QComboBox *>("agentTranslationModel"), "Translation preferences missing");
            batch->setValue(3500); spacing->setValue(1.5); review->setChecked(true); preferences.saveSettings();
            require(model.translationBatchCharacters() == 3500 && model.translationSpacing() == 1.5 && model.translationReviewWithAgent(), "Translation preferences did not save shared settings");
            model.setTranslationBatchCharacters(0); model.setTranslationSpacing(1); model.setTranslationReviewWithAgent(false);
        }
        MainWindow window(source);
        auto *controller = window.findChild<BuiltinPlugins::Translation::TranslationController *>();
        auto *dock = window.findChild<SigilAgent::AgentDock *>();
        require(controller && dock && !controller->agentSessionId().isEmpty(), "Agent translation controller missing its conversation owner");
        require(!window.findChild<QDockWidget *>("translationDock") && !window.findChild<QAction *>("actionTranslateBook"), "Standalone translation UI must not exist");
        auto *resource = qobject_cast<TextResource *>(window.GetCurrentBook()->GetFolderKeeper()->GetResourceByBookPathNoThrow("OEBPS/a.xhtml"));
        require(resource, "Chapter fixture missing"); window.OpenResourceAndWaitUntilLoaded(resource);
        auto *code = dynamic_cast<CodeViewEditor *>(window.GetCurrentContentTab()->GetSearchableContent());
        require(code, "Code View missing");
        const int at = resource->GetText().indexOf("Original paragraph"); require(at >= 0, "Edit marker missing");
        auto cursor = code->textCursor(); cursor.setPosition(at); cursor.setPosition(at + 18, QTextCursor::KeepAnchor);
        code->setTextCursor(cursor); code->insertPlainText(QStringLiteral("未保存正文😀"));
        int backgroundNotices = 0;
        QObject::connect(dock, &SigilAgent::AgentDock::eventObserved, &window, [&](const SigilAgent::AgentEvent &event) {
            if (event.type == SigilAgent::AgentEventType::UserMessage && event.payload.value("source") == "background") ++backgroundNotices;
        });
        TextResource *glossary = nullptr;
        dock->sendRequested(QStringLiteral("请全文翻译成中文双语，使用中文阅读格式，最后保存词表到 Misc。"), {});
        require(waitFor([&]() {
            for (auto *text : window.GetCurrentBook()->GetFolderKeeper()->GetResourceTypeList<TextResource>(true))
                if (text->GetRelativePath().endsWith("translation-glossary.json")) glossary = text;
            return !controller->running() && glossary && backgroundNotices > 0;
        }), "Agent did not finish its native translation and glossary follow-up");
        auto *taskCard = dock->findChild<QWidget *>("agentBackgroundTask");
        auto *taskCounts = dock->findChild<QLabel *>("agentBackgroundTaskCounts");
        require(taskCard && !taskCard->isHidden() && taskCard->property("state") == "completed" && taskCounts
            && taskCounts->text().contains("Written 224/224"), "Agent task card did not expose native workflow completion");
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
        const QString path = glossary ? glossary->GetRelativePath() : QString();
        require(glossary && glossary->GetText().contains("sigil-translation-glossary-v1"), "Native Misc glossary resource missing");
        const int requests = status.value("requests").toInt();
        require(controller->run({{"stages", QJsonArray {"translate"}}}).ok, "Repeat native run failed");
        require(waitFor([&]() { return !controller->running(); }), "Repeat workflow did not finish");
        require(controller->status().value("requests").toInt() == requests, "Repeat run unnecessarily called the model");
        const QString output = source + QStringLiteral(".translated.epub");
        ExportEPUB(output, window.GetCurrentBook()).WriteBook();
        // The conversation owns every temporary translation object, while
        // already-applied Book edits and a saved glossary remain in the Book.
        const QString ownerA = controller->agentSessionId();
        controller->context({{"kind", "memory"}, {"action", "update"}, {"value", "A-only memory"}});
        auto *fresh = dock->findChild<QPushButton *>("agentNewSessionButton");
        require(fresh && fresh->isEnabled(), "Agent New Session button unavailable after translation");
        fresh->click();
        const int noticesAfterNewSession = backgroundNotices;
        require(controller->agentSessionId() != ownerA && !controller->agentSessionId().isEmpty()
            && controller->contextId().isEmpty() && controller->status().value("paragraphs").toInt() == 0
            && controller->status().value("terms").toInt() == 0, "New Agent conversation inherited translation state");
        require(controller->prepare().ok && controller->context({{"kind", "glossary"}}).data.value("total").toInt() == 0
            && controller->context({{"kind", "memory"}}).data.value("memory").toString().isEmpty(), "Saved Book glossary was silently imported into a new conversation");
        require(glossary->GetText().contains("sigil-translation-glossary-v1"), "Conversation reset removed saved Book content");
        model.setTranslationConcurrency(1);
        std::atomic_bool entered {false}, release {false};
        controller->setCompletion([&](const auto &, const SigilAgent::ModelRequest &request, const auto &) {
            entered.store(true); while (!release.load()) QThread::msleep(1);
            const auto rows = QJsonDocument::fromJson(request.messages.last().content.toUtf8()).object().value("items").toArray();
            QJsonArray items;
            for (const auto &row : rows) items.append(QJsonObject {{"n", row.toObject().value("n")}, {"text", "Late old conversation output"}});
            SigilAgent::ModelTurn turn; turn.content = QString::fromUtf8(QJsonDocument(QJsonObject {{"items", items}}).toJson()); return turn;
        });
        const QString unchanged = resource->GetText();
        require(controller->run({{"stages", QJsonArray {"translate"}}, {"retranslate", true}}).ok, "Start old conversation request");
        require(waitFor([&]() { return entered.load(); }), "Old conversation request did not start");
        QFutureWatcherBase *watcher = nullptr;
        for (auto *candidate : controller->findChildren<QFutureWatcherBase *>()) if (!candidate->isFinished()) watcher = candidate;
        require(watcher, "Old conversation watcher missing");
        bool delivered = false;
        QObject::connect(watcher, &QFutureWatcherBase::finished, &window, [&]() { delivered = true; });
        auto *pauseTask = dock->findChild<QPushButton *>("agentBackgroundPause");
        require(pauseTask && !pauseTask->isHidden(), "Background task pause control missing");
        pauseTask->click(); require(controller->status().value("state") == "paused", "Task Pause did not control the native queue");
        pauseTask->click(); require(controller->status().value("state") == "running", "Task Resume did not control the native queue");
        const QString ownerB = controller->agentSessionId();
        require(fresh->isEnabled(), "Background translation locked New Session");
        fresh->click();
        require(controller->agentSessionId() != ownerB && controller->contextId().isEmpty()
            && !controller->running() && taskCard->isHidden(), "New Session did not synchronously discard old translation/task card");
        release.store(true); require(waitFor([&]() { return delivered; }), "Old conversation reply did not settle");
        require(resource->GetText() == unchanged && controller->contextId().isEmpty() && backgroundNotices == noticesAfterNewSession,
            "Late translation from old conversation wrote or notified the new conversation");
        require(controller->prepare().ok, "Prepare current conversation for Stop test");
        entered.store(false); release.store(false); delivered = false;
        require(controller->run({{"stages", QJsonArray {"translate"}}, {"retranslate", true}}).ok
            && waitFor([&]() { return entered.load(); }), "Agent Stop test request did not start");
        watcher = nullptr;
        for (auto *candidate : controller->findChildren<QFutureWatcherBase *>()) if (!candidate->isFinished()) watcher = candidate;
        require(watcher, "Stop test watcher missing");
        QObject::connect(watcher, &QFutureWatcherBase::finished, &window, [&]() { delivered = true; });
        auto *stop = dock->findChild<QPushButton *>("agentStopButton");
        require(stop && stop->isEnabled(), "Agent Stop button disabled during background translation");
        stop->click();
        require(!controller->running() && controller->status().value("state") == "stopped", "Agent Stop did not stop its translation task");
        require(!stop->isEnabled(), "Stopped translation left the Agent Stop button enabled");
        release.store(true); require(waitFor([&]() { return delivered; }), "Stopped request did not settle");
        require(resource->GetText() == unchanged, "Agent-stopped translation wrote a late result");
        {
            MainWindow reopened(output);
            auto *newController = reopened.findChild<BuiltinPlugins::Translation::TranslationController *>();
            require(newController && newController->contextId().isEmpty(), "New window unexpectedly restored a translation task");
            auto *savedGlossary = qobject_cast<TextResource *>(reopened.GetCurrentBook()->GetFolderKeeper()->GetResourceByBookPathNoThrow(path));
            require(savedGlossary, "Saved Misc JSON did not reopen as editable text");
            savedGlossary->InitialLoad();
            require(savedGlossary->GetText().contains("sigil-translation-glossary-v1"), "Saved glossary content missing after reopening EPUB");
            reopened.GetCurrentBook()->SetModified(false);
            require(reopened.close(), "Reopened test window did not close normally");
        }
        std::cout << "Native translation window/HTTP integration passed: " << requests << " requests, "
                  << status.value("paragraphs").toInt() << " reading paragraphs\n";
        window.GetCurrentBook()->SetModified(false);
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
    return 0;
}
