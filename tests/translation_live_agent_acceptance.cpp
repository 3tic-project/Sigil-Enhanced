#include "EmbedPython/EmbeddedPython.h"
#include <atomic>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QResource>
#include <QSaveFile>
#include <QThread>
#include <QTimer>
#include <QWebEngineUrlScheme>
#include <iostream>
#include <stdexcept>
#include "Agent/Persistence/AgentSettings.h"
#include "Agent/Persistence/AgentSessionExport.h"
#include "Agent/UI/AgentDock.h"
#include "Agent/Model/ModelRetry.h"
#include "BookManipulation/Book.h"
#include "BookManipulation/FolderKeeper.h"
#include "BuiltinPlugins/Translation/TranslationController.h"
#include "Exporters/ExportEPUB.h"
#include "MainUI/MainApplication.h"
#include "MainUI/MainWindow.h"
#include "Misc/SettingsStore.h"
#include "ResourceObjects/TextResource.h"

using namespace SigilAgent;
using namespace BuiltinPlugins::Translation;
static void require(bool ok, const QString &message) { if (!ok) throw std::runtime_error(message.toStdString()); }
static QByteArray read(const QString &path) { QFile file(path); require(file.open(QIODevice::ReadOnly), "Cannot read synthetic fixture"); return file.readAll(); }
static void write(const QString &path, const QJsonObject &object)
{
    QSaveFile file(path); require(file.open(QIODevice::WriteOnly), "Cannot write acceptance report");
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented)); require(file.commit(), "Cannot commit acceptance report");
}
class Sink final : public ModelStreamSink {
public:
    explicit Sink(std::shared_ptr<std::atomic_bool> flag) : cancelled(std::move(flag)) {}
    void onReasoningDelta(const QString &) override {}
    void onContentDelta(const QString &) override {}
    void onToolCallsUpdated(const QList<ToolCall> &) override {}
    bool isCancelled() const override { return cancelled->load(); }
    std::shared_ptr<std::atomic_bool> cancelled;
};
struct Counters { std::atomic_int injected {0}; std::atomic_int requests {0}; };

