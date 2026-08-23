/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef SIGIL_AGENT_MANUSCRIPT_PARSER_H
#define SIGIL_AGENT_MANUSCRIPT_PARSER_H

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace SigilAgent
{

struct ManuscriptCredits {
    QString author;
    QString illustrator;
    QString translator;
    QString source;
    QString transcriber;
    QString publisher;
    QString forum;
    QString originalTitle;
    QString title;
};

struct ManuscriptIllustration {
    QString name;
    int line = 0;
    bool inFrontMatter = false;
};

struct ManuscriptChapter {
    int index = 0;
    QString heading;
    QString numberLabel;
    QString title;
    int startLine = 0;
    int endLine = 0;
    int charCount = 0;
    int paragraphCount = 0;
    QStringList illustrationNames;
    QString body;
};

struct ParsedManuscript {
    QString title;
    QString originalTitle;
    ManuscriptCredits credits;
    QString synopsis;
    QStringList toc;
    QList<ManuscriptChapter> chapters;
    QList<ManuscriptIllustration> illustrations;
    QStringList frontIllustrationNames;
    int sourceLines = 0;
    int sourceChars = 0;
};

struct ParseOptions {
    QString headingRegex;
    QString illustrationRegex;
};

QString manuscriptPlainText(const QString &text);
ParsedManuscript parseManuscriptText(const QString &text, const QString &hint_title = QString(),
                                     const ParseOptions &options = ParseOptions());
QJsonObject manuscriptSummaryJson(const ParsedManuscript &parsed, int synopsis_limit = 800);
bool lineLooksLikeChapterHeading(const QString &line);
QString normalizeHeadingKey(const QString &line);
QString splitHeadingTitle(const QString &heading);
QString splitHeadingNumber(const QString &heading);
int chapterNumberFromHeading(const QString &heading);
QString chineseTalkLabel(int number);
QString illustrationNameInLine(const QString &line);

} // namespace SigilAgent

#endif
