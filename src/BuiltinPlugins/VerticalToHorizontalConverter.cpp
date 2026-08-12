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

#include "BuiltinPlugins/VerticalToHorizontalConverter.h"

#include <QDir>
#include <QDomDocument>
#include <QDomElement>
#include <QRegularExpression>
#include <QWriteLocker>

#include "BookManipulation/Book.h"
#include "BookManipulation/FolderKeeper.h"
#include "ResourceObjects/CSSResource.h"
#include "ResourceObjects/HTMLResource.h"
#include "ResourceObjects/OPFResource.h"
#include "ResourceObjects/Resource.h"
#include "ResourceObjects/TextResource.h"

#include "BuiltinPlugins/VerticalProfileDetector.h"
#include "BuiltinPlugins/VerticalStylesheetResolver.h"
#include "Misc/Utility.h"

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

QString resolveRelativeHref(const QString& baseBookPath, QString href)
{
    const int fragment = href.indexOf(QLatin1Char('#'));
    if (fragment >= 0) {
        href = href.left(fragment);
    }
    const int query = href.indexOf(QLatin1Char('?'));
    if (query >= 0) {
        href = href.left(query);
    }
    static const QRegularExpression scheme_re(
        QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]*:"));
    if (href.isEmpty() || href.startsWith(QLatin1Char('/'))
        || scheme_re.match(href).hasMatch()) {
        return QString();
    }
    href = Utility::URLDecodePath(href);
    const int slash = baseBookPath.lastIndexOf(QLatin1Char('/'));
    const QString dir = (slash >= 0) ? baseBookPath.left(slash) : QString();
    QString full = QDir::cleanPath(dir.isEmpty() ? href : dir + QLatin1Char('/') + href);
    if (full.startsWith(QLatin1Char('/')) || full == QStringLiteral("..")
        || full.startsWith(QStringLiteral("../"))) {
        return QString();
    }
    return full;
}

VerticalLayoutAnalyzer::CssAnalysis mergeCssAnalyses(
    const QStringList& paths, const QHash<QString, QString>& css_by_path)
{
    VerticalLayoutAnalyzer::CssAnalysis merged;
    merged.ok = true;
    for (const QString& path : paths) {
        const VerticalLayoutAnalyzer::CssAnalysis item =
            VerticalLayoutAnalyzer::analyzeCss(css_by_path.value(path));
        merged.ok = merged.ok && item.ok;
        merged.parseErrorCount += item.parseErrorCount;
        merged.hasVerticalWritingMode = merged.hasVerticalWritingMode || item.hasVerticalWritingMode;
        merged.hasHorizontalWritingMode = merged.hasHorizontalWritingMode || item.hasHorizontalWritingMode;
        merged.verticalWritingModeCount += item.verticalWritingModeCount;
        merged.hasVrtlClass = merged.hasVrtlClass || item.hasVrtlClass;
        merged.hasHltrClass = merged.hasHltrClass || item.hasHltrClass;
        merged.hasVerticalClass = merged.hasVerticalClass || item.hasVerticalClass;
        merged.hasTcy = merged.hasTcy || item.hasTcy;
        merged.hasUpright = merged.hasUpright || item.hasUpright;
        merged.hasTextCombine = merged.hasTextCombine || item.hasTextCombine;
        merged.hasTextOrientation = merged.hasTextOrientation || item.hasTextOrientation;
        merged.hasVertFeature = merged.hasVertFeature || item.hasVertFeature;
        merged.hasAbsolutePositioning = merged.hasAbsolutePositioning || item.hasAbsolutePositioning;
        merged.hasTransformRotate = merged.hasTransformRotate || item.hasTransformRotate;
        merged.hasFixedViewport = merged.hasFixedViewport || item.hasFixedViewport;
        merged.physicalSideUtilityCount += item.physicalSideUtilityCount;
        merged.reasons.append(item.reasons);
    }
    merged.hasPairedVrtlHltr = merged.hasVrtlClass && merged.hasHltrClass;
    return merged;
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

QStringList sortedAttributes(const QDomNode& node, const QStringList& names)
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
        values << sortedAttributes(child, names);
    }
    values.sort();
    return values;
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