static QJsonObject scenario(const QString &source, const QString &name, bool fault, bool correction, bool live)
{
    MainWindow window(source);
    auto *dock = window.findChild<AgentDock *>();
    auto *native = window.findChild<TranslationController *>();
    require(dock && native, "Native Agent/translation window missing"); dock->setMode(AgentMode::Auto);
    AgentSettings settings;
    const QString reportPath = qEnvironmentVariable("SIGIL_LIVE_AGENT_REPORT_DIR") + '/' + name + ".json";
    const QStringList secrets {settings.apiKey(), settings.baseUrl(), settings.providerConfig().baseUrl, source};
    const QByteArray original = read(source);
    QJsonArray events; QStringList tools; int backgrounds = 0, finals = 0, lastBackground = -1, lastFinal = -1;
    QHash<QString, bool> workflowCalls, recoveryCalls;
    int recoveryActions = 0, fullWorkflowRuns = 0;
    bool plan = false, expired = false, agentFailed = false, fullWorkflow = false; QString failure; QElapsedTimer elapsed; elapsed.start();
    const auto counters = std::make_shared<Counters>(); int retrySelections = 0;
    QJsonObject result {{"scenario", name}, {"transport", live ? "live" : "mock"}, {"agent_model", settings.model()},
        {"translation_model", settings.translationProviderConfig().model}, {"synthetic_content_only", true}, {"passed", false}};
    auto snapshot = [&]() {
        auto status = native->status(); status.remove("session_id");
        result["status"] = status; result["elapsed_ms"] = elapsed.elapsed(); result["events"] = events;
        result["tools"] = QJsonArray::fromStringList(tools); result["background_notifications"] = backgrounds;
        result["worker_requests"] = counters->requests.load(); result["injected_failures"] = counters->injected.load();
        result["retry_failed_selections"] = retrySelections;
        result["recovery_actions"] = recoveryActions;
        result["plan_observed"] = plan;
        result["full_workflow_selected"] = fullWorkflow;
        result["full_workflow_runs"] = fullWorkflowRuns;
        write(reportPath, redactJsonValue(result, secrets).toObject());
    };
    const auto observation = QObject::connect(dock, &AgentDock::eventObserved, &window, [&](const AgentEvent &event) {
        if (event.type == AgentEventType::AssistantDelta) return; // No chain of thought.
        QJsonObject payload = event.payload; payload.remove("reasoning_content"); payload.remove("reasoning_details");
        const bool beforeTranslation = native->status().value("translated").toInt() == 0;
        if (event.type == AgentEventType::ModelRequestStarted) payload = {{"model", settings.model()}, {"tool_context", payload.value("tool_context")}};
        if (event.type == AgentEventType::UserMessage && payload.value("source") == "background") { ++backgrounds; lastBackground = events.size(); }
        if (event.type == AgentEventType::RunStateChanged) agentFailed = payload.value("state") == "failed" || payload.value("state") == "cancelled";
        if (event.type == AgentEventType::AssistantMessage) {
            if (payload.value("is_final").toBool()) { ++finals; lastFinal = events.size(); }
            const QString text = payload.value("content").toString();
            if (beforeTranslation && (text.contains(QStringLiteral("计划")) || text.contains(QStringLiteral("先")))
                && text.contains(QStringLiteral("扫描")) && text.contains(QStringLiteral("翻译"))) plan = true;
        }
        if (event.type == AgentEventType::ToolRequested) {
            const QString tool = payload.value("name").toString(); tools.append(tool);
            const QString arguments = payload.value("arguments").isObject()
                ? QString::fromUtf8(QJsonDocument(payload.value("arguments").toObject()).toJson()) : payload.value("arguments").toString();
            const auto object = QJsonDocument::fromJson(arguments.toUtf8()).object();
            if (beforeTranslation && (tool == "session.task_add" || (tool == "translation.context" && object.value("kind") == "plan" && object.value("action") == "update"))) plan = true;
            if (tool == "translation.run") {
                const auto stages = object.value("stages").toArray();
                workflowCalls[payload.value("tool_call_id").toString()] = (!object.contains("stages") || object.value("stages").isArray())
                    && (stages.isEmpty() || (stages.contains("scan") && stages.contains("aggregate") && stages.contains("translate") && stages.contains("review")));
                recoveryCalls[payload.value("tool_call_id").toString()] = stages.contains("translate") && stages.contains("review")
                    && !stages.contains("scan") && !stages.contains("aggregate") && !object.value("retranslate").toBool();
            }
            if (tool == "translation.control" && object.value("action") == "retry_failed") {
                ++retrySelections; recoveryCalls[payload.value("tool_call_id").toString()] = true;
            }
        }
        if (event.type == AgentEventType::ToolCompleted && payload.value("name") == "translation.run" && payload.value("ok").toBool()
            && workflowCalls.value(payload.value("tool_call_id").toString())) { fullWorkflow = true; ++fullWorkflowRuns; }
        if (event.type == AgentEventType::ToolCompleted && payload.value("ok").toBool() && recoveryCalls.value(payload.value("tool_call_id").toString())) ++recoveryActions;
        events.append(QJsonObject {{"type", eventTypeName(event.type)}, {"elapsed_ms", elapsed.elapsed()},
            {"translated_before_event", native->status().value("translated")}, {"payload", redactJsonValue(payload, secrets)}});
        snapshot();
    });
    native->setCompletion([counters, fault](const OpenAIProviderConfig &config, const ModelRequest &request, const auto &cancel) {
        require(request.tools.isEmpty(), "Translation worker received tool schemas");
        if (fault && request.messages.first().content.contains(QStringLiteral("完整翻译每一")) && counters->injected.load() < MODEL_NETWORK_RETRIES + 1) {
            ++counters->injected; ModelTurn turn; turn.error = "HTTP 503: injected temporary outage in acceptance fixture"; return turn;
        }
        ++counters->requests; OpenAICompatibleProvider provider(config); Sink sink(cancel); return provider.stream(request, sink);
    });
    QTimer deadline; deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &window, [&]() { expired = true; native->control({{"action", "stop"}}); dock->stopRequested(); });
    deadline.start(live ? 480000 : 45000);
    auto glossary = [&]() -> TextResource * {
        for (auto *text : window.GetCurrentBook()->GetFolderKeeper()->GetResourceTypeList<TextResource>(true)) {
            if (!text->GetRelativePath().endsWith(".json")) continue;
            const auto object = QJsonDocument::fromJson(text->GetText().toUtf8()).object();
            if (object.value("format") == "sigil-translation-glossary-v1" && object.value("terms").isArray()) return text;
        }
        return nullptr;
    };
    auto wait = [&](int previousBackgrounds) {
        while (!expired) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 25); QThread::msleep(3);
            if (agentFailed && !native->running()) throw std::runtime_error("Agent run failed; inspect recorded model/tool events");
            if (!native->running() && backgrounds > previousBackgrounds && lastFinal > lastBackground && glossary()) return;
        }
        throw std::runtime_error("Agent did not finish the requested background work before deadline");
    };
    try {
        const QString prompt = QStringLiteral("请把当前打开的整本日文书翻成简体中文，保留原文做双语对照。先列一个简短计划并直接执行：完整扫描人物术语，汇总后翻译，再校正全部译文。ミナ译作米娜，リク译作陆。保留原书格式。若有短时失败，请自行处理并补齐，不要等我说继续。最后把完成的术语表保存到书内 Misc，用两三句话汇报实际翻译和校正数量，并说明 EPUB 仍未保存。请不要保存或另存 EPUB。");
        dock->sendRequested(prompt, {});
        require(tools.contains("translation.run"), "Real Agent did not choose native translation workflow");
        wait(0);
        require(plan, "No brief plan was observed before/during the native workflow");
        require(fullWorkflow, "Agent omitted a requested scan/aggregate/translate/review stage");
        auto status = native->status(); const int count = status.value("paragraphs").toInt();
        require(count >= 10 && status.value("scanned").toInt() == count && status.value("translated").toInt() == count
            && status.value("reviewed").toInt() == count && status.value("failed").toInt() == 0, "Agent did not complete scan/translation/review scope");
        require(tools.contains("translation.save_glossary"), "Agent did not save the glossary using native tool");
        if (fault) require(counters->injected.load() == MODEL_NETWORK_RETRIES + 1 && recoveryActions > 0 && backgrounds >= 2,
            "Agent did not yield and resume from the retry completion notice");
        result["initial_status"] = status;
        if (correction) {
            QHash<QString, QString> unchanged;
            for (auto *text : window.GetCurrentBook()->GetFolderKeeper()->GetResourceTypeList<TextResource>(true)) {
                if (!text->GetRelativePath().endsWith(".xhtml")) continue;
                const auto document = extract(text->GetText());
                for (int i = 0; i < document.paragraphs.size(); ++i) if (!document.paragraphs.at(i).text.contains(QStringLiteral("ミナ")))
                    unchanged.insert(text->GetRelativePath() + QString::number(i), document.paragraphs.at(i).translation);
            }
            const int previous = backgrounds;
            const int previousRevisions = tools.count("translation.revise");
            dock->sendRequested(QStringLiteral("将人物ミナ的中文译名统一改成美奈，保留リク为陆。只校正受影响的已有译文，更新刚才那份术语表，不要生成第二份；原文、链接和其他译文保持。完成后简短报告实际处理情况。"), {});
            require(tools.count("translation.revise") > previousRevisions, "Agent did not select targeted terminology revision");
            wait(previous);
            require(glossary()->GetText().contains(QStringLiteral("美奈")), "Agent did not update the existing glossary after correction");
            for (auto *text : window.GetCurrentBook()->GetFolderKeeper()->GetResourceTypeList<TextResource>(true)) {
                if (!text->GetRelativePath().endsWith(".xhtml")) continue;
                const auto document = extract(text->GetText());
                for (int i = 0; i < document.paragraphs.size(); ++i) {
                    const QString key = text->GetRelativePath() + QString::number(i);
                    if (unchanged.contains(key)) require(unchanged.value(key) == document.paragraphs.at(i).translation, "Terminology correction changed unrelated wording");
                }
            }
        }
        int glossaryCount = 0, mentions = 0, matching = 0, rikuMentions = 0, rikuMatching = 0;
        QJsonArray samples;
        for (auto *text : window.GetCurrentBook()->GetFolderKeeper()->GetResourceTypeList<TextResource>(true)) {
            if (text->GetRelativePath().endsWith(".json")) ++glossaryCount;
            if (!text->GetRelativePath().endsWith(".xhtml")) continue;
            for (const auto &paragraph : extract(text->GetText()).paragraphs) {
                if (paragraph.text.contains(QStringLiteral("ミナ"))) { ++mentions; matching += paragraph.translation.contains(correction ? QStringLiteral("美奈") : QStringLiteral("米娜")); }
                if (paragraph.text.contains(QStringLiteral("リク"))) { ++rikuMentions; rikuMatching += paragraph.translation.contains(QStringLiteral("陆")); }
                if (samples.size() < 8) samples.append(QJsonObject {{"source", paragraph.text}, {"translation", paragraph.translation}});
            }
        }
        require(glossaryCount == 1 && mentions > 0 && matching == mentions && rikuMentions > 0 && rikuMatching == rikuMentions, "Missing/duplicate glossary or inconsistent character name");
        require(fullWorkflowRuns == 1, "Agent unnecessarily restarted full scan/translation workflow");
        const auto terms = QJsonDocument::fromJson(glossary()->GetText().toUtf8()).object().value("terms").toArray();
        for (const auto &pair : QList<QPair<QString, QString>> {{QStringLiteral("ミナ"), correction ? QStringLiteral("美奈") : QStringLiteral("米娜")}, {QStringLiteral("リク"), QStringLiteral("陆")}}) {
            bool found = false;
            for (const auto &row : terms) if (row.toObject().value("source") == pair.first && row.toObject().value("target") == pair.second) found = true;
            require(found, "Saved glossary does not contain the user's character mapping");
        }
        for (const auto &tool : tools) require(!tool.startsWith("content.patch") && tool != "resource.replace_text"
            && tool != "content.replace_regex" && tool != "resource.patch_fragment", "Agent bypassed native translation/correction tools");
        require(read(source) == original && window.GetCurrentBook()->IsModified(), "Agent saved or changed the original fixture file");
        result["name_mentions"] = mentions; result["matching_name_mentions"] = matching; result["samples"] = samples;
        result["riku_mentions"] = rikuMentions; result["matching_riku_mentions"] = rikuMatching;
        result["glossary"] = QJsonDocument::fromJson(glossary()->GetText().toUtf8()).object();
        ExportEPUB(source + '.' + name + ".epub", window.GetCurrentBook()).WriteBook();
        result["passed"] = true;
    } catch (const std::exception &error) { failure = QString::fromUtf8(error.what()); result["error"] = failure; }
    snapshot(); deadline.stop(); native->control({{"action", "stop"}});
    QObject::disconnect(observation);
    window.GetCurrentBook()->SetModified(false); window.close();
    std::cout << name.toStdString() << ": " << (failure.isEmpty() ? "passed" : "failed") << ", " << elapsed.elapsed() << " ms\n" << std::flush;
    return result;
}

