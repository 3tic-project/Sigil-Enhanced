#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

namespace BuiltinPlugins::Translation {

// Byte ranges refer to one immutable UTF-8 resource buffer, never QString offsets.
struct Paragraph {
    QString text;
    QString readings;
    QString translation;
    QString classes;
    QString style;
    QString leadingWhitespace;
    QByteArray sourceKey;
    int start = 0;
    int end = 0;
    int insertAt = 0;
    int translationStart = -1;
    int translationEnd = -1;
    int line = 1;
    bool inlineTranslation = false;
    bool toc = false;
};

struct TextResult {
    QList<Paragraph> paragraphs;
    QString error;
};

struct Replacement {
    int index = -1;
    QByteArray expectedSource;
    QString expectedTranslation;
    QString text;
};

struct WriteResult {
    QString text;
    QString error;
    QList<int> applied;
    QList<int> conflicts;
};

TextResult extract(const QString &xhtml);
WriteResult writePairs(const QString &xhtml, const QList<Replacement> &items,
                       const QString &language = QStringLiteral("zh-Hans"), const TextResult *prepared = nullptr);
QString escapeText(const QString &text);
QString normalizedTranslation(const QString &source, QString translation);

} // namespace BuiltinPlugins::Translation
