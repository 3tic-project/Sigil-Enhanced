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
    BatchText huge {1, QStringLiteral("彼は笑った😀。「元気ですか？」\n").repeated(1200), {}, {}, "chapter"};
    const auto fragments = splitParagraph(huge, 3000);
    require(fragments.size() > 3, "long paragraph splits at reading boundaries");
    QString joined;
    for (const auto &part : fragments) {
        require(part.source.toUcs4().size() <= 3000 && part.fragments == fragments.size(), "fragment limits and identity");
        joined += part.source;
    }
    require(joined == huge.source, "sentence split neither drops nor duplicates Unicode text");
    huge.translation = QStringLiteral("他笑了😀。“你好吗？”\n").repeated(1200);
    const auto pairs = splitParagraph(huge, 4500, true);
    QString sourceJoined, translationJoined;
    for (const auto &part : pairs) { sourceJoined += part.source; translationJoined += part.translation; }
    require(!pairs.isEmpty() && sourceJoined == huge.source && translationJoined == huge.translation, "long review pairs preserve both complete texts");
    require(splitParagraph({0, QString(9000, QChar(0x3042))}, 3000).isEmpty(), "unsplittable text fails explicitly without truncation");
    QJsonArray aliases = normalizeTerms(QJsonArray {
        QJsonObject {{"source", "フルネーム"}, {"target", "正确译名"}, {"aliases", QJsonArray {"愛称"}}, {"locked", true}},
        QJsonObject {{"source", "別人"}, {"target", "别人"}},
        QJsonObject {{"source", "愛称"}, {"target", "旧名"}}});
    const auto unified = unifyAliases(aliases);
    require(unified.at(2).toObject().value("target") == "正确译名" && unified.at(1).toObject().value("target") == "别人", "explicit aliases unify across groups, unrelated names stay independent");
    const auto related = relatedTermGroups(aliases, 2);
    require(related.first().size() == 2 && related.first().at(1).value("source") == "愛称", "related candidates share model batch");
    const auto variants = unifyAliases(normalizeTerms(QJsonArray {
        QJsonObject {{"source", "山田太郎"}, {"target", "山田太郎"}, {"aliases", QJsonArray {"太郎"}}},
        QJsonObject {{"source", "太郎"}, {"target", "太郎"}}}));
    const auto nickname = matchingTerms(variants, "太郎");
    require(nickname.size() == 1 && nickname.first().toObject().value("target") == "太郎", "explicit nickname translation wins over full-name alias");
    qInfo() << "Translation batching/protocol tests passed";
}
