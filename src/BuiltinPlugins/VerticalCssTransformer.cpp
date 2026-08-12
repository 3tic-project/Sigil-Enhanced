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
    QString name = element.localName().isEmpty() ? element.tagName() : element.localName();
    const int colon = name.lastIndexOf(QLatin1Char(':'));
    if (colon >= 0) {
        name = name.mid(colon + 1);
    }
    return name.toLower();
}

QString childElementName(const QDomElement& parent, const QString& local_name)
{
    const QString parent_name = parent.tagName();
    const int colon = parent_name.lastIndexOf(QLatin1Char(':'));
    return colon >= 0 ? parent_name.left(colon + 1) + local_name : local_name;
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
        if (child.isElement() && localName(child) == QStringLiteral("style")) {
            const QDomElement style = child.toElement();
            const bool tagged = style.attribute(
                QStringLiteral("data-sigil-enhanced-layout-override")) == override_class;
            const bool legacy_exact = style.text().trimmed()
                == VerticalCssTransformer::buildOverrideCss(
                       override_class == QStringLiteral("se-h2v-vertical")
                           ? VerticalCssTransformer::ConversionDirection::HorizontalToVertical
                           : VerticalCssTransformer::ConversionDirection::VerticalToHorizontal,
                       override_class).trimmed();
            if (tagged || legacy_exact) {
                head.removeChild(child);
                changed = true;
            }
        }
        child = next;
    }
    return changed;
}

