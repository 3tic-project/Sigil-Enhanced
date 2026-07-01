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

#include "BuiltinPlugins/KfxParagraphNormalizer.h"

#include <QDomDocument>
#include <QRegularExpression>
#include <QSet>
#include <QtGlobal>

namespace BuiltinPlugins
{

namespace
{

const QString XHTML_NS = QStringLiteral("http://www.w3.org/1999/xhtml");
const int MIN_AUTO_PARAGRAPHS = 12;
const int MIN_AUTO_SPACERS = 4;
const QString KFX_NORMALIZER_BODY_CLASS = QStringLiteral("se-kfx-normalized");
const QString KFX_NORMALIZER_PARAGRAPH_CLASS = QStringLiteral("se-kfx-paragraph");
const QString KFX_NORMALIZER_TITLE_CLASS = QStringLiteral("se-kfx-title");
const QString KFX_NORMALIZER_IMAGE_CLASS = QStringLiteral("se-kfx-image");
const QString KFX_NORMALIZER_SCENE_CLASS = QStringLiteral("se-kfx-scene-break");
const QString KFX_NORMALIZER_CSS =
    QStringLiteral("body.se-kfx-normalized p.se-kfx-paragraph { display: block; margin: 0; margin-block-start: 0; margin-block-end: 0; padding: 0; border: 0; min-height: 0; font-size: inherit; font-family: inherit; line-height: inherit; text-align: inherit; writing-mode: inherit; -webkit-writing-mode: inherit; }\n"
                   "body.se-kfx-normalized p.se-kfx-paragraph * { line-height: inherit; }\n");

enum class SpacerKind {
    None,
    ZeroHeight,
    Gap
};

struct ParagraphRun {
    QList<QDomNode> nodes;
    QString text;
    bool hasHref = false;
    bool hasImage = false;
    bool hasVisibleText = false;
};

struct FlowItem {
    bool isSpacer = false;
    QDomNode spacer;
    ParagraphRun run;
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

QString normalizeLayoutSpaces(QString text)
{
    text.replace(QChar(0x00a0), QLatin1Char(' '));
    text.replace(QChar(0x202f), QLatin1Char(' '));
    text.replace(QChar(0xfeff), QLatin1Char(' '));
    return text;
}

bool isWhitespaceOnly(const QString& text)
{
    return normalizeLayoutSpaces(text).trimmed().isEmpty();
}

bool classListContains(const QString& class_list, const QString& class_name)
{
    return class_list.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).contains(class_name);
}

bool elementHasClass(const QDomElement& element, const QString& class_name)
{
    return classListContains(element.attribute(QStringLiteral("class")), class_name);
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

bool hasElementByLocalName(const QDomNode& root, const QString& name)
{
    if (root.isElement() && localName(root) == name) {
        return true;
    }
    for (QDomNode child = root.firstChild(); !child.isNull(); child = child.nextSibling()) {
        if (hasElementByLocalName(child, name)) {
            return true;
        }
    }
    return false;
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

bool parseCssLength(const QString& value, double& number, QString& unit)
{
    static const QRegularExpression length_pattern(
        QStringLiteral("^\\s*([-+]?(?:\\d+(?:\\.\\d+)?|\\.\\d+))\\s*([a-z%]*)\\s*$"),
        QRegularExpression::CaseInsensitiveOption);

    const QRegularExpressionMatch match = length_pattern.match(value);
    if (!match.hasMatch()) {
        return false;
    }

    bool ok = false;
    number = match.captured(1).toDouble(&ok);
    if (!ok) {
        return false;
    }
    unit = match.captured(2).toLower();
    return true;
}

QString cssProperty(const QString& style, const QString& property)
{
    const QString escaped = QRegularExpression::escape(property);
    const QRegularExpression pattern(QStringLiteral("(?:^|;)\\s*%1\\s*:\\s*([^;]+)")
                                         .arg(escaped),
                                     QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = pattern.match(style);
    return match.hasMatch() ? match.captured(1).trimmed() : QString();
}

bool containsMeaningfulElement(const QDomNode& node)
{
    if (!node.isElement()) {
        return false;
    }

    const QString name = localName(node);
    static const QSet<QString> meaningful_names = {
        QStringLiteral("a"),
        QStringLiteral("audio"),
        QStringLiteral("canvas"),
        QStringLiteral("embed"),
        QStringLiteral("iframe"),
        QStringLiteral("image"),
        QStringLiteral("img"),
        QStringLiteral("math"),
        QStringLiteral("object"),
        QStringLiteral("svg"),
        QStringLiteral("table"),
        QStringLiteral("video")
    };
    if (meaningful_names.contains(name)) {
        return true;
    }

    for (QDomNode child = node.firstChild(); !child.isNull(); child = child.nextSibling()) {
        if (containsMeaningfulElement(child)) {
            return true;
        }
    }
    return false;
}

SpacerKind spacerKind(const QDomNode& node)
{
    if (!node.isElement() || localName(node) != QStringLiteral("p")) {
        return SpacerKind::None;
    }

    const QDomElement element = node.toElement();
    if (element.hasAttribute(QStringLiteral("id")) ||
        element.hasAttribute(QStringLiteral("name")) ||
        element.hasAttribute(QStringLiteral("href")) ||
        element.hasAttribute(QStringLiteral("src")) ||
        containsMeaningfulElement(element)) {
        return SpacerKind::None;
    }

    if (!isWhitespaceOnly(visibleText(element))) {
        return SpacerKind::None;
    }

    const QString height = cssProperty(element.attribute(QStringLiteral("style")), QStringLiteral("height"));
    if (height.isEmpty()) {
        return SpacerKind::None;
    }

    double value = 0.0;
    QString unit;
    if (!parseCssLength(height, value, unit)) {
        return SpacerKind::None;
    }

    if (value <= 0.0001) {
        return SpacerKind::ZeroHeight;
    }

    return SpacerKind::Gap;
}

QString semanticText(const QDomNode& node)
{
    if (node.isText() || node.isCDATASection()) {
        const QString text = node.nodeValue();
        return isWhitespaceOnly(text) ? QString() : text;
    }
    if (!node.isElement()) {
        return QString();
    }
    if (spacerKind(node) != SpacerKind::None) {
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

bool isSceneBreakText(const QString& text)
{
    QString normalized = text.trimmed();
    normalized.remove(QRegularExpression(QStringLiteral("[\\s\\x{3000}]")));
    if (normalized.isEmpty()) {
        return false;
    }

    static const QRegularExpression scene_break(
        QStringLiteral("^[*＊☆★◇◆□■○●・･\\-－—―─━_＿=＝]+$"));
    return scene_break.match(normalized).hasMatch() && normalized.length() <= 16;
}

QString compactText(QString text)
{
    text = normalizeLayoutSpaces(text);
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

bool isTocLikeText(const QString& body_text, int candidate_paragraphs, int link_count, int br_count)
{
    const QString compact = compactText(body_text);
    if (compact.contains(QStringLiteral("目次"), Qt::CaseInsensitive) ||
        compact.contains(QStringLiteral("TableofContents"), Qt::CaseInsensitive) ||
        compact.contains(QStringLiteral("Contents"), Qt::CaseInsensitive)) {
        return true;
    }

    if (link_count >= 4 && br_count >= qMax(1, link_count - 2)) {
        return true;
    }

    return link_count >= 3 && link_count >= qMax(1, candidate_paragraphs);
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
        QStringLiteral("電子版発行"),
        QStringLiteral("本データには購買者"),
        QStringLiteral("複製"),
        QStringLiteral("頒布"),
        QStringLiteral("転売")
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
        QStringLiteral("発行"),
        QStringLiteral("著者"),
        QStringLiteral("挿絵"),
        QStringLiteral("ISBN"),
        QStringLiteral("©"),
        QStringLiteral("(c)"),
        QStringLiteral("Printedin")
    };
    foreach(const QString& keyword, publication_keywords) {
        if (compact.contains(keyword, Qt::CaseInsensitive)) {
            publication_score++;
        }
    }

    return publication_score >= 3;
}

bool isShortBodyFlow(int candidate_paragraphs, int spacer_count)
{
    return candidate_paragraphs < MIN_AUTO_PARAGRAPHS || spacer_count < MIN_AUTO_SPACERS;
}

bool isTitleLikeRun(const ParagraphRun& run, int run_index)
{
    if (run_index > 1 || !run.hasHref) {
        return false;
    }

    const QString text = run.text.trimmed();
    return !text.isEmpty() && text.length() <= 120;
}

ParagraphRun makeRun(const QList<QDomNode>& nodes)
{
    ParagraphRun run;
    run.nodes = nodes;

    foreach(const QDomNode& node, nodes) {
        run.text += visibleText(node);
        run.hasHref = run.hasHref || hasHrefElement(node);
        run.hasImage = run.hasImage || hasElementByLocalName(node, QStringLiteral("img")) ||
                       hasElementByLocalName(node, QStringLiteral("image"));
    }
    run.hasVisibleText = !normalizeLayoutSpaces(run.text).trimmed().isEmpty();
    return run;
}

bool runHasContent(const ParagraphRun& run)
{
    return run.hasVisibleText || run.hasImage;
}

QList<ParagraphRun> collectParagraphRuns(const QDomElement& body)
{
    QList<ParagraphRun> runs;
    QList<QDomNode> pending;

    auto flush = [&]() {
        ParagraphRun run = makeRun(pending);
        if (runHasContent(run)) {
            runs << run;
        }
        pending.clear();
    };

    for (QDomNode child = body.firstChild(); !child.isNull(); child = child.nextSibling()) {
        const SpacerKind kind = spacerKind(child);
        if (kind != SpacerKind::None) {
            flush();
            continue;
        }
        if ((child.isText() || child.isCDATASection()) && isWhitespaceOnly(child.nodeValue())) {
            continue;
        }
        if (isBlockElement(child)) {
            flush();
            continue;
        }
        pending << child.cloneNode(true);
    }

    flush();
    return runs;
}

QList<FlowItem> collectFlowItems(const QDomElement& body)
{
    QList<FlowItem> items;
    QList<QDomNode> pending;

    auto flush = [&]() {
        ParagraphRun run = makeRun(pending);
        if (runHasContent(run)) {
            FlowItem item;
            item.run = run;
            items << item;
        }
        pending.clear();
    };

    for (QDomNode child = body.firstChild(); !child.isNull(); child = child.nextSibling()) {
        const SpacerKind kind = spacerKind(child);
        if (kind != SpacerKind::None) {
            flush();
            if (kind == SpacerKind::Gap) {
                FlowItem item;
                item.isSpacer = true;
                item.spacer = child.cloneNode(true);
                items << item;
            }
            continue;
        }
        if ((child.isText() || child.isCDATASection()) && isWhitespaceOnly(child.nodeValue())) {
            continue;
        }
        if (isBlockElement(child)) {
            flush();
            continue;
        }
        pending << child.cloneNode(true);
    }

    flush();
    return items;
}

void addScopedVisualPreservationStyle(QDomDocument& document, QDomElement& body)
{
    appendClass(body, KFX_NORMALIZER_BODY_CLASS);

    QDomElement head = findElementByLocalName(document, QStringLiteral("head"));
    if (head.isNull()) {
        return;
    }

    const QDomNodeList styles = head.elementsByTagName(QStringLiteral("style"));
    for (int i = 0; i < styles.count(); ++i) {
        const QDomElement style = styles.at(i).toElement();
        if (style.text().contains(KFX_NORMALIZER_BODY_CLASS)) {
            return;
        }
    }

    QDomElement style = document.createElement(QStringLiteral("style"));
    style.setAttribute(QStringLiteral("type"), QStringLiteral("text/css"));
    style.appendChild(document.createTextNode(KFX_NORMALIZER_CSS));
    head.appendChild(style);
}

QDomElement createParagraph(QDomDocument& document, const ParagraphRun& run, int run_index)
{
    QDomElement paragraph = document.createElement(QStringLiteral("p"));
    appendClass(paragraph, KFX_NORMALIZER_PARAGRAPH_CLASS);

    if (isTitleLikeRun(run, run_index)) {
        appendClass(paragraph, KFX_NORMALIZER_TITLE_CLASS);
    }
    if (run.hasImage && !run.hasVisibleText) {
        appendClass(paragraph, KFX_NORMALIZER_IMAGE_CLASS);
    }
    if (run.hasVisibleText && isSceneBreakText(run.text)) {
        appendClass(paragraph, KFX_NORMALIZER_SCENE_CLASS);
    }

    QList<QDomNode> nodes = run.nodes;
    foreach(const QDomNode& node, nodes) {
        if ((node.isText() || node.isCDATASection()) && node.nodeValue().isEmpty()) {
            continue;
        }
        paragraph.appendChild(node);
    }

    return paragraph;
}

QDomNode createNonEmptySpacer(QDomDocument& document, const QDomNode& source)
{
    QDomNode spacer = source.cloneNode(true);
    if (!spacer.isElement()) {
        return spacer;
    }

    QDomElement element = spacer.toElement();
    bool has_child = false;
    for (QDomNode child = element.firstChild(); !child.isNull(); child = child.nextSibling()) {
        has_child = true;
        break;
    }
    if (!has_child) {
        element.appendChild(document.createTextNode(QString(QChar(0x00a0))));
    }
    return spacer;
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

void classifyAnalysis(KfxParagraphNormalizer::Analysis& analysis, const QDomElement& body)
{
    const QList<ParagraphRun> runs = collectParagraphRuns(body);
    analysis.candidateParagraphs = runs.count();

    for (int i = 0; i < runs.count(); ++i) {
        const ParagraphRun& run = runs.at(i);
        if (isTitleLikeRun(run, i)) {
            analysis.titleParagraphs++;
        }
        if (run.hasImage && !run.hasVisibleText) {
            analysis.imageParagraphs++;
        }
        if (run.hasVisibleText && isSceneBreakText(run.text)) {
            analysis.sceneBreaks++;
        }
    }

    const QString body_text = semanticText(body);
    analysis.bodyTextLength = body_text.trimmed().length();
    const bool has_direct_flow = analysis.directTextSegments > 0 || analysis.directInlineRuns > 0;
    const bool toc_like = isTocLikeText(body_text, analysis.candidateParagraphs,
                                        analysis.linkCount, analysis.brCount);

    if (!has_direct_flow && analysis.meaningfulParagraphs > 0) {
        analysis.pageKind = KfxParagraphNormalizer::PageKind::AlreadyNormalized;
        analysis.reason = QStringLiteral("body already has paragraph/block content and no direct text flow");
    } else if (analysis.directBlockChildren > 0) {
        if (analysis.imageCount > 0 || analysis.bodyTextLength < 120) {
            analysis.pageKind = KfxParagraphNormalizer::PageKind::ImageOrTitlePage;
            analysis.reason = QStringLiteral("body contains block layout for image/title-like content");
        } else {
            analysis.pageKind = KfxParagraphNormalizer::PageKind::BlockLayout;
            analysis.reason = QStringLiteral("body has mixed direct text and block layout");
        }
    } else if (toc_like) {
        analysis.pageKind = KfxParagraphNormalizer::PageKind::TocLike;
        analysis.reason = QStringLiteral("toc-like link/br page");
    } else if (analysis.spacerParagraphs == 0 || analysis.candidateParagraphs == 0 || !has_direct_flow) {
        analysis.pageKind = KfxParagraphNormalizer::PageKind::NoCandidate;
        analysis.reason = QStringLiteral("no KFX spacer paragraph flow found");
    } else if (analysis.imageCount > 0 && analysis.candidateParagraphs <= 3) {
        analysis.pageKind = KfxParagraphNormalizer::PageKind::ImageOrTitlePage;
        analysis.reason = QStringLiteral("image/title-like short flow");
    } else if (isNoticeOrImprintText(body_text, analysis.candidateParagraphs)) {
        analysis.pageKind = KfxParagraphNormalizer::PageKind::NoticeOrImprint;
        analysis.candidate = true;
        analysis.reason = QStringLiteral("notice/imprint-like KFX flow requires manual review");
    } else if (isShortBodyFlow(analysis.candidateParagraphs, analysis.spacerParagraphs)) {
        analysis.pageKind = KfxParagraphNormalizer::PageKind::ShortFlow;
        analysis.candidate = true;
        analysis.reason = QStringLiteral("short KFX spacer flow requires manual review");
    } else {
        analysis.pageKind = KfxParagraphNormalizer::PageKind::NormalBodyFlow;
        analysis.candidate = true;
        analysis.safeToNormalize = true;
        analysis.reason = QStringLiteral("normal KFX spacer paragraph flow");
    }

    if (analysis.safeToNormalize) {
        analysis.message = QStringLiteral("KFX 段落分析：可自动规范化正文页，发现 %1 个 spacer p（0 高度 %2，空白间距 %3），可生成约 %4 个段落（标题 %5，插图段 %6，场景分隔 %7）。")
            .arg(analysis.spacerParagraphs)
            .arg(analysis.zeroHeightSpacers)
            .arg(analysis.gapSpacers)
            .arg(analysis.candidateParagraphs)
            .arg(analysis.titleParagraphs)
            .arg(analysis.imageParagraphs)
            .arg(analysis.sceneBreaks);
    } else if (analysis.candidate) {
        analysis.message = QStringLiteral("KFX 段落分析：需人工确认（%1）。spacer=%2，0 高度=%3，空白间距=%4，可生成约 %5 个段落，正文字符=%6。")
            .arg(analysis.reason)
            .arg(analysis.spacerParagraphs)
            .arg(analysis.zeroHeightSpacers)
            .arg(analysis.gapSpacers)
            .arg(analysis.candidateParagraphs)
            .arg(analysis.bodyTextLength);
    } else {
        analysis.message = QStringLiteral("KFX 段落分析：已跳过（%1）。spacer=%2，候选段=%3，顶层块=%4，链接=%5，图片=%6。")
            .arg(analysis.reason)
            .arg(analysis.spacerParagraphs)
            .arg(analysis.candidateParagraphs)
            .arg(analysis.directBlockChildren)
            .arg(analysis.linkCount)
            .arg(analysis.imageCount);
    }
}

}

QString KfxParagraphNormalizer::pageKindName(PageKind pageKind)
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

KfxParagraphNormalizer::Analysis KfxParagraphNormalizer::analyzeXhtmlText(const QString& source)
{
    Analysis analysis;
    QDomDocument document;
    QString error;

    if (!parseDocument(source, document, error)) {
        analysis.pageKind = PageKind::ParseError;
        analysis.reason = error;
        analysis.message = QStringLiteral("KFX 段落分析：XML 解析失败，已跳过。%1").arg(error);
        return analysis;
    }

    analysis.ok = true;
    analysis.brCount = countElementsByLocalName(document, QStringLiteral("br"));
    analysis.linkCount = countElementsByLocalName(document, QStringLiteral("a"));
    analysis.imageCount = countImageElements(document);

    const QDomElement body = findElementByLocalName(document, QStringLiteral("body"));
    if (body.isNull()) {
        analysis.pageKind = PageKind::NoBody;
        analysis.reason = QStringLiteral("missing body element");
        analysis.message = QStringLiteral("KFX 段落分析：已跳过（missing body element）。");
        return analysis;
    }

    for (QDomNode child = body.firstChild(); !child.isNull(); child = child.nextSibling()) {
        const SpacerKind kind = spacerKind(child);
        if (kind == SpacerKind::ZeroHeight) {
            analysis.spacerParagraphs++;
            analysis.zeroHeightSpacers++;
            continue;
        }
        if (kind == SpacerKind::Gap) {
            analysis.spacerParagraphs++;
            analysis.gapSpacers++;
            continue;
        }

        if ((child.isText() || child.isCDATASection()) && !isWhitespaceOnly(child.nodeValue())) {
            analysis.directTextSegments++;
        } else if (isBlockElement(child)) {
            analysis.directBlockChildren++;
            if (localName(child) == QStringLiteral("p") && !isWhitespaceOnly(visibleText(child))) {
                analysis.meaningfulParagraphs++;
            }
        } else if (child.isElement()) {
            analysis.directInlineRuns++;
        }
    }

    classifyAnalysis(analysis, body);
    if (analysis.pageKind == PageKind::AlreadyNormalized &&
        elementHasClass(body, KFX_NORMALIZER_BODY_CLASS)) {
        analysis.reason = QStringLiteral("already normalized by KFX paragraph normalizer");
        analysis.message = QStringLiteral("KFX 段落分析：已规范化页面，发现 %1 个段落，保留 %2 个空白间距 p。")
            .arg(analysis.meaningfulParagraphs)
            .arg(analysis.gapSpacers);
    }
    return analysis;
}

KfxParagraphNormalizer::NormalizeResult KfxParagraphNormalizer::normalizeXhtmlText(const QString& source, bool allowManualReview)
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
        result.messages << QStringLiteral("KFX 段落规范化：已跳过，%1。").arg(result.before.reason);
        return result;
    }

    QDomDocument document;
    QString error;
    if (!parseDocument(source, document, error)) {
        result.messages << QStringLiteral("KFX 段落规范化：XML 解析失败，已跳过。%1").arg(error);
        return result;
    }

    QDomElement body = findElementByLocalName(document, QStringLiteral("body"));
    const QString before_text = semanticText(document);
    const QStringList before_ids = sortedAttributes(document, QStringList() << QStringLiteral("id") << QStringLiteral("name"));
    const QStringList before_links = sortedAttributes(document, QStringList() << QStringLiteral("href") << QStringLiteral("src"));
    const QList<FlowItem> flow_items = collectFlowItems(body);

    addScopedVisualPreservationStyle(document, body);

    while (!body.firstChild().isNull()) {
        body.removeChild(body.firstChild());
    }

    int paragraph_index = 0;
    foreach(const FlowItem& item, flow_items) {
        if (item.isSpacer) {
            body.appendChild(createNonEmptySpacer(document, item.spacer));
        } else {
            body.appendChild(createParagraph(document, item.run, paragraph_index));
            paragraph_index++;
        }
    }

    removeRedundantXhtmlNamespaceAttributes(document.documentElement(), true);
    result.text = document.toString(2);

    QDomDocument after_document;
    QString after_error;
    if (!parseDocument(result.text, after_document, after_error)) {
        result.messages << QStringLiteral("KFX 段落规范化：转换后 XML 解析失败，已回退。%1").arg(after_error);
        return result;
    }

    const QString after_text = semanticText(after_document);
    const QStringList after_ids = sortedAttributes(after_document, QStringList() << QStringLiteral("id") << QStringLiteral("name"));
    const QStringList after_links = sortedAttributes(after_document, QStringList() << QStringLiteral("href") << QStringLiteral("src"));

    if (before_text != after_text) {
        result.messages << QStringLiteral("KFX 段落规范化：转换后可见文本不一致，已回退。");
        return result;
    }
    if (before_ids != after_ids) {
        result.messages << QStringLiteral("KFX 段落规范化：转换后 id/name 集合不一致，已回退。");
        return result;
    }
    if (before_links != after_links) {
        result.messages << QStringLiteral("KFX 段落规范化：转换后 href/src 集合不一致，已回退。");
        return result;
    }

    result.after = analyzeXhtmlText(result.text);
    result.ok = true;
    result.changed = result.text != source;
    result.messages << QStringLiteral("KFX 段落规范化：已生成 %1 个段落（标题 %2，插图段 %3，场景分隔 %4），移除 %5 个 0 高度 spacer p，保留 %6 个空白间距 p。")
                           .arg(result.before.candidateParagraphs)
                           .arg(result.before.titleParagraphs)
                           .arg(result.before.imageParagraphs)
                           .arg(result.before.sceneBreaks)
                           .arg(result.before.zeroHeightSpacers)
                           .arg(result.before.gapSpacers);
    return result;
}

}
