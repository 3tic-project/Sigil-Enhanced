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

#include "BuiltinPlugins/CmoaParagraphNormalizer.h"

#include <algorithm>

#include <QCoreApplication>
#include <QDomDocument>
#include <QRegularExpression>
#include <QSet>

namespace BuiltinPlugins
{

namespace
{

QString trCmoa(const char* source)
{
    return QCoreApplication::translate("CmoaParagraphNormalizer", source);
}

QString localName(const QDomElement& element)
{
    const QString local_name = element.localName();
    return (local_name.isEmpty() ? element.tagName() : local_name).toLower();
}

QDomElement firstElement(const QDomNode& root, const QString& name)
{
    if (root.isElement()) {
        const QDomElement element = root.toElement();
        if (localName(element) == name) {
            return element;
        }
    }
    for (QDomNode child = root.firstChild(); !child.isNull(); child = child.nextSibling()) {
        const QDomElement found = firstElement(child, name);
        if (!found.isNull()) {
            return found;
        }
    }
    return QDomElement();
}

bool hasClass(const QDomElement& element, const QString& name)
{
    return element.attribute(QStringLiteral("class"))
        .split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts)
        .contains(name);
}

bool hasDescendantClass(const QDomNode& root, const QString& name)
{
    if (root.isElement() && hasClass(root.toElement(), name)) {
        return true;
    }
    for (QDomNode child = root.firstChild(); !child.isNull(); child = child.nextSibling()) {
        if (hasDescendantClass(child, name)) {
            return true;
        }
    }
    return false;
}

QString compactSelector(QString selector)
{
    selector = selector.toLower();
    selector.remove(QRegularExpression(QStringLiteral("\\s+")));
    return selector;
}

bool hasCmoaIndentReset(const QVector<DivParagraphCssAnalyzer::Source>& stylesheets)
{
    QString css;
    for (const DivParagraphCssAnalyzer::Source& stylesheet : stylesheets) {
        if (!stylesheet.available) {
            return false;
        }
        css += stylesheet.text;
        css += QLatin1Char('\n');
    }
    css.remove(QRegularExpression(
        QStringLiteral("/\\*.*?\\*/"),
        QRegularExpression::DotMatchesEverythingOption));

    const QRegularExpression reset(
        QStringLiteral("body\\s*,\\s*div\\s*,\\s*p\\s*\\{[^}]*"
                       "text-indent\\s*:\\s*0(?:\\.0+)?(?:em|px|rem|%)?\\s*;?[^}]*\\}"),
        QRegularExpression::CaseInsensitiveOption |
            QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpression inherited_children(
        QStringLiteral("body\\s*>\\s*p\\s*,\\s*div\\s*>\\s*p\\s*\\{[^}]*"
                       "text-indent\\s*:\\s*inherit\\s*;?[^}]*\\}"),
        QRegularExpression::CaseInsensitiveOption |
            QRegularExpression::DotMatchesEverythingOption);
    return reset.match(css).hasMatch() && inherited_children.match(css).hasMatch();
}

bool dependencyIsProvenIrrelevant(
    const DivParagraphCssAnalyzer::Dependency& dependency,
    bool title_page,
    bool has_indent_reset)
{
    if (dependency.selector.isEmpty()) {
        return false;
    }
    const QString selector = compactSelector(dependency.selector);
    if (!title_page && selector.contains(QStringLiteral(".p-titlepage"))) {
        return true;
    }
    if (has_indent_reset &&
        (selector == QLatin1String("body>p") || selector == QLatin1String("div>p"))) {
        return true;
    }
    return false;
}

bool isCmoaDocument(const QString& source, bool& title_page)
{
    QDomDocument document;
    QString error;
    int line = -1;
    int column = -1;
    if (!document.setContent(source, false, &error, &line, &column)) {
        return false;
    }
    const QDomElement html = firstElement(document, QStringLiteral("html"));
    const QDomElement body = firstElement(document, QStringLiteral("body"));
    if (html.isNull() || body.isNull()) {
        return false;
    }
    const bool reflow_orientation = hasClass(html, QStringLiteral("vrtl")) ||
                                    hasClass(html, QStringLiteral("hltr"));
    title_page = hasClass(body, QStringLiteral("p-titlepage"));
    return reflow_orientation && hasDescendantClass(body, QStringLiteral("main"));
}

}

CmoaParagraphNormalizer::Options CmoaParagraphNormalizer::defaultOptions()
{
    return Options::conservative();
}

QString CmoaParagraphNormalizer::presetId(const Options& options)
{
    const Options defaults = defaultOptions();
    if (options.convertParagraphs == defaults.convertParagraphs &&
        options.convertSpacerBr == defaults.convertSpacerBr &&
        options.convertSceneBreaks == defaults.convertSceneBreaks &&
        options.convertImageWrappers == defaults.convertImageWrappers &&
        options.convertSingleBlockWrappers == defaults.convertSingleBlockWrappers &&
        !options.addLegacyStyleCompensation) {
        return QStringLiteral("cmoa-conservative-v1");
    }
    return QStringLiteral("cmoa-custom-v1:%1%2%3%4%5")
        .arg(options.convertParagraphs ? 1 : 0)
        .arg(options.convertSpacerBr ? 1 : 0)
        .arg(options.convertSceneBreaks ? 1 : 0)
        .arg(options.convertImageWrappers ? 1 : 0)
        .arg(options.convertSingleBlockWrappers ? 1 : 0);
}

CmoaParagraphNormalizer::Analysis CmoaParagraphNormalizer::analyzeXhtmlText(
    const QString& source,
    const Options& options,
    const QVector<DivParagraphCssAnalyzer::Source>& stylesheets)
{
    // Ask the shared engine for structural classification without changing the
    // BookLive defaults.  The temporary compatibility flag only bypasses its
    // generic CSS gate during analysis; Cmoa CSS is checked independently below.
    Options structural_options = options;
    structural_options.addLegacyStyleCompensation = true;
    Analysis analysis = BookLiveParagraphNormalizer::analyzeXhtmlText(
        source, structural_options, QVector<DivParagraphCssAnalyzer::Source>());
    analysis.presetId = presetId(options);
    analysis.ruleVersion = QStringLiteral("cmoa-paragraph-v1");

    bool title_page = false;
    if (!isCmoaDocument(source, title_page)) {
        analysis.safeToNormalize = false;
        analysis.candidate = analysis.convertibleLeaves > 0;
        analysis.reason = QStringLiteral("not a recognized Cmoa/EBPAJ reflow document");
        analysis.message = trCmoa(
            QT_TRANSLATE_NOOP(
                "CmoaParagraphNormalizer",
                "Cmoa paragraph analysis: no vrtl/hltr and main-container profile was found; skipped."));
        return analysis;
    }

    if (!analysis.safeToNormalize) {
        analysis.message = trCmoa(
            QT_TRANSLATE_NOOP(
                "CmoaParagraphNormalizer",
                "Cmoa paragraph analysis: manual review required (%1); candidate DIVs: %2."))
            .arg(analysis.reason)
            .arg(analysis.convertibleLeaves);
        return analysis;
    }

    const Analysis css_analysis = BookLiveParagraphNormalizer::analyzeXhtmlText(
        source, options, stylesheets);
    analysis.cssDependencies = css_analysis.cssDependencies;
    const bool has_indent_reset = hasCmoaIndentReset(stylesheets);
    const bool margin_parity = !css_analysis.cssDependencies.isEmpty()
        ? !std::any_of(css_analysis.cssDependencies.cbegin(),
                      css_analysis.cssDependencies.cend(),
                      [title_page, has_indent_reset](
                          const DivParagraphCssAnalyzer::Dependency& dependency) {
                          return !dependencyIsProvenIrrelevant(
                              dependency, title_page, has_indent_reset);
                      })
        : true;

    if (!margin_parity) {
        analysis.safeToNormalize = false;
        analysis.pageKind = BookLiveParagraphNormalizer::PageKind::CssRisk;
        analysis.cssReviewRequired = true;
        analysis.reason = QStringLiteral("unrecognized Cmoa CSS dependency requires review");
        analysis.message = trCmoa(
            QT_TRANSLATE_NOOP(
                "CmoaParagraphNormalizer",
                "Cmoa paragraph analysis: CSS is outside the verified EBPAJ reset profile; "
                "%1 candidates are review-only."))
            .arg(analysis.convertibleLeaves);
        return analysis;
    }

    analysis.cssReviewRequired = false;
    analysis.cssDependencies.clear();
    analysis.message = trCmoa(
        QT_TRANSLATE_NOOP(
            "CmoaParagraphNormalizer",
            "Cmoa paragraph analysis: %1 body DIVs are safe to convert; headings, Ruby, "
            "blank lines, and source formatting are preserved."))
        .arg(analysis.convertibleLeaves);
    return analysis;
}

CmoaParagraphNormalizer::NormalizeResult CmoaParagraphNormalizer::normalizeXhtmlText(
    const QString& source,
    const Options& options,
    const QVector<DivParagraphCssAnalyzer::Source>& stylesheets)
{
    NormalizeResult result;
    result.before = analyzeXhtmlText(source, options, stylesheets);
    if (!result.before.ok) {
        result.messages << result.before.message;
        return result;
    }
    if (!result.before.candidate) {
        result.ok = true;
        result.text = source;
        result.after = result.before;
        result.afterHash = result.before.beforeHash;
        result.messages << trCmoa(
            QT_TRANSLATE_NOOP(
                "CmoaParagraphNormalizer",
                "Cmoa paragraph normalization: this file has no convertible items."));
        return result;
    }
    if (!result.before.safeToNormalize) {
        result.messages << result.before.message;
        return result;
    }

    NormalizeResult transformed = BookLiveParagraphNormalizer::normalizeXhtmlText(
        source, options, stylesheets, true);
    transformed.before = result.before;
    if (transformed.ok && transformed.changed) {
        transformed.after = analyzeXhtmlText(transformed.text, options, stylesheets);
        transformed.messages.prepend(trCmoa(
            QT_TRANSLATE_NOOP(
                "CmoaParagraphNormalizer",
                "Cmoa paragraph normalization: conversion used source-range patches.")));
    }
    return transformed;
}

}
