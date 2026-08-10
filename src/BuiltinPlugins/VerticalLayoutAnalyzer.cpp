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

#include "BuiltinPlugins/VerticalLayoutAnalyzer.h"

#include <QDomDocument>
#include <QDomElement>
#include <QRegularExpression>
#include <QSet>

#include "Parsers/CSSParser.h"

namespace BuiltinPlugins
{

namespace
{

const QString XHTML_NS = QStringLiteral("http://www.w3.org/1999/xhtml");

// ---------------------------------------------------------------------
// 通用小工具
// ---------------------------------------------------------------------

QString localName(const QDomNode& node)
{
    if (!node.isElement()) {
        return QString();
    }
    const QDomElement element = node.toElement();
    const QString local_name = element.localName();
    return (local_name.isEmpty() ? element.tagName() : local_name).toLower();
}

// 关闭 namespace processing 时，带前缀元素（如 dc:language）的 localName
// 会退回完整 tagName；这里去掉命名空间前缀便于匹配。
QString unprefixedName(const QDomNode& node)
{
    QString name = localName(node);
    const int colon = name.indexOf(QLatin1Char(':'));
    if (colon >= 0) {
        name = name.mid(colon + 1);
    }
    return name;
}

QStringList classTokens(const QString& class_attr)
{
    return class_attr.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
}

bool elementHasClass(const QDomElement& element, const QString& class_name)
{
    return classTokens(element.attribute(QStringLiteral("class"))).contains(class_name);
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
        || property == QStringLiteral("-ms-writing-mode")
        || property == QStringLiteral("writing-mode-css3");
}

// 把 inline style / style 属性拆成 (property, value) 声明。
// 只解析声明级结构，不做任何级联语义推断。
QList<QPair<QString, QString>> parseStyleDeclarations(const QString& style)
{
    QList<QPair<QString, QString>> declarations;
    const QStringList parts = style.split(QLatin1Char(';'));
    for (const QString& part : parts) {
        const int colon = part.indexOf(QLatin1Char(':'));
        if (colon <= 0) {
            continue;
        }
        const QString prop = part.left(colon).trimmed().toLower();
        QString value = part.mid(colon + 1).trimmed();
        if (value.endsWith(QStringLiteral("!important"))) {
            value = value.left(value.length() - 10).trimmed();
        }
        if (!prop.isEmpty() && !value.isEmpty()) {
            declarations.append(qMakePair(prop, value));
        }
    }
    return declarations;
}

void detectStyleSignals(const QString& style, bool& vertical, bool& horizontal,
                        int& absolute_count, int& rotate_count)
{
    const QList<QPair<QString, QString>> declarations = parseStyleDeclarations(style);
    for (const QPair<QString, QString>& declaration : declarations) {
        if (isWritingModeProperty(declaration.first)) {
            if (isVerticalWritingValue(declaration.second)) {
                vertical = true;
            } else if (isHorizontalWritingValue(declaration.second)) {
                horizontal = true;
            }
        } else if (declaration.first == QStringLiteral("position")
                   && declaration.second == QStringLiteral("absolute")) {
            absolute_count++;
        } else if (declaration.first == QStringLiteral("transform")
                   && declaration.second.contains(QStringLiteral("rotate"), Qt::CaseInsensitive)) {
            rotate_count++;
        }
    }
}

// 递归扫描样式表源码（内联 <style> 文本），复用一个轻量属性检测器。
void scanCssText(const QString& css, bool& vertical, bool& horizontal,
                 int& absolute_count, int& rotate_count)
{
    if (css.isEmpty()) {
        return;
    }
    CSSParser parser;
    parser.parse_css(css);
    QString current_property;
    while (true) {
        CSSParser::csstoken token = parser.get_next_token();
        if (token.type == TKN_CSS_END) {
            break;
        }
        if (token.type == TKN_PROPERTY) {
            current_property = token.data.trimmed().toLower();
        } else if (token.type == TKN_PROPERTY_VALUE) {
            if (isWritingModeProperty(current_property)) {
                if (isVerticalWritingValue(token.data)) {
                    vertical = true;
                } else if (isHorizontalWritingValue(token.data)) {
                    horizontal = true;
                }
            } else if (current_property == QStringLiteral("position")
                       && token.data.trimmed().toLower() == QStringLiteral("absolute")) {
                absolute_count++;
            } else if (current_property == QStringLiteral("transform")
                       && token.data.contains(QStringLiteral("rotate"), Qt::CaseInsensitive)) {
                rotate_count++;
            }
            current_property.clear();
        }
    }
}

QSet<QString> collectClassNamesFromCss(const QString& css)
{
    QSet<QString> classes;
    if (css.isEmpty()) {
        return classes;
    }
    CSSParser parser;
    parser.parse_css(css);
    const QRegularExpression class_re(QStringLiteral("\\.[A-Za-z_][A-Za-z0-9_-]*"));
    while (true) {
        CSSParser::csstoken token = parser.get_next_token();
        if (token.type == TKN_CSS_END) {
            break;
        }
        if (token.type == TKN_SELECTOR) {
            QRegularExpressionMatchIterator matches = class_re.globalMatch(token.data);
            while (matches.hasNext()) {
                classes.insert(matches.next().captured(0).mid(1).toLower());
            }
        }
    }
    return classes;
}

// 物理方向 utility class 的启发式：DPFJ/EBPAJ 常见 .start-*/.end-*，
// 以及 tailwind 式 mt/mb/ml/mr/pt/pb/pl/pr/ms/me/ps/pe 前缀。
bool isPhysicalSideUtilityClass(const QString& class_name)
{
    static const QRegularExpression start_end_re(QStringLiteral("^(start|end)(-|$|[0-9A-Za-z_-])"));
    static const QRegularExpression physical_re(
        QStringLiteral("^(mt|mb|ml|mr|pt|pb|pl|pr|ms|me|ps|pe|start|end)-[0-9A-Za-z]+$"));
    return start_end_re.match(class_name).hasMatch()
        || physical_re.match(class_name).hasMatch();
}

// 递归遍历计数
template <typename F>
void walkElements(const QDomNode& node, F&& fn)
{
    if (node.isElement()) {
        fn(node.toElement());
    }
    for (QDomNode child = node.firstChild(); !child.isNull(); child = child.nextSibling()) {
        walkElements(child, fn);
    }
}

int countElementsByName(const QDomNode& node, const QString& name)
{
    int count = 0;
    if (node.isElement() && localName(node) == name) {
        count++;
    }
    for (QDomNode child = node.firstChild(); !child.isNull(); child = child.nextSibling()) {
        count += countElementsByName(child, name);
    }
    return count;
}

int visibleTextLength(const QDomNode& node)
{
    if (node.isText() || node.isCDATASection()) {
        return node.nodeValue().length();
    }
    if (!node.isElement()) {
        return 0;
    }
    const QString name = localName(node);
    if (name == QStringLiteral("script") || name == QStringLiteral("style")) {
        return 0;
    }
    int length = 0;
    for (QDomNode child = node.firstChild(); !child.isNull(); child = child.nextSibling()) {
        length += visibleTextLength(child);
    }
    return length;
}

bool isNavDocument(const QDomElement& html)
{
    // epub:type="nav" 或 role="doc-toc" 或第一个导航元素
    QString epub_type = html.attribute(QStringLiteral("epub:type"));
    if (epub_type.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).contains(QStringLiteral("nav"))) {
        return true;
    }
    if (html.attribute(QStringLiteral("role")) == QStringLiteral("doc-toc")) {
        return true;
    }
    bool found_nav = false;
    walkElements(html, [&found_nav](const QDomElement& element) {
        const QString name = localName(element);
        if (name == QStringLiteral("nav")) {
            found_nav = true;
        }
    });
    return found_nav;
}

QString metaViewportFixed(const QDomElement& html)
{
    QStringList found;
    walkElements(html, [&found](const QDomElement& element) {
        const QString name = localName(element);
        if ((name == QStringLiteral("meta")) &&
            (element.attribute(QStringLiteral("name")) == QStringLiteral("viewport")
             || element.attribute(QStringLiteral("name")) == QStringLiteral("-epub-viewport"))) {
            found.append(element.attribute(QStringLiteral("content")));
        }
    });
    if (found.isEmpty()) {
        return QString();
    }
    const QString content = found.first();
    // 固定版式 viewport：含 width= 与 height= 具体值
    if (content.contains(QStringLiteral("width="), Qt::CaseInsensitive) &&
        content.contains(QStringLiteral("height="), Qt::CaseInsensitive)) {
        return content;
    }
    return QString();
}

// 页内 CSS 风险子分（不含 XHTML 结构信号）
int cssRiskSubscore(const VerticalLayoutAnalyzer::CssAnalysis& css)
{
    int score = 0;
    if (css.hasFixedViewport) {
        score += 30;
    }
    if (css.hasTransformRotate) {
        score += 15;
    }
    if (css.hasAbsolutePositioning) {
        score += 10;
    }
    if (css.physicalSideUtilityCount > 0) {
        score += 10;
    }
    return score;
}

// 页内 XHTML 结构风险子分
int xhtmlRiskSubscore(const VerticalLayoutAnalyzer::XhtmlAnalysis& xhtml)
{
    int score = 0;
    if (xhtml.fixedViewport) {
        score += 30;
    }
    if (xhtml.absolutePositionCount >= 3) {
        score += 20;
    } else if (xhtml.absolutePositionCount > 0) {
        score += 10;
    }
    if (xhtml.hasSvgText && xhtml.hasVerticalWritingMode) {
        score += 20;
    }
    if (xhtml.hasVerticalWritingMode && xhtml.hasHorizontalWritingMode) {
        score += 15;
    }
    if (xhtml.transformRotateCount > 0) {
        score += 15;
    }
    if (xhtml.scriptCount > 0 && (xhtml.absolutePositionCount > 0 || xhtml.hasVerticalWritingMode)) {
        score += 10;
    }
    if (xhtml.isNavDocument) {
        score += 5;
    }
    return score;
}

} // namespace

