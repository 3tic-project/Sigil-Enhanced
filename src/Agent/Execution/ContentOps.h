/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef SIGIL_AGENT_CONTENT_OPS_H
#define SIGIL_AGENT_CONTENT_OPS_H

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

namespace SigilAgent
{

struct RegexHit {
    int offset = 0;
    int length = 0;
    int line = 1;
    QString match;
    QStringList captures;
};

QString xmlEscape(const QString &text);
QString relativeBookHref(const QString &from_book_path, const QString &to_book_path);
QString extractBodyInner(const QString &xhtml);
QString replaceBodyInner(const QString &xhtml, const QString &inner);
QString setXhtmlTitle(const QString &xhtml, const QString &title);
QRegularExpression compileRegex(const QString &pattern, QString *error);
QList<RegexHit> regexHits(const QString &text, const QRegularExpression &re, int max_hits);
QJsonArray regexHitsJson(const QList<RegexHit> &hits, const QString &resource_id, const QString &book_path);
QString regexReplaceText(const QString &text, const QRegularExpression &re,
                         const QString &replacement, int max_replacements, int *count);
QString wrapMatchesText(const QString &text, const QRegularExpression &re,
                        const QString &open, const QString &close, int max_replacements, int *count);
QString insertAroundAnchor(const QString &text, const QString &anchor,
                           const QString &insert, bool before, QString *error);
struct SplitPiece {
    QString heading;
    QString inner;
};
QList<SplitPiece> splitByHeadingRegex(const QString &xhtml, const QRegularExpression &heading_re);
QString mergeBodyInners(const QString &skeleton, const QStringList &inners);
QString wrapPlainText(const QString &plain, const QJsonObject &rules);
QJsonArray headingsInXhtml(const QString &xhtml, const QRegularExpression &re, int default_level);
QString ncxFromEntries(const QJsonArray &entries, const QString &title);
QJsonArray brokenImageRefs(const QString &xhtml, const QStringList &image_names);

} // namespace SigilAgent

#endif
