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

#include "BuiltinPlugins/BookLiveParagraphNormalizer.h"

#include <QDomDocument>
#include <QRegularExpression>
#include <QSet>
#include <QVector>
#include <QtGlobal>

#include <tuple>

namespace BuiltinPlugins
{

namespace
{

const QString XHTML_NS = QStringLiteral("http://www.w3.org/1999/xhtml");
const QString NORMALIZED_CLASS = QStringLiteral("se-bl-normalized");
const QString PARAGRAPH_CLASS = QStringLiteral("se-bl-paragraph");
const QString INNER_BLOCK_CLASS = QStringLiteral("se-bl-inner-block");
const QString STYLE_MARKER = QStringLiteral("booklive-paragraph-normalizer");
const QString NORMALIZER_CSS =
    QStringLiteral(".se-bl-paragraph { display: block; width: auto; height: auto; margin: 0; padding: 0; border: 0; min-height: 0; text-indent: 0; }\n"
                   ".se-bl-inner-block { display: block; width: auto; height: auto; margin: 0; padding: 0; text-indent: 0; }\n");

const int MIN_STANDARD_PARENT_CHILDREN = 5;
const int MIN_SHORT_PARENT_CHILDREN = 2;
const int MIN_AUTO_PARAGRAPHS = 12;
const int MAX_CONTENT_PARENT_CHILDREN = 20000;
const double MIN_LEAFISH_SCORE = 0.75;

enum class LeafKind {
    Paragraph,
    SpacerBr,
    SceneBreak,
    ImageOnly,
    WrappedBlock,
    AnchorOnly,
    Heading,
    ExistingP,
    NestedComplex,
    Other
};

struct Leaf {
    QDomElement element;
    LeafKind kind = LeafKind::Other;
};

struct ParentMatch {
    QDomElement element;
    int childCount = 0;
    int depth = 0;
    double score = 0.0;
    bool usedShortPass = false;
};

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

QString compactText(QString text)
{
    text = normalizeLayoutSpaces(text);
    text.remove(QRegularExpression(QStringLiteral("[\\s\\x{3000}]")));
    return text;
}

bool classListContains(const QString& class_list, const QString& class_name)
{
    return class_list.split(QRegularExpression(QStringLiteral("\\s+")),
                            Qt::SkipEmptyParts).contains(class_name);
}

bool elementHasClass(const QDomElement& element, const QString& class_name)
{
    return classListContains(element.attribute(QStringLiteral("class")), class_name);
}

void appendClass(QDomElement& element, const QString& class_name)
{
    QStringList classes = element.attribute(QStringLiteral("class"))
                              .split(QRegularExpression(QStringLiteral("\\s+")),
                                     Qt::SkipEmptyParts);
    if (!classes.contains(class_name)) {
        classes << class_name;
    }
    element.setAttribute(QStringLiteral("class"), classes.join(QLatin1Char(' ')));
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

QDomElement findElementByLocalName(const QDomNode& root, const QString& name)
{
    if (root.isElement() && localName(root) == name) {
        return root.toElement();
    }
    for (QDomNode child = root.firstChild(); !child.isNull(); child = child.nextSibling()) {
        const QDomElement found = findElementByLocalName(child, name);
        if (!found.isNull()) {
            return found;
        }
    }
    return QDomElement();
}

int countElementsByLocalName(const QDomNode& root, const QString& name)
{
    int count = root.isElement() && localName(root) == name ? 1 : 0;
    for (QDomNode child = root.firstChild(); !child.isNull(); child = child.nextSibling()) {
        count += countElementsByLocalName(child, name);
    }
    return count;
}

int countHrefElements(const QDomNode& root)
{
    int count = 0;
    if (root.isElement()) {
        const QDomElement element = root.toElement();
        if (localName(root) == QStringLiteral("a") &&
            element.hasAttribute(QStringLiteral("href"))) {
            count++;
        }
    }
    for (QDomNode child = root.firstChild(); !child.isNull(); child = child.nextSibling()) {
        count += countHrefElements(child);
    }
    return count;
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

QString semanticText(const QDomNode& node)
{
    if (node.isText() || node.isCDATASection()) {
        return isWhitespaceOnly(node.nodeValue()) ? QString() : node.nodeValue();
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

bool isBlockElementName(const QString& name)
{
    static const QSet<QString> block_names = {
        QStringLiteral("address"), QStringLiteral("article"), QStringLiteral("aside"),
        QStringLiteral("blockquote"), QStringLiteral("center"), QStringLiteral("dd"),
        QStringLiteral("div"), QStringLiteral("dl"), QStringLiteral("dt"),
        QStringLiteral("fieldset"), QStringLiteral("figcaption"), QStringLiteral("figure"),
        QStringLiteral("footer"), QStringLiteral("form"), QStringLiteral("h1"),
        QStringLiteral("h2"), QStringLiteral("h3"), QStringLiteral("h4"),
        QStringLiteral("h5"), QStringLiteral("h6"), QStringLiteral("header"),
        QStringLiteral("hr"), QStringLiteral("li"), QStringLiteral("main"),
        QStringLiteral("nav"), QStringLiteral("ol"), QStringLiteral("p"),
        QStringLiteral("pre"), QStringLiteral("section"), QStringLiteral("table"),
        QStringLiteral("ul")
    };
    return block_names.contains(name);
}

bool hasBlockDescendant(const QDomNode& node)
{
    for (QDomNode child = node.firstChild(); !child.isNull(); child = child.nextSibling()) {
        if (!child.isElement()) {
            continue;
        }
        if (isBlockElementName(localName(child)) || hasBlockDescendant(child)) {
            return true;
        }
    }
    return false;
}

QVector<QDomElement> directElementChildren(const QDomElement& element)
{
    QVector<QDomElement> children;
    for (QDomNode child = element.firstChild(); !child.isNull(); child = child.nextSibling()) {
        if (child.isElement()) {
            children << child.toElement();
        }
    }
    return children;
}

bool hasMeaningfulDirectText(const QDomElement& element)
{
    for (QDomNode child = element.firstChild(); !child.isNull(); child = child.nextSibling()) {
        if ((child.isText() || child.isCDATASection()) && !isWhitespaceOnly(child.nodeValue())) {
            return true;
        }
    }
    return false;
}

bool isSceneBreakText(const QString& text)
{
    const QString normalized = compactText(text);
    if (normalized.isEmpty() || normalized.length() > 16) {
        return false;
    }
    static const QRegularExpression pattern(
        QStringLiteral("^[*＊☆★◇◆□■○●・･\\-－—―─━_＿=＝]+$"));
    return pattern.match(normalized).hasMatch();
}

bool containsOnlyBrAndWhitespace(const QDomNode& node)
{
    bool found_br = false;
    for (QDomNode child = node.firstChild(); !child.isNull(); child = child.nextSibling()) {
        if (child.isText() || child.isCDATASection()) {
            if (!isWhitespaceOnly(child.nodeValue())) {
                return false;
            }
            continue;
        }
        if (!child.isElement() || localName(child) != QStringLiteral("br")) {
            return false;
        }
        found_br = true;
    }
    return found_br;
}

bool isAnchorOnly(const QDomElement& element)
{
    return localName(element) == QStringLiteral("a") &&
           !element.hasAttribute(QStringLiteral("href")) &&
           (element.hasAttribute(QStringLiteral("name")) ||
            element.hasAttribute(QStringLiteral("id"))) &&
           isWhitespaceOnly(visibleText(element));
}

bool isHeadingName(const QString& name)
{
    return name.length() == 2 && name.at(0) == QLatin1Char('h') &&
           name.at(1) >= QLatin1Char('1') && name.at(1) <= QLatin1Char('6');
}

LeafKind classifyLeaf(const QDomElement& element)
{
    const QString name = localName(element);
    if (isAnchorOnly(element)) {
        return LeafKind::AnchorOnly;
    }
    if (isHeadingName(name)) {
        return LeafKind::Heading;
    }
    if (name == QStringLiteral("p")) {
        return LeafKind::ExistingP;
    }
    if (name != QStringLiteral("div")) {
        return LeafKind::Other;
    }

    const QString text = visibleText(element);
    const QVector<QDomElement> children = directElementChildren(element);
    QVector<QDomElement> direct_blocks;
    for (const QDomElement& child : children) {
        if (isBlockElementName(localName(child))) {
            direct_blocks << child;
        }
    }

    if (direct_blocks.isEmpty()) {
        if (isSceneBreakText(text)) {
            return LeafKind::SceneBreak;
        }
        if (containsOnlyBrAndWhitespace(element)) {
            return LeafKind::SpacerBr;
        }
        if (isWhitespaceOnly(text) && countImageElements(element) > 0) {
            return LeafKind::ImageOnly;
        }
        if (!isWhitespaceOnly(text) && !hasBlockDescendant(element)) {
            return LeafKind::Paragraph;
        }
        return LeafKind::Other;
    }

    if (direct_blocks.count() == 1 &&
        localName(direct_blocks.first()) == QStringLiteral("div") &&
        !hasBlockDescendant(direct_blocks.first())) {
        return LeafKind::WrappedBlock;
    }
    return LeafKind::NestedComplex;
}

bool isLeafish(LeafKind kind)
{
    switch (kind) {
    case LeafKind::Paragraph:
    case LeafKind::SpacerBr:
    case LeafKind::SceneBreak:
    case LeafKind::ImageOnly:
    case LeafKind::WrappedBlock:
    case LeafKind::AnchorOnly:
    case LeafKind::Heading:
    case LeafKind::ExistingP:
        return true;
    case LeafKind::NestedComplex:
    case LeafKind::Other:
        return false;
    }
    return false;
}

bool isConvertible(LeafKind kind)
{
    switch (kind) {
    case LeafKind::Paragraph:
    case LeafKind::SpacerBr:
    case LeafKind::SceneBreak:
    case LeafKind::ImageOnly:
    case LeafKind::WrappedBlock:
        return true;
    case LeafKind::AnchorOnly:
    case LeafKind::Heading:
    case LeafKind::ExistingP:
    case LeafKind::NestedComplex:
    case LeafKind::Other:
        return false;
    }
    return false;
}

QList<Leaf> classifyChildren(const QDomElement& parent)
{
    QList<Leaf> leaves;
    for (QDomNode child = parent.firstChild(); !child.isNull(); child = child.nextSibling()) {
        if (!child.isElement()) {
            continue;
        }
        const QDomElement element = child.toElement();
        leaves << Leaf{element, classifyLeaf(element)};
    }
    return leaves;
}

void considerParent(const QDomElement& element, int depth, int min_children,
                    ParentMatch& best)
{
    const QVector<QDomElement> children = directElementChildren(element);
    const int child_count = children.count();
    if (child_count >= min_children && child_count <= MAX_CONTENT_PARENT_CHILDREN &&
        !hasMeaningfulDirectText(element)) {
        int leafish = 0;
        int convertible = 0;
        for (const QDomElement& child : children) {
            const LeafKind kind = classifyLeaf(child);
            if (isLeafish(kind)) {
                leafish++;
            }
            if (isConvertible(kind)) {
                convertible++;
            }
        }
        const double score = child_count > 0 ?
            static_cast<double>(leafish) / static_cast<double>(child_count) : 0.0;
        const auto key = std::make_tuple(score, child_count, depth);
        const auto best_key = std::make_tuple(best.score, best.childCount, best.depth);
        if (score >= MIN_LEAFISH_SCORE && convertible > 0 &&
            (best.element.isNull() || key > best_key)) {
            best.element = element;
            best.childCount = child_count;
            best.depth = depth;
            best.score = score;
        }
    }

    for (const QDomElement& child : children) {
        if (localName(child) == QStringLiteral("div")) {
            considerParent(child, depth + 1, min_children, best);
        }
    }
}

ParentMatch findContentParent(const QDomElement& body)
{
    ParentMatch standard;
    for (const QDomElement& child : directElementChildren(body)) {
        if (localName(child) == QStringLiteral("div")) {
            considerParent(child, 1, MIN_STANDARD_PARENT_CHILDREN, standard);
        }
    }
    if (!standard.element.isNull()) {
        return standard;
    }

    ParentMatch short_match;
    for (const QDomElement& child : directElementChildren(body)) {
        if (localName(child) == QStringLiteral("div")) {
            considerParent(child, 1, MIN_SHORT_PARENT_CHILDREN, short_match);
        }
    }
    short_match.usedShortPass = !short_match.element.isNull();
    return short_match;
}

QStringList collectAttributes(const QDomNode& node, const QStringList& names)
{
    QStringList values;
    if (node.isElement()) {
        const QDomElement element = node.toElement();
        for (const QString& name : names) {
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

bool isInjectedStyle(const QDomElement& element)
{
    return localName(element) == QStringLiteral("style") &&
           element.attribute(QStringLiteral("data-sigil-enhancement")) == STYLE_MARKER;
}

void collectLegacyPresentation(const QDomNode& node, QStringList& values)
{
    if (node.isElement()) {
        const QDomElement element = node.toElement();
        if (isInjectedStyle(element)) {
            return;
        }
        const QDomNamedNodeMap attributes = element.attributes();
        for (int i = 0; i < attributes.count(); ++i) {
            const QDomAttr attribute = attributes.item(i).toAttr();
            const QString name = attribute.name().toLower();
            if (name == QStringLiteral("class")) {
                const QStringList classes = attribute.value().split(
                    QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
                for (const QString& class_name : classes) {
                    if (!class_name.startsWith(QStringLiteral("se-bl-"))) {
                        values << QStringLiteral("class=%1").arg(class_name);
                    }
                }
            } else {
                values << QStringLiteral("%1=%2").arg(name, attribute.value());
            }
        }
    }
    for (QDomNode child = node.firstChild(); !child.isNull(); child = child.nextSibling()) {
        collectLegacyPresentation(child, values);
    }
}

QStringList legacyPresentation(const QDomDocument& document)
{
    QStringList values;
    collectLegacyPresentation(document, values);
    values.sort();
    return values;
}

void copyAttributes(const QDomElement& source, QDomElement& target)
{
    const QDomNamedNodeMap attributes = source.attributes();
    for (int i = 0; i < attributes.count(); ++i) {
        const QDomAttr attribute = attributes.item(i).toAttr();
        target.setAttribute(attribute.name(), attribute.value());
    }
}

QDomElement createInnerBlockSpan(QDomDocument& document, const QDomElement& source)
{
    QDomElement span = document.createElement(QStringLiteral("span"));
    copyAttributes(source, span);
    appendClass(span, INNER_BLOCK_CLASS);
    for (QDomNode child = source.firstChild(); !child.isNull(); child = child.nextSibling()) {
        span.appendChild(child.cloneNode(true));
    }
    return span;
}

QDomElement createParagraph(QDomDocument& document, const Leaf& leaf)
{
    QDomElement paragraph = document.createElement(QStringLiteral("p"));
    copyAttributes(leaf.element, paragraph);
    appendClass(paragraph, PARAGRAPH_CLASS);

    for (QDomNode child = leaf.element.firstChild(); !child.isNull(); child = child.nextSibling()) {
        if (leaf.kind == LeafKind::WrappedBlock && child.isElement() &&
            localName(child) == QStringLiteral("div")) {
            paragraph.appendChild(createInnerBlockSpan(document, child.toElement()));
        } else {
            paragraph.appendChild(child.cloneNode(true));
        }
    }
    return paragraph;
}

void addVisualPreservationStyle(QDomDocument& document, QDomElement& body)
{
    appendClass(body, NORMALIZED_CLASS);
    QDomElement head = findElementByLocalName(document, QStringLiteral("head"));
    if (head.isNull()) {
        return;
    }
    const QDomNodeList styles = head.elementsByTagName(QStringLiteral("style"));
    for (int i = 0; i < styles.count(); ++i) {
        const QDomElement style = styles.at(i).toElement();
        if (isInjectedStyle(style) || style.text().contains(PARAGRAPH_CLASS)) {
            return;
        }
    }

    QDomElement style = document.createElement(QStringLiteral("style"));
    style.setAttribute(QStringLiteral("type"), QStringLiteral("text/css"));
    style.setAttribute(QStringLiteral("data-sigil-enhancement"), STYLE_MARKER);
    style.appendChild(document.createTextNode(NORMALIZER_CSS));
    if (head.firstChild().isNull()) {
        head.appendChild(style);
    } else {
        head.insertBefore(style, head.firstChild());
    }
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

bool containsAny(const QString& text, const QStringList& keywords)
{
    for (const QString& keyword : keywords) {
        if (text.contains(keyword, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

bool isTocLike(const QString& body_text, int link_count, int child_count)
{
    const QString compact = compactText(body_text);
    if (compact.contains(QStringLiteral("目次"), Qt::CaseInsensitive) ||
        compact.contains(QStringLiteral("TableofContents"), Qt::CaseInsensitive) ||
        compact.contains(QStringLiteral("Contents"), Qt::CaseInsensitive)) {
        return true;
    }
    return link_count >= 8 && link_count * 2 >= qMax(1, child_count);
}

bool isNoticeOrImprint(const QString& body_text, const BookLiveParagraphNormalizer::Analysis& analysis)
{
    if (analysis.wrappedBlockLeaves >= 1 && analysis.paragraphLeaves <= 3 &&
        analysis.bodyTextLength < 120) {
        return true;
    }
    if (analysis.paragraphLeaves + analysis.wrappedBlockLeaves > 25) {
        return false;
    }
    const QString compact = compactText(body_text);
    const QStringList strong_keywords = {
        QStringLiteral("イラスト"), QStringLiteral("奥付"), QStringLiteral("版権"),
        QStringLiteral("colophon"), QStringLiteral("©"), QStringLiteral("Copyright"),
        QStringLiteral("縦書き表示"), QStringLiteral("横書き表示"),
        QStringLiteral("著作権法"), QStringLiteral("本電子書籍")
    };
    if (containsAny(compact, strong_keywords) &&
        (analysis.usedShortParentPass || analysis.paragraphLeaves <= 6 ||
         analysis.bodyTextLength < 600)) {
        return true;
    }

    int publication_score = 0;
    const QStringList publication_keywords = {
        QStringLiteral("発行日"), QStringLiteral("発行者"), QStringLiteral("発行所"),
        QStringLiteral("発行元"), QStringLiteral("発行"), QStringLiteral("著者"),
        QStringLiteral("挿絵"), QStringLiteral("ISBN"), QStringLiteral("(c)"),
        QStringLiteral("Printedin")
    };
    for (const QString& keyword : publication_keywords) {
        if (compact.contains(keyword, Qt::CaseInsensitive)) {
            publication_score++;
        }
    }
    return publication_score >= 3;
}

void populateLeafCounts(BookLiveParagraphNormalizer::Analysis& analysis,
                        const QList<Leaf>& leaves)
{
    for (const Leaf& leaf : leaves) {
        switch (leaf.kind) {
        case LeafKind::Paragraph:
            analysis.paragraphLeaves++;
            break;
        case LeafKind::SpacerBr:
            analysis.spacerBrLeaves++;
            break;
        case LeafKind::SceneBreak:
            analysis.sceneBreaks++;
            break;
        case LeafKind::ImageOnly:
            analysis.imageLeaves++;
            break;
        case LeafKind::WrappedBlock:
            analysis.wrappedBlockLeaves++;
            break;
        case LeafKind::AnchorOnly:
            analysis.anchorOnly++;
            break;
        case LeafKind::Heading:
            analysis.headingBlocks++;
            break;
        case LeafKind::ExistingP:
            analysis.existingParagraphs++;
            break;
        case LeafKind::NestedComplex:
            analysis.nestedComplexLeaves++;
            break;
        case LeafKind::Other:
            analysis.otherLeaves++;
            break;
        }
        if (isConvertible(leaf.kind)) {
            analysis.convertibleLeaves++;
        }
    }
}

void classifyAnalysis(BookLiveParagraphNormalizer::Analysis& analysis,
                      const QDomElement& body, const ParentMatch& parent)
{
    const QList<Leaf> leaves = classifyChildren(parent.element);
    populateLeafCounts(analysis, leaves);
    analysis.contentParentChildCount = parent.childCount;
    analysis.wrapperDepth = parent.depth;
    analysis.usedShortParentPass = parent.usedShortPass;

    const QString body_text = semanticText(body);
    analysis.bodyTextLength = compactText(body_text).length();

    if (isTocLike(body_text, analysis.linkCount, analysis.contentParentChildCount)) {
        analysis.pageKind = BookLiveParagraphNormalizer::PageKind::TocLike;
        analysis.reason = QStringLiteral("toc-like nested div flow");
    } else if (analysis.nestedComplexLeaves > 0) {
        analysis.pageKind = BookLiveParagraphNormalizer::PageKind::BlockLayout;
        analysis.reason = QStringLiteral("content parent contains nested complex block leaves");
    } else if (analysis.convertibleLeaves == 0) {
        analysis.pageKind = BookLiveParagraphNormalizer::PageKind::NoCandidate;
        analysis.reason = QStringLiteral("content parent has no div pseudo-paragraph leaves");
    } else if (isNoticeOrImprint(body_text, analysis)) {
        analysis.pageKind = BookLiveParagraphNormalizer::PageKind::NoticeOrImprint;
        analysis.candidate = true;
        analysis.reason = QStringLiteral("notice/imprint-like short div flow requires manual review");
    } else if (analysis.paragraphLeaves < MIN_AUTO_PARAGRAPHS || parent.usedShortPass) {
        analysis.pageKind = BookLiveParagraphNormalizer::PageKind::ShortFlow;
        analysis.candidate = true;
        analysis.reason = QStringLiteral("short div paragraph flow requires manual review");
    } else {
        analysis.pageKind = BookLiveParagraphNormalizer::PageKind::NormalBodyFlow;
        analysis.candidate = true;
        analysis.safeToNormalize = true;
        analysis.reason = QStringLiteral("normal nested div paragraph flow");
    }

    if (analysis.safeToNormalize) {
        analysis.message = QStringLiteral("BookLive 段落分析：可自动规范化正文页；将原位转换 %1 个伪段落 div（正文 %2、空行 %3、场景分隔 %4、插图 %5、单层样式块 %6），保留布局 wrapper 与全部原 class/style。")
            .arg(analysis.convertibleLeaves)
            .arg(analysis.paragraphLeaves)
            .arg(analysis.spacerBrLeaves)
            .arg(analysis.sceneBreaks)
            .arg(analysis.imageLeaves)
            .arg(analysis.wrappedBlockLeaves);
    } else if (analysis.candidate) {
        analysis.message = QStringLiteral("BookLive 段落分析：需人工确认（%1）；候选 div=%2，正文=%3，空行=%4，单层样式块=%5，正文字符=%6。")
            .arg(analysis.reason)
            .arg(analysis.convertibleLeaves)
            .arg(analysis.paragraphLeaves)
            .arg(analysis.spacerBrLeaves)
            .arg(analysis.wrappedBlockLeaves)
            .arg(analysis.bodyTextLength);
    } else {
        analysis.message = QStringLiteral("BookLive 段落分析：已跳过（%1）；content-parent 子节点=%2，复杂块=%3，链接=%4，图片=%5。")
            .arg(analysis.reason)
            .arg(analysis.contentParentChildCount)
            .arg(analysis.nestedComplexLeaves)
            .arg(analysis.linkCount)
            .arg(analysis.imageCount);
    }
}

}

QString BookLiveParagraphNormalizer::pageKindName(PageKind pageKind)
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

BookLiveParagraphNormalizer::Analysis
BookLiveParagraphNormalizer::analyzeXhtmlText(const QString& source)
{
    Analysis analysis;
    QDomDocument document;
    QString error;
    if (!parseDocument(source, document, error)) {
        analysis.pageKind = PageKind::ParseError;
        analysis.reason = error;
        analysis.message = QStringLiteral("BookLive 段落分析：XML 解析失败，已跳过。%1").arg(error);
        return analysis;
    }

    analysis.ok = true;
    analysis.linkCount = countHrefElements(document);
    analysis.imageCount = countImageElements(document);
    const QDomElement body = findElementByLocalName(document, QStringLiteral("body"));
    if (body.isNull()) {
        analysis.pageKind = PageKind::NoBody;
        analysis.reason = QStringLiteral("missing body element");
        analysis.message = QStringLiteral("BookLive 段落分析：已跳过（missing body element）。");
        return analysis;
    }

    if (elementHasClass(body, NORMALIZED_CLASS)) {
        analysis.pageKind = PageKind::AlreadyNormalized;
        analysis.reason = QStringLiteral("already normalized by BookLive paragraph normalizer");
        analysis.existingParagraphs = countElementsByLocalName(body, QStringLiteral("p"));
        analysis.message = QStringLiteral("BookLive 段落分析：页面已规范化，现有 %1 个 p 段落。")
                               .arg(analysis.existingParagraphs);
        return analysis;
    }

    if (elementHasClass(body, QStringLiteral("p-image"))) {
        analysis.pageKind = PageKind::ImageOrTitlePage;
        analysis.reason = QStringLiteral("body.p-image image/title page");
        analysis.message = QStringLiteral("BookLive 段落分析：已跳过 body.p-image 图片/扉页。");
        return analysis;
    }

    if (findElementByLocalName(document, QStringLiteral("head")).isNull()) {
        analysis.pageKind = PageKind::NoCandidate;
        analysis.reason = QStringLiteral("missing head element needed for paragraph reset style");
        analysis.message = QStringLiteral("BookLive 段落分析：已跳过（缺少 head，无法安全注入 p 默认样式补偿）。");
        return analysis;
    }

    ParentMatch parent = findContentParent(body);
    if (parent.element.isNull()) {
        analysis.pageKind = PageKind::NoCandidate;
        analysis.reason = QStringLiteral("no div pseudo-paragraph content parent found");
        analysis.message = QStringLiteral("BookLive 段落分析：已跳过（未发现稳定的 div 伪段落正文容器）。");
        return analysis;
    }

    classifyAnalysis(analysis, body, parent);
    return analysis;
}

BookLiveParagraphNormalizer::NormalizeResult
BookLiveParagraphNormalizer::normalizeXhtmlText(const QString& source, bool allowManualReview)
{
    NormalizeResult result;
    result.before = analyzeXhtmlText(source);
    if (!result.before.ok) {
        result.messages << result.before.message;
        return result;
    }
    if (result.before.pageKind == PageKind::AlreadyNormalized) {
        result.ok = true;
        result.text = source;
        result.after = result.before;
        result.messages << QStringLiteral("BookLive 段落规范化：页面已规范化，无需修改。");
        return result;
    }
    if (!result.before.candidate) {
        result.messages << result.before.message;
        return result;
    }
    if (!result.before.safeToNormalize && !allowManualReview) {
        result.messages << QStringLiteral("BookLive 段落规范化：已跳过，%1。")
                               .arg(result.before.reason);
        return result;
    }

    QDomDocument document;
    QString error;
    if (!parseDocument(source, document, error)) {
        result.messages << QStringLiteral("BookLive 段落规范化：XML 解析失败，已跳过。%1").arg(error);
        return result;
    }

    QDomElement body = findElementByLocalName(document, QStringLiteral("body"));
    ParentMatch parent = findContentParent(body);
    if (parent.element.isNull()) {
        result.messages << QStringLiteral("BookLive 段落规范化：正文容器在写回前复核时消失，已回退。");
        return result;
    }
    const QList<Leaf> leaves = classifyChildren(parent.element);
    for (const Leaf& leaf : leaves) {
        if (leaf.kind == LeafKind::NestedComplex) {
            result.messages << QStringLiteral("BookLive 段落规范化：发现复杂嵌套块，已回退。");
            return result;
        }
    }

    const QString before_text = semanticText(document);
    const QStringList before_ids = sortedAttributes(
        document, QStringList() << QStringLiteral("id") << QStringLiteral("name"));
    const QStringList before_links = sortedAttributes(
        document, QStringList() << QStringLiteral("href") << QStringLiteral("src"));
    const QStringList before_presentation = legacyPresentation(document);
    const int before_ruby = countElementsByLocalName(document, QStringLiteral("ruby"));
    const int before_rt = countElementsByLocalName(document, QStringLiteral("rt"));
    const int before_rp = countElementsByLocalName(document, QStringLiteral("rp"));
    const int before_images = countImageElements(document);

    int converted = 0;
    for (const Leaf& leaf : leaves) {
        if (!isConvertible(leaf.kind)) {
            continue;
        }
        const QDomElement paragraph = createParagraph(document, leaf);
        parent.element.replaceChild(paragraph, leaf.element);
        converted++;
    }
    if (converted == 0) {
        result.messages << QStringLiteral("BookLive 段落规范化：没有可转换的伪段落，已跳过。");
        return result;
    }

    addVisualPreservationStyle(document, body);
    removeRedundantXhtmlNamespaceAttributes(document.documentElement(), true);
    result.text = document.toString(2);

    QDomDocument after_document;
    QString after_error;
    if (!parseDocument(result.text, after_document, after_error)) {
        result.messages << QStringLiteral("BookLive 段落规范化：转换后 XML 解析失败，已回退。%1")
                               .arg(after_error);
        return result;
    }

    const QString after_text = semanticText(after_document);
    const QStringList after_ids = sortedAttributes(
        after_document, QStringList() << QStringLiteral("id") << QStringLiteral("name"));
    const QStringList after_links = sortedAttributes(
        after_document, QStringList() << QStringLiteral("href") << QStringLiteral("src"));
    const QStringList after_presentation = legacyPresentation(after_document);
    if (before_text != after_text) {
        result.messages << QStringLiteral("BookLive 段落规范化：转换后可见文本不一致，已回退。");
        return result;
    }
    if (before_ids != after_ids) {
        result.messages << QStringLiteral("BookLive 段落规范化：转换后 id/name 集合不一致，已回退。");
        return result;
    }
    if (before_links != after_links) {
        result.messages << QStringLiteral("BookLive 段落规范化：转换后 href/src 集合不一致，已回退。");
        return result;
    }
    if (before_presentation != after_presentation) {
        result.messages << QStringLiteral("BookLive 段落规范化：转换后原 class/style/属性集合不一致，已回退。");
        return result;
    }
    if (before_ruby != countElementsByLocalName(after_document, QStringLiteral("ruby")) ||
        before_rt != countElementsByLocalName(after_document, QStringLiteral("rt")) ||
        before_rp != countElementsByLocalName(after_document, QStringLiteral("rp")) ||
        before_images != countImageElements(after_document)) {
        result.messages << QStringLiteral("BookLive 段落规范化：转换后 ruby/图片结构计数不一致，已回退。");
        return result;
    }

    result.after = analyzeXhtmlText(result.text);
    if (result.after.pageKind != PageKind::AlreadyNormalized) {
        result.messages << QStringLiteral("BookLive 段落规范化：幂等标记复核失败，已回退。");
        return result;
    }
    result.ok = true;
    result.changed = result.text != source;
    result.messages << QStringLiteral("BookLive 段落规范化：已原位转换 %1 个伪段落 div 为 p；布局 wrapper、空行、原 class/style、ruby、锚点、链接和图片均已保留。")
                           .arg(converted);
    return result;
}

}