// ---------------------------------------------------------------------
// CSS 分析
// ---------------------------------------------------------------------

VerticalLayoutAnalyzer::CssAnalysis VerticalLayoutAnalyzer::analyzeCss(const QString& css)
{
    CssAnalysis analysis;
    if (css.isEmpty()) {
        analysis.ok = true;
        return analysis;
    }

    CSSParser parser;
    parser.parse_css(css);
    analysis.parseErrorCount = parser.get_parse_errors().size();
    analysis.ok = (analysis.parseErrorCount == 0);

    QString current_property;
    while (true) {
        CSSParser::csstoken token = parser.get_next_token();
        if (token.type == TKN_CSS_END) {
            break;
        }
        switch (token.type) {
        case TKN_PROPERTY:
            current_property = token.data.trimmed().toLower();
            break;
        case TKN_PROPERTY_VALUE: {
            const QString value = token.data.trimmed().toLower();
            if (isWritingModeProperty(current_property)) {
                if (isVerticalWritingValue(value)) {
                    analysis.hasVerticalWritingMode = true;
                    analysis.verticalWritingModeCount++;
                } else if (isHorizontalWritingValue(value)) {
                    analysis.hasHorizontalWritingMode = true;
                }
            } else if (current_property == QStringLiteral("text-orientation")) {
                analysis.hasTextOrientation = true;
                if (value == QStringLiteral("upright") || value == QStringLiteral("sideways")) {
                    analysis.hasUpright = true;
                }
            } else if (current_property == QStringLiteral("text-combine-upright")
                       || current_property == QStringLiteral("text-combine-horizontal")
                       || current_property == QStringLiteral("-webkit-text-combine")) {
                analysis.hasTextCombine = true;
            } else if (current_property == QStringLiteral("font-feature-settings")
                       && (value.contains(QStringLiteral("vert")) || value.contains(QStringLiteral("vrt2")))) {
                analysis.hasVertFeature = true;
            } else if (current_property == QStringLiteral("position")
                       && value == QStringLiteral("absolute")) {
                analysis.hasAbsolutePositioning = true;
            } else if (current_property == QStringLiteral("transform")
                       && value.contains(QStringLiteral("rotate"))) {
                analysis.hasTransformRotate = true;
            }
            current_property.clear();
            break;
        }
        case TKN_AT_RULE_BEGIN:
        case TKN_AT_RULE_UNKNOWN:
            if (token.data.contains(QStringLiteral("viewport"), Qt::CaseInsensitive)) {
                analysis.hasFixedViewport = true;
            }
            break;
        default:
            break;
        }
    }

    const QSet<QString> classes = collectClassNamesFromCss(css);
    analysis.hasVrtlClass = classes.contains(QStringLiteral("vrtl"));
    analysis.hasHltrClass = classes.contains(QStringLiteral("hltr"));
    analysis.hasPairedVrtlHltr = analysis.hasVrtlClass && analysis.hasHltrClass;
    analysis.hasVerticalClass = classes.contains(QStringLiteral("vertical"));
    if (classes.contains(QStringLiteral("tcy"))) {
        analysis.hasTcy = true;
    }
    if (classes.contains(QStringLiteral("upright"))
        || classes.contains(QStringLiteral("upright-1"))
        || classes.contains(QStringLiteral("upright-2"))) {
        analysis.hasUpright = true;
    }
    for (const QString& class_name : classes) {
        if (isPhysicalSideUtilityClass(class_name)) {
            analysis.physicalSideUtilityCount++;
        }
    }

    if (analysis.hasFixedViewport) {
        analysis.reasons << QStringLiteral("CSS 声明了固定 viewport，属固定版式风险");
    }
    if (analysis.hasAbsolutePositioning) {
        analysis.reasons << QStringLiteral("CSS 出现 position:absolute");
    }
    if (analysis.hasTransformRotate) {
        analysis.reasons << QStringLiteral("CSS 出现 transform rotate");
    }
    if (analysis.physicalSideUtilityCount > 0) {
        analysis.reasons << QStringLiteral("CSS 出现 %1 个物理方向 utility class").arg(analysis.physicalSideUtilityCount);
    }
    analysis.riskScore = cssRiskSubscore(analysis);
    return analysis;
}