bool validateXhtmlInvariants(const QString& before, const QString& after, QString& error)
{
    QDomDocument before_doc;
    QDomDocument after_doc;
    if (!before_doc.setContent(before, false)) {
        error = QStringLiteral("转换前 XML 解析失败");
        return false;
    }
    if (!after_doc.setContent(after, false)) {
        error = QStringLiteral("转换后 XML 解析失败");
        return false;
    }
    if (semanticText(before_doc) != semanticText(after_doc)) {
        error = QStringLiteral("可见文本不一致");
        return false;
    }
    if (sortedAttributes(before_doc, QStringList() << QStringLiteral("id") << QStringLiteral("name")) !=
        sortedAttributes(after_doc, QStringList() << QStringLiteral("id") << QStringLiteral("name"))) {
        error = QStringLiteral("id/name 集合不一致");
        return false;
    }
    if (sortedAttributes(before_doc, QStringList() << QStringLiteral("href") << QStringLiteral("src")) !=
        sortedAttributes(after_doc, QStringList() << QStringLiteral("href") << QStringLiteral("src"))) {
        error = QStringLiteral("href/src 集合不一致");
        return false;
    }
    const QStringList ruby_elems = QStringList() << QStringLiteral("ruby") << QStringLiteral("rt") << QStringLiteral("rp");
    for (const QString& elem : ruby_elems) {
        if (countElementsByName(before_doc, elem) != countElementsByName(after_doc, elem)) {
            error = QStringLiteral("ruby/rt/rp 数量不一致");
            return false;
        }
    }
    if (countElementsByName(before_doc, QStringLiteral("img")) != countElementsByName(after_doc, QStringLiteral("img"))) {
        error = QStringLiteral("图片数量不一致");
        return false;
    }
    if (countElementsByName(before_doc, QStringLiteral("a")) != countElementsByName(after_doc, QStringLiteral("a"))) {
        error = QStringLiteral("<a> 数量不一致");
        return false;
    }
    return true;
}

bool isVerticalKind(VerticalLayoutAnalyzer::PageKind kind)
{
    switch (kind) {
    case VerticalLayoutAnalyzer::PageKind::ReflowVerticalSafe:
    case VerticalLayoutAnalyzer::PageKind::ReflowVerticalReview:
    case VerticalLayoutAnalyzer::PageKind::MixedWritingMode:
    case VerticalLayoutAnalyzer::PageKind::SvgTextLayout:
        return true;
    default:
        return false;
    }
}

bool isConvertibleKind(VerticalLayoutAnalyzer::PageKind kind, int riskScore)
{
    if (kind == VerticalLayoutAnalyzer::PageKind::ReflowVerticalSafe) {
        return true;
    }
    if (kind == VerticalLayoutAnalyzer::PageKind::ReflowVerticalReview) {
        return riskScore < 50;
    }
    return false;
}

} // namespace

// ---------------------------------------------------------------------
// 分析
// ---------------------------------------------------------------------

VerticalToHorizontalConverter::VerticalToHorizontalConverter(Book* book)
    : m_Book(book)
{
}