QString normalizedCssValue(QString value)
{
    value = value.trimmed().toLower();
    static const QRegularExpression important_re(
        QStringLiteral("\\s*!\\s*important\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    value.remove(important_re);
    return value.trimmed();
}

bool hasImportantSuffix(const QString& value)
{
    static const QRegularExpression important_re(
        QStringLiteral("!\\s*important\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    return important_re.match(value).hasMatch();
}

bool isVerticalWritingValue(const QString& value)
{
    const QString val = normalizedCssValue(value);
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
    const QString val = normalizedCssValue(value);
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

QString conversionMarkerName(ConversionDirection direction)
{
    return direction == ConversionDirection::VerticalToHorizontal
        ? QStringLiteral("se-v2h-converted")
        : QStringLiteral("se-h2v-converted");
}

// 把 style 属性里“源方向”的 writing-mode 值改写成目标方向值。
// 只替换属性值区间，必须逐字节保留其它声明和尾随空白；若用 split/join
// 重建整个 style，带尾分号的属性会在每次互转时累计一个空格。
QString rewriteStyleForDirection(const QString& style, ConversionDirection direction)
{
    struct StyleEdit {
        qsizetype start = 0;
        qsizetype length = 0;
        QString replacement;
    };

    static const QRegularExpression writing_mode_re(
        QStringLiteral("(^|;)\\s*(?:-(?:webkit|epub|ms)-)?writing-mode\\s*:\\s*([^;]*)"),
        QRegularExpression::CaseInsensitiveOption);

    QList<StyleEdit> edits;
    QRegularExpressionMatchIterator matches = writing_mode_re.globalMatch(style);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        const QString captured_value = match.captured(2);
        int leading = 0;
        while (leading < captured_value.size() && captured_value.at(leading).isSpace()) {
            ++leading;
        }
        int trailing = captured_value.size();
        while (trailing > leading && captured_value.at(trailing - 1).isSpace()) {
            --trailing;
        }
        const QString value = captured_value.mid(leading, trailing - leading);
        if (!isSourceWritingValue(value, direction)) {
            continue;
        }

        QString important;
        if (hasImportantSuffix(value)) {
            important = QStringLiteral(" !important");
        }
        edits.append(StyleEdit {
            match.capturedStart(2) + leading,
            trailing - leading,
            targetWritingMode(direction) + important
        });
    }

    QString rewritten = style;
    for (auto it = edits.crbegin(); it != edits.crend(); ++it) {
        rewritten.replace(it->start, it->length, it->replacement);
    }
    return rewritten;
}

struct XmlStartTag {
    int start = -1;
    int end = -1; // one past '>'
};

XmlStartTag findXmlStartTag(const QString& source, const QString& wanted_local_name)
{
    int cursor = 0;
    while ((cursor = source.indexOf(QLatin1Char('<'), cursor)) >= 0) {
        if (source.mid(cursor, 4) == QStringLiteral("<!--")) {
            const int end = source.indexOf(QStringLiteral("-->"), cursor + 4);
            cursor = end < 0 ? source.size() : end + 3;
            continue;
        }
        if (source.mid(cursor, 9) == QStringLiteral("<![CDATA[")) {
            const int end = source.indexOf(QStringLiteral("]]>") , cursor + 9);
            cursor = end < 0 ? source.size() : end + 3;
            continue;
        }
        if (source.mid(cursor, 2) == QStringLiteral("<!")) {
            // Skip declarations such as a DOCTYPE internal subset. A quoted
            // entity value may legally contain text resembling <spine>.
            QChar quote;
            int subset_depth = 0;
            int end = cursor + 2;
            for (; end < source.size(); ++end) {
                const QChar ch = source.at(end);
                if (!quote.isNull()) {
                    if (ch == quote) {
                        quote = QChar();
                    }
                } else if (ch == QLatin1Char('\'') || ch == QLatin1Char('"')) {
                    quote = ch;
                } else if (ch == QLatin1Char('[')) {
                    ++subset_depth;
                } else if (ch == QLatin1Char(']') && subset_depth > 0) {
                    --subset_depth;
                } else if (ch == QLatin1Char('>') && subset_depth == 0) {
                    ++end;
                    break;
                }
            }
            cursor = end;
            continue;
        }
        if (cursor + 1 >= source.size()
            || source.at(cursor + 1) == QLatin1Char('/')
            || source.at(cursor + 1) == QLatin1Char('!')
            || source.at(cursor + 1) == QLatin1Char('?')) {
            cursor++;
            continue;
        }
        int name_start = cursor + 1;
        while (name_start < source.size() && source.at(name_start).isSpace()) {
            ++name_start;
        }
        int name_end = name_start;
        while (name_end < source.size()
               && !source.at(name_end).isSpace()
               && source.at(name_end) != QLatin1Char('>')
               && source.at(name_end) != QLatin1Char('/')) {
            ++name_end;
        }
        QString name = source.mid(name_start, name_end - name_start).toLower();
        const int colon = name.lastIndexOf(QLatin1Char(':'));
        if (colon >= 0) {
            name = name.mid(colon + 1);
        }

        QChar quote;
        int end = name_end;
        for (; end < source.size(); ++end) {
            const QChar ch = source.at(end);
            if (!quote.isNull()) {
                if (ch == quote) {
                    quote = QChar();
                }
            } else if (ch == QLatin1Char('\'') || ch == QLatin1Char('"')) {
                quote = ch;
            } else if (ch == QLatin1Char('>')) {
                ++end;
                break;
            }
        }
        if (name == wanted_local_name.toLower() && end <= source.size()) {
            return XmlStartTag { cursor, end };
        }
        cursor = qMax(cursor + 1, end);
    }
    return XmlStartTag();
}

QString xmlAttributeValue(const QString& source, const XmlStartTag& tag,
                          const QString& attr_name, bool* present = nullptr)
{
    if (present) {
        *present = false;
    }
    if (tag.start < 0 || tag.end <= tag.start) {
        return QString();
    }
    const QString tag_text = source.mid(tag.start, tag.end - tag.start);
    const QRegularExpression attr_re(
        QStringLiteral("(?:^|\\s)%1\\s*=\\s*(?:\"([^\"]*)\"|'([^']*)'|([^\\s>]+))")
            .arg(QRegularExpression::escape(attr_name)),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = attr_re.match(tag_text);
    if (!match.hasMatch()) {
        return QString();
    }
    if (present) {
        *present = true;
    }
    for (int i = 1; i <= 3; ++i) {
        if (match.capturedStart(i) >= 0) {
            return match.captured(i);
        }
    }
    return QString();
}

bool setXmlAttributeInTag(QString& source, const XmlStartTag& tag,
                          const QString& attr_name, const QString& attr_value)
{
    if (tag.start < 0 || tag.end <= tag.start) {
        return false;
    }
    QString tag_text = source.mid(tag.start, tag.end - tag.start);
    const QRegularExpression attr_re(
        QStringLiteral("((?:^|\\s)%1\\s*=\\s*)(\"[^\"]*\"|'[^']*'|[^\\s>]+)")
            .arg(QRegularExpression::escape(attr_name)),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch attr_match = attr_re.match(tag_text);
    if (attr_match.hasMatch()) {
        const QString old_value = attr_match.captured(2);
        QString replacement_value = attr_value;
        if (old_value.size() >= 2
            && ((old_value.front() == QLatin1Char('"') && old_value.back() == QLatin1Char('"'))
                || (old_value.front() == QLatin1Char('\'') && old_value.back() == QLatin1Char('\'')))) {
            replacement_value = old_value.left(1) + attr_value + old_value.right(1);
        }
        tag_text.replace(attr_match.capturedStart(0), attr_match.capturedLength(0),
                         attr_match.captured(1) + replacement_value);
    } else {
        int insert_at = tag_text.lastIndexOf(QLatin1Char('>'));
        int slash = insert_at - 1;
        while (slash >= 0 && tag_text.at(slash).isSpace()) {
            --slash;
        }
        if (slash >= 0 && tag_text.at(slash) == QLatin1Char('/')) {
            insert_at = slash;
        }
        tag_text.insert(insert_at,
                        QStringLiteral(" %1=\"%2\"").arg(attr_name, attr_value));
    }
    source.replace(tag.start, tag.end - tag.start, tag_text);
    return true;
}

bool removeXmlAttributeInTag(QString& source, const XmlStartTag& tag,
                             const QString& attr_name)
{
    if (tag.start < 0 || tag.end <= tag.start) {
        return false;
    }
    QString tag_text = source.mid(tag.start, tag.end - tag.start);
    const QRegularExpression attr_re(
        QStringLiteral("\\s+%1\\s*=\\s*(?:\"[^\"]*\"|'[^']*'|[^\\s>]+)")
            .arg(QRegularExpression::escape(attr_name)),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = attr_re.match(tag_text);
    if (!match.hasMatch()) {
        return true;
    }
    tag_text.remove(match.capturedStart(), match.capturedLength());
    source.replace(tag.start, tag.end - tag.start, tag_text);
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
    static const QRegularExpression important_re(
        QStringLiteral("\\s*!\\s*important\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression vertical_feature_re(
        QStringLiteral("^\\s*(['\"]?)(vert|vrt2)\\1(?:\\s+(on|off|[+-]?\\d+))?\\s*$"),
        QRegularExpression::CaseInsensitiveOption);

    QString feature_value = value;
    const bool important = important_re.match(feature_value).hasMatch();
    feature_value.remove(important_re);
    QStringList segments = feature_value.split(QLatin1Char(','));
    QStringList kept;
    for (const QString& segment : segments) {
        const QString trimmed = segment.trimmed();
        const QRegularExpressionMatch match = vertical_feature_re.match(trimmed);
        if (match.hasMatch()) {
            const QString state = match.captured(3).toLower();
            const bool enabled = state.isEmpty() || state == QStringLiteral("on")
                || (state != QStringLiteral("off") && state.toInt() != 0);
            if (enabled) {
                changed = true;
                continue;
            }
        }
        kept.append(trimmed);
    }
    QString cleaned = kept.join(QStringLiteral(", "));
    if (!cleaned.isEmpty() && important) {
        cleaned.append(QStringLiteral(" !important"));
    }
    return cleaned;
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
                    || (!marker.isEmpty()
                        && (child.toElement().attribute(
                                QStringLiteral("data-sigil-enhanced-layout-override")) == marker
                            || child.toElement().text().trimmed() == cssText.trimmed())))) {
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
        head = document.createElement(childElementName(html, QStringLiteral("head")));
        QDomNode first = html.firstChild();
        if (first.isNull()) {
            html.appendChild(head);
        } else {
            html.insertBefore(head, first);
        }
    }

    QDomElement style = document.createElement(childElementName(head, QStringLiteral("style")));
    style.setAttribute(QStringLiteral("type"), QStringLiteral("text/css"));
    if (!marker.isEmpty()) {
        style.setAttribute(QStringLiteral("data-sigil-enhanced-layout-override"), marker);
    }
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
    const QString marker = conversionMarkerName(options.direction);
    const QString opposite_marker = conversionMarkerName(
        options.direction == ConversionDirection::VerticalToHorizontal
            ? ConversionDirection::HorizontalToVertical
            : ConversionDirection::VerticalToHorizontal);
    const QString from_class = options.direction == ConversionDirection::VerticalToHorizontal
        ? QStringLiteral("vrtl") : QStringLiteral("hltr");
    const QString to_class = options.direction == ConversionDirection::VerticalToHorizontal
        ? QStringLiteral("hltr") : QStringLiteral("vrtl");
    QDomElement body = findElementByLocalName(html, QStringLiteral("body"));
    QDomElement head = findElementByLocalName(html, QStringLiteral("head"));

    // An opposite override/marker proves that this page was changed by an
    // earlier plugin run. Reversing it must restore the original page rather
    // than applying a fresh target-direction override on top.
    const bool reversing_profile = hasClassToken(html, opposite_marker)
        || (!body.isNull() && hasClassToken(body, opposite_marker));
    const bool reversing_override = hasClassToken(html, opposite_cls)
        || (!body.isNull() && hasClassToken(body, opposite_cls));
    const bool reversing_generated = reversing_profile || reversing_override;

    // A profile marker records a class-based conversion. If the expected
    // current class disappeared, the page was edited after conversion; do
    // not silently replace that manual state with a compatibility override.
    // The provenance determines how to reverse, not the currently selected
    // UI mode. A compatibility-generated page must be restored by removing
    // its override even when structured mode is selected this time.
    const bool use_profile_switch = reversing_profile
        || (switchToTargetClass && !reversing_generated);
    if (reversing_profile
        && !hasClassToken(html, from_class)
        && (body.isNull() || !hasClassToken(body, from_class))) {
        result.ok = false;
        result.text = source;
        result.messages << QStringLiteral(
            "排版方向转换：页面布局 class 在上次转换后被修改，未覆盖人工修改。");
        return result;
    }

    // Make direction changes reversible: discard an earlier override for the
    // opposite direction before applying this one.
    result.changed = removeClassToken(html, opposite_cls) || result.changed;
    if (!body.isNull()) {
        result.changed = removeClassToken(body, opposite_cls) || result.changed;
    }
    result.changed = removeOverrideStyles(head, opposite_cls) || result.changed;
    result.changed = removeClassToken(html, opposite_marker) || result.changed;
    if (!body.isNull()) {
        result.changed = removeClassToken(body, opposite_marker) || result.changed;
    }

    bool use_override = !use_profile_switch && !reversing_generated;
    if (use_profile_switch) {
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
            if (reversing_generated) {
                result.changed = removeClassToken(html, marker) || result.changed;
                if (!body.isNull()) {
                    result.changed = removeClassToken(body, marker) || result.changed;
                }
            } else {
                const QString before_marker = html.attribute(QStringLiteral("class"));
                appendClass(html, marker);
                result.changed = result.changed
                    || html.attribute(QStringLiteral("class")) != before_marker;
            }
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

    // Do not rewrite inline declarations in the automatic whole-page path.
    // They cannot be distinguished later from an originally-opposite nested
    // subflow, so rewriting them makes round trips lossy. The compatibility
    // override outranks ordinary inline declarations; source-direction
    // inline !important declarations are classified for manual review by the
    // analyzer. transformInlineWritingMode() remains available explicitly.

    if (use_override) {
        bool already_injected = false;
        if (!head.isNull()) {
            for (QDomNode child = head.firstChild(); !child.isNull(); child = child.nextSibling()) {
                if (child.isElement() && localName(child) == QStringLiteral("style")
                    && (child.toElement().attribute(
                            QStringLiteral("data-sigil-enhanced-layout-override")) == cls
                        || child.toElement().text().trimmed()
                            == buildOverrideCss(options.direction).trimmed())) {
                    already_injected = true;
                    break;
                }
            }
        }
        if (!already_injected) {
            if (head.isNull()) {
                head = document.createElement(childElementName(html, QStringLiteral("head")));
                QDomNode first = html.firstChild();
                if (first.isNull()) {
                    html.appendChild(head);
                } else {
                    html.insertBefore(head, first);
                }
            }
            QDomElement style = document.createElement(childElementName(head, QStringLiteral("style")));
            style.setAttribute(QStringLiteral("type"), QStringLiteral("text/css"));
            style.setAttribute(QStringLiteral("data-sigil-enhanced-layout-override"), cls);
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
            if (hasImportantSuffix(token.data)) {
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
                   && (value.contains(QStringLiteral("vert"), Qt::CaseInsensitive)
                       || value.contains(QStringLiteral("vrt2"), Qt::CaseInsensitive))) {
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
    QDomDocument document;
    QString parse_error;
    if (!parseXml(opf, document, parse_error)) {
        result.ok = false;
        result.messages << QStringLiteral("排版方向转换：OPF XML 解析失败，未修改翻页方向。%1")
                               .arg(parse_error);
        result.text = opf;
        return result;
    }

    QString text = opf;
    const QString target = toLtr ? QStringLiteral("ltr") : QStringLiteral("rtl");
    XmlStartTag spine = findXmlStartTag(text, QStringLiteral("spine"));
    if (spine.start < 0) {
        result.ok = false;
        result.messages << QStringLiteral("排版方向转换：未找到 OPF spine 标签，未修改 page-progression-direction。");
        result.text = opf;
        return result;
    }

    static const QRegularExpression marker_re(
        QStringLiteral("<!--\\s*sigil-enhanced-layout-progression\\s+"
                       "original=(absent|default|ltr|rtl)\\s+applied=(ltr|rtl)\\s*-->(?:\\r?\\n)?"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch marker_match = marker_re.match(text);

    bool has_progression = false;
    QString current = xmlAttributeValue(
        text, spine, QStringLiteral("page-progression-direction"), &has_progression)
                          .trimmed().toLower();
    if (!has_progression) {
        current = QStringLiteral("absent");
    } else if (current != QStringLiteral("ltr")
               && current != QStringLiteral("rtl")
               && current != QStringLiteral("default")) {
        result.ok = false;
        result.messages << QStringLiteral(
            "排版方向转换：page-progression-direction 的原值“%1”无效，未修改 OPF。")
                               .arg(current);
        result.text = opf;
        return result;
    }

    if (marker_match.hasMatch()) {
        const QString original = marker_match.captured(1).toLower();
        const QString applied = marker_match.captured(2).toLower();
        if (current != applied) {
            result.ok = false;
            result.messages << QStringLiteral(
                "排版方向转换：翻页方向在上次转换后被改为“%1”，为避免覆盖人工修改，未修改 OPF。")
                                   .arg(current);
            result.text = opf;
            return result;
        }
        if (target == applied) {
            result.text = opf;
            result.ok = true;
            return result;
        }

        // Opposite-direction conversion restores the exact pre-conversion
        // value, including an absent/default attribute, then removes the
        // provenance comment. This makes repeated round trips lossless.
        if (original == QStringLiteral("absent")) {
            removeXmlAttributeInTag(text, spine,
                                    QStringLiteral("page-progression-direction"));
        } else {
            setXmlAttributeInTag(text, spine,
                                 QStringLiteral("page-progression-direction"), original);
        }
        const QRegularExpressionMatch current_marker = marker_re.match(text);
        if (current_marker.hasMatch()) {
            text.remove(current_marker.capturedStart(), current_marker.capturedLength());
        }
        result.text = text;
        result.changed = text != opf;
        result.ok = true;
        return result;
    }

    const QString original = current;
    if (!setXmlAttributeInTag(text, spine,
                              QStringLiteral("page-progression-direction"), target)) {
        result.ok = false;
        result.messages << QStringLiteral("排版方向转换：无法更新 OPF spine 翻页方向。");
        result.text = opf;
        return result;
    }
    spine = findXmlStartTag(text, QStringLiteral("spine"));
    text.insert(spine.start,
                QStringLiteral("<!-- sigil-enhanced-layout-progression original=%1 applied=%2 -->\n")
                    .arg(original, target));
    result.text = text;
    result.changed = text != opf;
    result.ok = true;
    return result;
}

} // namespace BuiltinPlugins
