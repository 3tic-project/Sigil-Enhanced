/************************************************************************
**
**  Copyright (C) 2026 3TIC-Project
**
**  This file is part of Sigil-Enhanced.
**
**  Sigil-Enhanced is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  (at your option) any later version.
**
*************************************************************************/

#include "BuiltinPlugins/DivParagraphCssAnalyzer.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

namespace BuiltinPlugins
{

namespace
{

struct SelectorTypeInfo {
    QString canonical;
    int typeCount = 0;
    bool hasDiv = false;
    bool hasParagraph = false;
};

bool isIdentifierStart(QChar ch)
{
    return ch.isLetter() || ch == QLatin1Char('_') || ch == QLatin1Char('-');
}

bool isIdentifierCharacter(QChar ch)
{
    return ch.isLetterOrNumber() || ch == QLatin1Char('_') || ch == QLatin1Char('-');
}

int matchingBrace(const QString& css, int opening)
{
    int depth = 1;
    QChar quote;
    bool comment = false;
    for (int i = opening + 1; i < css.length(); ++i) {
        const QChar ch = css.at(i);
        const QChar next = i + 1 < css.length() ? css.at(i + 1) : QChar();
        if (comment) {
            if (ch == QLatin1Char('*') && next == QLatin1Char('/')) {
                comment = false;
                i++;
            }
            continue;
        }
        if (!quote.isNull()) {
            if (ch == QLatin1Char('\\')) {
                i++;
            } else if (ch == quote) {
                quote = QChar();
            }
            continue;
        }
        if (ch == QLatin1Char('/') && next == QLatin1Char('*')) {
            comment = true;
            i++;
        } else if (ch == QLatin1Char('\'') || ch == QLatin1Char('"')) {
            quote = ch;
        } else if (ch == QLatin1Char('{')) {
            depth++;
        } else if (ch == QLatin1Char('}') && --depth == 0) {
            return i;
        }
    }
    return -1;
}

QStringList splitSelectors(const QString& prelude)
{
    QStringList selectors;
    int start = 0;
    int parentheses = 0;
    int brackets = 0;
    QChar quote;
    for (int i = 0; i < prelude.length(); ++i) {
        const QChar ch = prelude.at(i);
        if (!quote.isNull()) {
            if (ch == QLatin1Char('\\')) {
                i++;
            } else if (ch == quote) {
                quote = QChar();
            }
            continue;
        }
        if (ch == QLatin1Char('\'') || ch == QLatin1Char('"')) {
            quote = ch;
        } else if (ch == QLatin1Char('(')) {
            parentheses++;
        } else if (ch == QLatin1Char(')') && parentheses > 0) {
            parentheses--;
        } else if (ch == QLatin1Char('[')) {
            brackets++;
        } else if (ch == QLatin1Char(']') && brackets > 0) {
            brackets--;
        } else if (ch == QLatin1Char(',') && parentheses == 0 && brackets == 0) {
            selectors << prelude.mid(start, i - start).trimmed();
            start = i + 1;
        }
    }
    selectors << prelude.mid(start).trimmed();
    selectors.removeAll(QString());
    return selectors;
}

SelectorTypeInfo typeInfo(const QString& selector)
{
    SelectorTypeInfo info;
    int bracket_depth = 0;
    QChar quote;
    int copied = 0;
    for (int i = 0; i < selector.length();) {
        const QChar ch = selector.at(i);
        if (!quote.isNull()) {
            if (ch == QLatin1Char('\\')) {
                i += 2;
            } else {
                if (ch == quote) {
                    quote = QChar();
                }
                i++;
            }
            continue;
        }
        if (ch == QLatin1Char('\'') || ch == QLatin1Char('"')) {
            quote = ch;
            i++;
            continue;
        }
        if (ch == QLatin1Char('[')) {
            bracket_depth++;
            i++;
            continue;
        }
        if (ch == QLatin1Char(']') && bracket_depth > 0) {
            bracket_depth--;
            i++;
            continue;
        }
        if (bracket_depth == 0 && isIdentifierStart(ch)) {
            int end = i + 1;
            while (end < selector.length() && isIdentifierCharacter(selector.at(end))) {
                end++;
            }
            const QString identifier = selector.mid(i, end - i).toLower();
            int previous = i - 1;
            while (previous >= 0 && selector.at(previous).isSpace()) {
                previous--;
            }
            const bool type_position = previous < 0 || selector.at(i - 1).isSpace() ||
                QStringLiteral(">+~,(|").contains(selector.at(previous));
            if (type_position && (identifier == QStringLiteral("div") ||
                                  identifier == QStringLiteral("p"))) {
                info.canonical += selector.mid(copied, i - copied);
                info.canonical += QStringLiteral("$block");
                copied = end;
                info.typeCount++;
                info.hasDiv = info.hasDiv || identifier == QStringLiteral("div");
                info.hasParagraph = info.hasParagraph || identifier == QStringLiteral("p");
            }
            i = end;
            continue;
        }
        i++;
    }
    info.canonical += selector.mid(copied);
    return info;
}

void analyzeRule(const DivParagraphCssAnalyzer::Source& source,
                 const QString& prelude,
                 const QString& declarations,
                 DivParagraphCssAnalyzer::Result& result)
{
    const QStringList selectors = splitSelectors(prelude);
    QHash<QString, QSet<QString>> variants;
    QHash<QString, SelectorTypeInfo> infos;
    static const QRegularExpression of_type(
        QStringLiteral(":(?:first|last|only|nth|nth-last)-of-type\\b"),
        QRegularExpression::CaseInsensitiveOption);

    for (const QString& selector : selectors) {
        const SelectorTypeInfo info = typeInfo(selector);
        infos.insert(selector, info);
        if (info.typeCount == 1) {
            if (info.hasDiv) {
                variants[info.canonical].insert(QStringLiteral("div"));
            }
            if (info.hasParagraph) {
                variants[info.canonical].insert(QStringLiteral("p"));
            }
        }
    }

    static const QRegularExpression margin_shorthand(
        QStringLiteral("(?:^|;)\\s*margin(?:-block)?\\s*:"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression margin_start(
        QStringLiteral("(?:^|;)\\s*margin-(?:block-start|top)\\s*:"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression margin_end(
        QStringLiteral("(?:^|;)\\s*margin-(?:block-end|bottom)\\s*:"),
        QRegularExpression::CaseInsensitiveOption);
    const bool resets_margin = margin_shorthand.match(declarations).hasMatch() ||
        (margin_start.match(declarations).hasMatch() &&
         margin_end.match(declarations).hasMatch());
    if (resets_margin &&
        variants.value(QStringLiteral("$block")).contains(QStringLiteral("div")) &&
        variants.value(QStringLiteral("$block")).contains(QStringLiteral("p"))) {
        result.paragraphMarginParity = true;
    }

    for (const QString& selector : selectors) {
        const SelectorTypeInfo info = infos.value(selector);
        QString reason;
        if (of_type.match(selector).hasMatch()) {
            reason = QStringLiteral("type-position pseudo-class can change sibling matching");
        } else if (info.typeCount > 1) {
            reason = QStringLiteral("complex selector contains multiple div/p type tests");
        } else if (info.typeCount == 1 && variants.value(info.canonical).size() < 2) {
            reason = QStringLiteral("div/p tag selector has no equivalent paired selector");
        }
        if (!reason.isEmpty()) {
            result.dependencies << DivParagraphCssAnalyzer::Dependency {
                source.id, selector, reason
            };
        }
    }
}

bool isContainerAtRule(const QString& prelude)
{
    const QString lower = prelude.toLower();
    return lower.startsWith(QStringLiteral("@media")) ||
           lower.startsWith(QStringLiteral("@supports")) ||
           lower.startsWith(QStringLiteral("@layer")) ||
           lower.startsWith(QStringLiteral("@container")) ||
           lower.startsWith(QStringLiteral("@document")) ||
           lower.startsWith(QStringLiteral("@scope"));
}

void analyzeBlock(const DivParagraphCssAnalyzer::Source& source,
                  int begin,
                  int end,
                  DivParagraphCssAnalyzer::Result& result)
{
    int statement_start = begin;
    QChar quote;
    bool comment = false;
    int parentheses = 0;
    int brackets = 0;
    for (int i = begin; i < end; ++i) {
        const QChar ch = source.text.at(i);
        const QChar next = i + 1 < end ? source.text.at(i + 1) : QChar();
        if (comment) {
            if (ch == QLatin1Char('*') && next == QLatin1Char('/')) {
                comment = false;
                i++;
            }
            continue;
        }
        if (!quote.isNull()) {
            if (ch == QLatin1Char('\\')) {
                i++;
            } else if (ch == quote) {
                quote = QChar();
            }
            continue;
        }
        if (ch == QLatin1Char('/') && next == QLatin1Char('*')) {
            comment = true;
            i++;
        } else if (ch == QLatin1Char('\'') || ch == QLatin1Char('"')) {
            quote = ch;
        } else if (ch == QLatin1Char('(')) {
            parentheses++;
        } else if (ch == QLatin1Char(')') && parentheses > 0) {
            parentheses--;
        } else if (ch == QLatin1Char('[')) {
            brackets++;
        } else if (ch == QLatin1Char(']') && brackets > 0) {
            brackets--;
        } else if (ch == QLatin1Char(';') && parentheses == 0 && brackets == 0) {
            statement_start = i + 1;
        } else if (ch == QLatin1Char('{') && parentheses == 0 && brackets == 0) {
            const int closing = matchingBrace(source.text, i);
            if (closing < 0 || closing > end) {
                return;
            }
            const QString prelude = source.text.mid(statement_start, i - statement_start).trimmed();
            if (isContainerAtRule(prelude)) {
                analyzeBlock(source, i + 1, closing, result);
            } else if (!prelude.startsWith(QLatin1Char('@'))) {
                analyzeRule(source, prelude,
                            source.text.mid(i + 1, closing - i - 1), result);
            }
            i = closing;
            statement_start = closing + 1;
        }
    }
}

}

DivParagraphCssAnalyzer::Result
DivParagraphCssAnalyzer::analyze(const QVector<Source>& sources)
{
    Result result;
    for (const Source& source : sources) {
        if (!source.available) {
            result.dependencies << Dependency {
                source.id, QString(),
                QStringLiteral("referenced stylesheet could not be resolved")
            };
            continue;
        }
        analyzeBlock(source, 0, source.text.length(), result);
    }
    if (!result.paragraphMarginParity) {
        result.dependencies << Dependency {
            QStringLiteral("user-agent stylesheet"),
            QStringLiteral("p"),
            QStringLiteral("default paragraph margins are not neutralized by a paired div/p rule")
        };
    }
    result.reviewRequired = !result.dependencies.isEmpty();
    return result;
}

}
