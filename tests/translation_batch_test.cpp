#include <QCoreApplication>
#include <QDebug>
#include <QJsonDocument>
#include "BuiltinPlugins/Translation/TranslationBatch.h"

using namespace BuiltinPlugins::Translation;
static void require(bool ok, const char *message) { if (!ok) qFatal("%s", message); }

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QList<BatchText> items;
    for (int i = 0; i < 1000; ++i) items.append({i, QString(20, QLatin1Char('x')), {}, {}, {}});
    auto batches = makeBatches(items, {});
    require(batches.size() <= 10 && batches.front().size() > 15, "short dialogue packed by content, not fixed 15");
    int expected = 0;
    for (const auto &batch : batches) for (const auto &item : batch) require(item.paragraph == expected++, "reading order preserved");
    require(expected == 1000, "no dropped paragraph");
    items.clear();
    for (int i = 0; i < 197; ++i) items.append({i, QString(30, QLatin1Char('x')), {}, {}, {}});
    batches = makeBatches(items, {3000, 4500, 96});
    require(batches.last().size() > 30 && batches.last().size() <= 96, "tiny tail balanced without exceeding cap");
    const auto partial = parseBatch(QStringLiteral("translate"), QStringLiteral("{\"items\":[{\"n\":2,\"text\":\"二\"}]}"), 3);
    require(partial.error.isEmpty() && partial.texts.value(1) == QStringLiteral("二") && partial.missing.size() == 2, "partial success retained, only missing retried");
    for (const QString &bad : {QStringLiteral("{\"items\":[{\"n\":1,\"text\":\"A\"},{\"n\":1,\"text\":\"B\"}]}"),
         QStringLiteral("{\"items\":[{\"n\":0,\"text\":\"A\"}]}"), QStringLiteral("{\"items\":[{\"n\":1.1,\"text\":\"A\"}]}"),
         QStringLiteral("{\"items\":[{\"n\":\"1\",\"text\":\"A\"}]}"), QStringLiteral("{\"items\":[{\"n\":1,\"text\":12}]}"),
         QStringLiteral("```json\n{\"items\":[]}\n```"), QStringLiteral("{\"items\":[")}) {
        require(!parseBatch(QStringLiteral("translate"), bad, 2).error.isEmpty(), "malformed identity rejected");
    }
    require(parseBatch(QStringLiteral("review"), QStringLiteral("{\"changes\":[]}"), 10).error.isEmpty(), "empty correction is valid");
    require(!parseBatch(QStringLiteral("review"), QStringLiteral("{}"), 10).error.isEmpty(), "missing correction field not accepted");
    require(parseBatch(QStringLiteral("scan"), QStringLiteral("{\"terms\":[],\"memory\":\"\"}"), 10).error.isEmpty(), "empty terms valid");
    QString error;
    auto terms = normalizeTerms(QJsonArray {QJsonObject {{"src", "アイ"}, {"dst", "爱"}},
        QJsonObject {{"source", "Alice"}, {"target", "爱丽丝"}, {"locked", true}},
        QJsonObject {{"source", "Alice Smith"}, {"target", "爱丽丝·史密斯"}}}, &error);
    require(error.isEmpty() && terms.size() == 3, "Orion and Vela glossary formats accepted");
    require(matchingTerms(terms, QStringLiteral("アイテム AliceX")).isEmpty(), "short kana and Latin word boundaries");
    require(matchingTerms(terms, QStringLiteral("Alice Smith")).size() == 1, "longest match without conflicting fragments");
    auto merged = mergeTerms(terms, normalizeTerms(QJsonArray {QJsonObject {{"source", "Alice"}, {"target", "错误"}}}));
    require(merged.at(1).toObject().value(QStringLiteral("target")).toString() == QStringLiteral("爱丽丝"), "manual locked mapping retained");
    items = {{99, QStringLiteral("Alice Smith"), QStringLiteral("译文"), {}, QStringLiteral("序章")}};
    const auto payload = batchPayload(QStringLiteral("translate"), items, terms, QStringLiteral("短记忆"));
    require(payload.value(QStringLiteral("terms")).toArray().size() == 1, "only matched terms sent");
    require(payload.value(QStringLiteral("items")).toArray().first().toObject().value(QStringLiteral("n")).toInt() == 1, "batch local short index");
    require(systemPrompt(QStringLiteral("translate"), "zh-Hans", "忠实") == systemPrompt(QStringLiteral("translate"), "zh-Hans", "忠实"), "deterministic prompt prefix");
    require(!QJsonDocument(payload).toJson().contains("paragraph"), "internal paragraph ID not sent");
    qInfo() << "Translation batching/protocol tests passed";
}
