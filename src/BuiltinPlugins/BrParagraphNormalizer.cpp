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

#include "BuiltinPlugins/BrParagraphNormalizer.h"

#include <QDomDocument>
#include <QRegularExpression>
#include <QSet>
#include <QtGlobal>

namespace BuiltinPlugins
{

namespace
{

const QString XHTML_NS = QStringLiteral("http://www.w3.org/1999/xhtml");
const int MIN_AUTO_PARAGRAPHS = 4;
const int MIN_AUTO_BR_COUNT = 4;
const QString BR_NORMALIZER_BODY_CLASS = QStringLiteral("se-br-normalized");
const QString BR_NORMALIZER_GAP_CLASS = QStringLiteral("se-br-gap-before");
const QString BR_NORMALIZER_CSS =
    QStringLiteral("body.se-br-normalized p { margin: 0; margin-block-start: 0; margin-block-end: 0; }\n"
                   "body.se-br-normalized p.se-br-gap-before { margin-block-start: 1em; }\n");

struct ParagraphRun {
    QList<QDomNode> nodes;
    QString text;
    int gapBefore = 0;
    bool hasHref = false;
    bool hasFontSize = false;
    bool hasVisibleText = false;
};

bool parseDocument(const QString& source, QDomDocument& document, QString& error);

QString localName(const QDomNode& node)
{
    if (!node.isElement()) {
        return QString();
    }

    const QDomElement element = node.toElement();
    const QString local_name = element.localName();
    return (local_name.isEmpty() ? element.tagName() : local_name).toLower();
}

bool isWhitespaceOnly(const QString& text)
{
    return text.trimmed().isEmpty();
}

bool classListContains(const QString& class_list, const QString& class_name)
{
    return class_list.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).contains(class_name);
}

bool elementHasClass(const QDomElement& element, const QString& class_name)
{
    return classListContains(element.attribute(QStringLiteral("class")), class_name);
}

bool isBrElement(const QDomNode& node)
{
    return node.isElement() && localName(node) == QStringLiteral("br");
}

bool isBlockElementName(const QString& name)
{
    static const QSet<QString> block_names = {
        QStringLiteral("address"),
        QStringLiteral("article"),
        QStringLiteral("aside"),
        QStringLiteral("blockquote"),
        QStringLiteral("center"),
        QStringLiteral("dd"),
        QStringLiteral("div"),
        QStringLiteral("dl"),
        QStringLiteral("dt"),
        QStringLiteral("fieldset"),
        QStringLiteral("figcaption"),
        QStringLiteral("figure"),
        QStringLiteral("footer"),
        QStringLiteral("form"),
        QStringLiteral("h1"),
        QStringLiteral("h2"),
        QStringLiteral("h3"),
        QStringLiteral("h4"),
        QStringLiteral("h5"),
        QStringLiteral("h6"),
        QStringLiteral("header"),
        QStringLiteral("hr"),
        QStringLiteral("li"),
        QStringLiteral("main"),
        QStringLiteral("nav"),
        QStringLiteral("ol"),
        QStringLiteral("p"),
        QStringLiteral("pre"),
        QStringLiteral("section"),
        QStringLiteral("table"),
        QStringLiteral("ul")
    };
    return block_names.contains(name);
}

bool isBlockElement(const QDomNode& node)
{
    return node.isElement() && isBlockElementName(localName(node));
}

QDomElement findElementByLocalName(const QDomNode& root, const QString& name)
{
    if (root.isElement() && localName(root) == name) {
        return root.toElement();
    }

    for (QDomNode child = root.firstChild(); !child.isNull(); child = child.nextSibling()) {
        QDomElement found = findElementByLocalName(child, name);
        if (!found.isNull()) {
            return found;
        }
    }

    return QDomElement();
}

int countElementsByLocalName(const QDomNode& root, const QString& name)
{
    int count = 0;
    if (root.isElement() && localName(root) == name) {
        count++;
    }

    for (QDomNode child = root.firstChild(); !child.isNull(); child = child.nextSibling()) {
        count += countElementsByLocalName(child, name);
    }
    return count;
}

QString visibleText(const QDomNode& node)
{
    if (node.isText() || node.isCDATASection()) {
        return node.nodeValue();
    }
    if (!node.isElement()) {
        return QString();
    }

    const QString name = localName(node);
    if (name == QStringLiteral("script") || name == QStringLiteral("style")) {
        return QString();
    }

    QString text;
    for (QDomNode child = node.firstChild(); !child.isNull(); child = child.nextSibling()) {
        text += visibleText(child);
    }
    return text;
}

QString semanticText(const QDomNode& node)
{
    if (node.isText() || node.isCDATASection()) {
        const QString text = node.nodeValue();
        return text.trimmed().isEmpty() ? QString() : text;
    }
    if (!node.isElement()) {
        return QString();
    }

    const QString name = localName(node);
    if (name == QStringLiteral("script") || name == QStringLiteral("style")) {
        return QString();
    }

    QString text;
    for (QDomNode child = node.firstChild(); !child.isNull(); child = child.nextSibling()) {
        text += semanticText(child);
    }
    return text;
}

QStringList collectAttributes(const QDomNode& node, const QStringList& names)
{
    QStringList values;

    if (node.isElement()) {
        const QDomElement element = node.toElement();
        foreach(const QString& name, names) {
            if (element.hasAttribute(name)) {
                values << QStringLiteral("%1=%2").arg(name, element.attribute(name));
            }
        }
    }

    for (QDomNode child = node.firstChild(); !child.isNull(); child = child.nextSibling()) {
        values << collectAttributes(child, names);
    }
    return values;
}

QStringList sortedAttributes(const QDomDocument& document, const QStringList& names)
{
    QStringList values = collectAttributes(document, names);
    values.sort();
    return values;
}

bool hasHrefElement(const QDomNode& node)
{
    if (node.isElement()) {
        const QDomElement element = node.toElement();
        if (localName(node) == QStringLiteral("a") && element.hasAttribute(QStringLiteral("href"))) {
            return true;
        }
    }

    for (QDomNode child = node.firstChild(); !child.isNull(); child = child.nextSibling()) {
        if (hasHrefElement(child)) {
            return true;
        }
    }
    return false;
}

bool hasFontSizeStyle(const QDomNode& node)
{
    if (node.isElement()) {
        const QDomElement element = node.toElement();
        if (element.attribute(QStringLiteral("style")).contains(QStringLiteral("font-size"), Qt::CaseInsensitive)) {
            return true;
        }
    }

    for (QDomNode child = node.firstChild(); !child.isNull(); child = child.nextSibling()) {
        if (hasFontSizeStyle(child)) {
            return true;
        }
    }
    return false;
}

void removeRedundantXhtmlNamespaceAttributes(QDomNode node, bool is_root = true)
{
    if (node.isElement()) {
        QDomElement element = node.toElement();
        if (!is_root && element.attribute(QStringLiteral("xmlns")) == XHTML_NS) {
            element.removeAttribute(QStringLiteral("xmlns"));
        }
    }

    for (QDomNode child = node.firstChild(); !child.isNull(); child = child.nextSibling()) {
        removeRedundantXhtmlNamespaceAttributes(child, false);
    }
}

int countImageElements(const QDomNode& root)
{
    int count = 0;
    if (root.isElement()) {
        const QString name = localName(root);
        if (name == QStringLiteral("img") || name == QStringLiteral("image")) {
            count++;
        }
    }

    for (QDomNode child = root.firstChild(); !child.isNull(); child = child.nextSibling()) {
        count += countImageElements(child);
    }
    return count;
}

QString trimAsciiBoundaryWhitespace(QString text, bool trimStart, bool trimEnd)
{
    if (trimStart) {
        int index = 0;
        while (index < text.length()) {
            const QChar ch = text.at(index);
            if (ch == QLatin1Char(' ') || ch == QLatin1Char('\t') ||
                ch == QLatin1Char('\r') || ch == QLatin1Char('\n')) {
                index++;
            } else {
                break;
            }
        }
        text = text.mid(index);
    }

    if (trimEnd) {
        int index = text.length() - 1;
        while (index >= 0) {
            const QChar ch = text.at(index);
            if (ch == QLatin1Char(' ') || ch == QLatin1Char('\t') ||
                ch == QLatin1Char('\r') || ch == QLatin1Char('\n')) {
                index--;
            } else {
                break;
            }
        }
        text = text.left(index + 1);
    }

    return text;
}

void normalizeRunBoundaryText(QList<QDomNode>& nodes)
{
    int first_text = -1;
    int last_text = -1;

    for (int i = 0; i < nodes.count(); ++i) {
        if (nodes.at(i).isText() || nodes.at(i).isCDATASection()) {
            if (first_text == -1) {
                first_text = i;
            }
            last_text = i;
        }
    }

    if (first_text != -1) {
        QDomText text = nodes[first_text].toText();
        text.setData(trimAsciiBoundaryWhitespace(text.data(), true, first_text == last_text));
    }
    if (last_text != -1 && last_text != first_text) {
        QDomText text = nodes[last_text].toText();
        text.setData(trimAsciiBoundaryWhitespace(text.data(), false, true));
    }
}

bool isSceneBreakText(const QString& text)
{
    QString normalized = text.trimmed();
    normalized.remove(QRegularExpression(QStringLiteral("[\\s\\x{3000}]")));
    if (normalized.isEmpty()) {
        return false;
    }

    static const QRegularExpression scene_break(
        QStringLiteral("^[*＊◇◆□■○●・･\\-－—―─━_＿=＝]+$"));
    return scene_break.match(normalized).hasMatch() && normalized.length() <= 16;
}

QString compactText(QString text)
{
    text.remove(QRegularExpression(QStringLiteral("[\\s\\x{3000}]")));
    return text;
}

bool containsAny(const QString& text, const QStringList& keywords)
{
    foreach(const QString& keyword, keywords) {
        if (text.contains(keyword, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

bool isTocLikeText(const QString& body_text, int candidate_paragraphs, int link_runs)
{
    const QString compact = compactText(body_text);
    if (compact.contains(QStringLiteral("目次"), Qt::CaseInsensitive) ||
        compact.contains(QStringLiteral("TableofContents"), Qt::CaseInsensitive) ||
        compact.contains(QStringLiteral("Contents"), Qt::CaseInsensitive)) {
        return true;
    }

    return link_runs >= 2 && link_runs * 2 >= qMax(1, candidate_paragraphs);
}

bool isNoticeOrImprintText(const QString& body_text, int candidate_paragraphs)
{
    if (candidate_paragraphs > 25) {
        return false;
    }

    const QString compact = compactText(body_text);
    const QStringList strong_keywords = {
        QStringLiteral("縦書き表示"),
        QStringLiteral("横書き表示"),
        QStringLiteral("外部リンク"),
        QStringLiteral("本電子書籍"),
        QStringLiteral("私的利用"),
        QStringLiteral("著作権法"),
        QStringLiteral("ブックデザイン"),
        QStringLiteral("ファンレター"),
        QStringLiteral("初出"),
        QStringLiteral("未読の方"),
        QStringLiteral("単行本として刊行"),
        QStringLiteral("電子書籍化"),
        QStringLiteral("掲載されました"),
        QStringLiteral("書き下ろし")
    };
    if (containsAny(compact, strong_keywords)) {
        return true;
    }

    int publication_score = 0;
    const QStringList publication_keywords = {
        QStringLiteral("発行日"),
        QStringLiteral("発行者"),
        QStringLiteral("発行所"),
        QStringLiteral("発行元"),
        QStringLiteral("発行："),
        QStringLiteral("著者："),
        QStringLiteral("©"),
        QStringLiteral("ISBN"),
        QStringLiteral("〒")
    };
    foreach(const QString& keyword, publication_keywords) {
        if (compact.contains(keyword, Qt::CaseInsensitive)) {
            publication_score++;
        }
    }

    return publication_score >= 2;
}

bool isShortBodyFlow(int candidate_paragraphs, int br_count)
{
    return candidate_paragraphs < MIN_AUTO_PARAGRAPHS || br_count < MIN_AUTO_BR_COUNT;
}

bool looksLikePreviousNormalizerOutput(const QString& source)
{
    return source.contains(QStringLiteral("se-chapter-title")) ||
           source.contains(QStringLiteral("se-scene-break")) ||
           source.contains(QStringLiteral("<p xmlns=\"http://www.w3.org/1999/xhtml\"")) ||
           source.contains(QStringLiteral("<p xmlns='http://www.w3.org/1999/xhtml'"));
}

bool isTitleLikeRun(const ParagraphRun& run, int run_index)
{
    if (run_index > 1 || !run.hasFontSize) {
        return false;
    }

    const QString text = run.text.trimmed();
    if (text.isEmpty() || text.length() > 120) {
        return false;
    }

    return text.contains(QStringLiteral("章")) ||
           text.contains(QStringLiteral("プロローグ")) ||
           text.contains(QStringLiteral("エピローグ")) ||
           text.contains(QStringLiteral("序")) ||
           text.contains(QStringLiteral("終"));
}

ParagraphRun makeRun(const QList<QDomNode>& nodes)
{
    ParagraphRun run;
    run.nodes = nodes;

    foreach(const QDomNode& node, nodes) {
        run.text += visibleText(node);
        run.hasHref = run.hasHref || hasHrefElement(node);
        run.hasFontSize = run.hasFontSize || hasFontSizeStyle(node);
    }
    run.hasVisibleText = !run.text.trimmed().isEmpty();
    return run;
}

void appendClass(QDomElement& element, const QString& class_name)
{
    QStringList classes = element.attribute(QStringLiteral("class")).split(QRegularExpression(QStringLiteral("\\s+")),
                                                                          Qt::SkipEmptyParts);
    if (!classes.contains(class_name)) {
        classes << class_name;
    }
    element.setAttribute(QStringLiteral("class"), classes.join(QLatin1Char(' ')));
}

QList<ParagraphRun> collectParagraphRuns(const QDomElement& body)
{
    QList<ParagraphRun> runs;
    QList<QDomNode> pending;
    int pending_gap_before = 0;
    int empty_breaks = 0;

    auto flush = [&]() {
        ParagraphRun run = makeRun(pending);
        if (run.hasVisibleText) {
            run.gapBefore = pending_gap_before;
            runs << run;
        }
        pending.clear();
        pending_gap_before = 0;
    };

    for (QDomNode child = body.firstChild(); !child.isNull(); child = child.nextSibling()) {
        if (isBrElement(child)) {
            ParagraphRun run = makeRun(pending);
            if (run.hasVisibleText) {
                run.gapBefore = pending_gap_before;
                runs << run;
                pending.clear();
                pending_gap_before = 0;
                empty_breaks = 0;
            } else {
                pending.clear();
                empty_breaks++;
            }
            continue;
        }
        if (isBlockElement(child)) {
            flush();
            empty_breaks = 0;
            continue;
        }
        if ((child.isText() || child.isCDATASection()) && isWhitespaceOnly(child.nodeValue())) {
            continue;
        }
        if (pending.isEmpty() && empty_breaks > 0) {
            pending_gap_before = empty_breaks;
            empty_breaks = 0;
        }
        pending << child.cloneNode(true);
    }

    flush();
    return runs;
}

void addScopedVisualPreservationStyle(QDomDocument& document, QDomElement& body)
{
    appendClass(body, BR_NORMALIZER_BODY_CLASS);

    QDomElement head = findElementByLocalName(document, QStringLiteral("head"));
    if (head.isNull()) {
        return;
    }

    const QDomNodeList styles = head.elementsByTagName(QStringLiteral("style"));
    for (int i = 0; i < styles.count(); ++i) {
        const QDomElement style = styles.at(i).toElement();
        if (style.text().contains(BR_NORMALIZER_BODY_CLASS)) {
            return;
        }
    }

    QDomElement style = document.createElement(QStringLiteral("style"));
    style.setAttribute(QStringLiteral("type"), QStringLiteral("text/css"));
    style.appendChild(document.createTextNode(BR_NORMALIZER_CSS));
    head.appendChild(style);
}

bool repairAlreadyNormalizedVisualStyle(const QString& source, BrParagraphNormalizer::NormalizeResult& result)
{
    QDomDocument document;
    QString error;
    if (!parseDocument(source, document, error)) {
        result.messages << QStringLiteral("BR 段落样式修复：XML 解析失败，已跳过。%1").arg(error);
        return false;
    }

    QDomElement body = findElementByLocalName(document, QStringLiteral("body"));
    if (body.isNull()) {
        result.messages << QStringLiteral("BR 段落样式修复：missing body element，已跳过。");
        return false;
    }

    const QString before_text = semanticText(document);
    const QStringList before_ids = sortedAttributes(document, QStringList() << QStringLiteral("id") << QStringLiteral("name"));
    const QStringList before_links = sortedAttributes(document, QStringList() << QStringLiteral("href") << QStringLiteral("src"));

    addScopedVisualPreservationStyle(document, body);
    removeRedundantXhtmlNamespaceAttributes(document.documentElement(), true);

    result.text = document.toString(2);

    QDomDocument after_document;
    QString after_error;
    if (!parseDocument(result.text, after_document, after_error)) {
        result.messages << QStringLiteral("BR 段落样式修复：修复后 XML 解析失败，已回退。%1").arg(after_error);
        return false;
    }

    const QString after_text = semanticText(after_document);
    const QStringList after_ids = sortedAttributes(after_document, QStringList() << QStringLiteral("id") << QStringLiteral("name"));
    const QStringList after_links = sortedAttributes(after_document, QStringList() << QStringLiteral("href") << QStringLiteral("src"));

    if (before_text != after_text) {
        result.messages << QStringLiteral("BR 段落样式修复：修复后可见文本不一致，已回退。");
        return false;
    }
    if (before_ids != after_ids) {
        result.messages << QStringLiteral("BR 段落样式修复：修复后 id/name 集合不一致，已回退。");
        return false;
    }
    if (before_links != after_links) {
        result.messages << QStringLiteral("BR 段落样式修复：修复后 href/src 集合不一致，已回退。");
        return false;
    }

    result.after = BrParagraphNormalizer::analyzeXhtmlText(result.text);
    result.ok = true;
    result.changed = result.text != source;
    result.messages << QStringLiteral("BR 段落样式修复：已补充段落视觉保持样式并清理冗余 XHTML namespace。");
    return true;
}

bool parseDocument(const QString& source, QDomDocument& document, QString& error)
{
    QString parse_error;
    int line = -1;
    int column = -1;
    if (!document.setContent(source, false, &parse_error, &line, &column)) {
        error = QStringLiteral("%1:%2 %3").arg(line).arg(column).arg(parse_error);
        return false;
    }
    return true;
}

void classifyAnalysis(BrParagraphNormalizer::Analysis& analysis, const QDomElement& body)
{
    const QList<ParagraphRun> runs = collectParagraphRuns(body);
    analysis.candidateParagraphs = runs.count();

    for (int i = 0; i < runs.count(); ++i) {
        const ParagraphRun& run = runs.at(i);
        if (run.hasHref) {
            analysis.linkRuns++;
        }
        if (isSceneBreakText(run.text)) {
            analysis.sceneBreaks++;
        }
        if (isTitleLikeRun(run, i)) {
            analysis.titleParagraphs++;
        }
    }

    const QString body_text = visibleText(body);
    analysis.bodyTextLength = body_text.trimmed().length();
    const bool toc_like = isTocLikeText(body_text, analysis.candidateParagraphs, analysis.linkRuns);

    if (analysis.pCount > 0) {
        analysis.pageKind = BrParagraphNormalizer::PageKind::AlreadyNormalized;
        analysis.reason = QStringLiteral("already contains p elements");
    } else if (analysis.directBlockChildren > 0) {
        if (analysis.imageCount > 0 || body_text.trimmed().length() < 120) {
            analysis.pageKind = BrParagraphNormalizer::PageKind::ImageOrTitlePage;
            analysis.reason = QStringLiteral("body contains block layout for image/title-like content");
        } else {
            analysis.pageKind = BrParagraphNormalizer::PageKind::BlockLayout;
            analysis.reason = QStringLiteral("body already contains direct block layout");
        }
    } else if (toc_like) {
        analysis.pageKind = BrParagraphNormalizer::PageKind::TocLike;
        analysis.reason = QStringLiteral("toc-like page");
    } else if (analysis.imageCount > 0) {
        analysis.pageKind = BrParagraphNormalizer::PageKind::ImageOrTitlePage;
        analysis.reason = QStringLiteral("image/title-like page");
    } else if (analysis.brCount == 0 || analysis.candidateParagraphs == 0) {
        analysis.pageKind = BrParagraphNormalizer::PageKind::NoCandidate;
        analysis.reason = QStringLiteral("no br paragraph flow found");
    } else if (isNoticeOrImprintText(body_text, analysis.candidateParagraphs)) {
        analysis.pageKind = BrParagraphNormalizer::PageKind::NoticeOrImprint;
        analysis.candidate = true;
        analysis.reason = QStringLiteral("notice/imprint-like br flow requires manual review");
    } else if (isShortBodyFlow(analysis.candidateParagraphs, analysis.brCount)) {
        analysis.pageKind = BrParagraphNormalizer::PageKind::ShortFlow;
        analysis.candidate = true;
        analysis.reason = QStringLiteral("short br flow requires manual review");
    } else {
        analysis.pageKind = BrParagraphNormalizer::PageKind::NormalBodyFlow;
        analysis.candidate = true;
        analysis.safeToNormalize = true;
        analysis.reason = QStringLiteral("normal body-level br paragraph flow");
    }

    if (analysis.safeToNormalize) {
        analysis.message = QStringLiteral("BR 段落分析：可自动规范化正文页，发现 %1 个 br，可生成约 %2 个段落（标题 %3，场景分隔 %4，链接段 %5）。")
            .arg(analysis.brCount)
            .arg(analysis.candidateParagraphs)
            .arg(analysis.titleParagraphs)
            .arg(analysis.sceneBreaks)
            .arg(analysis.linkRuns);
    } else if (analysis.candidate) {
        analysis.message = QStringLiteral("BR 段落分析：需人工确认（%1）。br=%2，p=%3，可生成约 %4 个段落，正文字符=%5，链接段=%6。")
            .arg(analysis.reason)
            .arg(analysis.brCount)
            .arg(analysis.pCount)
            .arg(analysis.candidateParagraphs)
            .arg(analysis.bodyTextLength)
            .arg(analysis.linkRuns);
    } else {
        analysis.message = QStringLiteral("BR 段落分析：已跳过（%1）。br=%2，p=%3，候选段=%4，顶层块=%5，图片=%6。")
            .arg(analysis.reason)
            .arg(analysis.brCount)
            .arg(analysis.pCount)
            .arg(analysis.candidateParagraphs)
            .arg(analysis.directBlockChildren)
            .arg(analysis.imageCount);
    }
}

QDomElement createParagraph(QDomDocument& document, const ParagraphRun& run, int run_index)
{
    QDomElement paragraph = document.createElement(QStringLiteral("p"));
    if (isSceneBreakText(run.text)) {
        appendClass(paragraph, QStringLiteral("se-scene-break"));
    } else if (isTitleLikeRun(run, run_index)) {
        appendClass(paragraph, QStringLiteral("se-chapter-title"));
    }
    if (run.gapBefore > 0) {
        appendClass(paragraph, BR_NORMALIZER_GAP_CLASS);
    }

    QList<QDomNode> nodes = run.nodes;
    normalizeRunBoundaryText(nodes);
    foreach(const QDomNode& node, nodes) {
        if ((node.isText() || node.isCDATASection()) && node.nodeValue().isEmpty()) {
            continue;
        }
        paragraph.appendChild(node);
    }

    return paragraph;
}

}

QString BrParagraphNormalizer::pageKindName(PageKind pageKind)
{
    switch (pageKind) {
    case PageKind::NormalBodyFlow:
        return QStringLiteral("normal-body-flow");
    case PageKind::AlreadyNormalized:
        return QStringLiteral("already-normalized");
    case PageKind::TocLike:
        return QStringLiteral("toc-like");
    case PageKind::NoticeOrImprint:
        return QStringLiteral("notice-or-imprint");
    case PageKind::ShortFlow:
        return QStringLiteral("short-flow");
    case PageKind::BlockLayout:
        return QStringLiteral("block-layout");
    case PageKind::ImageOrTitlePage:
        return QStringLiteral("image-or-title-page");
    case PageKind::NoCandidate:
        return QStringLiteral("no-candidate");
    case PageKind::NoBody:
        return QStringLiteral("no-body");
    case PageKind::ParseError:
        return QStringLiteral("parse-error");
    }
    return QStringLiteral("unknown");
}

BrParagraphNormalizer::Analysis BrParagraphNormalizer::analyzeXhtmlText(const QString& source)
{
    Analysis analysis;
    QDomDocument document;
    QString error;

    if (!parseDocument(source, document, error)) {
        analysis.pageKind = PageKind::ParseError;
        analysis.reason = error;
        analysis.message = QStringLiteral("BR 段落分析：XML 解析失败，已跳过。%1").arg(error);
        return analysis;
    }

    analysis.ok = true;
    analysis.brCount = countElementsByLocalName(document, QStringLiteral("br"));
    analysis.pCount = countElementsByLocalName(document, QStringLiteral("p"));
    analysis.imageCount = countImageElements(document);

    const QDomElement body = findElementByLocalName(document, QStringLiteral("body"));
    if (body.isNull()) {
        analysis.pageKind = PageKind::NoBody;
        analysis.reason = QStringLiteral("missing body element");
        analysis.message = QStringLiteral("BR 段落分析：已跳过（missing body element）。");
        return analysis;
    }

    for (QDomNode child = body.firstChild(); !child.isNull(); child = child.nextSibling()) {
        if ((child.isText() || child.isCDATASection()) && !isWhitespaceOnly(child.nodeValue())) {
            analysis.directTextSegments++;
        } else if (isBlockElement(child)) {
            analysis.directBlockChildren++;
        }
    }

    classifyAnalysis(analysis, body);
    if (analysis.pageKind == PageKind::AlreadyNormalized &&
        !elementHasClass(body, BR_NORMALIZER_BODY_CLASS) &&
        looksLikePreviousNormalizerOutput(source)) {
        analysis.candidate = true;
        analysis.safeToNormalize = true;
        analysis.candidateParagraphs = analysis.pCount;
        analysis.reason = QStringLiteral("previous br normalization output missing visual preservation style");
        analysis.message = QStringLiteral("BR 段落分析：已规范化页面可补充视觉保持样式。p=%1，br=%2。")
            .arg(analysis.pCount)
            .arg(analysis.brCount);
    }
    return analysis;
}

BrParagraphNormalizer::NormalizeResult BrParagraphNormalizer::normalizeXhtmlText(const QString& source, bool allowManualReview)
{
    NormalizeResult result;
    result.before = analyzeXhtmlText(source);
    if (!result.before.ok) {
        result.messages << result.before.message;
        return result;
    }
    if (!result.before.candidate) {
        result.messages << result.before.message;
        return result;
    }
    if (!result.before.safeToNormalize && !allowManualReview) {
        result.messages << QStringLiteral("BR 段落规范化：已跳过，%1。").arg(result.before.reason);
        return result;
    }
    if (result.before.pageKind == PageKind::AlreadyNormalized) {
        repairAlreadyNormalizedVisualStyle(source, result);
        return result;
    }

    QDomDocument document;
    QString error;
    if (!parseDocument(source, document, error)) {
        result.messages << QStringLiteral("BR 段落规范化：XML 解析失败，已跳过。%1").arg(error);
        return result;
    }

    QDomElement body = findElementByLocalName(document, QStringLiteral("body"));
    const QString before_text = semanticText(document);
    const QStringList before_ids = sortedAttributes(document, QStringList() << QStringLiteral("id") << QStringLiteral("name"));
    const QStringList before_links = sortedAttributes(document, QStringList() << QStringLiteral("href") << QStringLiteral("src"));
    QList<ParagraphRun> runs = collectParagraphRuns(body);

    addScopedVisualPreservationStyle(document, body);

    while (!body.firstChild().isNull()) {
        body.removeChild(body.firstChild());
    }

    for (int i = 0; i < runs.count(); ++i) {
        body.appendChild(createParagraph(document, runs.at(i), i));
    }

    result.text = document.toString(2);
    result.after = analyzeXhtmlText(result.text);
    if (!result.after.ok) {
        result.messages << result.after.message;
        return result;
    }

    QDomDocument after_document;
    QString after_error;
    if (!parseDocument(result.text, after_document, after_error)) {
        result.messages << QStringLiteral("BR 段落规范化：转换后 XML 解析失败，已回退。%1").arg(after_error);
        return result;
    }

    const QString after_text = semanticText(after_document);
    const QStringList after_ids = sortedAttributes(after_document, QStringList() << QStringLiteral("id") << QStringLiteral("name"));
    const QStringList after_links = sortedAttributes(after_document, QStringList() << QStringLiteral("href") << QStringLiteral("src"));

    if (before_text != after_text) {
        result.messages << QStringLiteral("BR 段落规范化：转换后可见文本不一致，已回退。");
        return result;
    }
    if (before_ids != after_ids) {
        result.messages << QStringLiteral("BR 段落规范化：转换后 id/name 集合不一致，已回退。");
        return result;
    }
    if (before_links != after_links) {
        result.messages << QStringLiteral("BR 段落规范化：转换后 href/src 集合不一致，已回退。");
        return result;
    }

    result.ok = true;
    result.changed = result.text != source;
    result.messages << QStringLiteral("BR 段落规范化：已生成 %1 个段落（标题 %2，场景分隔 %3）。")
                           .arg(result.before.candidateParagraphs)
                           .arg(result.before.titleParagraphs)
                           .arg(result.before.sceneBreaks);
    return result;
}

}
