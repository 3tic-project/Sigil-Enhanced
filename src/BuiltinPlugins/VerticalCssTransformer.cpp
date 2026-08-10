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

#include "BuiltinPlugins/VerticalCssTransformer.h"

#include <functional>

#include <QDomDocument>
#include <QDomElement>
#include <QRegularExpression>
#include <QSet>

#include "Parsers/CSSParser.h"

namespace BuiltinPlugins
{

namespace
{

QString localName(const QDomNode& node)
{
    if (!node.isElement()) {
        return QString();
    }
    const QDomElement element = node.toElement();
    const QString local_name = element.localName();
    return (local_name.isEmpty() ? element.tagName() : local_name).toLower();
}

QStringList classTokens(const QString& class_attr)
{
    return class_attr.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
}

void appendClass(QDomElement& element, const QString& class_name)
{
    QStringList classes = classTokens(element.attribute(QStringLiteral("class")));
    if (!classes.contains(class_name)) {
        classes.append(class_name);
        element.setAttribute(QStringLiteral("class"), classes.join(QLatin1Char(' ')));
    }
}

bool hasClassToken(const QDomElement& element, const QString& class_name)
{
    return classTokens(element.attribute(QStringLiteral("class"))).contains(class_name);
}

bool removeClassToken(QDomElement& element, const QString& class_name)
{
    QStringList classes = classTokens(element.attribute(QStringLiteral("class")));
    const int removed = classes.removeAll(class_name);
    if (removed == 0) {
        return false;
    }
    if (classes.isEmpty()) {
        element.removeAttribute(QStringLiteral("class"));
    } else {
        element.setAttribute(QStringLiteral("class"), classes.join(QLatin1Char(' ')));
    }
    return true;
}

bool replaceClassToken(QDomElement& element, const QString& from_class, const QString& to_class)
{
    QStringList classes = classTokens(element.attribute(QStringLiteral("class")));
    bool changed = false;
    for (int i = 0; i < classes.size(); ++i) {
        if (classes.at(i) == from_class) {
            classes[i] = to_class;
            changed = true;
        }
    }
    if (changed) {
        element.setAttribute(QStringLiteral("class"), classes.join(QLatin1Char(' ')));
    }
    return changed;
}

bool removeOverrideStyles(QDomElement& head, const QString& override_class)
{
    if (head.isNull()) {
        return false;
    }
    bool changed = false;
    QDomNode child = head.firstChild();
    while (!child.isNull()) {
        const QDomNode next = child.nextSibling();
        if (child.isElement() && localName(child) == QStringLiteral("style")
            && child.toElement().text().contains(override_class)) {
            head.removeChild(child);
            changed = true;
        }
        child = next;
    }
    return changed;
}

bool isVerticalWritingValue(const QString& value)
{
    QString val = value.trimmed().toLower();
    if (val.endsWith(QStringLiteral("!important"))) {
        val = val.left(val.length() - 10).trimmed();
    }
    return val == QStringLiteral("vertical-rl")
        || val == QStringLiteral("vertical-lr")
        || val == QStringLiteral("sideways-rl")
        || val == QStringLiteral("sideways-lr")
        || val == QStringLiteral("vertical")
        || val == QStringLiteral("tb")
        || val == QStringLiteral("tb-rl");
}

bool isHorizontalWritingValue(const QString& value)
{
    QString val = value.trimmed().toLower();
    if (val.endsWith(QStringLiteral("!important"))) {
        val = val.left(val.length() - 10).trimmed();
    }
    return val == QStringLiteral("horizontal-tb")
        || val == QStringLiteral("horizontal")
        || val == QStringLiteral("lr")
        || val == QStringLiteral("lr-tb")
        || val == QStringLiteral("rl")
        || val == QStringLiteral("rl-tb");
}

bool isWritingModeProperty(const QString& property)
{
    return property == QStringLiteral("writing-mode")
        || property == QStringLiteral("-webkit-writing-mode")
        || property == QStringLiteral("-epub-writing-mode")
        || property == QStringLiteral("-ms-writing-mode");
}

bool selectorTargetsRootFlow(const QString& selector)
{
    static const QRegularExpression root_re(
        QStringLiteral("(^|[\\s>+~,])(?:html|body|:root)(?=($|[\\s>+~.#:\\[]))"),
        QRegularExpression::CaseInsensitiveOption);
    return root_re.match(selector).hasMatch();
}

using ConversionDirection = VerticalCssTransformer::ConversionDirection;

QString targetWritingMode(ConversionDirection direction)
{
    return direction == ConversionDirection::VerticalToHorizontal
        ? QStringLiteral("horizontal-tb")
        : QStringLiteral("vertical-rl");
}

bool isSourceWritingValue(const QString& value, ConversionDirection direction)
{
    return direction == ConversionDirection::VerticalToHorizontal
        ? isVerticalWritingValue(value)
        : isHorizontalWritingValue(value);
}

QString overrideClassName(ConversionDirection direction)
{
    return direction == ConversionDirection::VerticalToHorizontal
        ? QStringLiteral("se-v2h-horizontal")
        : QStringLiteral("se-h2v-vertical");
}

// 把 style 属性里“源方向”的 writing-mode 值改写成目标方向值，保留其它声明。
QString rewriteStyleForDirection(const QString& style, ConversionDirection direction)
{
    const QStringList parts = style.split(QLatin1Char(';'));
    QStringList rebuilt;
    for (const QString& part : parts) {
        const int colon = part.indexOf(QLatin1Char(':'));
        if (colon <= 0) {
            rebuilt.append(part);
            continue;
        }
        QString prop = part.left(colon).trimmed().toLower();
        QString value = part.mid(colon + 1).trimmed();
        if (isWritingModeProperty(prop) && isSourceWritingValue(value, direction)) {
            QString important;
            if (value.endsWith(QStringLiteral("!important"))) {
                important = QStringLiteral(" !important");
            }
            value = targetWritingMode(direction) + important;
        }
        rebuilt.append(prop + QStringLiteral(": ") + value);
    }
    return rebuilt.join(QStringLiteral("; "));
}

// 在 tag 开标签的属性段中设置/新增某 XML 属性（保持其它字节不变）。
bool setXmlAttributeInTag(QString& source, const QString& tagName,
                          const QString& attrName, const QString& attrValue)
{
    const QRegularExpression tag_re(
        QStringLiteral("<%1\\b([^>]*)>").arg(tagName),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch tag_match = tag_re.match(source);
    if (!tag_match.hasMatch()) {
        return false;
    }
    const int attrs_start = tag_match.capturedStart(1);
    const int attrs_end = tag_match.capturedEnd(1);
    const QString attrs = tag_match.captured(1);
    // 自闭合标签（<spine/>）：属性应插在 "/" 之前
    const int self_close_offset = attrs.trimmed().endsWith(QLatin1Char('/'))
        ? attrs.lastIndexOf(QLatin1Char('/')) : -1;

    const QRegularExpression attr_re(
        QStringLiteral("\\b%1\\s*=\\s*(\"[^\"]*\"|'[^']*'|[^\\s>]+)").arg(attrName),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch attr_match = attr_re.match(attrs);
    if (attr_match.hasMatch()) {
        const int value_start = attrs_start + attr_match.capturedStart(1);
        source.replace(value_start, attr_match.captured(1).length(),
                       QStringLiteral("\"%1\"").arg(attrValue));
    } else if (self_close_offset >= 0) {
        source.insert(attrs_start + self_close_offset,
                      QStringLiteral(" %1=\"%2\"").arg(attrName, attrValue));
    } else {
        source.insert(attrs_end, QStringLiteral(" %1=\"%2\"").arg(attrName, attrValue));
    }
    return true;
}

bool parseXml(const QString& source, QDomDocument& document, QString& error)
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

QDomElement findElementByLocalName(QDomNode root, const QString& name)
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

// QDomDocument::elementsByTagName("*") 在 Qt 下返回空；用递归遍历替代。
void walkDomElements(QDomNode node, const std::function<void(QDomElement&)>& fn)
{
    if (node.isElement()) {
        QDomElement element = node.toElement();
        fn(element);
    }
    for (QDomNode child = node.firstChild(); !child.isNull(); child = child.nextSibling()) {
        walkDomElements(child, fn);
    }
}

// 从 font-feature-settings 值中移除 vert/vrt2 特征，保留其它。
QString cleanFontFeatureSettings(const QString& value, bool& changed)
{
    QStringList segments = value.split(QLatin1Char(','));
    QStringList kept;
    for (const QString& segment : segments) {
        const QString trimmed = segment.trimmed();
        const QString lower = trimmed.toLower();
        if (lower.contains(QStringLiteral("\"vert\""))
            || lower.contains(QStringLiteral("\"vrt2\""))
            || lower == QStringLiteral("vert")
            || lower == QStringLiteral("vrt2")
            || lower.startsWith(QStringLiteral("vert 1"))
            || lower.startsWith(QStringLiteral("vrt2 1"))
            || lower.startsWith(QStringLiteral("vert\""))
            || lower.startsWith(QStringLiteral("vrt2\""))) {
            changed = true;
            continue;
        }
        kept.append(trimmed);
    }
    return kept.join(QStringLiteral(", "));
}

struct CssEdit {
    int start = 0;
    int end = 0;          // 删除区间 [start, end)
    QString replacement;  // 替换文本；空表示删除
};

} // namespace

// ---------------------------------------------------------------------
// 兼容覆盖样式
// ---------------------------------------------------------------------

QString VerticalCssTransformer::buildOverrideCss(ConversionDirection direction,
                                                 const QString& overrideClass)
{
    const QString cls = overrideClass.isEmpty() ? overrideClassName(direction) : overrideClass;
    if (direction == ConversionDirection::VerticalToHorizontal) {
        return QStringLiteral(
            "html.%1,\n"
            "html.%1 body {\n"
            "  writing-mode: horizontal-tb !important;\n"
            "  -epub-writing-mode: horizontal-tb !important;\n"
            "  -webkit-writing-mode: horizontal-tb !important;\n"
            "}\n"
            "html.%1 * {\n"
            "  writing-mode: horizontal-tb !important;\n"
            "  -epub-writing-mode: horizontal-tb !important;\n"
            "  -webkit-writing-mode: horizontal-tb !important;\n"
            "  text-combine-upright: none !important;\n"
            "  -webkit-text-combine: none !important;\n"
            "  text-orientation: initial !important;\n"
            "}\n").arg(cls);
    }
    return QStringLiteral(
        "html.%1,\n"
        "html.%1 body {\n"
        "  writing-mode: vertical-rl !important;\n"
        "  -epub-writing-mode: vertical-rl !important;\n"
        "  -webkit-writing-mode: vertical-rl !important;\n"
        "}\n"
        "html.%1 * {\n"
        "  text-orientation: mixed !important;\n"
        "}\n").arg(cls);
}

// ---------------------------------------------------------------------
// XHTML 变换
// ---------------------------------------------------------------------

VerticalCssTransformer::TransformResult VerticalCssTransformer::addRootOverrideClass(
    const QString& source, const QString& overrideClass)
{
    TransformResult result;
    QDomDocument document;
    QString error;
    if (!parseXml(source, document, error)) {
        result.ok = false;
        result.messages << QStringLiteral("排版方向转换：XML 解析失败，未写回。%1").arg(error);
        return result;
    }
    QDomElement html = document.documentElement();
    if (localName(html) != QStringLiteral("html")) {
        result.ok = false;
        result.messages << QStringLiteral("排版方向转换：缺少 html 根元素，未写回。");
        return result;
    }
    const QString cls = overrideClass.isEmpty() ? QStringLiteral("se-v2h-horizontal") : overrideClass;
    const QString before_class = html.attribute(QStringLiteral("class"));
    appendClass(html, cls);
    result.changed = html.attribute(QStringLiteral("class")) != before_class;
    result.text = document.toString(2);
    result.ok = true;
    return result;
}

VerticalCssTransformer::TransformResult VerticalCssTransformer::injectOverrideStyle(
    const QString& source, const QString& cssText)
{
    TransformResult result;
    QDomDocument document;
    QString error;
    if (!parseXml(source, document, error)) {
        result.ok = false;
        result.messages << QStringLiteral("排版方向转换：XML 解析失败，未写回。%1").arg(error);
        return result;
    }
    QDomElement html = document.documentElement();
    if (localName(html) != QStringLiteral("html")) {
        result.ok = false;
        result.messages << QStringLiteral("排版方向转换：缺少 html 根元素，未写回。");
        return result;
    }

    // 幂等：已存在包含本次 override 选择器的 <style> 则不再注入。
    QString marker;
    for (const QString& candidate : {QStringLiteral("se-v2h-horizontal"),
                                     QStringLiteral("se-h2v-vertical")}) {
        if (cssText.contains(candidate)) {
            marker = candidate;
            break;
        }
    }
    bool already_injected = false;
    QDomElement head = findElementByLocalName(html, QStringLiteral("head"));
    if (!head.isNull()) {
        for (QDomNode child = head.firstChild(); !child.isNull(); child = child.nextSibling()) {
            if (child.isElement() && localName(child) == QStringLiteral("style")
                && ((marker.isEmpty() && child.toElement().text() == cssText)
                    || (!marker.isEmpty() && child.toElement().text().contains(marker)))) {
                already_injected = true;
                break;
            }
        }
    }

    if (already_injected) {
        result.text = source;
        result.ok = true;
        return result;
    }

    if (head.isNull()) {
        head = document.createElement(QStringLiteral("head"));
        QDomNode first = html.firstChild();
        if (first.isNull()) {
            html.appendChild(head);
        } else {
            html.insertBefore(head, first);
        }
    }

    QDomElement style = document.createElement(QStringLiteral("style"));
    style.setAttribute(QStringLiteral("type"), QStringLiteral("text/css"));
    style.appendChild(document.createTextNode(cssText));
    head.appendChild(style);

    result.text = document.toString(2);
    result.changed = true;
    result.ok = true;
    return result;
}

VerticalCssTransformer::TransformResult VerticalCssTransformer::switchLayoutClass(
    const QString& source, ConversionDirection direction)
{
    TransformResult result;
    QDomDocument document;
    QString error;
    if (!parseXml(source, document, error)) {
        result.ok = false;
        result.messages << QStringLiteral("排版方向转换：XML 解析失败，未写回。%1").arg(error);
        return result;
    }
    QDomElement html = document.documentElement();
    if (localName(html) != QStringLiteral("html")) {
        result.ok = false;
        result.messages << QStringLiteral("排版方向转换：缺少 html 根元素，未写回。");
        return result;
    }
    const QString from_class = direction == ConversionDirection::VerticalToHorizontal
        ? QStringLiteral("vrtl") : QStringLiteral("hltr");
    const QString to_class = direction == ConversionDirection::VerticalToHorizontal
        ? QStringLiteral("hltr") : QStringLiteral("vrtl");
    bool changed = replaceClassToken(html, from_class, to_class);
    QDomElement body = findElementByLocalName(html, QStringLiteral("body"));
    if (!body.isNull()) {
        changed = replaceClassToken(body, from_class, to_class) || changed;
    }
    result.changed = changed;
    result.text = document.toString(2);
    result.ok = true;
    return result;
}

VerticalCssTransformer::TransformResult VerticalCssTransformer::transformInlineWritingMode(
    const QString& source, ConversionDirection direction)
{
    TransformResult result;
    QDomDocument document;
    QString error;
    if (!parseXml(source, document, error)) {
        result.ok = false;
        result.messages << QStringLiteral("排版方向转换：XML 解析失败，未写回。%1").arg(error);
        return result;
    }

    int changed_count = 0;
    walkDomElements(document, [&](QDomElement& element) {
        const QString style = element.attribute(QStringLiteral("style"));
        if (style.isEmpty()) {
            return;
        }
        const QString rewritten = rewriteStyleForDirection(style, direction);
        if (rewritten != style) {
            element.setAttribute(QStringLiteral("style"), rewritten);
            changed_count++;
        }
    });
    result.changed = changed_count > 0;
    result.text = document.toString(2);
    result.ok = true;
    return result;
}

VerticalCssTransformer::TransformResult VerticalCssTransformer::transformXhtml(
    const QString& source, const Options& options, bool switchToTargetClass)
{
    TransformResult result;
    QDomDocument document;
    QString error;
    if (!parseXml(source, document, error)) {
        result.ok = false;
        result.messages << QStringLiteral("排版方向转换：XML 解析失败，未写回。%1").arg(error);
        return result;
    }
    QDomElement html = document.documentElement();
    if (localName(html) != QStringLiteral("html")) {
        result.ok = false;
        result.messages << QStringLiteral("排版方向转换：缺少 html 根元素，未写回。");
        return result;
    }

    const QString cls = overrideClassName(options.direction);
    const QString opposite_cls = overrideClassName(
        options.direction == ConversionDirection::VerticalToHorizontal
            ? ConversionDirection::HorizontalToVertical
            : ConversionDirection::VerticalToHorizontal);
    const QString from_class = options.direction == ConversionDirection::VerticalToHorizontal
        ? QStringLiteral("vrtl") : QStringLiteral("hltr");
    const QString to_class = options.direction == ConversionDirection::VerticalToHorizontal
        ? QStringLiteral("hltr") : QStringLiteral("vrtl");
    QDomElement body = findElementByLocalName(html, QStringLiteral("body"));
    QDomElement head = findElementByLocalName(html, QStringLiteral("head"));

    // Make direction changes reversible: discard an earlier override for the
    // opposite direction before applying this one.
    result.changed = removeClassToken(html, opposite_cls) || result.changed;
    if (!body.isNull()) {
        result.changed = removeClassToken(body, opposite_cls) || result.changed;
    }
    result.changed = removeOverrideStyles(head, opposite_cls) || result.changed;

    bool use_override = !switchToTargetClass;
    if (switchToTargetClass) {
        bool class_switched = replaceClassToken(html, from_class, to_class);
        if (!body.isNull()) {
            class_switched = replaceClassToken(body, from_class, to_class)
                || class_switched;
        }
        const bool target_class_present = hasClassToken(html, to_class)
            || (!body.isNull() && hasClassToken(body, to_class));
        if (class_switched || target_class_present) {
            result.changed = class_switched || result.changed;
            // A paired profile class is sufficient. Remove compatibility
            // overrides so the shared .vrtl/.hltr stylesheet remains intact.
            result.changed = removeClassToken(html, cls) || result.changed;
            if (!body.isNull()) {
                result.changed = removeClassToken(body, cls) || result.changed;
            }
            result.changed = removeOverrideStyles(head, cls) || result.changed;
        } else {
            // The book profile may be paired globally while an individual
            // page has no layout class. Fall back to a local override instead
            // of reporting a successful no-op.
            use_override = true;
        }
    }

    if (use_override) {
        const QString before_class = html.attribute(QStringLiteral("class"));
        appendClass(html, cls);
        result.changed = result.changed
            || html.attribute(QStringLiteral("class")) != before_class;
    }

    // Inline !important declarations outrank a compatibility stylesheet.
    // Rewrite root declarations in both directions; when converting to
    // vertical, preserve explicit horizontal descendant subflows by default.
    {
        walkDomElements(document, [&](QDomElement& element) {
            const QString style = element.attribute(QStringLiteral("style"));
            if (style.isEmpty()) {
                return;
            }
            if (options.direction == ConversionDirection::HorizontalToVertical
                && options.preserveHorizontalSubflows
                && localName(element) != QStringLiteral("html")
                && localName(element) != QStringLiteral("body")) {
                return;
            }
            const QString rewritten = rewriteStyleForDirection(style, options.direction);
            if (rewritten != style) {
                element.setAttribute(QStringLiteral("style"), rewritten);
                result.changed = true;
            }
        });
    }

    if (use_override) {
        bool already_injected = false;
        if (!head.isNull()) {
            for (QDomNode child = head.firstChild(); !child.isNull(); child = child.nextSibling()) {
                if (child.isElement() && localName(child) == QStringLiteral("style")
                    && child.toElement().text().contains(cls)) {
                    already_injected = true;
                    break;
                }
            }
        }
        if (!already_injected) {
            if (head.isNull()) {
                head = document.createElement(QStringLiteral("head"));
                QDomNode first = html.firstChild();
                if (first.isNull()) {
                    html.appendChild(head);
                } else {
                    html.insertBefore(head, first);
                }
            }
            QDomElement style = document.createElement(QStringLiteral("style"));
            style.setAttribute(QStringLiteral("type"), QStringLiteral("text/css"));
            style.appendChild(document.createTextNode(buildOverrideCss(options.direction)));
            head.appendChild(style);
            result.changed = true;
        }
    }

    result.text = document.toString(2);
    result.ok = true;
    return result;
}

// ---------------------------------------------------------------------
// CSS 结构化改写
// ---------------------------------------------------------------------

VerticalCssTransformer::TransformResult VerticalCssTransformer::transformCss(
    const QString& css, const Options& options)
{
    TransformResult result;
    if (css.isEmpty()) {
        result.ok = true;
        result.text = css;
        return result;
    }

    CSSParser parser;
    parser.parse_css(css);
    if (!parser.get_parse_errors().isEmpty()) {
        result.ok = false;
        result.messages << QStringLiteral("排版方向转换：CSS 解析失败，不写回该样式表。");
        result.text = css;
        return result;
    }

    const bool to_horizontal = options.direction == ConversionDirection::VerticalToHorizontal;
    QList<CssEdit> edits;
    QString current_selector;
    QString current_property;
    int current_property_pos = 0;
    const auto declarationRemoveEnd = [&css](int value_end) {
        int remove_end = value_end;
        if (remove_end < css.length() && css.at(remove_end) == QLatin1Char(';')) {
            remove_end++;
        }
        while (remove_end < css.length()
               && (css.at(remove_end) == QLatin1Char(' ') || css.at(remove_end) == QLatin1Char('\t'))) {
            remove_end++;
        }
        return remove_end;
    };
    while (true) {
        CSSParser::csstoken token = parser.get_next_token();
        if (token.type == TKN_CSS_END) {
            break;
        }
        if (token.type == TKN_SELECTOR) {
            current_selector = token.data.trimmed();
            continue;
        }
        if (token.type == TKN_PROPERTY) {
            current_property = token.data.trimmed().toLower();
            current_property_pos = token.pos;
            continue;
        }
        if (token.type != TKN_PROPERTY_VALUE || current_property.isEmpty()) {
            continue;
        }
        const int value_start = token.pos;
        const int value_end = value_start + token.data.length();
        const QString value = token.data.trimmed();

        if (isWritingModeProperty(current_property) && isSourceWritingValue(value, options.direction)
            && (to_horizontal || !options.preserveHorizontalSubflows
                || selectorTargetsRootFlow(current_selector))) {
            QString important;
            if (token.data.endsWith(QStringLiteral("!important"))) {
                important = QStringLiteral(" !important");
            }
            edits.append(CssEdit { value_start, value_end, targetWritingMode(options.direction) + important });
        } else if (to_horizontal
                   && (current_property == QStringLiteral("text-orientation")
                       || current_property == QStringLiteral("text-combine-upright")
                       || current_property == QStringLiteral("text-combine-horizontal")
                       || current_property == QStringLiteral("-webkit-text-combine"))
                   && options.neutralizeVerticalTextProperties) {
            // 删除整个声明（含分号）
            edits.append(CssEdit { current_property_pos, declarationRemoveEnd(value_end), QString() });
        } else if (to_horizontal
                   && current_property == QStringLiteral("font-feature-settings")
                   && (value.contains(QStringLiteral("vert")) || value.contains(QStringLiteral("vrt2")))) {
            bool cleaned_changed = false;
            const QString cleaned = cleanFontFeatureSettings(value, cleaned_changed);
            if (cleaned_changed) {
                if (cleaned.isEmpty()) {
                    edits.append(CssEdit { current_property_pos, declarationRemoveEnd(value_end), QString() });
                } else {
                    edits.append(CssEdit { value_start, value_end, cleaned });
                }
            }
        }
        current_property.clear();
    }

    // 从后往前应用编辑，保持位置有效
    std::sort(edits.begin(), edits.end(), [](const CssEdit& a, const CssEdit& b) {
        return a.start > b.start;
    });
    QString text = css;
    for (const CssEdit& edit : edits) {
        text.replace(edit.start, edit.end - edit.start, edit.replacement);
    }
    result.text = text;
    result.changed = text != css;
    result.ok = true;
    return result;
}

// ---------------------------------------------------------------------
// OPF page progression
// ---------------------------------------------------------------------

VerticalCssTransformer::TransformResult VerticalCssTransformer::transformOpfProgression(
    const QString& opf, bool toLtr)
{
    TransformResult result;
    QString text = opf;
    const QString target = toLtr ? QStringLiteral("ltr") : QStringLiteral("rtl");
    if (!setXmlAttributeInTag(text, QStringLiteral("spine"),
                              QStringLiteral("page-progression-direction"), target)) {
        result.ok = false;
        result.messages << QStringLiteral("排版方向转换：未找到 OPF spine 标签，未修改 page-progression-direction。");
        result.text = opf;
        return result;
    }
    result.text = text;
    result.changed = text != opf;
    result.ok = true;
    return result;
}

} // namespace BuiltinPlugins
