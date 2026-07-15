/************************************************************************
**
**  Copyright (C) 2026 Sigil Enhanced contributors
**
**  This file is part of Sigil.
**
*************************************************************************/

#include "ChineseConversion/ChineseTextConversionPlan.h"

#include <algorithm>

#include <QSet>

#include "ChineseConversion/OpenCCConverter.h"
#include "gumbo.h"

namespace
{

QString ElementName(const GumboNode *node)
{
    if (!node || node->type != GUMBO_NODE_ELEMENT) {
        return QString();
    }
    const char *normalized = gumbo_normalized_tagname(node->v.element.tag);
    if (normalized && *normalized) {
        return QString::fromLatin1(normalized).toLower();
    }
    GumboStringPiece original = node->v.element.original_tag;
    gumbo_tag_from_original_text(&original);
    return QString::fromUtf8(original.data, static_cast<qsizetype>(original.length)).toLower();
}

bool IsJapaneseLanguage(const QString& language)
{
    const QString normalized = language.trimmed().toLower();
    return normalized == QStringLiteral("ja") || normalized.startsWith(QStringLiteral("ja-"));
}

QString ElementLanguage(const GumboNode *node, const QString& inherited)
{
    const GumboVector& attributes = node->v.element.attributes;
    for (unsigned int index = 0; index < attributes.length; ++index) {
        const auto *attribute = static_cast<const GumboAttribute *>(attributes.data[index]);
        const QString name = QString::fromUtf8(attribute->name).toLower();
        if (name == QStringLiteral("lang")
            && (attribute->attr_namespace == GUMBO_ATTR_NAMESPACE_NONE
                || attribute->attr_namespace == GUMBO_ATTR_NAMESPACE_XML)) {
            return QString::fromUtf8(attribute->value);
        }
    }
    return inherited;
}

QByteArray EscapeText(const QString& text)
{
    QString escaped = text;
    escaped.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    escaped.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    escaped.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    return escaped.toUtf8();
}

QByteArray EscapeAttribute(const QString& text, char quote)
{
    QString escaped = text;
    escaped.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    escaped.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    if (quote == '\'') {
        escaped.replace(QLatin1Char('\''), QStringLiteral("&apos;"));
    } else {
        escaped.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    }
    QByteArray result;
    result.append(quote);
    result.append(escaped.toUtf8());
    result.append(quote);
    return result;
}

struct BuildContext {
    QList<ChineseTextChange> *changes = nullptr;
    QStringList *warnings = nullptr;
    QString *error = nullptr;
    int *skippedJapaneseSegments = nullptr;
    int *skippedProtectedSegments = nullptr;
    const OpenCCConverter *converter = nullptr;
    const ChineseConversionOptions *options = nullptr;
    ChineseDocumentKind documentKind = ChineseDocumentKind::Xhtml;
    const char *sourceBegin = nullptr;
    const char *sourceEnd = nullptr;
};

bool SourceSlice(const BuildContext& context,
                 const GumboStringPiece& piece,
                 qsizetype *start,
                 qsizetype *length)
{
    if (!piece.data || piece.length == 0 || piece.data < context.sourceBegin
        || piece.data > context.sourceEnd
        || piece.length > static_cast<size_t>(context.sourceEnd - piece.data)) {
        return false;
    }
    *start = static_cast<qsizetype>(piece.data - context.sourceBegin);
    *length = static_cast<qsizetype>(piece.length);
    return true;
}

bool IsProtectedElement(const QString& name, const ChineseConversionOptions& options)
{
    static const QSet<QString> alwaysProtected {
        QStringLiteral("script"), QStringLiteral("style")
    };
    static const QSet<QString> codeElements {
        QStringLiteral("code"), QStringLiteral("kbd"),
        QStringLiteral("samp"), QStringLiteral("var")
    };
    return alwaysProtected.contains(name)
        || (options.skipCodeElements && codeElements.contains(name))
        || (options.skipPreElements && name == QStringLiteral("pre"));
}

bool SvgTextContainer(const QString& name)
{
    static const QSet<QString> textElements {
        QStringLiteral("text"), QStringLiteral("tspan"),
        QStringLiteral("title"), QStringLiteral("desc")
    };
    return textElements.contains(name);
}

bool AllowedAttribute(const QString& name, const ChineseConversionOptions& options)
{
    return (options.includeAltText && name == QStringLiteral("alt"))
        || (options.includeTitleAttributes && name == QStringLiteral("title"))
        || (options.includeAriaLabels
            && (name == QStringLiteral("aria-label")
                || name == QStringLiteral("aria-description")));
}

void AddTextChange(BuildContext& context,
                   const GumboNode *node,
                   const QString& path)
{
    const QString before = QString::fromUtf8(node->v.text.text);
    if (before.trimmed().isEmpty()) {
        return;
    }
    QString conversionError;
    const QString after = context.converter->Convert(before, &conversionError);
    if (!conversionError.isEmpty()) {
        *context.error = conversionError;
        return;
    }
    if (after == before) {
        return;
    }
    qsizetype start = -1;
    qsizetype length = 0;
    if (!SourceSlice(context, node->v.text.original_text, &start, &length)) {
        context.warnings->append(
            QStringLiteral("Skipped an injected or unmapped text node at %1").arg(path));
        return;
    }
    context.changes->append({
        start, length, before, after, path, QString(), ChineseTextSourceKind::TextNode
    });
}

void AddAttributeChanges(BuildContext& context,
                         const GumboNode *node,
                         const QString& path)
{
    const GumboVector& attributes = node->v.element.attributes;
    for (unsigned int index = 0; index < attributes.length; ++index) {
        const auto *attribute = static_cast<const GumboAttribute *>(attributes.data[index]);
        const QString name = QString::fromUtf8(attribute->name).toLower();
        if (!AllowedAttribute(name, *context.options)) {
            continue;
        }
        const QString before = QString::fromUtf8(attribute->value);
        QString conversionError;
        const QString after = context.converter->Convert(before, &conversionError);
        if (!conversionError.isEmpty()) {
            *context.error = conversionError;
            return;
        }
        if (after == before) {
            continue;
        }
        qsizetype start = -1;
        qsizetype length = 0;
        if (!SourceSlice(context, attribute->original_value, &start, &length)
            || length < 2) {
            context.warnings->append(
                QStringLiteral("Skipped an unmapped %1 attribute at %2").arg(name, path));
            continue;
        }
        context.changes->append({
            start, length, before, after, path, name, ChineseTextSourceKind::Attribute
        });
    }
}

void Traverse(BuildContext& context,
              const GumboNode *node,
              const QString& inheritedLanguage,
              bool insideXhtmlBody,
              bool insideSvgText,
              bool protectedContent,
              const QString& parentPath)
{
    if (!node || !context.error->isEmpty()) {
        return;
    }
    if (node->type == GUMBO_NODE_TEXT || node->type == GUMBO_NODE_WHITESPACE) {
        if (!protectedContent
            && ((context.documentKind == ChineseDocumentKind::Xhtml && insideXhtmlBody)
                || (context.documentKind == ChineseDocumentKind::Svg && insideSvgText))) {
            AddTextChange(context, node, parentPath + QStringLiteral("/text()"));
        } else if (!QString::fromUtf8(node->v.text.text).trimmed().isEmpty()) {
            ++*context.skippedProtectedSegments;
        }
        return;
    }
    if (node->type != GUMBO_NODE_DOCUMENT && node->type != GUMBO_NODE_ELEMENT) {
        return;
    }

    QString path = parentPath;
    QString language = inheritedLanguage;
    bool body = insideXhtmlBody;
    bool svgText = insideSvgText;
    bool isProtected = protectedContent;
    const GumboVector *children = nullptr;

    if (node->type == GUMBO_NODE_DOCUMENT) {
        children = &node->v.document.children;
    } else {
        const QString name = ElementName(node);
        path += QLatin1Char('/') + name;
        language = ElementLanguage(node, inheritedLanguage);
        body = body || name == QStringLiteral("body");
        svgText = svgText || SvgTextContainer(name);
        isProtected = isProtected || IsProtectedElement(name, *context.options);
        if (context.options->preserveJapaneseText && IsJapaneseLanguage(language)) {
            isProtected = true;
        }
        if (!isProtected
            && ((context.documentKind == ChineseDocumentKind::Xhtml && body)
                || context.documentKind == ChineseDocumentKind::Svg)) {
            AddAttributeChanges(context, node, path);
        }
        children = &node->v.element.children;
    }

    for (unsigned int index = 0; index < children->length; ++index) {
        const auto *child = static_cast<const GumboNode *>(children->data[index]);
        const bool childHasText = child && (child->type == GUMBO_NODE_TEXT
            || child->type == GUMBO_NODE_WHITESPACE)
            && !QString::fromUtf8(child->v.text.text).trimmed().isEmpty();
        if (childHasText && context.options->preserveJapaneseText
            && IsJapaneseLanguage(language)) {
            ++*context.skippedJapaneseSegments;
        }
        Traverse(context, child, language, body, svgText, isProtected, path);
    }
}

QByteArray ReplacementFor(const ChineseTextChange& change, const QByteArray& source)
{
    if (change.sourceKind == ChineseTextSourceKind::TextNode) {
        return EscapeText(change.after);
    }
    const char quote = change.byteLength > 0
        && source.at(change.byteStart) == '\'' ? '\'' : '"';
    return EscapeAttribute(change.after, quote);
}

}