// ---------------------------------------------------------------------
// XHTML 分析
// ---------------------------------------------------------------------

VerticalLayoutAnalyzer::XhtmlAnalysis VerticalLayoutAnalyzer::analyzeXhtml(const QString& source)
{
    XhtmlAnalysis analysis;
    QDomDocument document;
    QString parse_error;
    int line = -1;
    int column = -1;
    if (!document.setContent(source, false, &parse_error, &line, &column)) {
        analysis.reasons << QStringLiteral("XML 解析失败：%1:%2 %3").arg(line).arg(column).arg(parse_error);
        analysis.pageKind = PageKind::ParseError;
        return analysis;
    }
    analysis.ok = true;

    const QDomElement html = document.documentElement();
    const QString html_class = html.attribute(QStringLiteral("class"));
    const QStringList html_classes = classTokens(html_class);
    analysis.htmlHasVrtlClass = html_classes.contains(QStringLiteral("vrtl"));
    analysis.htmlHasHltrClass = html_classes.contains(QStringLiteral("hltr"));
    analysis.hasV2hOverrideClass =
        html_classes.contains(QStringLiteral("se-v2h-horizontal"));
    analysis.hasH2vOverrideClass =
        html_classes.contains(QStringLiteral("se-h2v-vertical"));

    QDomElement body;
    walkElements(html, [&body](const QDomElement& element) {
        if (body.isNull() && localName(element) == QStringLiteral("body")) {
            body = element;
        }
    });
    if (!body.isNull()) {
        const QString body_class = body.attribute(QStringLiteral("class"));
        const QStringList body_classes = classTokens(body_class);
        analysis.bodyHasVrtlClass = body_classes.contains(QStringLiteral("vrtl"));
        analysis.bodyHasHltrClass = body_classes.contains(QStringLiteral("hltr"));
        analysis.hasV2hOverrideClass = analysis.hasV2hOverrideClass
            || body_classes.contains(QStringLiteral("se-v2h-horizontal"));
        analysis.hasH2vOverrideClass = analysis.hasH2vOverrideClass
            || body_classes.contains(QStringLiteral("se-h2v-vertical"));
    }

    // inline style：html / body 级（根级 writing-mode 判定）
    bool html_vertical = false, html_horizontal = false;
    bool body_vertical = false, body_horizontal = false;
    int root_absolute = 0, root_rotate = 0;
    detectStyleSignals(html.attribute(QStringLiteral("style")), html_vertical, html_horizontal,
                       root_absolute, root_rotate);
    if (!body.isNull()) {
        detectStyleSignals(body.attribute(QStringLiteral("style")), body_vertical, body_horizontal,
                           root_absolute, root_rotate);
    }
    analysis.absolutePositionCount += root_absolute;
    analysis.transformRotateCount += root_rotate;

    // 内联 <style> 块：追加扫描
    QString inline_style_text;
    walkElements(html, [&inline_style_text](const QDomElement& element) {
        if (localName(element) == QStringLiteral("style")) {
            inline_style_text += element.text() + QStringLiteral("\n");
        }
    });
    bool style_vertical = false, style_horizontal = false;
    int style_absolute = 0, style_rotate = 0;
    scanCssText(inline_style_text, style_vertical, style_horizontal, style_absolute, style_rotate);
    analysis.absolutePositionCount += style_absolute;
    analysis.transformRotateCount += style_rotate;

    // 元素级 inline style：position:absolute / transform rotate / 嵌套 writing-mode
    bool nested_vertical = false, nested_horizontal = false;
    walkElements(html, [&](const QDomElement& element) {
        const QString style = element.attribute(QStringLiteral("style"));
        if (style.isEmpty()) {
            return;
        }
        bool v = false, h = false;
        int abs_count = 0, rotate_count = 0;
        detectStyleSignals(style, v, h, abs_count, rotate_count);
        analysis.absolutePositionCount += abs_count;
        analysis.transformRotateCount += rotate_count;
        if (v) {
            nested_vertical = true;
        }
        if (h) {
            nested_horizontal = true;
        }
    });

    analysis.hasVerticalWritingMode = html_vertical || body_vertical || style_vertical || nested_vertical;
    analysis.hasHorizontalWritingMode = html_horizontal || body_horizontal || style_horizontal || nested_horizontal;
    if (html_vertical || body_vertical) {
        analysis.hasInlineVerticalStyle = true;
    }

    analysis.rubyCount = countElementsByName(html, QStringLiteral("ruby"))
        + countElementsByName(html, QStringLiteral("rt"))
        + countElementsByName(html, QStringLiteral("rp"));
    analysis.hasRuby = analysis.rubyCount > 0;
    analysis.tableCount = countElementsByName(html, QStringLiteral("table"));
    analysis.hasTable = analysis.tableCount > 0;
    analysis.imageCount = countElementsByName(html, QStringLiteral("img"))
        + countElementsByName(html, QStringLiteral("image"));
    analysis.hasImage = analysis.imageCount > 0;
    analysis.scriptCount = countElementsByName(html, QStringLiteral("script"));
    analysis.hasScript = analysis.scriptCount > 0;
    int svg_text_count = 0;
    walkElements(html, [&svg_text_count](const QDomElement& element) {
        if (localName(element) == QStringLiteral("text")) {
            // 只有位于 svg 内的 text 才算
            QDomNode ancestor = element.parentNode();
            while (!ancestor.isNull()) {
                if (ancestor.isElement() && localName(ancestor) == QStringLiteral("svg")) {
                    svg_text_count++;
                    break;
                }
                ancestor = ancestor.parentNode();
            }
        }
    });
    analysis.hasSvgText = svg_text_count > 0;
    if (countElementsByName(html, QStringLiteral("svg")) > 0) {
        analysis.hasSvg = true;
    }

    analysis.visibleTextLength = visibleTextLength(html);

    const QString viewport = metaViewportFixed(html);
    analysis.fixedViewport = !viewport.isEmpty();

    analysis.isNavDocument = isNavDocument(html);

    // linked stylesheets
    walkElements(html, [&analysis](const QDomElement& element) {
        const QString name = localName(element);
        if (name == QStringLiteral("link")) {
            const QString rel = element.attribute(QStringLiteral("rel")).toLower();
            if (rel.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).contains(QStringLiteral("stylesheet"))) {
                const QString href = element.attribute(QStringLiteral("href"));
                if (!href.isEmpty()) {
                    analysis.linkedStylesheets.append(href);
                }
            }
        }
    });

    // PageKind 分类
    if (analysis.fixedViewport) {
        analysis.pageKind = PageKind::FixedLayout;
    } else if (analysis.hasSvgText && analysis.hasVerticalWritingMode) {
        analysis.pageKind = PageKind::SvgTextLayout;
    } else if (analysis.isNavDocument) {
        analysis.pageKind = PageKind::TocOrNav;
    } else if (analysis.hasImage && analysis.visibleTextLength <= 2) {
        analysis.pageKind = PageKind::ImageOnly;
    } else if (analysis.hasScript && analysis.absolutePositionCount > 0) {
        analysis.pageKind = PageKind::ScriptDriven;
    } else if (analysis.hasVerticalWritingMode && analysis.hasHorizontalWritingMode) {
        analysis.pageKind = PageKind::MixedWritingMode;
    } else if (analysis.hasVerticalWritingMode) {
        const int risk = combinedRiskScore(CssAnalysis(), analysis, false);
        analysis.pageKind = (risk >= 50) ? PageKind::ReflowVerticalReview : PageKind::ReflowVerticalSafe;
    } else if (analysis.visibleTextLength > 0 && analysis.visibleTextLength < 400) {
        analysis.pageKind = PageKind::TitleOrColophon;
    } else {
        analysis.pageKind = PageKind::AlreadyHorizontal;
    }

    analysis.riskScore = combinedRiskScore(CssAnalysis(), analysis, false);
    return analysis;
}

