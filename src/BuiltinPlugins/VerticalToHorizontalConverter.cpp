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
#include <QSet>
#include <QWriteLocker>

#include "BookManipulation/Book.h"
#include "BookManipulation/FolderKeeper.h"
#include "ResourceObjects/CSSResource.h"
#include "ResourceObjects/HTMLResource.h"
#include "ResourceObjects/OPFResource.h"
#include "ResourceObjects/Resource.h"
#include "ResourceObjects/TextResource.h"

#include "BuiltinPlugins/VerticalProfileDetector.h"

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
    if (href.isEmpty() || href.startsWith(QLatin1Char('/'))) {
        return QString();
    }
    if (href.startsWith(QStringLiteral("http:"), Qt::CaseInsensitive)
        || href.startsWith(QStringLiteral("https:"), Qt::CaseInsensitive)
        || href.startsWith(QStringLiteral("data:"), Qt::CaseInsensitive)
        || href.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive)) {
        return QString();
    }
    const int slash = baseBookPath.lastIndexOf(QLatin1Char('/'));
    const QString dir = (slash >= 0) ? baseBookPath.left(slash) : QString();
    QString full = QDir::cleanPath(dir.isEmpty() ? href : dir + QLatin1Char('/') + href);
    if (full.startsWith(QLatin1Char('/')) || full.startsWith(QStringLiteral("../"))) {
        return QString();
    }
    return full;
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
    analysis.fixedLayoutBook = (opf_analysis.renditionLayout == QStringLiteral("pre-paginated"))
        || (opf_analysis.spineItemCount > 0
            && opf_analysis.fixedLayoutCount >= opf_analysis.spineItemCount);

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
        const PageContext context = analyzePage(resource, bookpath, css_text_cache, analysis);

        FileAnalysis file;
        file.bookpath = bookpath;
        file.kind = context.kind;
        file.riskScore = context.riskScore;
        file.profileConfidence = analysis.profileConfidence;
        file.profileName = analysis.profileName;
        file.reasons = context.reasons;
        file.generatedByV2h = context.xhtml.hasV2hOverrideClass
            || context.xhtml.hasV2hConversionMarker;
        file.generatedByH2v = context.xhtml.hasH2vOverrideClass
            || context.xhtml.hasH2vConversionMarker;

        const bool in_conversion_scope = !analysis.restoringGeneratedConversion
            || (to_horizontal ? file.generatedByH2v : file.generatedByV2h);

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
            if (isConvertibleKind(context.kind, context.riskScore)) {
                analysis.safeCount++;
                file.plannedChanges << QStringLiteral("写入横排覆盖样式");
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
            if (horizontal_candidate && context.riskScore < 50) {
                analysis.safeCount++;
                file.plannedChanges << QStringLiteral("写入竖排覆盖样式");
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
    }
    return analysis;
}

VerticalToHorizontalConverter::PageContext VerticalToHorizontalConverter::analyzePage(
    TextResource* resource,
    const QString& bookpath,
    const QHash<QString, QString>& css_text_cache,
    const Analysis& bookLevel) const
{
    PageContext context;
    const QString source = resource->GetText();
    context.xhtml = VerticalLayoutAnalyzer::analyzeXhtml(source);

    QStringList css_texts;
    const QStringList linked = linkedStylesheetBookPaths(source, bookpath, css_text_cache);
    for (const QString& css_path : linked) {
        if (css_text_cache.contains(css_path)) {
            css_texts.append(css_text_cache.value(css_path));
        }
    }
    QString joined;
    for (const QString& text : css_texts) {
        joined += text + QStringLiteral("\n");
    }
    context.css = VerticalLayoutAnalyzer::analyzeCss(joined);

    context.writingMode =
        VerticalLayoutAnalyzer::effectiveWritingMode(context.css, context.xhtml);
    context.vertical = context.writingMode == VerticalLayoutAnalyzer::WritingMode::Vertical
        || context.writingMode == VerticalLayoutAnalyzer::WritingMode::Mixed;

    context.riskScore = VerticalLayoutAnalyzer::combinedRiskScore(
        context.css, context.xhtml, bookLevel.canSwitchHltr);

    classifyPage(context, bookLevel);
    return context;
}

void VerticalToHorizontalConverter::classifyPage(PageContext& context,
                                                 const Analysis& bookLevel) const
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
    if (x.hasSvgText && context.vertical) {
        context.kind = PageKind::SvgTextLayout;
        context.reasons << QStringLiteral("SVG 内含纵排文本");
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
    QStringList result;
    QDomDocument document;
    if (!document.setContent(xhtmlSource, false)) {
        return result;
    }
    const QDomNodeList links = document.elementsByTagName(QStringLiteral("link"));
    for (int i = 0; i < links.count(); ++i) {
        QDomElement link = links.at(i).toElement();
        const QString rel = link.attribute(QStringLiteral("rel")).toLower();
        if (!rel.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).contains(QStringLiteral("stylesheet"))) {
            continue;
        }
        const QString resolved = resolveRelativeHref(bookpath, link.attribute(QStringLiteral("href")));
        if (!resolved.isEmpty() && css_by_path.contains(resolved) && !result.contains(resolved)) {
            result.append(resolved);
        }
    }
    return result;
}