ChineseTextConversionPlan ChineseTextConversionPlan::Build(
    const QString& source,
    ChineseDocumentKind documentKind,
    const ChineseConversionOptions& options,
    const OpenCCConverter& converter)
{
    ChineseTextConversionPlan plan;
    plan.m_Source = source;
    plan.m_Utf8Source = source.toUtf8();
    if (!converter.IsValid()) {
        plan.m_Error = converter.ErrorString();
        return plan;
    }
    if (plan.m_Utf8Source.isEmpty()) {
        return plan;
    }

    GumboOptions gumboOptions = kGumboDefaultOptions;
    gumboOptions.use_xhtml_rules = true;
    gumboOptions.stop_on_first_error = false;
    gumboOptions.max_tree_depth = 400;
    gumboOptions.max_errors = 50;
    GumboOutput *output = gumbo_parse_with_options(
        &gumboOptions, plan.m_Utf8Source.constData(),
        static_cast<size_t>(plan.m_Utf8Source.size()));
    if (!output || output->status != GUMBO_STATUS_OK) {
        plan.m_Error = QStringLiteral("Gumbo could not parse the document");
        if (output) {
            gumbo_destroy_output(output);
        }
        return plan;
    }

    BuildContext context {
        &plan.m_Changes, &plan.m_Warnings, &plan.m_Error,
        &plan.m_SkippedJapaneseSegments, &plan.m_SkippedProtectedSegments,
        &converter, &options, documentKind,
        plan.m_Utf8Source.constData(),
        plan.m_Utf8Source.constData() + plan.m_Utf8Source.size()
    };
    Traverse(context, output->document, QString(), false, false, false, QString());
    gumbo_destroy_output(output);

    std::sort(plan.m_Changes.begin(), plan.m_Changes.end(),
              [](const ChineseTextChange& left, const ChineseTextChange& right) {
                  return left.byteStart < right.byteStart;
              });
    qsizetype previousEnd = -1;
    for (const auto& change : plan.m_Changes) {
        if (change.byteStart < 0 || change.byteLength <= 0
            || change.byteStart + change.byteLength > plan.m_Utf8Source.size()
            || change.byteStart < previousEnd) {
            plan.m_Error = QStringLiteral("Generated Chinese conversion patches overlap or are out of range");
            plan.m_Changes.clear();
            break;
        }
        previousEnd = change.byteStart + change.byteLength;
    }
    return plan;
}

