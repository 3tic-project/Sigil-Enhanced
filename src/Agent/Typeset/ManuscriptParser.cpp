/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/Typeset/ManuscriptParser.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

namespace SigilAgent
{

namespace
{

QString foldSpaces(QString text)
{
    return text.trimmed();
}

QChar fullwidthToAsciiDigit(QChar ch)
{
    const ushort u = ch.unicode();
    if (u >= 0xFF10 && u <= 0xFF19) return QChar(QLatin1Char(char('0' + (u - 0xFF10))));
    return ch;
}

QString nfkcDigits(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (QChar ch : text) out.append(fullwidthToAsciiDigit(ch));
    return out;
}

int chineseDigit(QChar ch)
{
    switch (ch.unicode()) {
        case 0x4E00: return 1; // 一
        case 0x4E8C: return 2;
        case 0x4E09: return 3;
        case 0x56DB: return 4;
        case 0x4E94: return 5;
        case 0x516D: return 6;
        case 0x4E03: return 7;
        case 0x516B: return 8;
        case 0x4E5D: return 9;
        case 0x96F6: return 0;
        default: return -1;
    }
}

int parseChineseInt(const QString &text)
{
    if (text.isEmpty()) return -1;
    int value = 0;
    int current = 0;
    bool any = false;
    for (QChar ch : text) {
        if (ch == QChar(0x5341)) { // 十
            if (current == 0) current = 1;
            value += current * 10;
            current = 0;
            any = true;
            continue;
        }
        if (ch == QChar(0x767E)) { // 百
            if (current == 0) current = 1;
            value += current * 100;
            current = 0;
            any = true;
            continue;
        }
        if (ch == QChar(0x5343)) { // 千
            if (current == 0) current = 1;
            value += current * 1000;
            current = 0;
            any = true;
            continue;
        }
        const int digit = chineseDigit(ch);
        if (digit >= 0) {
            current = current * 10 + digit;
            any = true;
            continue;
        }
        if (ch.isDigit()) {
            current = current * 10 + ch.digitValue();
            any = true;
        }
    }
    value += current;
    return any ? value : -1;
}

bool isTalkOrChapterSuffix(QChar ch)
{
    return ch == QChar(0x8A71) || ch == QChar(0x8BDD) || ch == QChar(0x7AE0) // 話 话 章
        || ch == QChar(0x7BC7); // 篇
}

QRegularExpression headingStartRe()
{
    static const QRegularExpression re(
        QStringLiteral("^\\s*(第|最終章|最终章|後記|后记|尾聲|尾声|題外話|题外话|番外)"),
        QRegularExpression::UseUnicodePropertiesOption);
    return re;
}

bool isSpecialHeading(const QString &line)
{
    const QString t = foldSpaces(line);
    static const QStringList exact = {
        QStringLiteral("最終章"), QStringLiteral("最终章"),
        QStringLiteral("後記"), QStringLiteral("后记"),
        QStringLiteral("尾聲"), QStringLiteral("尾声"),
        QStringLiteral("終章"), QStringLiteral("终章")
    };
    for (const QString &item : exact) {
        if (t == item || t.startsWith(item)) return true;
    }
    if (t.startsWith(QStringLiteral("題外話")) || t.startsWith(QStringLiteral("题外话"))
        || t.startsWith(QStringLiteral("番外"))) {
        return true;
    }
    if (t.contains(QStringLiteral("加筆短篇")) || t.contains(QStringLiteral("加笔短篇"))) {
        return t.startsWith(QStringLiteral("第"));
    }
    return false;
}

QString decodeEntities(QString text)
{
    text.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
    text.replace(QStringLiteral("&#160;"), QStringLiteral(" "));
    text.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    text.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    text.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    text.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    return text;
}

QString creditValue(const QString &line, const QStringList &keys)
{
    const QString trimmed = foldSpaces(line);
    for (const QString &key : keys) {
        if (trimmed.startsWith(key)) {
            QString rest = trimmed.mid(key.size());
            if (rest.startsWith(QChar(0xFF1A)) || rest.startsWith(QLatin1Char(':'))) {
                rest = rest.mid(1);
            }
            return rest.trimmed();
        }
    }
    return QString();
}

bool isTocHeader(const QString &line)
{
    QString compact = foldSpaces(line);
    compact.remove(QLatin1Char(' '));
    compact.remove(QChar(0x3000));
    compact.remove(QChar(0x00A0));
    const QString lower = compact.toLower();
    return compact == QStringLiteral("目录") || compact == QStringLiteral("目錄")
        || lower == QLatin1String("contents")
        || compact == QStringLiteral("ＣＯＮＴＥＮＴＳ")
        || compact == QStringLiteral("CONTENTS");
}

bool isSynopsisHeader(const QString &line)
{
    const QString t = foldSpaces(line);
    return t == QStringLiteral("简介") || t == QStringLiteral("簡介")
        || t == QStringLiteral("內容簡介") || t == QStringLiteral("内容简介")
        || t.startsWith(QStringLiteral("內容簡介")) || t.startsWith(QStringLiteral("内容简介"));
}

QRegularExpression illustrationRe()
{
    static const QRegularExpression re(
        QStringLiteral("［插[图圖]：([^］]+)］|\\[插[图圖]：([^\\]]+)\\]|【插[图圖]：([^】]+)】"),
        QRegularExpression::UseUnicodePropertiesOption);
    return re;
}

QStringList splitLines(const QString &text)
{
    QString normalized = text;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return normalized.split(QLatin1Char('\n'));
}

} // namespace

QString manuscriptPlainText(const QString &text)
{
    if (!text.contains(QLatin1Char('<'))) return text;
    QString t = text;
    t.replace(QRegularExpression(QStringLiteral("<br\\s*/?>"), QRegularExpression::CaseInsensitiveOption),
              QStringLiteral("\n"));
    t.replace(QRegularExpression(QStringLiteral("</p\\s*>"), QRegularExpression::CaseInsensitiveOption),
              QStringLiteral("\n"));
    t.replace(QRegularExpression(QStringLiteral("<[^>]+>")), QString());
    return decodeEntities(t);
}

QString illustrationNameInLine(const QString &line)
{
    const QRegularExpressionMatch match = illustrationRe().match(line);
    if (!match.hasMatch()) return QString();
    for (int i = 1; i <= 3; ++i) {
        const QString captured = match.captured(i).trimmed();
        if (!captured.isEmpty()) return captured;
    }
    return QString();
}

bool lineLooksLikeChapterHeading(const QString &line)
{
    const QString t = foldSpaces(line);
    if (t.isEmpty()) return false;
    if (isSpecialHeading(t)) return true;
    if (!headingStartRe().match(t).hasMatch()) return false;
    if (!t.startsWith(QStringLiteral("第"))) return false;
    const QString digits = nfkcDigits(t.mid(1));
    int i = 0;
    while (i < digits.size()) {
        const QChar ch = digits.at(i);
        if (ch.isDigit() || chineseDigit(ch) >= 0 || ch == QChar(0x5341) || ch == QChar(0x767E)
            || ch == QChar(0x5343)) {
            ++i;
            continue;
        }
        break;
    }
    if (i == 0) {
        return t.contains(QStringLiteral("卷加筆")) || t.contains(QStringLiteral("卷加笔"));
    }
    if (i >= digits.size()) return false;
    return isTalkOrChapterSuffix(digits.at(i)) || digits.at(i) == QChar(0x5377); // 卷
}

QString normalizeHeadingKey(const QString &line)
{
    QString t = nfkcDigits(foldSpaces(line));
    t.replace(QChar(0x8A71), QChar(0x8BDD)); // 話 → 话
    t.replace(QChar(0x3000), QLatin1Char(' '));
    while (t.contains(QStringLiteral("  "))) t.replace(QStringLiteral("  "), QStringLiteral(" "));
    return t;
}

QString splitHeadingNumber(const QString &heading)
{
    const QString t = foldSpaces(heading);
    if (isSpecialHeading(t) && !t.startsWith(QStringLiteral("第"))) {
        const int space = t.indexOf(QRegularExpression(QStringLiteral("[\\s　]")));
        return space > 0 ? t.left(space) : t;
    }
    int i = 0;
    if (t.startsWith(QStringLiteral("第"))) i = 1;
    while (i < t.size()) {
        const QChar ch = t.at(i);
        if (isTalkOrChapterSuffix(ch) || ch == QChar(0x5377)) {
            return t.left(i + 1);
        }
        ++i;
    }
    const int space = t.indexOf(QRegularExpression(QStringLiteral("[\\s　]")));
    return space > 0 ? t.left(space) : t;
}

QString splitHeadingTitle(const QString &heading)
{
    const QString number = splitHeadingNumber(heading);
    QString rest = foldSpaces(heading.mid(number.size()));
    while (rest.startsWith(QChar(0x3000)) || rest.startsWith(QLatin1Char(' '))) rest = rest.mid(1);
    return rest.trimmed();
}

int chapterNumberFromHeading(const QString &heading)
{
    const QString number = nfkcDigits(splitHeadingNumber(heading));
    if (!number.startsWith(QStringLiteral("第"))) return -1;
    QString core = number.mid(1);
    if (!core.isEmpty() && isTalkOrChapterSuffix(core.back())) core.chop(1);
    if (!core.isEmpty() && core.back() == QChar(0x5377)) core.chop(1);
    bool ok = false;
    const int arabic = core.toInt(&ok);
    if (ok) return arabic;
    return parseChineseInt(core);
}

QString chineseTalkLabel(int number)
{
    static const QString digits = QStringLiteral("零一二三四五六七八九");
    if (number <= 0) return QStringLiteral("第零话");
    QString body;
    if (number < 10) {
        body = digits.at(number);
    } else if (number == 10) {
        body = QStringLiteral("十");
    } else if (number < 20) {
        body = QStringLiteral("十") + digits.at(number - 10);
    } else if (number < 100) {
        body = digits.at(number / 10) + QStringLiteral("十");
        if (number % 10) body += digits.at(number % 10);
    } else {
        body = QString::number(number);
    }
    return QStringLiteral("第") + body + QStringLiteral("话");
}

ParsedManuscript parseManuscriptText(const QString &text, const QString &hint_title,
                                     const ParseOptions &options)
{
    ParsedManuscript parsed;
    const QString plain = manuscriptPlainText(text);
    const QStringList lines = splitLines(plain);
    parsed.sourceLines = lines.size();
    parsed.sourceChars = plain.size();
    parsed.title = hint_title;

    QRegularExpression heading_re;
    if (!options.headingRegex.isEmpty()) {
        heading_re = QRegularExpression(options.headingRegex, QRegularExpression::UseUnicodePropertiesOption);
    }
    QRegularExpression illustration_re;
    if (!options.illustrationRegex.isEmpty()) {
        illustration_re = QRegularExpression(options.illustrationRegex,
                                             QRegularExpression::UseUnicodePropertiesOption);
    }

    struct HeadingHit {
        int line = 0;
        QString raw;
        QString key;
    };
    QList<HeadingHit> headings;
    for (int i = 0; i < lines.size(); ++i) {
        const bool is_heading = heading_re.isValid() && !options.headingRegex.isEmpty()
            ? heading_re.match(foldSpaces(lines.at(i))).hasMatch()
            : lineLooksLikeChapterHeading(lines.at(i));
        if (is_heading) {
            HeadingHit hit;
            hit.line = i;
            hit.raw = foldSpaces(lines.at(i));
            hit.key = normalizeHeadingKey(hit.raw);
            headings.append(hit);
        }
        QString illus;
        if (illustration_re.isValid() && !options.illustrationRegex.isEmpty()) {
            const QRegularExpressionMatch match = illustration_re.match(lines.at(i));
            if (match.hasMatch()) {
                illus = match.lastCapturedIndex() >= 1 ? match.captured(1) : match.captured();
            }
        } else {
            illus = illustrationNameInLine(lines.at(i));
        }
        if (!illus.isEmpty()) {
            ManuscriptIllustration marker;
            marker.name = illus.trimmed();
            marker.line = i;
            parsed.illustrations.append(marker);
        }
    }

    int body_start = lines.size();
    QSet<QString> seen;
    for (const HeadingHit &hit : headings) {
        if (seen.contains(hit.key)) {
            body_start = hit.line;
            break;
        }
        seen.insert(hit.key);
    }
    if (body_start == lines.size() && !headings.isEmpty()) {
        body_start = headings.first().line;
    }

    QList<HeadingHit> chapter_hits;
    seen.clear();
    for (const HeadingHit &hit : headings) {
        if (hit.line < body_start) continue;
        if (seen.contains(hit.key)) continue;
        seen.insert(hit.key);
        chapter_hits.append(hit);
    }
    if (chapter_hits.isEmpty() && !headings.isEmpty()) {
        chapter_hits = headings;
        body_start = headings.first().line;
    }

    for (int i = 0; i < chapter_hits.size(); ++i) {
        ManuscriptChapter chapter;
        chapter.index = i;
        chapter.heading = chapter_hits.at(i).raw;
        chapter.numberLabel = splitHeadingNumber(chapter.heading);
        chapter.title = splitHeadingTitle(chapter.heading);
        chapter.startLine = chapter_hits.at(i).line;
        chapter.endLine = (i + 1 < chapter_hits.size()) ? chapter_hits.at(i + 1).line : lines.size();
        QStringList body_lines;
        for (int line = chapter.startLine + 1; line < chapter.endLine; ++line) {
            const QString raw = lines.at(line);
            if (!foldSpaces(raw).isEmpty()) ++chapter.paragraphCount;
            body_lines.append(raw);
        }
        chapter.body = body_lines.join(QLatin1Char('\n'));
        chapter.charCount = chapter.body.size();
        parsed.chapters.append(chapter);
    }

    for (ManuscriptIllustration &marker : parsed.illustrations) {
        marker.inFrontMatter = marker.line < body_start;
        if (marker.inFrontMatter) parsed.frontIllustrationNames.append(marker.name);
        for (ManuscriptChapter &chapter : parsed.chapters) {
            if (marker.line > chapter.startLine && marker.line < chapter.endLine) {
                chapter.illustrationNames.append(marker.name);
                break;
            }
        }
    }

    static const QStringList author_keys = { QStringLiteral("作者") };
    static const QStringList illustrator_keys = {
        QStringLiteral("插画"), QStringLiteral("插畫"), QStringLiteral("插图"), QStringLiteral("插圖")
    };
    static const QStringList translator_keys = {
        QStringLiteral("译者"), QStringLiteral("譯者"), QStringLiteral("翻译"), QStringLiteral("翻譯")
    };
    static const QStringList source_keys = { QStringLiteral("图源"), QStringLiteral("圖源") };
    static const QStringList transcriber_keys = { QStringLiteral("录入"), QStringLiteral("錄入") };
    static const QStringList publisher_keys = { QStringLiteral("发布"), QStringLiteral("發布") };
    static const QStringList forum_keys = { QStringLiteral("论坛"), QStringLiteral("論壇") };
    static const QStringList title_keys = { QStringLiteral("中文標題"), QStringLiteral("中文标题") };
    static const QStringList original_keys = { QStringLiteral("日文書名"), QStringLiteral("日文书名") };

    const int front_end = qMin(body_start, lines.size());
    QStringList synopsis_lines;
    bool in_synopsis = false;
    bool in_toc = false;
    for (int i = 0; i < front_end; ++i) {
        const QString line = lines.at(i);
        const QString author = creditValue(line, author_keys);
        if (!author.isEmpty()) parsed.credits.author = author;
        const QString illustrator = creditValue(line, illustrator_keys);
        if (!illustrator.isEmpty() && illustrationNameInLine(line).isEmpty()) {
            parsed.credits.illustrator = illustrator;
        }
        const QString translator = creditValue(line, translator_keys);
        if (!translator.isEmpty()) parsed.credits.translator = translator;
        const QString source = creditValue(line, source_keys);
        if (!source.isEmpty()) parsed.credits.source = source;
        const QString transcriber = creditValue(line, transcriber_keys);
        if (!transcriber.isEmpty()) parsed.credits.transcriber = transcriber;
        const QString publisher = creditValue(line, publisher_keys);
        if (!publisher.isEmpty()) parsed.credits.publisher = publisher;
        const QString forum = creditValue(line, forum_keys);
        if (!forum.isEmpty()) parsed.credits.forum = forum;
        const QString title = creditValue(line, title_keys);
        if (!title.isEmpty()) {
            parsed.credits.title = title;
            parsed.title = title;
        }
        const QString original = creditValue(line, original_keys);
        if (!original.isEmpty()) {
            parsed.credits.originalTitle = original;
            parsed.originalTitle = original;
        }

        if (isSynopsisHeader(line)) {
            in_synopsis = true;
            in_toc = false;
            const QString rest = creditValue(line, QStringList {
                QStringLiteral("內容簡介"), QStringLiteral("内容简介"),
                QStringLiteral("簡介"), QStringLiteral("简介")
            });
            if (!rest.isEmpty()) synopsis_lines.append(rest);
            continue;
        }
        if (isTocHeader(line)) {
            in_synopsis = false;
            in_toc = true;
            continue;
        }
        if (in_synopsis) {
            if (!illustrationNameInLine(line).isEmpty() || lineLooksLikeChapterHeading(line)) {
                in_synopsis = false;
            } else if (!foldSpaces(line).isEmpty()
                       && !foldSpaces(line).startsWith(QStringLiteral("─"))
                       && !foldSpaces(line).startsWith(QStringLiteral("—"))) {
                synopsis_lines.append(foldSpaces(line));
            }
        }
        if (in_toc && lineLooksLikeChapterHeading(line)) {
            parsed.toc.append(foldSpaces(line));
        }
    }
    parsed.synopsis = synopsis_lines.join(QLatin1Char('\n'));
    if (parsed.toc.isEmpty()) {
        for (const ManuscriptChapter &chapter : parsed.chapters) {
            parsed.toc.append(chapter.heading);
        }
    }
    if (parsed.title.isEmpty() && !hint_title.isEmpty()) parsed.title = hint_title;
    if (parsed.title.isEmpty() && !parsed.credits.title.isEmpty()) parsed.title = parsed.credits.title;
    return parsed;
}

QJsonObject manuscriptSummaryJson(const ParsedManuscript &parsed, int synopsis_limit)
{
    const int limit = qMax(80, synopsis_limit);
    QString synopsis = parsed.synopsis;
    const bool truncated = synopsis.size() > limit;
    if (truncated) synopsis = synopsis.left(limit);
    QJsonArray toc;
    for (const QString &entry : parsed.toc) toc.append(entry);
    QJsonArray chapters;
    for (const ManuscriptChapter &chapter : parsed.chapters) {
        QJsonArray illustrations;
        for (const QString &name : chapter.illustrationNames) illustrations.append(name);
        chapters.append(QJsonObject {
            { QStringLiteral("index"), chapter.index },
            { QStringLiteral("heading"), chapter.heading },
            { QStringLiteral("number"), chapter.numberLabel },
            { QStringLiteral("title"), chapter.title },
            { QStringLiteral("chars"), chapter.charCount },
            { QStringLiteral("paragraphs"), chapter.paragraphCount },
            { QStringLiteral("start_line"), chapter.startLine + 1 },
            { QStringLiteral("illustrations"), illustrations }
        });
    }
    QJsonArray illustrations;
    for (const ManuscriptIllustration &marker : parsed.illustrations) {
        illustrations.append(QJsonObject {
            { QStringLiteral("name"), marker.name },
            { QStringLiteral("line"), marker.line + 1 },
            { QStringLiteral("front_matter"), marker.inFrontMatter }
        });
    }
    QJsonArray front;
    for (const QString &name : parsed.frontIllustrationNames) front.append(name);
    return QJsonObject {
        { QStringLiteral("title"), parsed.title },
        { QStringLiteral("original_title"), parsed.originalTitle },
        { QStringLiteral("credits"), QJsonObject {
            { QStringLiteral("author"), parsed.credits.author },
            { QStringLiteral("illustrator"), parsed.credits.illustrator },
            { QStringLiteral("translator"), parsed.credits.translator },
            { QStringLiteral("source"), parsed.credits.source },
            { QStringLiteral("transcriber"), parsed.credits.transcriber },
            { QStringLiteral("publisher"), parsed.credits.publisher },
            { QStringLiteral("forum"), parsed.credits.forum }
        } },
        { QStringLiteral("synopsis"), synopsis },
        { QStringLiteral("synopsis_truncated"), truncated },
        { QStringLiteral("toc"), toc },
        { QStringLiteral("chapters"), chapters },
        { QStringLiteral("chapter_count"), parsed.chapters.size() },
        { QStringLiteral("front_illustrations"), front },
        { QStringLiteral("illustrations"), illustrations },
        { QStringLiteral("source_chars"), parsed.sourceChars },
        { QStringLiteral("source_lines"), parsed.sourceLines }
    };
}

} // namespace SigilAgent