VerticalToHorizontalConverter::Analysis VerticalToHorizontalConverter::analyze(const Options& options) const
{
    Analysis analysis;
    if (!m_Book || !m_Book->GetFolderKeeper() || !m_Book->GetOPF()) {
        analysis.reasons << QStringLiteral("没有打开的 EPUB");
        return analysis;
    }

    const bool to_horizontal =
        options.direction == VerticalCssTransformer::ConversionDirection::VerticalToHorizontal;

    OPFResource* opf = m_Book->GetOPF();
    opf->InitialLoad();
    const QString opf_source = opf->GetText();
    const VerticalLayoutAnalyzer::OpfAnalysis opf_analysis = VerticalLayoutAnalyzer::analyzeOpf(opf_source);
    analysis.ok = opf_analysis.ok;
    analysis.epubVersion = opf_analysis.epubVersion;
    analysis.languages = opf_analysis.languages;
    analysis.pageProgression = opf_analysis.pageProgression;
    analysis.fixedLayoutBook = opf_analysis.spineItemCount > 0
        && opf_analysis.fixedLayoutCount >= opf_analysis.spineItemCount;
    analysis.hasFixedLayoutItems = opf_analysis.fixedLayoutCount > 0;
    for (const QString& href : opf_analysis.fixedLayoutHrefs) {
        const QString fixed_path = resolveRelativeHref(opf->GetRelativePath(), href);
        if (!fixed_path.isEmpty() && !analysis.fixedLayoutBookPaths.contains(fixed_path)) {
            analysis.fixedLayoutBookPaths.append(fixed_path);
        }
    }
    if (!analysis.ok) {
        analysis.reasons = opf_analysis.reasons;
        return analysis;
    }

    // CSS 文本缓存
    QHash<QString, QString> css_text_cache;
    const QList<CSSResource*> css_resources = m_Book->GetFolderKeeper()->GetResourceTypeList<CSSResource>(true);
    for (CSSResource* css : css_resources) {
        css->InitialLoad();
        css_text_cache.insert(css->GetRelativePath(), css->GetText());
    }

    const QStringList generators = generatorMetadata(opf_source);

    // Profile 判定（先于页面循环，使风险评分能应用 DPFJ/EBPAJ 折扣）
    QStringList css_names;
    QStringList css_texts;
    for (const QString& path : css_text_cache.keys()) {
        QString base = path;
        const int slash = base.lastIndexOf(QLatin1Char('/'));
        if (slash >= 0) {
            base = base.mid(slash + 1);
        }
        if (!css_names.contains(base)) {
            css_names.append(base);
        }
        css_texts.append(css_text_cache.value(path));
    }
    const VerticalProfileDetector::Detection profile =
        VerticalProfileDetector::detect(css_names, css_texts, generators);
    analysis.profileName = profile.profileName;
    analysis.profileConfidence = profile.confidence;
    analysis.canSwitchHltr = profile.canSwitchHltr;

    const QList<HTMLResource*> html_resources = m_Book->GetFolderKeeper()->GetResourceTypeList<HTMLResource>(true);
    // If the opposite direction left provenance on any selected page, this is
    // a reverse operation. Restrict it to pages changed by that earlier run so
    // originally-horizontal title pages (or originally-vertical subdocuments)
    // retain their original direction.
    for (HTMLResource* resource : html_resources) {
        resource->InitialLoad();
        const QString bookpath = resource->GetRelativePath();
        if (!options.selectedBookPaths.isEmpty() && !options.selectedBookPaths.contains(bookpath)) {
            continue;
        }
        const VerticalLayoutAnalyzer::XhtmlAnalysis xhtml =
            VerticalLayoutAnalyzer::analyzeXhtml(resource->GetText());
        const bool opposite_generated = to_horizontal
            ? (xhtml.hasH2vOverrideClass || xhtml.hasH2vConversionMarker)
            : (xhtml.hasV2hOverrideClass || xhtml.hasV2hConversionMarker);
        if (opposite_generated) {
            analysis.restoringGeneratedConversion = true;
            break;
        }
    }

    for (HTMLResource* resource : html_resources) {
        resource->InitialLoad();
        const QString bookpath = resource->GetRelativePath();
        if (!options.selectedBookPaths.isEmpty() && !options.selectedBookPaths.contains(bookpath)) {
            continue;
        }
        const PageContext context = analyzePage(resource, bookpath, css_text_cache, analysis, options);

        FileAnalysis file;
        file.bookpath = bookpath;
        file.kind = context.kind;
        file.riskScore = context.riskScore;
        file.profileConfidence = analysis.profileConfidence;
        file.profileName = analysis.profileName;
        file.reasons = context.reasons;
        file.writingMode = context.writingMode;
        file.generatedByV2h = context.xhtml.hasV2hOverrideClass
            || context.xhtml.hasV2hConversionMarker;
        file.generatedByH2v = context.xhtml.hasH2vOverrideClass
            || context.xhtml.hasH2vConversionMarker;
        file.generatedByV2hProfile = context.xhtml.hasV2hConversionMarker;
        file.generatedByH2vProfile = context.xhtml.hasH2vConversionMarker;
        file.canSwitchLayoutClass = analysis.canSwitchHltr
            && context.css.hasPairedVrtlHltr;
        if ((to_horizontal && file.generatedByH2vProfile)
            || (!to_horizontal && file.generatedByV2hProfile)) {
            file.canSwitchLayoutClass = true;
        }
        file.fixedLayoutFromOpf = analysis.fixedLayoutBookPaths.contains(bookpath);

        const bool in_conversion_scope = !analysis.restoringGeneratedConversion
            || (to_horizontal ? file.generatedByH2v : file.generatedByV2h);
        const bool nav_candidate = file.kind == PageKind::TocOrNav
            && file.riskScore < 50
            && (to_horizontal
                    ? file.writingMode == VerticalLayoutAnalyzer::WritingMode::Vertical
                    : file.writingMode == VerticalLayoutAnalyzer::WritingMode::Horizontal);

        if (context.writingMode == VerticalLayoutAnalyzer::WritingMode::Vertical) {
            analysis.verticalCount++;
        } else if (context.writingMode == VerticalLayoutAnalyzer::WritingMode::Horizontal) {
            analysis.horizontalCount++;
        } else if (context.writingMode == VerticalLayoutAnalyzer::WritingMode::Mixed
                   || context.writingMode == VerticalLayoutAnalyzer::WritingMode::Unknown) {
            // Count mixed or ambiguous pages in both totals so neither
            // direction is incorrectly reported as already converted.
            analysis.verticalCount++;
            analysis.horizontalCount++;
        }

        if (!in_conversion_scope) {
            analysis.skippedCount++;
            file.plannedChanges << QStringLiteral("不属于上次方向转换，跳过");
        } else if (to_horizontal) {
            if (isConvertibleKind(context.kind, context.riskScore) || nav_candidate) {
                analysis.safeCount++;
                file.plannedChanges << (nav_candidate
                    ? QStringLiteral("转换导航文档布局并保留链接")
                    : QStringLiteral("写入横排覆盖样式"));
            } else if (isVerticalKind(context.kind) && context.riskScore < 75) {
                analysis.reviewCount++;
                file.plannedChanges << QStringLiteral("人工复核（风险 %1）").arg(context.riskScore);
            } else if (context.writingMode == VerticalLayoutAnalyzer::WritingMode::Horizontal) {
                analysis.skippedCount++;
                file.plannedChanges << QStringLiteral("已是横排，跳过");
            } else {
                analysis.skippedCount++;
            }
        } else {
            const bool horizontal_candidate =
                context.kind == VerticalLayoutAnalyzer::PageKind::AlreadyHorizontal
                || context.kind == VerticalLayoutAnalyzer::PageKind::TitleOrColophon;
            if ((horizontal_candidate && context.riskScore < 50) || nav_candidate) {
                analysis.safeCount++;
                file.plannedChanges << (nav_candidate
                    ? QStringLiteral("转换导航文档布局并保留链接")
                    : QStringLiteral("写入竖排覆盖样式"));
            } else if (horizontal_candidate) {
                analysis.reviewCount++;
                file.plannedChanges << QStringLiteral("人工复核（风险 %1）").arg(context.riskScore);
            } else {
                analysis.skippedCount++;
                if (context.writingMode == VerticalLayoutAnalyzer::WritingMode::Vertical) {
                    file.plannedChanges << QStringLiteral("已是竖排，跳过");
                }
            }
        }
        analysis.files.append(file);
    }

    if (analysis.verticalCount > 0 && analysis.horizontalCount == 0) {
        analysis.detectedWritingMode = QStringLiteral("vertical-rl");
    } else if (analysis.horizontalCount > 0 && analysis.verticalCount == 0) {
        analysis.detectedWritingMode = QStringLiteral("horizontal-tb");
    } else if (analysis.verticalCount > 0) {
        analysis.detectedWritingMode = QStringLiteral("mixed");
    } else {
        analysis.detectedWritingMode = QStringLiteral("unknown");
    }

    analysis.reasons = opf_analysis.reasons;
    if (analysis.fixedLayoutBook) {
        analysis.reasons << QStringLiteral("整书为固定版式（pre-paginated），默认禁止自动重排");
    } else if (analysis.hasFixedLayoutItems) {
        analysis.reasons << QStringLiteral("书中含局部固定版式页面；这些页面不会转换，且不会自动修改全局翻页方向");
    }
    return analysis;
}

