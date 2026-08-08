#include <cstdlib>
#include <iostream>

#include <QElapsedTimer>
#include <QHash>
#include <QString>

#include "Misc/SearchBatchRunner.h"

namespace
{

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

}

int main()
{
    constexpr int resourceCount = 1000;
    constexpr int ruleCount = 50;
    constexpr int tokensPerResource = 256;

    QHash<QString, QString> originals;
    QStringList paths;
    const QString originalText = QStringLiteral("v0 ").repeated(tokensPerResource);
    for (int resourceIndex = 0; resourceIndex < resourceCount; ++resourceIndex) {
        const QString path = QStringLiteral("Text/chapter-%1.xhtml").arg(resourceIndex);
        paths.append(path);
        originals.insert(path, originalText);
    }

    QList<SearchBatch::Rule> rules;
    for (int ruleIndex = 0; ruleIndex < ruleCount; ++ruleIndex) {
        SearchBatch::Rule rule;
        rule.id = QString::number(ruleIndex);
        rule.name = QStringLiteral("benchmark rule %1").arg(ruleIndex);
        rule.searchRegex = QStringLiteral("v%1").arg(ruleIndex);
        rule.replacement = QStringLiteral("v%1").arg(ruleIndex + 1);
        rule.resourcePaths = paths;
        rules.append(rule);
    }

    qint64 evaluations = 0;
    QElapsedTimer timer;
    timer.start();
    const SearchBatch::Result result = SearchBatch::Runner::Run(
        rules, originals,
        [&evaluations](const SearchBatch::Rule& rule, const QString&, const QString& text) {
            ++evaluations;
            SearchBatch::ApplyResult applied;
            applied.text = text;
            applied.replacementCount = applied.text.count(rule.searchRegex);
            applied.text.replace(rule.searchRegex, rule.replacement);
            return applied;
        });
    const qint64 elapsedMs = timer.elapsed();

    const qint64 legacyWriteCandidates = static_cast<qint64>(resourceCount) * ruleCount;
    const qint64 expectedReplacements = legacyWriteCandidates * tokensPerResource;
    Require(result.success, "target benchmark batch should succeed");
    Require(evaluations == legacyWriteCandidates, "target benchmark evaluation count mismatch");
    Require(result.replacementCount == expectedReplacements,
            "target benchmark replacement count mismatch");
    Require(result.changedTexts.size() == resourceCount,
            "target benchmark must publish one final text per resource");
    Require(result.changedTexts.value(paths.first()).startsWith(QStringLiteral("v50 ")),
            "target benchmark final text mismatch");

    std::cout << "search_batch_benchmark"
              << " resources=" << resourceCount
              << " rules=" << ruleCount
              << " evaluations=" << evaluations
              << " replacements=" << result.replacementCount
              << " legacy_write_candidates=" << legacyWriteCandidates
              << " staged_write_candidates=" << result.changedTexts.size()
              << " write_reduction=" << ruleCount << "x"
              << " elapsed_ms=" << elapsedMs << '\n';
    return 0;
}