// ---------------------------------------------------------------------
// OPF 分析
// ---------------------------------------------------------------------

VerticalLayoutAnalyzer::OpfAnalysis VerticalLayoutAnalyzer::analyzeOpf(const QString& opfSource)
{
    OpfAnalysis analysis;
    QDomDocument document;
    QString parse_error;
    int line = -1;
    int column = -1;
    if (!document.setContent(opfSource, false, &parse_error, &line, &column)) {
        analysis.reasons << QStringLiteral("OPF 解析失败：%1:%2 %3").arg(line).arg(column).arg(parse_error);
        return analysis;
    }
    analysis.ok = true;

    const QDomElement package = document.documentElement();
    if (localName(package) != QStringLiteral("package")) {
        analysis.reasons << QStringLiteral("根元素不是 package");
        return analysis;
    }
    analysis.epubVersion = package.attribute(QStringLiteral("version")).trimmed();

    // metadata
    QDomElement metadata;
    walkElements(package, [&metadata](const QDomElement& element) {
        if (metadata.isNull() && localName(element) == QStringLiteral("metadata")) {
            metadata = element;
        }
    });
    if (!metadata.isNull()) {
        walkElements(metadata, [&analysis](const QDomElement& element) {
            const QString name = unprefixedName(element);
            if (name == QStringLiteral("language")
                && (element.namespaceURI() == QStringLiteral("http://purl.org/dc/elements/1.1/")
                    || element.namespaceURI().isEmpty())) {
                const QString lang = element.text().trimmed();
                if (!lang.isEmpty() && !analysis.languages.contains(lang)) {
                    analysis.languages.append(lang);
                }
            } else if (name == QStringLiteral("meta")
                       && element.attribute(QStringLiteral("property")) == QStringLiteral("rendition:layout")) {
                QString layout = element.text().trimmed();
                if (layout.isEmpty()) {
                    layout = element.attribute(QStringLiteral("content")).trimmed();
                }
                analysis.renditionLayout = layout.toLower();
            }
        });
    }

    // manifest
    QHash<QString, QString> manifest_id_to_media_type;
    QHash<QString, QString> manifest_id_to_properties;
    walkElements(package, [&](const QDomElement& element) {
        if (localName(element) != QStringLiteral("manifest")) {
            return;
        }
        walkElements(element, [&](const QDomElement& item) {
            if (localName(item) != QStringLiteral("item")) {
                return;
            }
            const QString id = item.attribute(QStringLiteral("id"));
            const QString media_type = item.attribute(QStringLiteral("media-type")).toLower();
            if (!id.isEmpty()) {
                manifest_id_to_media_type.insert(id, media_type);
                manifest_id_to_properties.insert(id, item.attribute(QStringLiteral("properties")));
            }
        });
    });

    // spine
    const QString global_rendition = analysis.renditionLayout;
    const bool global_pre_paginated = (global_rendition == QStringLiteral("pre-paginated"));
    QDomElement spine;
    walkElements(package, [&spine](const QDomElement& element) {
        if (spine.isNull() && localName(element) == QStringLiteral("spine")) {
            spine = element;
        }
    });
    if (!spine.isNull()) {
        const QString progression = spine.attribute(QStringLiteral("page-progression-direction")).trimmed().toLower();
        analysis.pageProgression = progression.isEmpty() ? QStringLiteral("absent") : progression;
        walkElements(spine, [&](const QDomElement& itemref) {
            if (localName(itemref) != QStringLiteral("itemref")) {
                return;
            }
            analysis.spineItemCount++;
            const QString idref = itemref.attribute(QStringLiteral("idref"));
            const QString media_type = manifest_id_to_media_type.value(idref);
            const QString properties = manifest_id_to_properties.value(idref);
            const bool is_pre_paginated = global_pre_paginated
                || properties.contains(QStringLiteral("rendition:layout-pre-paginated"))
                || itemref.attribute(QStringLiteral("rendition:layout")) == QStringLiteral("pre-paginated");
            if (is_pre_paginated) {
                analysis.fixedLayoutCount++;
            }
            if (media_type.startsWith(QStringLiteral("image/"))) {
                analysis.imageOnlyCount++;
            }
            if (media_type == QStringLiteral("application/xhtml+xml")
                || media_type == QStringLiteral("application/xml")
                || media_type.endsWith(QStringLiteral("+xml"))
                || media_type.endsWith(QStringLiteral("html"))) {
                analysis.verticalCandidateCount++;
            }
        });
    }

    if (analysis.pageProgression == QStringLiteral("rtl")) {
        analysis.reasons << QStringLiteral("OPF spine 声明 rtl 阅读方向");
    }
    if (analysis.fixedLayoutCount > 0) {
        analysis.reasons << QStringLiteral("检测到 %1 个固定版式 itemref").arg(analysis.fixedLayoutCount);
    }
    return analysis;
}