VerticalToHorizontalConverter::PageContext VerticalToHorizontalConverter::analyzePage(
    TextResource* resource,
    const QString& bookpath,
    const QHash<QString, QString>& css_text_cache,
    const Analysis& bookLevel,
    const Options& options) const
{
    PageContext context;
    const QString source = resource->GetText();
    context.xhtml = VerticalLayoutAnalyzer::analyzeXhtml(source);

    const QStringList linked = linkedStylesheetBookPaths(source, bookpath, css_text_cache);
    context.css = mergeCssAnalyses(linked, css_text_cache);

    context.writingMode =
        VerticalLayoutAnalyzer::effectiveWritingMode(context.css, context.xhtml);
    context.vertical = context.writingMode == VerticalLayoutAnalyzer::WritingMode::Vertical
        || context.writingMode == VerticalLayoutAnalyzer::WritingMode::Mixed;

    context.riskScore = VerticalLayoutAnalyzer::combinedRiskScore(
        context.css, context.xhtml,
        bookLevel.canSwitchHltr && context.css.hasPairedVrtlHltr);

    const bool blocked_by_important_inline =
        options.direction == VerticalCssTransformer::ConversionDirection::VerticalToHorizontal
            ? context.xhtml.hasImportantInlineVerticalStyle
            : context.xhtml.hasImportantRootHorizontalStyle;
    if (blocked_by_important_inline) {
        context.riskScore = qMax(50, context.riskScore);
        context.reasons << QStringLiteral("inline !important writing-mode 会覆盖转换样式，需人工复核");
    }

    classifyPage(context, bookLevel, bookpath);
    return context;
}

