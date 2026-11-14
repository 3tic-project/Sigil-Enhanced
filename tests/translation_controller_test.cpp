#include <atomic>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QThread>
#include "Agent/Execution/MemoryBookWorkspace.h"
#include "Agent/Tools/TranslationTools.h"
#include "Agent/Tools/ToolRegistry.h"
#include "Agent/Security/PermissionPolicy.h"
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

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
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