int main(int argc, char **argv)
{
    QCoreApplication::setAttribute(Qt::AA_DisableShaderDiskCache); QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QWebEngineUrlScheme scheme("sigil"); scheme.setSyntax(QWebEngineUrlScheme::Syntax::Path);
    scheme.setFlags(QWebEngineUrlScheme::SecureScheme | QWebEngineUrlScheme::LocalScheme | QWebEngineUrlScheme::LocalAccessAllowed | QWebEngineUrlScheme::ContentSecurityPolicyIgnored | QWebEngineUrlScheme::FetchApiAllowed);
    QWebEngineUrlScheme::registerScheme(scheme); MainApplication app(argc, argv);
    QResource::registerResource(QCoreApplication::applicationDirPath() + "/../Resources/main.rcc");
    try {
        require(argc == 4, "Expected source root, synthetic EPUB and smoke/live mode");
        const QString root = QString::fromLocal8Bit(argv[1]), source = QString::fromLocal8Bit(argv[2]);
        const QByteArray mode(argv[3]); const bool live = mode.startsWith("live");
        const QString configuration = qEnvironmentVariable("SIGIL_LIVE_AGENT_CONFIG");
        require(!configuration.isEmpty(), "Missing explicit Agent configuration path");
        SettingsStore original(configuration), isolated; original.beginGroup("native_agent"); isolated.beginGroup("native_agent");
        for (const QString &key : original.childKeys()) if (!key.startsWith("provider_")) isolated.setValue(key, original.value(key));
        isolated.sync();
        AgentSettings model; require(!model.apiKey().isEmpty() && !model.model().isEmpty(), "Existing Agent connection is incomplete");
        model.setDefaultMode(AgentMode::Auto); model.setMaxModelSteps(16); model.setMaxToolCalls(64); model.setFirstTokenTimeoutSeconds(90);
        SettingsStore settings; settings.setCleanOn(0); settings.setPreserveOPFSource(true);
        auto &python = EmbeddedPython::instance(); python.addToPythonSysPath(qEnvironmentVariable("SIGIL_TEST_PYTHON_ROOT"));
        python.addToPythonSysPath(root + "/src/Resource_Files/plugin_launchers/python"); python.addToPythonSysPath(root + "/src/Resource_Files/python3lib");
        bool passed = true;
        if (mode != "live-recovery") passed = scenario(source, "normal", false, live, live).value("passed").toBool();
        if (live && mode != "live-normal") passed = scenario(source, "recovery", true, false, true).value("passed").toBool() && passed;
        return passed ? 0 : 1;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