void VerticalToHorizontalConverter::classifyPage(PageContext& context,
                                                 const Analysis& bookLevel,
                                                 const QString& bookpath) const
{
    const VerticalLayoutAnalyzer::XhtmlAnalysis& x = context.xhtml;
    if (!x.ok) {
        context.kind = PageKind::ParseError;
        context.reasons = x.reasons;
        return;
    }
    if (bookLevel.fixedLayoutBook) {
        context.kind = PageKind::FixedLayout;
        context.reasons << QStringLiteral("整书固定版式，默认跳过");
        return;
    }
    if (bookLevel.fixedLayoutBookPaths.contains(bookpath)) {
        context.kind = PageKind::FixedLayout;
        context.reasons << QStringLiteral("OPF itemref 声明局部固定版式");
        return;
    }
    if (!context.css.ok) {
        context.kind = PageKind::ParseError;
        context.reasons << QStringLiteral("关联 CSS 解析失败，禁止自动改写");
        return;
    }
    if (x.inlineCssParseErrorCount > 0) {
        context.kind = PageKind::ParseError;
        context.reasons << QStringLiteral("页内 style CSS 解析失败，禁止自动改写");
        return;
    }
    if (x.fixedViewport) {
        context.kind = PageKind::FixedLayout;
        context.reasons << QStringLiteral("页面固定 viewport");
        return;
    }
    if (x.isNavDocument) {
        context.kind = PageKind::TocOrNav;
        context.reasons << QStringLiteral("nav/TOC 文档");
        return;
    }
    if (x.hasImage && x.visibleTextLength <= 2) {
        context.kind = PageKind::ImageOnly;
        context.reasons << QStringLiteral("图片页");
        return;
    }
    if (x.hasScript && x.absolutePositionCount > 0) {
        context.kind = PageKind::ScriptDriven;
        context.reasons << QStringLiteral("脚本注入布局");
        return;
    }
    if (x.hasSvgText) {
        context.kind = PageKind::SvgTextLayout;
        context.reasons << QStringLiteral("SVG 内含文本，默认跳过自动重排");
        return;
    }
    if (context.writingMode == VerticalLayoutAnalyzer::WritingMode::Unknown) {
        context.kind = PageKind::MixedWritingMode;
        context.reasons << QStringLiteral("共用样式表同时定义纵横排，但页面没有明确的根布局 class");
        return;
    }
    if (context.writingMode == VerticalLayoutAnalyzer::WritingMode::Mixed) {
        context.kind = PageKind::MixedWritingMode;
        context.reasons << QStringLiteral("纵横混排或方向信号冲突");
        return;
    }
    if (context.writingMode == VerticalLayoutAnalyzer::WritingMode::Horizontal) {
        if (x.visibleTextLength > 0 && x.visibleTextLength < 400) {
            context.kind = PageKind::TitleOrColophon;
            context.reasons << QStringLiteral("标题/版权页，非正文");
        } else {
            context.kind = PageKind::AlreadyHorizontal;
            context.reasons << QStringLiteral("已是横排");
        }
        return;
    }
    if (context.riskScore >= 75) {
        context.kind = PageKind::ReflowVerticalReview;
        context.reasons << QStringLiteral("风险 %1：%2").arg(context.riskScore)
                               .arg(VerticalLayoutAnalyzer::riskLevelName(context.riskScore));
        return;
    }
    if (context.riskScore >= 50) {
        context.kind = PageKind::ReflowVerticalReview;
        context.reasons << QStringLiteral("风险 %1：%2").arg(context.riskScore)
                               .arg(VerticalLayoutAnalyzer::riskLevelName(context.riskScore));
        return;
    }
    context.kind = PageKind::ReflowVerticalSafe;
    context.reasons << QStringLiteral("可自动转换（风险 %1）").arg(context.riskScore);
}