// ---------------------------------------------------------------------
// 风险评分
// ---------------------------------------------------------------------

int VerticalLayoutAnalyzer::combinedRiskScore(const CssAnalysis& css,
                                              const XhtmlAnalysis& xhtml,
                                              bool profileHighConfidence)
{
    int score = xhtmlRiskSubscore(xhtml) + cssRiskSubscore(css);

    // 纯文本 reflow + 单一 root writing-mode 折扣
    if (xhtml.hasVerticalWritingMode
        && !xhtml.hasHorizontalWritingMode
        && xhtml.absolutePositionCount == 0
        && xhtml.transformRotateCount == 0
        && !xhtml.hasTable
        && !xhtml.hasSvg
        && xhtml.visibleTextLength >= 300) {
        score -= 10;
    }
    if (profileHighConfidence) {
        score -= 20;
    }
    return qBound(0, score, 100);
}

VerticalLayoutAnalyzer::WritingMode VerticalLayoutAnalyzer::effectiveWritingMode(
    const CssAnalysis& css,
    const XhtmlAnalysis& xhtml)
{
    // Compatibility overrides use !important and are intentionally stronger
    // than an original .vrtl/.hltr class left on the document root.
    if (xhtml.hasV2hOverrideClass && xhtml.hasH2vOverrideClass) {
        return WritingMode::Mixed;
    }
    if (xhtml.hasV2hOverrideClass) {
        return WritingMode::Horizontal;
    }
    if (xhtml.hasH2vOverrideClass) {
        return WritingMode::Vertical;
    }

    const bool class_vertical = xhtml.htmlHasVrtlClass || xhtml.bodyHasVrtlClass;
    const bool class_horizontal = xhtml.htmlHasHltrClass || xhtml.bodyHasHltrClass;

    if (class_vertical && class_horizontal) {
        return WritingMode::Mixed;
    }
    if (class_vertical) {
        return WritingMode::Vertical;
    }
    if (class_horizontal) {
        return WritingMode::Horizontal;
    }

    if (xhtml.hasVerticalWritingMode && xhtml.hasHorizontalWritingMode) {
        return WritingMode::Mixed;
    }
    if (xhtml.hasVerticalWritingMode) {
        return WritingMode::Vertical;
    }
    if (xhtml.hasHorizontalWritingMode) {
        return WritingMode::Horizontal;
    }

    // A shared profile stylesheet can contain both directions. Without a
    // matching root class, its effective direction cannot be inferred safely.
    if (css.hasPairedVrtlHltr) {
        return WritingMode::Unknown;
    }
    if (css.hasVerticalWritingMode && css.hasHorizontalWritingMode) {
        return WritingMode::Mixed;
    }
    if (css.hasVerticalWritingMode) {
        return WritingMode::Vertical;
    }
    if (css.hasHorizontalWritingMode) {
        return WritingMode::Horizontal;
    }

    // CSS writing-mode defaults to horizontal-tb.
    return WritingMode::Horizontal;
}

