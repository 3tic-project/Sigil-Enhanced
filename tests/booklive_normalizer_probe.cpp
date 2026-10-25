#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "BuiltinPlugins/BookLiveParagraphNormalizer.h"

using BuiltinPlugins::BookLiveParagraphNormalizer;

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QFile input;
    if (!input.open(stdin, QIODevice::ReadOnly)) return 1;
    QJsonParseError error;
    const auto request = QJsonDocument::fromJson(input.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !request.isArray()) return 1;

    QJsonArray results;
    for (const auto& value : request.array()) {
        const QJsonObject item = value.toObject();
        const QString source = item.value("text").toString();
        const auto normalized = BookLiveParagraphNormalizer::normalizeXhtmlText(source);
        const auto again = normalized.changed
            ? BookLiveParagraphNormalizer::normalizeXhtmlText(normalized.text) : normalized;
        results << QJsonObject {
            { "path", item.value("path") },
            { "ok", normalized.ok },
            { "changed", normalized.changed },
            { "safe", normalized.before.safeToNormalize },
            { "kind", BookLiveParagraphNormalizer::pageKindName(normalized.before.pageKind) },
            { "conversions", normalized.before.convertibleLeaves },
            { "messages", QJsonArray::fromStringList(normalized.messages) },
            { "idempotent", !normalized.changed ||
                (again.ok && !again.changed && again.text == normalized.text) },
            { "text", normalized.changed ? normalized.text : source }
        };
    }
    QFile output;
    if (!output.open(stdout, QIODevice::WriteOnly)) return 1;
    const QByteArray bytes = QJsonDocument(results).toJson(QJsonDocument::Compact);
    return output.write(bytes) == bytes.size() ? 0 : 1;
}