QStringList VerticalToHorizontalConverter::linkedStylesheetBookPaths(
    const QString& xhtmlSource,
    const QString& bookpath,
    const QHash<QString, QString>& css_by_path) const
{
    return VerticalStylesheetResolver::resolve(xhtmlSource, bookpath, css_by_path);
}

QStringList VerticalToHorizontalConverter::generatorMetadata(const QString& opfSource) const
{
    QStringList generators;
    QDomDocument document;
    if (!document.setContent(opfSource, false)) {
        return generators;
    }
    QList<QDomNode> nodes { document.documentElement() };
    while (!nodes.isEmpty()) {
        const QDomNode node = nodes.takeLast();
        if (!node.isElement() || localName(node) != QStringLiteral("meta")) {
            for (QDomNode child = node.firstChild(); !child.isNull(); child = child.nextSibling()) {
                nodes.append(child);
            }
            continue;
        }
        const QDomElement meta = node.toElement();
        const QString name = meta.attribute(QStringLiteral("name")).toLower();
        const QString property = meta.attribute(QStringLiteral("property")).toLower();
        if (name == QStringLiteral("generator") || property == QStringLiteral("generator")) {
            QString value = meta.attribute(QStringLiteral("content")).trimmed();
            if (value.isEmpty()) {
                value = meta.text().trimmed();
            }
            if (!value.isEmpty()) {
                generators.append(value);
            }
        }
        for (QDomNode child = node.firstChild(); !child.isNull(); child = child.nextSibling()) {
            nodes.append(child);
        }
    }
    return generators;
}

// ---------------------------------------------------------------------
// 转换
// ---------------------------------------------------------------------