QString VerticalLayoutAnalyzer::pageKindName(PageKind pageKind)
{
    switch (pageKind) {
    case PageKind::ReflowVerticalSafe:
        return QStringLiteral("reflow-vertical-safe");
    case PageKind::ReflowVerticalReview:
        return QStringLiteral("reflow-vertical-review");
    case PageKind::AlreadyHorizontal:
        return QStringLiteral("already-horizontal");
    case PageKind::MixedWritingMode:
        return QStringLiteral("mixed-writing-mode");
    case PageKind::TocOrNav:
        return QStringLiteral("toc-or-nav");
    case PageKind::TitleOrColophon:
        return QStringLiteral("title-or-colophon");
    case PageKind::ImageOnly:
        return QStringLiteral("image-only");
    case PageKind::FixedLayout:
        return QStringLiteral("fixed-layout");
    case PageKind::SvgTextLayout:
        return QStringLiteral("svg-text-layout");
    case PageKind::ScriptDriven:
        return QStringLiteral("script-driven");
    case PageKind::ParseError:
        return QStringLiteral("parse-error");
    }
    return QStringLiteral("unknown");
}

QString VerticalLayoutAnalyzer::riskLevelName(int riskScore)
{
    if (riskScore < 25) {
        return QStringLiteral("auto-safe");
    }
    if (riskScore < 50) {
        return QStringLiteral("auto-safe-with-warnings");
    }
    if (riskScore < 75) {
        return QStringLiteral("manual-review");
    }
    return QStringLiteral("unsupported");
}

} // namespace BuiltinPlugins
