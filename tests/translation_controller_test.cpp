#include <atomic>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QThread>
#include <QFile>
#include <QFutureWatcher>
#include "Agent/Execution/MemoryBookWorkspace.h"
#include "Agent/Tools/TranslationTools.h"
#include "Agent/Tools/ToolRegistry.h"
#include "Agent/Security/PermissionPolicy.h"
#include "Agent/Model/AgentProviderPreset.h"
#include "Agent/Model/ModelRetry.h"
#include "BuiltinPlugins/Translation/TranslationController.h"

using namespace BuiltinPlugins::Translation;
using namespace SigilAgent;
static void require(bool ok, const char *message) { if (!ok) qFatal("%s", message); }
static bool waitFor(const std::function<bool()> &condition, int timeout = 10000)
{
    QElapsedTimer elapsed; elapsed.start();
    while (!condition() && elapsed.elapsed() < timeout) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(1);
    }
    return condition();
}
static MemoryBookWorkspace makeBook(int count)
{
    MemoryBookWorkspace book;
    QString body;
    for (int i = 0; i < count; ++i) body += QStringLiteral("<p id=\"s%1\">原段%1これは完全な文章です。名前と会話を残す。</p>").arg(i);
    book.addResource({"chapter", "OPS/Text/ch.xhtml", "application/xhtml+xml", "xhtml",
        QStringLiteral("<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>T</title></head><body>") + body + "</body></html>"});
    book.setSpine({"chapter"}); return book;
}
static RequestConfiguration configuration()
{
    RequestConfiguration c; c.agent.baseUrl = c.translator.baseUrl = "http://127.0.0.1/mock";
    c.agent.model = "planner"; c.translator.model = "translator"; c.translator.thinking = false;
    c.agent.apiKey = c.translator.apiKey = "synthetic-loopback-key";
    c.agentContext = c.translatorContext = 128000; return c;
}
static ModelTurn reply(const ModelRequest &request)
{
    const auto payload = QJsonDocument::fromJson(request.messages.last().content.toUtf8()).object();
    QJsonObject response;
    if (request.messages.first().content.contains(QStringLiteral("完整阅读本批"))
        || request.messages.first().content.contains(QStringLiteral("整理候选词表"))) {
        response = {{"terms", QJsonArray {QJsonObject {{"source", "名前"}, {"target", "名字"}}}}, {"memory", "短篇记忆"}};
    } else if (request.messages.first().content.contains(QStringLiteral("校正全部"))) response = {{"changes", QJsonArray {}}};
    else {
        QJsonArray items;
        for (const auto &row : payload.value("items").toArray()) {
            const auto item = row.toObject();
            const QString translated = QStringLiteral("译：") + item.value("source").toString();
            items.append(QJsonObject {{"n", item.value("n")}, {"text", translated}});
        }
        response = {{"items", items}};
    }
    ModelTurn turn; turn.content = QString::fromUtf8(QJsonDocument(response).toJson(QJsonDocument::Compact));
    turn.finishReason = "stop"; turn.usage.inputTokens = 10; turn.usage.outputTokens = 5; return turn;
}

