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

#include <QCryptographicHash>
#include <QDomDocument>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QVector>
#include <QtGlobal>

#include <algorithm>
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
const QString RULE_VERSION = QStringLiteral("div-paragraph-normalizer-v2");
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
    ProtectedHeading,
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

struct ElementTagRange {
    int parentIndex = -1;
    int elementStart = -1;
    int elementEnd = -1;
    int openNameStart = -1;
    int openNameLength = 0;
    int closeNameStart = -1;
    int closeNameLength = 0;
};

struct OpenElement {
    int index = -1;
    QString qualifiedName;
    ElementTagRange range;
};

QString sha256(const QString& text)
{
    return QString::fromLatin1(QCryptographicHash::hash(
        text.toUtf8(), QCryptographicHash::Sha256).toHex());
}

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

bool hasOnlyAllowedPhrasingContent(const QDomNode& node)
{
    static const QSet<QString> allowed_names = {
        QStringLiteral("a"), QStringLiteral("abbr"), QStringLiteral("b"),
        QStringLiteral("bdi"), QStringLiteral("bdo"), QStringLiteral("br"),
        QStringLiteral("cite"), QStringLiteral("code"), QStringLiteral("data"),
        QStringLiteral("del"), QStringLiteral("dfn"), QStringLiteral("em"),
        QStringLiteral("i"), QStringLiteral("img"), QStringLiteral("ins"),
        QStringLiteral("kbd"), QStringLiteral("mark"), QStringLiteral("q"),
        QStringLiteral("rb"), QStringLiteral("rp"), QStringLiteral("rt"),
        QStringLiteral("rtc"), QStringLiteral("ruby"), QStringLiteral("s"),
        QStringLiteral("samp"), QStringLiteral("small"), QStringLiteral("span"),
        QStringLiteral("strong"), QStringLiteral("sub"), QStringLiteral("sup"),
        QStringLiteral("time"), QStringLiteral("u"), QStringLiteral("var"),
        QStringLiteral("wbr")
    };

    for (QDomNode child = node.firstChild(); !child.isNull(); child = child.nextSibling()) {
        if (child.isText() || child.isCDATASection() || child.isComment()) {
            continue;
        }
        if (!child.isElement()) {
            return false;
        }
        if (!allowed_names.contains(localName(child)) ||
            !hasOnlyAllowedPhrasingContent(child)) {
            return false;
        }
    }
    return true;
}