QStringList VerticalToHorizontalConverter::generatorMetadata(const QString& opfSource) const
{
    QStringList generators;
    QDomDocument document;
    if (!document.setContent(opfSource, false)) {
        return generators;
    }
    const QDomNodeList metas = document.elementsByTagName(QStringLiteral("meta"));
    for (int i = 0; i < metas.count(); ++i) {
        QDomElement meta = metas.at(i).toElement();
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

    const bool switch_target_class = result.before.canSwitchHltr
        && options.mode == VerticalCssTransformer::ConversionMode::ProfileAwareRewrite;

    struct PlannedPage {
        TextResource* resource = nullptr;
        QString bookpath;
        QString source;
        QString transformed;
    };
    QList<PlannedPage> pages;

    const bool restoring_generated = result.before.restoringGeneratedConversion;
    const auto isConvertible = [to_horizontal, restoring_generated](const FileAnalysis& file) {
        if (restoring_generated
            && !(to_horizontal ? file.generatedByH2v : file.generatedByV2h)) {
            return false;
        }
        if (to_horizontal) {
            return isConvertibleKind(file.kind, file.riskScore);
        }
        return (file.kind == PageKind::AlreadyHorizontal
                || file.kind == PageKind::TitleOrColophon)
            && file.riskScore < 50;
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
        const VerticalCssTransformer::TransformResult transform =
            VerticalCssTransformer::transformXhtml(source, options, switch_target_class);
        if (!transform.ok) {
            addResult(result, ValidationResult::ResType_Error, file.bookpath,
                      QStringLiteral("%1：转换失败，未写回。%2").arg(op_name, transform.messages.join(QLatin1Char(' '))));
            continue;
        }
        if (options.validateAfterConversion) {
            QString error;
            if (!validateXhtmlInvariants(source, transform.text, error)) {
                addResult(result, ValidationResult::ResType_Error, file.bookpath,
                          QStringLiteral("%1：不变量校验失败（%2），未写回该文件。").arg(op_name, error));
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

    if (pages.isEmpty()) {
        addResult(result, ValidationResult::ResType_Info, QString(),
                  QStringLiteral("%1：没有可自动转换的正文页（其余页面需人工复核或已跳过）。").arg(op_name));
        result.ok = true;
        return result;
    }

    // 结构化模式：改写被转换页面引用的 CSS
    QHash<QString, QString> css_text_cache;
    const QList<CSSResource*> css_resources = m_Book->GetFolderKeeper()->GetResourceTypeList<CSSResource>(true);
    for (CSSResource* css : css_resources) {
        css->InitialLoad();
        css_text_cache.insert(css->GetRelativePath(), css->GetText());
    }
    QList<QPair<CSSResource*, QString>> css_changes; // resource + new text
    if (options.mode == VerticalCssTransformer::ConversionMode::ProfileAwareRewrite
        && !switch_target_class) {
        QSet<QString> linked_css_paths;
        for (const PlannedPage& page : pages) {
            const QStringList linked = linkedStylesheetBookPaths(page.source, page.bookpath, css_text_cache);
            for (const QString& css_path : linked) {
                linked_css_paths.insert(css_path);
            }
        }
        for (const QString& css_path : linked_css_paths) {
            Resource* resource = m_Book->GetFolderKeeper()->GetResourceByBookPath(css_path);
            CSSResource* css_resource = qobject_cast<CSSResource*>(resource);
            if (!css_resource) {
                continue;
            }
            const QString css_text = css_resource->GetText();
            const VerticalCssTransformer::TransformResult transform =
                VerticalCssTransformer::transformCss(css_text, options);
            if (transform.ok && transform.changed) {
                css_changes.append(qMakePair(css_resource, transform.text));
            }
        }
    }

    // OPF page progression（V2H -> ltr，H2V -> rtl）
    OPFResource* opf = m_Book->GetOPF();
    opf->InitialLoad();
    const QString opf_source = opf->GetText();
    QString opf_text = opf_source;
    bool opf_changed = false;
    if (options.updatePageProgression) {
        const VerticalCssTransformer::TransformResult transform =
            VerticalCssTransformer::transformOpfProgression(opf_text, to_horizontal);
        if (transform.ok && transform.changed) {
            opf_text = transform.text;
            opf_changed = true;
        }
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
    for (const QPair<CSSResource*, QString>& change : css_changes) {
        if (change.first->GetText() != css_text_cache.value(change.first->GetRelativePath())) {
            addResult(result, ValidationResult::ResType_Error, change.first->GetRelativePath(),
                      QStringLiteral("%1：CSS 在分析后被修改，未写回任何文件。").arg(op_name));
            return result;
        }
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
    for (const QPair<CSSResource*, QString>& change : css_changes) {
        if (!options.dryRun) {
            QWriteLocker locker(&change.first->GetLock());
            change.first->SetTextAsUndoableEdit(change.second);
            result.modified = true;
        }
        result.changedBookPaths.append(change.first->GetRelativePath());
        changed_resources++;
        addResult(result, ValidationResult::ResType_Info, change.first->GetRelativePath(),
                  to_horizontal
                      ? QStringLiteral("%1将中和纵向专属 CSS 声明。").arg(prefix)
                      : QStringLiteral("%1将改写 writing-mode 为纵向。").arg(prefix));
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