static int benchmark(bool live, bool fullMatrix, bool workflow)
{
    auto settings = configuration();
    if (live) {
        settings.translator.baseUrl = qEnvironmentVariable("SIGIL_TRANSLATION_BENCH_URL");
        settings.translator.apiKey = qEnvironmentVariable("SIGIL_TRANSLATION_BENCH_API_KEY");
        settings.translator.model = qEnvironmentVariable("SIGIL_TRANSLATION_BENCH_MODEL");
        if (settings.translator.baseUrl.isEmpty() || settings.translator.apiKey.isEmpty() || settings.translator.model.isEmpty()) {
            qCritical("Live benchmark requires SIGIL_TRANSLATION_BENCH_URL/API_KEY/MODEL; no request was sent."); return 2;
        }
        settings.translatorContext = qEnvironmentVariableIntValue("SIGIL_TRANSLATION_BENCH_CONTEXT");
        if (settings.translatorContext < 4096) settings.translatorContext = 32768;
        const auto kind = providerKindFromName(qEnvironmentVariable("SIGIL_TRANSLATION_BENCH_PROVIDER", "custom"));
        settings.translator.baseUrl = chatCompletionsUrl(kind, settings.translator.baseUrl);
        settings.translator.reasoningProtocol = reasoningProtocolFor(kind, settings.translator.baseUrl);
        settings.translator.openCodeGo = kind == AgentProviderKind::OpenCodeGo;
        settings.translator.userAgent = agentUserAgent(); settings.translator.httpReferer = agentHttpReferer(); settings.translator.httpTitle = agentHttpTitle();
        settings.agent = settings.translator; settings.agentContext = settings.translatorContext;
        const QString planner = qEnvironmentVariable("SIGIL_TRANSLATION_BENCH_AGENT_MODEL");
        if (!planner.isEmpty()) settings.agent.model = planner;
    }
    const QList<int> concurrencies = workflow ? QList<int> {2} : live && !fullMatrix ? QList<int> {1, 4} : QList<int> {1, 2, 4, 8};
    const QList<int> targets = live && !fullMatrix ? QList<int> {3000} : QList<int> {1500, 3000, 4500};
    QJsonArray report;
    for (int target : targets) for (int concurrency : concurrencies) {
        auto book = makeBook(live ? 180 : 600);
        if (live) {
            const QStringList sample {
                QStringLiteral("ミナは窓の外を見た。「リク、雨が止んだら、約束の場所へ行こう。」"),
                QStringLiteral("リクは頷いた。彼がポケットから取り出したのは、昨日ミナが渡した小さな鍵だった。"),
                QStringLiteral("町の時計台が六時を告げた。しかし、二人が待っていた列車は、まだ姿を見せなかった。"),
                QStringLiteral("「怖くないと言えば嘘になる。でも、一人で待つよりはずっといい」とミナは笑った。"),
                QStringLiteral("その笑顔を見て、リクはようやく気づいた。守るべき約束は、遠い昔のものだけではない。"),
                QStringLiteral("雨音が少しずつ弱くなった。ミナは鍵を受け取り、もう一度だけ駅の入り口を振り返った。")};
            QString body;
            for (int i = 0; i < (workflow ? 6 : 180); ++i) body += QStringLiteral("<p id=\"p%1\">%2</p>").arg(i).arg(sample.at(i % sample.size()));
            book.beginTransaction("synthetic original text");
            book.replaceText("chapter", QStringLiteral("<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>Original synthetic scene</title></head><body>") + body + "</body></html>", book.resourceRevision("chapter"));
            book.commitTransaction(book.revision());
        }
        TranslationController controller; controller.setWorkspace(&book);
        settings.concurrency = concurrency; settings.translationCharacters = target;
        controller.setConfigurationProvider([settings]() { return settings; });
        if (!live) controller.setCompletion([](const auto &, const auto &request, const auto &) { QThread::msleep(15); return reply(request); });
        require(controller.prepare().ok, "benchmark preparation");
        if (live) controller.context({{"kind", "glossary"}, {"action", "update"}, {"value", QJsonArray {
            QJsonObject {{"source", "ミナ"}, {"target", "米娜"}}, QJsonObject {{"source", "リク"}, {"target", "陆"}}}}});
        QElapsedTimer elapsed; elapsed.start();
        const QJsonArray stages = workflow ? QJsonArray {"scan", "aggregate", "translate", "review"} : QJsonArray {"translate"};
        require(controller.run({{"stages", stages}}).ok, "benchmark start");
        require(waitFor([&]() { return !controller.running(); }, live ? 300000 : 15000), "benchmark completes");
        auto row = controller.status(); row.remove("session_id");
        row["transport"] = live ? "live" : "mock"; row["target"] = target; row["concurrency"] = concurrency; row["elapsed_ms"] = elapsed.elapsed();
        row["semantic_quality_evaluated"] = false;
        row["workflow"] = workflow; if (live) row["model"] = settings.translator.model;
        if (live) {
            QJsonArray samples; int expected = 0, matched = 0;
            for (const auto &paragraph : extract(book.resourceText("chapter")).paragraphs) {
                if (samples.size() < 6) samples.append(QJsonObject {{"source", paragraph.text}, {"translation", paragraph.translation}});
                for (const auto &pair : QList<QPair<QString, QString>> {{"ミナ", "米娜"}, {"リク", "陆"}})
                    if (paragraph.text.contains(pair.first)) { ++expected; matched += paragraph.translation.contains(pair.second); }
            }
            row["samples"] = samples; row["expected_term_mentions"] = expected; row["matched_term_mentions"] = matched;
        }
        report.append(row);
        qInfo().noquote() << QJsonDocument(row).toJson(QJsonDocument::Compact);
        require(row.value("failed").toInt() == 0 && row.value("translated") == row.value("paragraphs"), "benchmark preserves complete coverage");
    }
    const QString output = qEnvironmentVariable("SIGIL_TRANSLATION_BENCH_REPORT");
    if (!output.isEmpty()) { QFile file(output); require(file.open(QIODevice::WriteOnly), "benchmark report file"); file.write(QJsonDocument(report).toJson(QJsonDocument::Indented)); }
    return 0;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().contains("--benchmark")) return benchmark(app.arguments().contains("--live"), app.arguments().contains("--full-matrix"), app.arguments().contains("--workflow"));
    auto book = makeBook(300);
    TranslationController controller;
    controller.setWorkspace(&book); controller.setMiscFolder("OPS/Misc");
    controller.setConfigurationProvider(configuration);
    std::atomic_int calls {0};
    controller.setCompletion([&](const OpenAIProviderConfig &, const ModelRequest &request, const auto &) {
        ++calls; require(request.tools.isEmpty(), "worker never receives Agent tool schemas");
        if (request.model == "translator") require(!request.thinking, "translation thinking independent of planner");
        return reply(request);
    });
    require(controller.prepare().ok, "prepare current memory book");
    require(controller.status().value("paragraphs").toInt() == 300, "correct natural paragraph count");
    require(controller.run({{"stages", QJsonArray {"scan", "aggregate", "translate", "review"}}}).ok, "start whole workflow");
    require(waitFor([&]() { return !controller.running(); }), "asynchronous workflow finishes");
    auto status = controller.status();
    require(status.value("scanned").toInt() == 300 && status.value("translated").toInt() == 300
            && status.value("reviewed").toInt() == 300 && status.value("failed").toInt() == 0, "entire scope processed");
    require(calls.load() < 15, "model call count scales with batches, not sentences");
    require(book.resourceText("chapter").count(QStringLiteral("se-translation\"")) == 300, "direct in-book bilingual writes");
    const int previous = calls.load();
    require(controller.run({{"stages", QJsonArray {"translate"}}}).ok, "run again");
    require(waitFor([&]() { return !controller.running(); }), "repeat completes");
    require(calls.load() == previous, "existing translations skip requests");
    require(controller.saveGlossary().ok, "glossary saved through book resource transaction");
    require(controller.saveGlossary().ok, "second glossary uses unique path");
    int glossaries = 0;
    for (const auto &row : book.resources()) if (row.toObject().value("book_path").toString().startsWith("OPS/Misc/translation-glossary")) ++glossaries;
    require(glossaries == 2, "real Misc folder and unique filenames");
    ToolRegistry registry; registerTranslationTools(&registry, &controller);
    require(registry.descriptors().size() == 8, "eight real native tools registered");
    PermissionPolicy policy;
    for (const auto &descriptor : registry.descriptors()) {
        if (descriptor.mutatesBook) require(policy.evaluate(AgentMode::Plan, descriptor) == PermissionAction::Deny
            && policy.evaluate(AgentMode::Ask, descriptor) == PermissionAction::Deny, "read-only/Plan cannot start book writes");
    }
    require(!registry.find("translation.prepare")->execute({{"arbitrary_shell", "bad"}}).ok, "unexpected tool fields rejected");
    require(registry.find("translation.status")->execute({}).ok, "status tool returns window state");
    auto *contextTool = registry.find("translation.context");
    require(contextTool->execute({{"kind", "glossary"}, {"action", "update"}, {"terms", QJsonArray {
        QJsonObject {{"source", "ミナ"}, {"target", "米娜"}, {"locked", true}}}}}).ok, "typed glossary tool parameter");
    require(contextTool->execute({{"kind", "glossary"}, {"action", "update"},
        {"value", QStringLiteral("[{\"source\":\"リク\",\"target\":\"陆\"}]")}}).ok, "provider JSON-string glossary accepted without extra model turns");
    require(contextTool->execute({{"kind", "plan"}, {"action", "update"}, {"plan", QJsonObject {{"goal", "translate"}}}}).ok, "typed temporary plan");
    require(contextTool->execute({{"kind", "memory"}, {"action", "update"}, {"memory", "short memory"}}).ok, "typed temporary memory");
    require(!contextTool->execute({{"kind", "glossary"}, {"action", "update"}, {"value", "[broken"}}).ok, "malformed string glossary rejected strictly");
    require(!contextTool->execute({{"kind", "glossary"}, {"action", "update"}, {"plan", QJsonObject {}}}).ok, "mismatched context field rejected");
    require(controller.context({{"kind", "glossary"}, {"action", "update"}, {"value", QJsonArray {
        QJsonObject {{"source", "本名"}, {"target", "旧译"}, {"aliases", QJsonArray {"名前"}}}}}}).ok, "manual alias imported");
    controller.setConfigurationProvider([]() { auto config = configuration(); config.reviewWithAgent = true; return config; });
    std::atomic_int aliasReviews {0};
    controller.setCompletion([&](const auto &, const ModelRequest &request, const auto &) {
        require(request.model == "planner", "review can independently select Agent model");
        const auto payload = QJsonDocument::fromJson(request.messages.last().content.toUtf8()).object();
        const auto terms = payload.value("terms").toArray();
        require(terms.size() == 1 && terms.first().toObject().value("target") == "新译", "term cache invalidates on explicit correction");
        aliasReviews += payload.value("items").toArray().size(); return reply(request);
    });
    require(controller.revise({{"terms", QJsonArray {QJsonObject {{"source", "本名"}, {"target", "新译"}}}}}).ok, "alias correction starts");
    require(waitFor([&]() { return !controller.running(); }) && aliasReviews.load() == 300, "correction selects paragraphs containing only an existing alias");
    require(controller.status().value("resource_cache_hits").toInt() > 0, "unchanged review resources reuse extraction");

    auto aggregateBook = makeBook(1);
    TranslationController aggregate;
    aggregate.setWorkspace(&aggregateBook); aggregate.setConfigurationProvider(configuration);
    QJsonArray candidates;
    for (int i = 0; i < 310; ++i) candidates.append(QJsonObject {{"source", QStringLiteral("人名%1").arg(i)}, {"target", QStringLiteral("译名%1").arg(i)}});
    require(aggregate.prepare().ok, "prepare aggregation test");
    require(aggregate.context({{"kind", "glossary"}, {"action", "update"}, {"value", candidates}}).ok, "load multiple glossary chunks");
    std::atomic_int aggregateCalls {0}; std::atomic_bool sawTail {false}, sawAllSummaries {false};
    aggregate.setCompletion([&](const auto &, const ModelRequest &request, const auto &) {
        ++aggregateCalls;
        const auto payload = QJsonDocument::fromJson(request.messages.last().content.toUtf8()).object();
        const auto terms = payload.value("terms").toArray();
        QString summary;
        for (const auto &term : terms) {
            if (term.toObject().value("source").toString() == "人名309") sawTail.store(true);
        }
        if (!terms.isEmpty()) summary = terms.first().toObject().value("source").toString();
        else {
            const auto memory = payload.value("memory").toString();
            sawAllSummaries.store(memory.contains("人名0") && memory.contains("人名120") && memory.contains("人名240"));
            summary = "聚合完成";
        }
        ModelTurn turn; turn.content = QString::fromUtf8(QJsonDocument(QJsonObject {{"terms", QJsonArray {}}, {"memory", summary}}).toJson());
        return turn;
    });
    require(aggregate.run({{"stages", QJsonArray {"aggregate"}}}).ok, "aggregate starts");
    require(waitFor([&]() { return !aggregate.running(); }), "hierarchical aggregate terminates");
    require(aggregateCalls.load() == 4 && sawTail.load() && sawAllSummaries.load(), "every glossary chunk and summary reaches aggregation");

    auto earlyBook = makeBook(1);
    TranslationController early;
    early.setWorkspace(&earlyBook); early.setConfigurationProvider(configuration);
    early.setCompletion([](const auto &, const auto &request, const auto &) { return reply(request); });
    require(early.prepare().ok && early.run({{"stages", QJsonArray {"translate"}}}).ok, "early pause run starts");
    require(early.control({{"action", "pause"}}).ok && early.control({{"action", "resume"}}).ok, "pause/resume before first event tick");
    require(waitFor([&]() { return !early.running(); }) && early.status().value("translated").toInt() == 1, "early resume cannot strand the first phase");

    auto rejectedBook = makeBook(1);
    TranslationController rejected;
    rejected.setWorkspace(&rejectedBook); rejected.setConfigurationProvider(configuration);
    std::atomic_int rejectedCalls {0};
    rejected.setCompletion([&](const auto &, const auto &, const auto &) {
        ++rejectedCalls; ModelTurn turn; turn.error = "HTTP 401: invalid key"; return turn;
    });
    require(rejected.prepare().ok && rejected.run({{"stages", QJsonArray {"translate"}}}).ok, "authentication failure run starts");
    require(waitFor([&]() { return !rejected.running(); }), "authentication failure stops workflow");
    require(rejectedCalls.load() == 1 && rejected.status().value("failed").toInt() == 1, "permanent errors do not fan out into retries");

    auto retryBook = makeBook(1);
    TranslationController network; network.setWorkspace(&retryBook); network.setConfigurationProvider(configuration);
    std::atomic_int networkCalls {0};
    network.setCompletion([&](const auto &, const auto &request, const auto &) {
        if (++networkCalls <= 5) { ModelTurn turn; turn.error = "HTTP error: 连接已关闭"; turn.retryableError = true; return turn; }
        return reply(request);
    });
    require(network.prepare().ok && network.run({{"stages", QJsonArray {"translate"}}}).ok
        && waitFor([&]() { return !network.running(); }, 12000), "five network retries settle");
    require(networkCalls == 6 && network.status().value("network_retries").toInt() == 5
        && network.status().value("written").toInt() == 1, "last network retry recovers and writes once");
    require(network.diagnostics().value("recent_requests").toArray().size() == 6, "bounded worker diagnostics retain retry and output metadata");
    require(modelRetryDelay(1, 3000) >= 3000 && modelRetryDelay(5) <= 4000, "Retry-After and exponential backoff bounds");

    auto scopeBook = makeBook(6);
    TranslationController scope; scope.setWorkspace(&scopeBook); scope.setConfigurationProvider(configuration);
    ToolRegistry scopedTools; registerTranslationTools(&scopedTools, &scope);
    scope.setCompletion([](const auto &, const auto &request, const auto &) { return reply(request); });
    require(scopedTools.find("translation.prepare")->execute({{"paragraph_offset", 1}, {"paragraph_limit", 3}}).ok
        && scope.status().value("paragraphs").toInt() == 3, "prepare just a few reading paragraphs");
    const auto rows = scopedTools.find("translation.status")->execute({{"include_paragraphs", true}, {"offset", 1}, {"limit", 1}});
    require(rows.ok && rows.data.value("items").toArray().size() == 1
        && rows.data.value("items").toArray().first().toObject().value("index").toInt() == 1, "status exposes bounded stable scope indexes");
    require(!scope.run({{"stages", QJsonArray {}}}).ok && !scope.run({{"only_paragraphs", QJsonArray {9}}}).ok
        && !scope.run({{"only_paragraphs", QJsonArray {}}}).ok, "invalid narrow requests must not silently run the whole book");
    require(scopedTools.find("translation.run")->execute({{"stages", QJsonArray {"translate"}}, {"only_paragraphs", QJsonArray {1}}}).ok
        && waitFor([&]() { return !scope.running(); }), "translate exactly one prepared paragraph");
    require(scope.status().value("translated").toInt() == 1 && scope.status().value("scanned").toInt() == 0
        && scope.status().value("reviewed").toInt() == 0, "small sample does not introduce scan/review work");
    const int narrowRequests = scope.status().value("requests").toInt();
    require(scope.control({{"action", "retry_failed"}}).ok && !scope.running() && scope.status().value("requests").toInt() == narrowRequests,
        "retry must not expand a completed sample to untranslated paragraphs outside its requested scope");
    const auto narrowResult = extract(scopeBook.resourceText("chapter"));
    require(narrowResult.paragraphs.at(2).translation.size() > 0 && narrowResult.paragraphs.at(1).translation.isEmpty()
        && narrowResult.paragraphs.at(3).translation.isEmpty(), "prepared index maps to the right original paragraph");

    scope.setCompletion([](const auto &, const auto &, const auto &) { ModelTurn turn; turn.error = "fixture scan failure"; return turn; });
    require(scope.run({{"stages", QJsonArray {"scan"}}}).ok && waitFor([&]() { return !scope.running(); })
        && scope.status().value("failed").toInt() == 3, "fixture leaves earlier analysis incomplete");
    scope.setCompletion([](const auto &, const auto &request, const auto &) { return reply(request); });
    require(scope.run({{"stages", QJsonArray {"translate"}}, {"only_paragraphs", QJsonArray {0}}}).ok
        && waitFor([&]() { return !scope.running(); }) && scope.status().value("state") == "completed"
        && scope.status().value("failed").toInt() == 0 && scope.status().value("written").toInt() == 2,
        "a new explicit sample must not inherit unrelated scan issues or expand its scope");
    require(scope.control({{"action", "retry_failed"}}).ok && !scope.running(), "sample retry must not silently restart earlier analysis stages");

    auto scanBook = makeBook(4);
    TranslationController scanRetry; scanRetry.setWorkspace(&scanBook); scanRetry.setConfigurationProvider(configuration);
    std::atomic_bool failScan {true}; std::atomic_int scanCount {0}, translateCount {0};
    scanRetry.setCompletion([&](const auto &, const ModelRequest &request, const auto &) {
        if (request.messages.first().content.contains(QStringLiteral("完整阅读本批"))) {
            ++scanCount;
            if (failScan) { ModelTurn turn; turn.error = "temporary fixture failure"; return turn; }
        }
        if (request.messages.first().content.contains(QStringLiteral("完整翻译每一"))) ++translateCount;
        return reply(request);
    });
    require(scanRetry.prepare().ok && scanRetry.run({{"stages", QJsonArray {"scan", "aggregate", "translate"}}}).ok
        && waitFor([&]() { return !scanRetry.running(); }), "partial scan workflow settles");
    require(scanRetry.status().value("stage_failures").toObject().value("scan").toInt() == 4
        && scanRetry.status().value("written").toInt() == 4, "successful translation must not erase scan failures or hide Book writes");
    const int previousTranslateCount = translateCount;
    failScan.store(false);
    require(scanRetry.control({{"action", "retry_failed"}}).ok && waitFor([&]() { return !scanRetry.running(); })
        && scanRetry.status().value("scanned").toInt() == 4 && translateCount == previousTranslateCount
        && scanRetry.status().value("failed").toInt() == 0, "retry completes scan/aggregation without retranslating completed text");

    auto truncatedBook = makeBook(10);
    TranslationController truncated; truncated.setWorkspace(&truncatedBook); truncated.setConfigurationProvider(configuration);
    std::atomic_int truncationCalls {0}, firstBudget {0}; std::atomic_bool biggerBudget {false};
    truncated.setCompletion([&](const auto &, const ModelRequest &request, const auto &) {
        if (++truncationCalls == 1) { firstBudget = request.maxOutputTokens; ModelTurn turn; turn.finishReason = "length"; return turn; }
        if (request.maxOutputTokens > firstBudget) biggerBudget = true;
        return reply(request);
    });
    require(truncated.prepare().ok && truncated.run({{"stages", QJsonArray {"scan"}}}).ok
        && waitFor([&]() { return !truncated.running(); }) && biggerBudget && truncated.status().value("scanned").toInt() == 10,
        "output truncation splits input and increases output reserve without network retry amplification");

    auto formatBook = makeBook(1);
    TranslationController ownFormat; ownFormat.setWorkspace(&formatBook); ownFormat.setConfigurationProvider(configuration);
    ownFormat.setCompletion([](const auto &, const auto &request, const auto &) { return reply(request); });
    ownFormat.setFormatHandler([&](const QJsonObject &) {
        auto text = formatBook.resourceText("chapter"); text.replace(QStringLiteral("これは完全"), QStringLiteral("これは 完全"));
        auto result = formatBook.beginTransaction("native formatting");
        if (result.ok) result = formatBook.replaceText("chapter", text, formatBook.resourceRevision("chapter"));
        if (result.ok) result = formatBook.commitTransaction(formatBook.revision());
        return result;
    });
    require(ownFormat.prepare().ok && ownFormat.run({{"stages", QJsonArray {"translate", "format"}}}).ok
        && waitFor([&]() { return !ownFormat.running(); }) && ownFormat.run({{"stages", QJsonArray {"review"}}}).ok
        && waitFor([&]() { return !ownFormat.running(); }) && ownFormat.status().value("reviewed").toInt() == 1
        && ownFormat.status().value("failed").toInt() == 0, "native format changes must refresh identities before later review");

    ownFormat.setCompletion([](const auto &, const auto &, const auto &) { ModelTurn turn; turn.error = "fixture batch failure"; return turn; });
    require(ownFormat.run({{"stages", QJsonArray {"translate"}}, {"retranslate", true}}).ok
        && waitFor([&]() { return !ownFormat.running(); }) && ownFormat.status().value("failed").toInt() == 1
        && ownFormat.status().value("written").toInt() == 1, "failed retranslation retains the existing Book translation");
    const int beforeRetranslationRetry = ownFormat.status().value("requests").toInt();
    ownFormat.setCompletion([](const auto &, const auto &request, const auto &) { return reply(request); });
    require(ownFormat.control({{"action", "retry_failed"}}).ok && waitFor([&]() { return !ownFormat.running(); })
        && ownFormat.status().value("failed").toInt() == 0 && ownFormat.status().value("requests").toInt() == beforeRetranslationRetry + 1,
        "retry must redo failed retranslation even when the older translation still exists");

    auto sessionBook = makeBook(1);
    TranslationController isolated;
    isolated.setWorkspace(&sessionBook); isolated.setConfigurationProvider(configuration);
    isolated.bindAgentSession("agent-A");
    require(isolated.prepare({{"language", "fr"}, {"style", "A-only style"}}).ok, "prepare session A");
    require(isolated.context({{"kind", "memory"}, {"action", "update"}, {"value", "private A memory"}}).ok, "session A memory");
    require(isolated.context({{"kind", "glossary"}, {"action", "update"}, {"value", QJsonArray {
        QJsonObject {{"source", "名前"}, {"target", "A-only name"}}}}}).ok, "session A glossary");
    require(isolated.prepare().ok && isolated.context({{"kind", "plan"}}).data.value("plan").toObject().value("language") == "fr",
        "prepare retains language and style only within the same Agent conversation");
    const QString oldContext = isolated.contextId();
    isolated.bindAgentSession("agent-A"); require(isolated.contextId() == oldContext, "same-session binding preserves prepared context");
    ToolRegistry sessionATools; registerTranslationTools(&sessionATools, &isolated);
    std::atomic_bool sessionEntered {false}, sessionRelease {false};
    isolated.setCompletion([&](const auto &, const ModelRequest &request, const auto &) {
        sessionEntered.store(true);
        while (!sessionRelease.load()) QThread::msleep(1); // Deliberately ignore cancellation.
        return reply(request);
    });
    int sessionFinishes = 0;
    QObject::connect(&isolated, &TranslationController::finished, [&]() { ++sessionFinishes; });
    require(isolated.run({{"stages", QJsonArray {"translate"}}}).ok && waitFor([&]() { return sessionEntered.load(); }), "old session request in flight");
    auto *oldWatcher = isolated.findChild<QFutureWatcherBase *>(); require(oldWatcher, "old request watcher");
    bool oldDelivered = false;
    QObject::connect(oldWatcher, &QFutureWatcherBase::finished, &isolated, [&]() { oldDelivered = true; });
    isolated.bindAgentSession("agent-B");
    require(isolated.agentSessionId() == "agent-B" && isolated.contextId().isEmpty()
        && isolated.status().value("state") == "idle" && isolated.status().value("terms").toInt() == 0
        && isolated.status().value("requests").toInt() == 0 && isolated.status().value("paragraphs").toInt() == 0,
        "new Agent conversation clears progress, glossary and prepared scope");
    require(!sessionATools.find("translation.status")->execute({}).ok && !sessionATools.find("translation.prepare")->execute({}).ok,
        "old conversation tool references cannot inspect or alter the new conversation");
    ToolRegistry sessionBTools; registerTranslationTools(&sessionBTools, &isolated);
    require(sessionBTools.find("translation.status")->execute({}).ok, "new session receives fresh tools");
    require(!sessionBTools.find("translation.status")->execute({{"session_id", oldContext}}).ok
        && !sessionBTools.find("translation.prepare")->execute({{"session_id", oldContext}}).ok,
        "old scope IDs cannot read or prepare state in the new conversation");
    require(isolated.prepare().ok && isolated.context({{"kind", "glossary"}}).data.value("total").toInt() == 0
        && isolated.context({{"kind", "memory"}}).data.value("memory").toString().isEmpty()
        && isolated.context({{"kind", "plan"}}).data.value("plan").toObject().value("language") == "zh-Hans",
        "new session does not inherit language, memory or terminology");
    require(isolated.context({{"kind", "memory"}, {"action", "update"}, {"value", "B memory"}}).ok, "write B memory");
    isolated.setCompletion([](const auto &, const ModelRequest &request, const auto &) { return reply(request); });
    require(isolated.run({{"stages", QJsonArray {"translate"}}}).ok && waitFor([&]() { return !isolated.running(); }), "new conversation completes independently");
    const QString bookAfterB = sessionBook.resourceText("chapter");
    sessionRelease.store(true);
    require(waitFor([&]() { return oldDelivered; }), "old provider finally replies");
    require(sessionBook.resourceText("chapter") == bookAfterB && sessionFinishes == 1 && isolated.agentSessionId() == "agent-B"
        && isolated.context({{"kind", "memory"}}).data.value("memory") == "B memory", "late A result cannot write, notify or overwrite B memory");
    isolated.bindAgentSession("agent-A");
    require(isolated.contextId().isEmpty() && isolated.status().value("terms").toInt() == 0
        && sessionBook.resourceText("chapter") == bookAfterB, "discarded conversations stay empty; Book edits survive");
    auto replacementBook = makeBook(1); isolated.setWorkspace(&replacementBook);
    require(isolated.agentSessionId() == "agent-A" && isolated.contextId().isEmpty(), "changing books clears context within the same conversation");

    auto commitBook = makeBook(1);
    TranslationController duringCommit; duringCommit.setWorkspace(&commitBook); duringCommit.setConfigurationProvider(configuration);
    duringCommit.bindAgentSession("commit-A"); duringCommit.setCompletion([](const auto &, const auto &request, const auto &) { return reply(request); });
    QObject::connect(&duringCommit, &TranslationController::bookModified, [&]() { duringCommit.bindAgentSession("commit-B"); });
    require(duringCommit.prepare().ok && duringCommit.run({{"stages", QJsonArray {"translate"}}}).ok, "session change during commit signal");
    require(waitFor([&]() { return duringCommit.agentSessionId() == "commit-B"; })
        && duringCommit.status().value("state") == "idle" && duringCommit.status().value("paragraphs").toInt() == 0,
        "reentrant conversation change during book update cannot reuse old paragraph indexes");

    auto partialBook = makeBook(120);
    TranslationController partial;
    partial.setWorkspace(&partialBook); partial.setConfigurationProvider(configuration);
    std::atomic_bool dropped {false}; std::atomic_int smallest {10000};
    partial.setCompletion([&](const auto &, const ModelRequest &request, const auto &) {
        auto turn = reply(request);
        const auto payload = QJsonDocument::fromJson(request.messages.last().content.toUtf8()).object();
        const int count = payload.value("items").toArray().size();
        if (count < smallest.load()) smallest.store(count);
        if (!dropped.exchange(true)) {
            auto object = QJsonDocument::fromJson(turn.content.toUtf8()).object();
            auto items = object.value("items").toArray(); items.removeLast(); object["items"] = items;
            turn.content = QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
        }
        return turn;
    });
    require(partial.prepare().ok && partial.run({{"stages", QJsonArray {"translate"}}}).ok, "partial run starts");
    require(waitFor([&]() { return !partial.running(); }), "partial workflow completes");
    require(partial.status().value("translated").toInt() == 120 && smallest.load() == 1, "only one missing complete paragraph retried");

    auto editedBook = makeBook(2);
    TranslationController edited;
    edited.setWorkspace(&editedBook); edited.setConfigurationProvider(configuration);
    std::atomic_bool entered {false}, release {false};
    edited.setCompletion([&](const auto &, const ModelRequest &request, const auto &cancel) {
        entered.store(true);
        while (!release.load() && !cancel->load()) QThread::msleep(1);
        return reply(request);
    });
    require(edited.prepare().ok && edited.run({{"stages", QJsonArray {"translate"}}}).ok, "late result test starts");
    require(waitFor([&]() { return entered.load(); }), "request in flight");
    require(edited.control({{"action", "pause"}}).ok, "pause accepted immediately");
    QString changed = editedBook.resourceText("chapter"); changed.replace("原段0", "人工改0");
    require(editedBook.beginTransaction("manual").ok, "manual transaction");
    require(editedBook.replaceText("chapter", changed, editedBook.resourceRevision("chapter")).ok, "manual source edit");
    require(editedBook.commitTransaction(editedBook.revision()).ok, "manual source commit");
    release.store(true);
    require(waitFor([&]() { return edited.status().value("in_flight").toInt() == 0; }), "in-flight settles while paused");
    require(edited.status().value("state").toString() == "paused", "user pause preserved");
    require(editedBook.resourceText("chapter").contains("人工改0"), "human source retained");
    require(edited.status().value("failed").toInt() == 1 && edited.status().value("translated").toInt() == 1, "only stale paragraph skipped");
    require(edited.control({{"action", "resume"}}).ok && waitFor([&]() { return !edited.running(); }), "resume after settling");

    auto cancelledBook = makeBook(1);
    auto longBook = makeBook(0);
    const QString longSource = QStringLiteral("これは長い段落です😀。人物の口調も残します。\n").repeated(900);
    longBook.beginTransaction("long paragraph");
    longBook.replaceText("chapter", QStringLiteral("<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>T</title></head><body><p>") + longSource + "</p></body></html>", longBook.resourceRevision("chapter"));
    longBook.commitTransaction(longBook.revision());
    TranslationController longRun; longRun.setWorkspace(&longBook); longRun.setConfigurationProvider(configuration);
    std::atomic_int longCalls {0}; int commits = 0;
    QObject::connect(&longRun, &TranslationController::bookModified, [&]() { ++commits; });
    longRun.setCompletion([&](const auto &, const ModelRequest &request, const auto &) {
        const auto payload = QJsonDocument::fromJson(request.messages.last().content.toUtf8()).object();
        if (!request.messages.first().content.contains(QStringLiteral("校正全部"))) {
            ++longCalls;
            for (const auto &row : payload.value("items").toArray()) {
                require(row.toObject().value("parts").toInt() > 1, "long paragraph sent as numbered fragments");
                require(row.toObject().value("source").toString().toUcs4().size() <= 4500, "fragment stays bounded");
            }
        }
        return reply(request);
    });
    require(longRun.prepare().ok && longRun.run({{"stages", QJsonArray {"translate", "review"}}}).ok, "long paragraph workflow starts");
    require(waitFor([&]() { return !longRun.running(); }), "long paragraph workflow completes");
    require(longCalls.load() > 1 && longRun.status().value("translated").toInt() == 1
        && longRun.status().value("reviewed").toInt() == 1 && longRun.status().value("failed").toInt() == 0, "fragments assemble into one reviewed paragraph");
    require(longBook.resourceText("chapter").count(QStringLiteral("class=\"se-translation\"")) == 1 && commits == 2, "each phase writes only after all fragments arrive");
    auto failedLongBook = makeBook(0);
    failedLongBook.beginTransaction("long source");
    const QString untranslated = QStringLiteral("<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>T</title></head><body><p>") + longSource + "</p></body></html>";
    failedLongBook.replaceText("chapter", untranslated, failedLongBook.resourceRevision("chapter")); failedLongBook.commitTransaction(failedLongBook.revision());
    TranslationController failedLong; failedLong.setWorkspace(&failedLongBook); failedLong.setConfigurationProvider(configuration);
    failedLong.setCompletion([](const auto &, const ModelRequest &request, const auto &) {
        const auto rows = QJsonDocument::fromJson(request.messages.last().content.toUtf8()).object().value("items").toArray();
        if (!rows.isEmpty() && rows.first().toObject().value("part").toInt() == 2) { ModelTurn turn; turn.content = "invalid JSON"; return turn; }
        QThread::msleep(5); return reply(request);
    });
    require(failedLong.prepare().ok && failedLong.run({{"stages", QJsonArray {"translate"}}}).ok, "fragment failure starts");
    require(waitFor([&]() { return !failedLong.running(); }), "fragment failure terminates");
    require(failedLong.status().value("failed").toInt() == 1 && failedLong.status().value("translated").toInt() == 0
        && failedLongBook.resourceText("chapter") == untranslated, "one failed fragment retains failure and writes no partial paragraph");
    failedLong.setCompletion([](const auto &, const auto &request, const auto &) { return reply(request); });
    require(failedLong.control({{"action", "retry_failed"}}).ok && waitFor([&]() { return !failedLong.running(); })
        && failedLong.status().value("translated").toInt() == 1 && failedLong.status().value("failed").toInt() == 0, "failed assembled paragraph can be retried successfully");

    TranslationController stopped;
    stopped.setWorkspace(&cancelledBook); stopped.setConfigurationProvider(configuration);
    entered.store(false); release.store(false);
    stopped.setCompletion([&](const auto &, const ModelRequest &request, const auto &cancel) {
        entered.store(true); while (!release.load() && !cancel->load()) QThread::msleep(1); return reply(request);
    });
    require(stopped.prepare().ok && stopped.run({{"stages", QJsonArray {"translate"}}}).ok, "cancel test starts");
    require(waitFor([&]() { return entered.load(); }), "cancel request in flight");
    stopped.control({{"action", "stop"}}); stopped.reset(); release.store(true);
    waitFor([&]() { return stopped.status().value("in_flight").toInt() == 0; });
    QCoreApplication::processEvents();
    require(!cancelledBook.resourceText("chapter").contains("se-translation"), "stopped/closed context cannot write late");
    qInfo("Translation controller tests passed");
}