VerticalToHorizontalConverter::Result VerticalToHorizontalConverter::convert(const Options& options)
{
    Result result;
    if (!m_Book || !m_Book->GetFolderKeeper() || !m_Book->GetOPF()) {
        addResult(result, ValidationResult::ResType_Error, QString(),
                  QStringLiteral("排版方向转换：没有打开的 EPUB。"));
        return result;
    }

    const bool to_horizontal =
        options.direction == VerticalCssTransformer::ConversionDirection::VerticalToHorizontal;
    const QString op_name = to_horizontal ? QStringLiteral("竖排转横排") : QStringLiteral("横排转竖排");

    result.before = analyze(options);
    if (!result.before.ok) {
        addResult(result, ValidationResult::ResType_Error, QString(),
                  QStringLiteral("%1：整书分析失败，未写回任何文件。").arg(op_name));
        return result;
    }
    if (result.before.fixedLayoutBook) {
        addResult(result, ValidationResult::ResType_Error, QString(),
                  QStringLiteral("%1：整书为固定版式（pre-paginated），禁止自动重排。").arg(op_name));
        result.ok = true;
        return result;
    }
    const bool already_target = to_horizontal
        ? (result.before.verticalCount == 0 && result.before.reviewCount == 0)
        : (result.before.horizontalCount == 0 && result.before.reviewCount == 0);
    if (already_target) {
        addResult(result, ValidationResult::ResType_Info, QString(),
                  to_horizontal
                      ? QStringLiteral("竖排转横排：书籍已是横排，无需转换。")
                      : QStringLiteral("横排转竖排：书籍已是竖排，无需转换。"));
        result.ok = true;
        return result;
    }

    struct PlannedPage {
        TextResource* resource = nullptr;
        QString bookpath;
        QString source;
        QString transformed;
    };
    QList<PlannedPage> pages;
    bool planning_failed = false;

    const bool restoring_generated = result.before.restoringGeneratedConversion;
    const auto isConvertible = [to_horizontal, restoring_generated](const FileAnalysis& file) {
        if (restoring_generated
            && !(to_horizontal ? file.generatedByH2v : file.generatedByV2h)) {
            return false;
        }
        if (to_horizontal) {
            return isConvertibleKind(file.kind, file.riskScore)
                || (file.kind == PageKind::TocOrNav
                    && file.writingMode == VerticalLayoutAnalyzer::WritingMode::Vertical
                    && file.riskScore < 50);
        }
        return ((file.kind == PageKind::AlreadyHorizontal
                 || file.kind == PageKind::TitleOrColophon)
                && file.riskScore < 50)
            || (file.kind == PageKind::TocOrNav
                && file.writingMode == VerticalLayoutAnalyzer::WritingMode::Horizontal
                && file.riskScore < 50);
    };

    for (const FileAnalysis& file : result.before.files) {
        if (!isConvertible(file)) {
            continue;
        }
        Resource* resource = m_Book->GetFolderKeeper()->GetResourceByBookPath(file.bookpath);
        TextResource* text_resource = qobject_cast<TextResource*>(resource);
        if (!text_resource) {
            continue;
        }
        text_resource->InitialLoad();
        const QString source = text_resource->GetText();
        const bool switch_target_class = file.canSwitchLayoutClass
            && options.mode == VerticalCssTransformer::ConversionMode::ProfileAwareRewrite;
        const VerticalCssTransformer::TransformResult transform =
            VerticalCssTransformer::transformXhtml(source, options, switch_target_class);
        if (!transform.ok) {
            addResult(result, ValidationResult::ResType_Error, file.bookpath,
                      QStringLiteral("%1：转换失败，未写回。%2").arg(op_name, transform.messages.join(QLatin1Char(' '))));
            planning_failed = true;
            continue;
        }
        if (options.validateAfterConversion) {
            QString error;
            if (!validateXhtmlInvariants(source, transform.text, error)) {
                addResult(result, ValidationResult::ResType_Error, file.bookpath,
                          QStringLiteral("%1：不变量校验失败（%2），未写回该文件。").arg(op_name, error));
                planning_failed = true;
                continue;
            }
        }
        if (transform.changed) {
            PlannedPage page;
            page.resource = text_resource;
            page.bookpath = file.bookpath;
            page.source = source;
            page.transformed = transform.text;
            pages.append(page);
        }
    }

    if (planning_failed) {
        addResult(result, ValidationResult::ResType_Error, QString(),
                  QStringLiteral("%1：预检失败，未写回任何文件。").arg(op_name));
        return result;
    }

    if (pages.isEmpty()) {
        addResult(result, ValidationResult::ResType_Info, QString(),
                  QStringLiteral("%1：没有可自动转换的正文页（其余页面需人工复核或已跳过）。").arg(op_name));
        result.ok = true;
        return result;
    }

    // Unknown/unpaired stylesheets are never rewritten by the book-level
    // workflow. Structured mode switches a proven .vrtl/.hltr page class;
    // every other page falls back to the reversible compatibility override.
    // transformCss() remains available as a pure lower-level primitive, but
    // destructive generic CSS cleanup is not safe for automatic round trips.

    // OPF page progression（V2H -> ltr，H2V -> rtl）
    OPFResource* opf = m_Book->GetOPF();
    opf->InitialLoad();
    const QString opf_source = opf->GetText();
    QString opf_text = opf_source;
    bool opf_changed = false;
    const bool restoring_tracked_progression = result.before.restoringGeneratedConversion
        && opf_source.contains(QStringLiteral("sigil-enhanced-layout-progression"));
    const bool can_update_global_progression = options.selectedBookPaths.isEmpty()
        && (!result.before.hasFixedLayoutItems || restoring_tracked_progression);
    if (options.updatePageProgression && can_update_global_progression) {
        const VerticalCssTransformer::TransformResult transform =
            VerticalCssTransformer::transformOpfProgression(opf_text, to_horizontal);
        if (!transform.ok) {
            addResult(result, ValidationResult::ResType_Error, opf->GetRelativePath(),
                      QStringLiteral("%1：%2").arg(op_name, transform.messages.join(QLatin1Char(' '))));
            return result;
        }
        if (transform.changed) {
            opf_text = transform.text;
            opf_changed = true;
        }
    } else if (options.updatePageProgression && result.before.hasFixedLayoutItems) {
        addResult(result, ValidationResult::ResType_Warn, opf->GetRelativePath(),
                  QStringLiteral("%1：书中含局部固定版式页面，已保留全局 page-progression-direction。")
                      .arg(op_name));
    } else if (options.updatePageProgression && !options.selectedBookPaths.isEmpty()) {
        addResult(result, ValidationResult::ResType_Info, opf->GetRelativePath(),
                  QStringLiteral("%1：仅转换部分文件，已保留全局 page-progression-direction。")
                      .arg(op_name));
    }

    // stale-source 校验
    for (const PlannedPage& page : pages) {
        if (page.resource->GetText() != page.source) {
            addResult(result, ValidationResult::ResType_Error, page.bookpath,
                      QStringLiteral("%1：%2 在分析后被修改，未写回任何文件。").arg(op_name, page.bookpath));
            return result;
        }
    }
    if (opf_changed && opf->GetText() != opf_source) {
        addResult(result, ValidationResult::ResType_Error, opf->GetRelativePath(),
                  QStringLiteral("%1：OPF 在分析后被修改，未写回任何文件。").arg(op_name));
        return result;
    }

    // 写回（调用方已先创建 Checkpoint；dryRun 时只报告计划）
    int changed_resources = 0;
    int skipped = 0;
    int warnings = 0;
    const QString target_label = to_horizontal ? QStringLiteral("横排") : QStringLiteral("竖排");
    const QString prefix = options.dryRun ? QStringLiteral("%1（dry-run）：").arg(op_name) : QStringLiteral("%1：").arg(op_name);
    for (const PlannedPage& page : pages) {
        if (!options.dryRun) {
            QWriteLocker locker(&page.resource->GetLock());
            page.resource->SetTextAsUndoableEdit(page.transformed);
            result.modified = true;
        }
        result.changedBookPaths.append(page.bookpath);
        changed_resources++;
        addResult(result, ValidationResult::ResType_Info, page.bookpath,
                  QStringLiteral("%1将%2。").arg(prefix, target_label));
    }
    if (opf_changed) {
        if (!options.dryRun) {
            QWriteLocker locker(&opf->GetLock());
            opf->SetTextAsUndoableEdit(opf_text);
            result.modified = true;
        }
        result.changedBookPaths.append(opf->GetRelativePath());
        changed_resources++;
        addResult(result, ValidationResult::ResType_Info, opf->GetRelativePath(),
                  QStringLiteral("%1将 page-progression-direction 改为 %2。")
                      .arg(prefix, to_horizontal ? QStringLiteral("ltr") : QStringLiteral("rtl")));
    }

    for (const FileAnalysis& file : result.before.files) {
        if (restoring_generated
            && !(to_horizontal ? file.generatedByH2v : file.generatedByV2h)) {
            continue;
        }
        const bool needs_review = to_horizontal
            ? (isVerticalKind(file.kind) && file.riskScore >= 50)
            : ((file.kind == PageKind::AlreadyHorizontal || file.kind == PageKind::TitleOrColophon)
               && file.riskScore >= 50);
        if (needs_review) {
            warnings++;
            addResult(result, ValidationResult::ResType_Warn, file.bookpath,
                      QStringLiteral("%1：需人工复核（风险 %2）。").arg(op_name).arg(file.riskScore));
        } else if (file.kind == PageKind::FixedLayout || file.kind == PageKind::ImageOnly
                   || file.kind == PageKind::ParseError) {
            skipped++;
            addResult(result, ValidationResult::ResType_Info, file.bookpath,
                      QStringLiteral("%1：已跳过（%2）。").arg(op_name, VerticalLayoutAnalyzer::pageKindName(file.kind)));
        }
    }

    if (result.modified && !options.dryRun) {
        m_Book->SetModified();
    }
    result.bookBrowserRefreshRequired = result.modified;
    result.after = analyze(options);
    result.ok = true;
    addResult(result, ValidationResult::ResType_Info, QString(),
              options.dryRun
                  ? QStringLiteral("%1（dry-run）：计划修改 %2 个文件，%3 个跳过，%4 个警告。")
                        .arg(op_name).arg(changed_resources).arg(skipped).arg(warnings)
                  : QStringLiteral("%1完成：%2 个文件已修改，%3 个跳过，%4 个警告。")
                        .arg(op_name).arg(changed_resources).arg(skipped).arg(warnings));
    return result;
}

// ---------------------------------------------------------------------
// 结果辅助
// ---------------------------------------------------------------------

void VerticalToHorizontalConverter::addResult(Result& result,
                                              ValidationResult::ResType type,
                                              const QString& bookpath,
                                              const QString& message) const
{
    result.validationResults.append(ValidationResult(type, bookpath, 0, 0, message));
}

void VerticalToHorizontalConverter::addResult(Result& result,
                                              ValidationResult::ResType type,
                                              const QString& bookpath,
                                              int line,
                                              int charoffset,
                                              const QString& message) const
{
    result.validationResults.append(ValidationResult(type, bookpath, line, charoffset, message));
}

} // namespace BuiltinPlugins
