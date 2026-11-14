#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace BuiltinPlugins::Translation {

struct BatchText {
    int paragraph = -1;
    QString source;
    QString translation;
    QString readings;
    QString section;
    int fragment = -1;
    int fragments = 0;
};

struct BatchLimits {
    int targetCharacters = 3000;
    int maximumCharacters = 4500;
    int maximumItems = 128;
};

struct ParsedBatch {
    QString error;
    QHash<int, QString> texts; // Zero-based batch positions, not paragraph IDs.
    QList<int> missing;
    QJsonArray terms;
    QString memory;
};

QList<QList<BatchText>> makeBatches(const QList<BatchText> &items, const BatchLimits &limits,
                                   bool includeTranslation = false);
QJsonArray normalizeTerms(const QJsonValue &value, QString *error = nullptr);
QJsonArray mergeTerms(const QJsonArray &existing, const QJsonArray &incoming);
QJsonArray unifyAliases(const QJsonArray &terms);
QList<QList<QJsonObject>> relatedTermGroups(const QJsonArray &terms, int maximum = 120);
QList<BatchText> splitParagraph(const BatchText &item, int maximumCharacters, bool pair = false);
QJsonArray matchingTerms(const QJsonArray &terms, const QString &text);
QString systemPrompt(const QString &phase, const QString &language, const QString &style);
QJsonObject batchPayload(const QString &phase, const QList<BatchText> &items,
                         const QJsonArray &terms, const QString &memory,
                         const QStringList &neighbors = {}, bool termsAlreadyMatched = false);
ParsedBatch parseBatch(const QString &phase, const QString &response, int count);

} // namespace BuiltinPlugins::Translation