bool ChineseTextConversionPlan::IsValid() const
{
    return m_Error.isEmpty();
}

bool ChineseTextConversionPlan::HasChanges() const
{
    return !m_Changes.isEmpty();
}

QString ChineseTextConversionPlan::ErrorString() const
{
    return m_Error;
}

QStringList ChineseTextConversionPlan::Warnings() const
{
    return m_Warnings;
}

QList<ChineseTextChange> ChineseTextConversionPlan::Changes() const
{
    return m_Changes;
}

int ChineseTextConversionPlan::SkippedJapaneseSegments() const
{
    return m_SkippedJapaneseSegments;
}

int ChineseTextConversionPlan::SkippedProtectedSegments() const
{
    return m_SkippedProtectedSegments;
}

QString ChineseTextConversionPlan::Apply(QString *error) const
{
    QSet<int> enabledChanges;
    for (int index = 0; index < m_Changes.size(); ++index) {
        enabledChanges.insert(index);
    }
    return Apply(enabledChanges, error);
}

QString ChineseTextConversionPlan::Apply(const QSet<int>& enabledChanges, QString *error) const
{
    if (error) {
        error->clear();
    }
    if (!IsValid()) {
        if (error) {
            *error = m_Error;
        }
        return m_Source;
    }
    QByteArray output = m_Utf8Source;
    for (int index = m_Changes.size() - 1; index >= 0; --index) {
        if (!enabledChanges.contains(index)) {
            continue;
        }
        const ChineseTextChange& change = m_Changes.at(index);
        output.replace(change.byteStart, change.byteLength,
                       ReplacementFor(change, m_Utf8Source));
    }
    return QString::fromUtf8(output);
}