bool isProtectedHeadingWrapper(const QDomElement& element)
{
    if (localName(element) != QStringLiteral("div")) {
        return false;
    }

    int heading_count = 0;
    for (QDomNode child = element.firstChild(); !child.isNull(); child = child.nextSibling()) {
        if (child.isText() || child.isCDATASection()) {
            if (!isWhitespaceOnly(child.nodeValue())) {
                return false;
            }
            continue;
        }
        if (child.isComment()) {
            continue;
        }
        if (!child.isElement()) {
            return false;
        }
        const QDomElement child_element = child.toElement();
        if (isAnchorOnly(child_element)) {
            continue;
        }
        if (isHeadingName(localName(child_element)) &&
            hasOnlyAllowedPhrasingContent(child_element)) {
            heading_count++;
            continue;
        }
        return false;
    }
    return heading_count == 1;
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

    if (isProtectedHeadingWrapper(element)) {
        return LeafKind::ProtectedHeading;
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
        if (!isWhitespaceOnly(text) && hasOnlyAllowedPhrasingContent(element)) {
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
    case LeafKind::ProtectedHeading:
    case LeafKind::ExistingP:
        return true;
    case LeafKind::NestedComplex:
    case LeafKind::Other:
        return false;
    }
    return false;
}

bool isConvertible(LeafKind kind,
                   const BookLiveParagraphNormalizer::Options& options)
{
    switch (kind) {
    case LeafKind::Paragraph:
        return options.convertParagraphs;
    case LeafKind::SpacerBr:
        return options.convertSpacerBr;
    case LeafKind::SceneBreak:
        return options.convertSceneBreaks;
    case LeafKind::ImageOnly:
        return options.convertImageWrappers;
    case LeafKind::WrappedBlock:
        return options.convertSingleBlockWrappers;
    case LeafKind::AnchorOnly:
    case LeafKind::Heading:
    case LeafKind::ProtectedHeading:
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
                    const BookLiveParagraphNormalizer::Options& options,
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
            if (isConvertible(kind, options)) {
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
            considerParent(child, depth + 1, min_children, options, best);
        }
    }
}

ParentMatch findContentParent(const QDomElement& body,
                              const BookLiveParagraphNormalizer::Options& options)
{
    ParentMatch standard;
    for (const QDomElement& child : directElementChildren(body)) {
        if (localName(child) == QStringLiteral("div")) {
            considerParent(child, 1, MIN_STANDARD_PARENT_CHILDREN, options, standard);
        }
    }
    if (!standard.element.isNull()) {
        return standard;
    }

    ParentMatch short_match;
    for (const QDomElement& child : directElementChildren(body)) {
        if (localName(child) == QStringLiteral("div")) {
            considerParent(child, 1, MIN_SHORT_PARENT_CHILDREN, options, short_match);
        }
    }
    short_match.usedShortPass = !short_match.element.isNull();
    return short_match;
}

BookLiveParagraphNormalizer::CandidateKind candidateKind(LeafKind kind)
{
    switch (kind) {
    case LeafKind::SpacerBr:
        return BookLiveParagraphNormalizer::CandidateKind::SpacerBr;
    case LeafKind::SceneBreak:
        return BookLiveParagraphNormalizer::CandidateKind::SceneBreak;
    case LeafKind::ImageOnly:
        return BookLiveParagraphNormalizer::CandidateKind::ImageWrapper;
    case LeafKind::WrappedBlock:
        return BookLiveParagraphNormalizer::CandidateKind::SingleBlockWrapper;
    case LeafKind::Paragraph:
    case LeafKind::AnchorOnly:
    case LeafKind::Heading:
    case LeafKind::ProtectedHeading:
    case LeafKind::ExistingP:
    case LeafKind::NestedComplex:
    case LeafKind::Other:
        return BookLiveParagraphNormalizer::CandidateKind::Paragraph;
    }
    return BookLiveParagraphNormalizer::CandidateKind::Paragraph;
}

void collectClassifiedIndexes(
    const QDomNode& node,
    const QDomElement& content_parent,
    const BookLiveParagraphNormalizer::Options& options,
    int& index,
    QHash<int, LeafKind>& candidate_indexes,
    QSet<int>& protected_indexes)
{
    if (node.isElement()) {
        const int current_index = index++;
        if (!content_parent.isNull() && node.parentNode() == content_parent) {
            const LeafKind kind = classifyLeaf(node.toElement());
            if (isConvertible(kind, options)) {
                candidate_indexes.insert(current_index, kind);
            } else if (kind != LeafKind::AnchorOnly && kind != LeafKind::ExistingP) {
                protected_indexes.insert(current_index);
            }
        }
    }
    for (QDomNode child = node.firstChild(); !child.isNull(); child = child.nextSibling()) {
        collectClassifiedIndexes(child, content_parent, options, index,
                                 candidate_indexes, protected_indexes);
    }
}

int tagEnd(const QString& source, int start, bool declaration)
{
    QChar quote;
    int subset_depth = 0;
    for (int i = start; i < source.length(); ++i) {
        const QChar ch = source.at(i);
        if (!quote.isNull()) {
            if (ch == quote) {
                quote = QChar();
            }
            continue;
        }
        if (ch == QLatin1Char('\'') || ch == QLatin1Char('"')) {
            quote = ch;
        } else if (declaration && ch == QLatin1Char('[')) {
            subset_depth++;
        } else if (declaration && ch == QLatin1Char(']') && subset_depth > 0) {
            subset_depth--;
        } else if (ch == QLatin1Char('>') && subset_depth == 0) {
            return i;
        }
    }
    return -1;
}

QPair<int, int> localNameRange(const QString& qualified_name, int name_start)
{
    const int colon = qualified_name.lastIndexOf(QLatin1Char(':'));
    const int offset = colon < 0 ? 0 : colon + 1;
    return qMakePair(name_start + offset, qualified_name.length() - offset);
}

QHash<int, ElementTagRange> scanElementRanges(const QString& source)
{
    QHash<int, ElementTagRange> ranges;
    QVector<OpenElement> stack;
    int element_index = 0;
    int position = 0;
    while ((position = source.indexOf(QLatin1Char('<'), position)) >= 0) {
        if (source.mid(position, 4) == QStringLiteral("<!--")) {
            const int end = source.indexOf(QStringLiteral("-->"), position + 4);
            position = end < 0 ? source.length() : end + 3;
            continue;
        }
        if (source.mid(position, 9) == QStringLiteral("<![CDATA[")) {
            const int end = source.indexOf(QStringLiteral("]]>") , position + 9);
            position = end < 0 ? source.length() : end + 3;
            continue;
        }
        if (source.mid(position, 2) == QStringLiteral("<?")) {
            const int end = source.indexOf(QStringLiteral("?>"), position + 2);
            position = end < 0 ? source.length() : end + 2;
            continue;
        }
        if (source.mid(position, 2) == QStringLiteral("<!")) {
            const int end = tagEnd(source, position + 2, true);
            position = end < 0 ? source.length() : end + 1;
            continue;
        }

        const bool closing = position + 1 < source.length() &&
                             source.at(position + 1) == QLatin1Char('/');
        int name_start = position + (closing ? 2 : 1);
        while (name_start < source.length() && source.at(name_start).isSpace()) {
            name_start++;
        }
        int name_end = name_start;
        while (name_end < source.length() && !source.at(name_end).isSpace() &&
               source.at(name_end) != QLatin1Char('/') &&
               source.at(name_end) != QLatin1Char('>')) {
            name_end++;
        }
        if (name_end == name_start) {
            position++;
            continue;
        }
        const QString qualified_name = source.mid(name_start, name_end - name_start);
        const int end = tagEnd(source, name_end, false);
        if (end < 0) {
            break;
        }
        const QPair<int, int> local_range = localNameRange(qualified_name, name_start);

        if (closing) {
            if (!stack.isEmpty()) {
                OpenElement open = stack.takeLast();
                open.range.closeNameStart = local_range.first;
                open.range.closeNameLength = local_range.second;
                open.range.elementEnd = end + 1;
                ranges.insert(open.index, open.range);
            }
        } else {
            OpenElement open;
            open.index = element_index++;
            open.qualifiedName = qualified_name;
            open.range.parentIndex = stack.isEmpty() ? -1 : stack.constLast().index;
            open.range.elementStart = position;
            open.range.openNameStart = local_range.first;
            open.range.openNameLength = local_range.second;
            int before_end = end - 1;
            while (before_end > name_end && source.at(before_end).isSpace()) {
                before_end--;
            }
            if (source.at(before_end) == QLatin1Char('/')) {
                open.range.elementEnd = end + 1;
                ranges.insert(open.index, open.range);
            } else {
                stack << open;
            }
        }
        position = end + 1;
    }
    return ranges;
}

void populateSourceRanges(BookLiveParagraphNormalizer::Analysis& analysis,
                          const QString& source,
                          const QDomDocument& document,
                          const QDomElement& content_parent,
                          const BookLiveParagraphNormalizer::Options& options)
{
    QHash<int, LeafKind> candidate_indexes;
    QSet<int> protected_indexes;
    int index = 0;
    collectClassifiedIndexes(document, content_parent, options, index,
                             candidate_indexes, protected_indexes);
    const QHash<int, ElementTagRange> ranges = scanElementRanges(source);

    QList<int> ordered_candidates = candidate_indexes.keys();
    std::sort(ordered_candidates.begin(), ordered_candidates.end());
    for (int candidate_index : ordered_candidates) {
        const ElementTagRange range = ranges.value(candidate_index);
        if (range.elementStart < 0 || range.elementEnd < 0) {
            analysis.warnings << QStringLiteral("source range unavailable for candidate element");
            continue;
        }
        analysis.candidateRanges << BookLiveParagraphNormalizer::SourceRange {
            range.elementStart, range.elementEnd,
            candidateKind(candidate_indexes.value(candidate_index))
        };
    }

    QList<int> ordered_protected = protected_indexes.values();
    std::sort(ordered_protected.begin(), ordered_protected.end());
    for (int protected_index : ordered_protected) {
        const ElementTagRange range = ranges.value(protected_index);
        if (range.elementStart >= 0 && range.elementEnd >= 0) {
            analysis.protectedRanges << BookLiveParagraphNormalizer::SourceRange {
                range.elementStart, range.elementEnd,
                BookLiveParagraphNormalizer::CandidateKind::Paragraph
            };
        }
    }
}

struct TextPatch {
    int start = -1;
    int length = 0;
    QString replacement;
};

bool sourcePreservingTransform(const QString& source,
                               const QDomDocument& document,
                               const QDomElement& content_parent,
                               const BookLiveParagraphNormalizer::Options& options,
                               QString& output,
                               int& converted,
                               QString& error)
{
    QHash<int, LeafKind> candidate_indexes;
    QSet<int> protected_indexes;
    int index = 0;
    collectClassifiedIndexes(document, content_parent, options, index,
                             candidate_indexes, protected_indexes);
    const QHash<int, ElementTagRange> ranges = scanElementRanges(source);
    QVector<TextPatch> patches;

    for (auto it = candidate_indexes.constBegin(); it != candidate_indexes.constEnd(); ++it) {
        if (!ranges.contains(it.key())) {
            error = QStringLiteral("candidate source range is unavailable");
            return false;
        }
        const ElementTagRange range = ranges.value(it.key());
        if (range.openNameStart < 0 || range.closeNameStart < 0 ||
            source.mid(range.openNameStart, range.openNameLength)
                    .compare(QStringLiteral("div"), Qt::CaseInsensitive) != 0 ||
            source.mid(range.closeNameStart, range.closeNameLength)
                    .compare(QStringLiteral("div"), Qt::CaseInsensitive) != 0) {
            error = QStringLiteral("candidate tag-name range is inconsistent");
            return false;
        }
        patches << TextPatch { range.openNameStart, range.openNameLength,
                               QStringLiteral("p") };
        patches << TextPatch { range.closeNameStart, range.closeNameLength,
                               QStringLiteral("p") };
        if (it.value() == LeafKind::WrappedBlock) {
            bool patched_child = false;
            for (auto child = ranges.constBegin(); child != ranges.constEnd(); ++child) {
                const ElementTagRange child_range = child.value();
                if (child_range.parentIndex != it.key() ||
                    source.mid(child_range.openNameStart, child_range.openNameLength)
                            .compare(QStringLiteral("div"), Qt::CaseInsensitive) != 0 ||
                    child_range.closeNameStart < 0) {
                    continue;
                }
                patches << TextPatch { child_range.openNameStart,
                                       child_range.openNameLength,
                                       QStringLiteral("span") };
                patches << TextPatch { child_range.closeNameStart,
                                       child_range.closeNameLength,
                                       QStringLiteral("span") };
                patched_child = true;
                break;
            }
            if (!patched_child) {
                error = QStringLiteral("single block wrapper child range is unavailable");
                return false;
            }
        }
        converted++;
    }

    std::sort(patches.begin(), patches.end(), [](const TextPatch& left, const TextPatch& right) {
        return left.start > right.start;
    });
    output = source;
    for (const TextPatch& patch : patches) {
        output.replace(patch.start, patch.length, patch.replacement);
    }
    return true;
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

void collectInlineStyles(const QDomNode& node,
                         QVector<DivParagraphCssAnalyzer::Source>& styles,
                         int& style_index)
{
    if (node.isElement()) {
        const QDomElement element = node.toElement();
        if (localName(element) == QStringLiteral("style") && !isInjectedStyle(element)) {
            styles << DivParagraphCssAnalyzer::Source {
                QStringLiteral("inline-style-%1").arg(++style_index), element.text()
            };
        }
    }
    for (QDomNode child = node.firstChild(); !child.isNull(); child = child.nextSibling()) {
        collectInlineStyles(child, styles, style_index);
    }
}

void applyCssRisk(BookLiveParagraphNormalizer::Analysis& analysis,
                  const QDomDocument& document,
                  const QVector<DivParagraphCssAnalyzer::Source>& external_stylesheets)
{
    QVector<DivParagraphCssAnalyzer::Source> stylesheets = external_stylesheets;
    int style_index = 0;
    collectInlineStyles(document, stylesheets, style_index);
    const DivParagraphCssAnalyzer::Result css_result =
        DivParagraphCssAnalyzer::analyze(stylesheets);
    analysis.cssReviewRequired = css_result.reviewRequired;
    analysis.cssDependencies = css_result.dependencies;
    if (!analysis.candidate || !analysis.cssReviewRequired) {
        return;
    }

    analysis.safeToNormalize = false;
    analysis.pageKind = BookLiveParagraphNormalizer::PageKind::CssRisk;
    analysis.reason = QStringLiteral("tag-dependent CSS selectors require review");
    analysis.message = QStringLiteral("DIV 段落分析：需检查 %1 条标签相关 CSS 选择器；候选正文=%2，受保护块=%3。")
        .arg(analysis.cssDependencies.count())
        .arg(analysis.convertibleLeaves)
        .arg(analysis.protectedRanges.count());
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
        case LeafKind::ProtectedHeading:
            analysis.protectedHeadingBlocks++;
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
    }
}

void classifyAnalysis(BookLiveParagraphNormalizer::Analysis& analysis,
                      const QDomElement& body, const ParentMatch& parent,
                      const BookLiveParagraphNormalizer::Options& options)
{
    const QList<Leaf> leaves = classifyChildren(parent.element);
    populateLeafCounts(analysis, leaves);
    for (const Leaf& leaf : leaves) {
        if (isConvertible(leaf.kind, options)) {
            analysis.convertibleLeaves++;
        }
    }
    analysis.contentParentChildCount = parent.childCount;
    analysis.wrapperDepth = parent.depth;
    analysis.usedShortParentPass = parent.usedShortPass;

    const QString body_text = semanticText(body);
    analysis.bodyTextLength = compactText(body_text).length();

    if (isTocLike(body_text, analysis.linkCount, analysis.contentParentChildCount)) {
        analysis.pageKind = BookLiveParagraphNormalizer::PageKind::TocLike;
        analysis.reason = QStringLiteral("toc-like nested div flow");
    } else if (analysis.nestedComplexLeaves > 0 || analysis.otherLeaves > 0) {
        analysis.pageKind = BookLiveParagraphNormalizer::PageKind::BlockLayout;
        analysis.reason = QStringLiteral("content parent contains unknown or nested complex block leaves");
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

BookLiveParagraphNormalizer::Options
BookLiveParagraphNormalizer::Options::conservative()
{
    return Options();
}

BookLiveParagraphNormalizer::Options
BookLiveParagraphNormalizer::Options::bookLiveCompatibility()
{
    Options options;
    options.convertSpacerBr = true;
    options.convertSceneBreaks = true;
    options.convertImageWrappers = true;
    options.convertSingleBlockWrappers = true;
    options.addLegacyStyleCompensation = true;
    return options;
}

QString BookLiveParagraphNormalizer::Options::presetId() const
{
    const Options conservative_options = conservative();
    if (convertParagraphs == conservative_options.convertParagraphs &&
        convertSpacerBr == conservative_options.convertSpacerBr &&
        convertSceneBreaks == conservative_options.convertSceneBreaks &&
        convertImageWrappers == conservative_options.convertImageWrappers &&
        convertSingleBlockWrappers == conservative_options.convertSingleBlockWrappers &&
        addLegacyStyleCompensation == conservative_options.addLegacyStyleCompensation) {
        return QStringLiteral("conservative-v2");
    }

    const Options compatibility_options = bookLiveCompatibility();
    if (convertParagraphs == compatibility_options.convertParagraphs &&
        convertSpacerBr == compatibility_options.convertSpacerBr &&
        convertSceneBreaks == compatibility_options.convertSceneBreaks &&
        convertImageWrappers == compatibility_options.convertImageWrappers &&
        convertSingleBlockWrappers == compatibility_options.convertSingleBlockWrappers &&
        addLegacyStyleCompensation == compatibility_options.addLegacyStyleCompensation) {
        return QStringLiteral("booklive-compat-v1");
    }

    return QStringLiteral("custom-v2:%1%2%3%4%5%6")
        .arg(convertParagraphs ? 1 : 0)
        .arg(convertSpacerBr ? 1 : 0)
        .arg(convertSceneBreaks ? 1 : 0)
        .arg(convertImageWrappers ? 1 : 0)
        .arg(convertSingleBlockWrappers ? 1 : 0)
        .arg(addLegacyStyleCompensation ? 1 : 0);
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
    case PageKind::CssRisk:
        return QStringLiteral("css-risk");
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
    return analyzeXhtmlText(source, Options::bookLiveCompatibility());
}

BookLiveParagraphNormalizer::Analysis
BookLiveParagraphNormalizer::analyzeXhtmlText(const QString& source,
                                              const Options& options)
{
    return analyzeXhtmlText(source, options,
                            QVector<DivParagraphCssAnalyzer::Source>());
}

BookLiveParagraphNormalizer::Analysis
BookLiveParagraphNormalizer::analyzeXhtmlText(
    const QString& source,
    const Options& options,
    const QVector<DivParagraphCssAnalyzer::Source>& stylesheets)
{
    Analysis analysis;
    analysis.presetId = options.presetId();
    analysis.ruleVersion = RULE_VERSION;
    analysis.beforeHash = sha256(source);
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

    if (elementHasClass(body, QStringLiteral("p-image"))) {
        analysis.pageKind = PageKind::ImageOrTitlePage;
        analysis.reason = QStringLiteral("body.p-image image/title page");
        analysis.message = QStringLiteral("BookLive 段落分析：已跳过 body.p-image 图片/扉页。");
        return analysis;
    }

    if (options.addLegacyStyleCompensation &&
        findElementByLocalName(document, QStringLiteral("head")).isNull()) {
        analysis.pageKind = PageKind::NoCandidate;
        analysis.reason = QStringLiteral("missing head element needed for paragraph reset style");
        analysis.message = QStringLiteral("BookLive 段落分析：已跳过（缺少 head，无法安全注入 p 默认样式补偿）。");
        return analysis;
    }

    ParentMatch parent = findContentParent(body, options);
    if (parent.element.isNull()) {
        if (elementHasClass(body, NORMALIZED_CLASS)) {
            analysis.pageKind = PageKind::AlreadyNormalized;
            analysis.reason = QStringLiteral("no remaining candidates after BookLive normalization");
            analysis.existingParagraphs = countElementsByLocalName(body, QStringLiteral("p"));
            analysis.message = QStringLiteral("BookLive 段落分析：页面已规范化，现有 %1 个 p 段落。")
                                   .arg(analysis.existingParagraphs);
            return analysis;
        }
        analysis.pageKind = PageKind::NoCandidate;
        analysis.reason = QStringLiteral("no div pseudo-paragraph content parent found");
        analysis.message = QStringLiteral("BookLive 段落分析：已跳过（未发现稳定的 div 伪段落正文容器）。");
        return analysis;
    }

    classifyAnalysis(analysis, body, parent, options);
    populateSourceRanges(analysis, source, document, parent.element, options);
    if (analysis.candidateRanges.count() != analysis.convertibleLeaves) {
        analysis.safeToNormalize = false;
        analysis.warnings << QStringLiteral("candidate source ranges are incomplete");
        analysis.reason = QStringLiteral("candidate source ranges are incomplete");
    }
    if (elementHasClass(body, NORMALIZED_CLASS) && analysis.convertibleLeaves > 0) {
        analysis.warnings << QStringLiteral("new candidates found after an earlier normalization");
    }
    if (!options.addLegacyStyleCompensation) {
        applyCssRisk(analysis, document, stylesheets);
    }
    return analysis;
}

BookLiveParagraphNormalizer::NormalizeResult
BookLiveParagraphNormalizer::normalizeXhtmlText(const QString& source, bool allowManualReview)
{
    return normalizeXhtmlText(source, Options::bookLiveCompatibility(), allowManualReview);
}

BookLiveParagraphNormalizer::NormalizeResult
BookLiveParagraphNormalizer::normalizeXhtmlText(const QString& source,
                                                const Options& options,
                                                bool allowManualReview)
{
    return normalizeXhtmlText(source, options,
                              QVector<DivParagraphCssAnalyzer::Source>(),
                              allowManualReview);
}

BookLiveParagraphNormalizer::NormalizeResult
BookLiveParagraphNormalizer::normalizeXhtmlText(
    const QString& source,
    const Options& options,
    const QVector<DivParagraphCssAnalyzer::Source>& stylesheets,
    bool allowManualReview)
{
    NormalizeResult result;
    result.before = analyzeXhtmlText(source, options, stylesheets);
    if (!result.before.ok) {
        result.messages << result.before.message;
        return result;
    }
    if (result.before.pageKind == PageKind::AlreadyNormalized) {
        result.ok = true;
        result.text = source;
        result.afterHash = result.before.beforeHash;
        result.after = result.before;
        result.messages << QStringLiteral("BookLive 段落规范化：页面已规范化，无需修改。");
        return result;
    }
    if (!result.before.candidate) {
        if (result.before.pageKind == PageKind::NoCandidate) {
            result.ok = true;
            result.text = source;
            result.after = result.before;
            result.afterHash = result.before.beforeHash;
            result.messages << QStringLiteral("DIV 段落规范化：当前预设没有可转换项，无需修改。");
            return result;
        }
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
    ParentMatch parent = findContentParent(body, options);
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
    if (options.addLegacyStyleCompensation) {
        for (const Leaf& leaf : leaves) {
            if (!isConvertible(leaf.kind, options)) {
                continue;
            }
            const QDomElement paragraph = createParagraph(document, leaf);
            parent.element.replaceChild(paragraph, leaf.element);
            converted++;
        }
        addVisualPreservationStyle(document, body);
        removeRedundantXhtmlNamespaceAttributes(document.documentElement(), true);
        result.text = document.toString(2);
    } else if (!sourcePreservingTransform(source, document, parent.element, options,
                                          result.text, converted, error)) {
        result.messages << QStringLiteral("BookLive 段落规范化：源码范围补丁失败，已回退。%1")
                               .arg(error);
        return result;
    }
    if (converted == 0) {
        result.messages << QStringLiteral("BookLive 段落规范化：没有可转换的伪段落，已跳过。");
        return result;
    }

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

    result.after = analyzeXhtmlText(result.text, options, stylesheets);
    if (result.after.candidate || result.after.convertibleLeaves != 0) {
        result.messages << QStringLiteral("BookLive 段落规范化：幂等复核仍发现同一预设的候选项，已回退。");
        return result;
    }
    result.ok = true;
    result.changed = result.text != source;
    result.afterHash = sha256(result.text);
    result.messages << (options.addLegacyStyleCompensation ?
        QStringLiteral("BookLive 段落规范化：已按兼容预设转换 %1 个伪段落 div 为 p；布局 wrapper、原 class/style、ruby、锚点、链接和图片均已保留。") :
        QStringLiteral("DIV 段落规范化：已用源码范围补丁转换 %1 个正文 div 为 p；未选类型和其余源码字节保持不变。"))
                           .arg(converted);
    return result;
}

}
