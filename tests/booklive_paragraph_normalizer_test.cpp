#include <QCoreApplication>
#include <QDomDocument>
#include <QRegularExpression>
#include <QString>
#include <QTextStream>

#include "BuiltinPlugins/BookLiveParagraphNormalizer.h"
#include "BuiltinPlugins/DivParagraphCssAnalyzer.h"
#include "BuiltinPlugins/DivParagraphNormalizationPlan.h"

using BuiltinPlugins::BookLiveParagraphNormalizer;
using BuiltinPlugins::DivParagraphCssAnalyzer;
using BuiltinPlugins::DivParagraphNormalizationPlan;

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

bool hasClass(const QDomElement& element, const QString& class_name)
{
    return element.attribute(QStringLiteral("class"))
        .split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts)
        .contains(class_name);
}

QDomElement findClass(const QDomNode& root, const QString& class_name)
{
    if (root.isElement() && hasClass(root.toElement(), class_name)) {
        return root.toElement();
    }
    for (QDomNode child = root.firstChild(); !child.isNull(); child = child.nextSibling()) {
        const QDomElement found = findClass(child, class_name);
        if (!found.isNull()) {
            return found;
        }
    }
    return QDomElement();
}

int countElements(const QDomNode& root, const QString& name)
{
    int count = root.isElement() && localName(root) == name ? 1 : 0;
    for (QDomNode child = root.firstChild(); !child.isNull(); child = child.nextSibling()) {
        count += countElements(child, name);
    }
    return count;
}

bool generatedParagraphContainsDiv(const QDomNode& root)
{
    if (root.isElement()) {
        const QDomElement element = root.toElement();
        if (localName(element) == QStringLiteral("p") &&
            hasClass(element, QStringLiteral("se-bl-paragraph")) &&
            countElements(element, QStringLiteral("div")) > 0) {
            return true;
        }
    }
    for (QDomNode child = root.firstChild(); !child.isNull(); child = child.nextSibling()) {
        if (generatedParagraphContainsDiv(child)) {
            return true;
        }
    }
    return false;
}

QString paragraphs(int count)
{
    QString result;
    for (int i = 0; i < count; ++i) {
        result += QStringLiteral("<div class=\"para\" style=\"line-height:1.75\">"
                                 "　正文%1<ruby>字<rt>じ</rt></ruby>。</div>")
                      .arg(i);
    }
    return result;
}

QString taggedParagraphs(int count, const QString& tag)
{
    QString result;
    for (int i = 0; i < count; ++i) {
        result += QStringLiteral("<%1 class='para' data-order=\"%2\">"
                                 "　正文%2\u00a0<ruby>字<rp>(</rp><rt>じ</rt><rp>)</rp></ruby>。"
                                 "</%1>\n")
                      .arg(tag)
                      .arg(i);
    }
    return result;
}

QString conservativeBody(const QString& paragraph_tag)
{
    return QStringLiteral("<div class=\"main\">\n"
                          "  <div class=\"content\">\n"
                          "    <div class=\"chapter-title\" data-keep=\"yes\">\n"
                          "      <!-- keep title wrapper byte-for-byte -->"
                          "<a id=\"chapter-1\"></a><h1>第一章</h1>\n"
                          "    </div>\n"
                          "    <div class=\"spacer\"><br /></div>\n"
                          "    <div class=\"scene\">◆ ◆ ◆</div>\n"
                          "    <div class=\"image\"><img src=\"cover.jpg\" /></div>\n"
                          "    <div class=\"styled\"><div class=\"inner\">题记</div></div>\n") +
        taggedParagraphs(12, paragraph_tag) +
        QStringLiteral("  </div>\n</div>");
}

QString xhtml(const QString& body, const QString& body_class = QString())
{
    return QStringLiteral(
        "<!DOCTYPE html><html xmlns=\"http://www.w3.org/1999/xhtml\" class=\"vrtl\">"
        "<head><meta charset=\"UTF-8\"/><title>x</title>"
        "<link rel=\"stylesheet\" href=\"../style/generated_styles.css\"/></head>"
        "<body class=\"%1\">%2</body></html>")
        .arg(body_class, body);
}

int fail(const QString& message)
{
    QTextStream(stderr) << message << '\n';
    return 1;
}

int runTests()
{
    const QString body =
        QStringLiteral("<div class=\"main\"><div class=\"outer\"><div class=\"justify\">"
                       "<div class=\"content\"><a name=\"start\"></a>"
                       "<div class=\"image-box\"><img class=\"fit\" src=\"image.jpg\"/></div>"
                       "<div class=\"spacer\"><br/></div>"
                       "<div class=\"title-outer\" style=\"margin-top:2em\">"
                       "<div class=\"title-inner\">小节标题</div></div>") +
        paragraphs(12) +
        QStringLiteral("<div class=\"scene\">　◆　◆　◆</div>"
                       "</div></div></div></div>");
    const QString source = xhtml(body, QStringLiteral("chapter-page"));
    const BookLiveParagraphNormalizer::Analysis analysis =
        BookLiveParagraphNormalizer::analyzeXhtmlText(source);
    if (!analysis.safeToNormalize || analysis.paragraphLeaves != 12 ||
        analysis.wrappedBlockLeaves != 1 || analysis.spacerBrLeaves != 1 ||
        analysis.sceneBreaks != 1 || analysis.imageLeaves != 1 || analysis.anchorOnly != 1) {
        return fail(QStringLiteral("normal flow analysis failed: %1").arg(analysis.message));
    }

    const BookLiveParagraphNormalizer::NormalizeResult result =
        BookLiveParagraphNormalizer::normalizeXhtmlText(source);
    if (!result.ok || !result.changed ||
        result.after.pageKind != BookLiveParagraphNormalizer::PageKind::AlreadyNormalized) {
        return fail(QStringLiteral("normalization failed: %1")
                        .arg(result.messages.join(QStringLiteral("; "))));
    }

    QDomDocument output;
    if (!output.setContent(result.text)) {
        return fail(QStringLiteral("normalized output is not well-formed XML"));
    }
    const QDomElement title_outer = findClass(output, QStringLiteral("title-outer"));
    const QDomElement title_inner = findClass(output, QStringLiteral("title-inner"));
    const QDomElement spacer = findClass(output, QStringLiteral("spacer"));
    if (findClass(output, QStringLiteral("outer")).isNull() ||
        findClass(output, QStringLiteral("justify")).isNull() ||
        findClass(output, QStringLiteral("content")).isNull() ||
        localName(title_outer) != QStringLiteral("p") ||
        title_outer.attribute(QStringLiteral("style")) != QStringLiteral("margin-top:2em") ||
        localName(title_inner) != QStringLiteral("span") ||
        !hasClass(title_inner, QStringLiteral("se-bl-inner-block")) ||
        localName(spacer) != QStringLiteral("p") || countElements(spacer, QStringLiteral("br")) != 1) {
        return fail(QStringLiteral("layout, title style, or spacer was not preserved"));
    }
    if (generatedParagraphContainsDiv(output) ||
        result.text.contains(QStringLiteral("se-bl-credit")) ||
        result.text.contains(QStringLiteral("margin-top: 5em")) ||
        countElements(output, QStringLiteral("ruby")) != 12 ||
        countElements(output, QStringLiteral("rt")) != 12 ||
        !result.text.contains(QStringLiteral("name=\"start\"")) ||
        !result.text.contains(QStringLiteral("src=\"image.jpg\""))) {
        return fail(QStringLiteral("flattening or semantic preservation failed"));
    }

    const BookLiveParagraphNormalizer::NormalizeResult second =
        BookLiveParagraphNormalizer::normalizeXhtmlText(result.text);
    if (!second.ok || second.changed || second.text != result.text) {
        return fail(QStringLiteral("normalization is not idempotent"));
    }

    const BookLiveParagraphNormalizer::Options conservative =
        BookLiveParagraphNormalizer::Options::conservative();
    const QString conservative_source = xhtml(conservativeBody(QStringLiteral("div")));
    const QString conservative_expected = xhtml(conservativeBody(QStringLiteral("p")));
    const BookLiveParagraphNormalizer::Analysis conservative_analysis =
        BookLiveParagraphNormalizer::analyzeXhtmlText(conservative_source, conservative);
    if (!conservative_analysis.safeToNormalize ||
        conservative_analysis.presetId != QStringLiteral("conservative-v2") ||
        conservative_analysis.ruleVersion.isEmpty() ||
        conservative_analysis.beforeHash.length() != 64 ||
        conservative_analysis.paragraphLeaves != 12 ||
        conservative_analysis.convertibleLeaves != 12 ||
        conservative_analysis.protectedHeadingBlocks != 1 ||
        conservative_analysis.candidateRanges.count() != 12 ||
        conservative_analysis.protectedRanges.count() != 5) {
        return fail(QStringLiteral("conservative analysis or protected ranges failed: %1")
                        .arg(conservative_analysis.message));
    }

    const BookLiveParagraphNormalizer::NormalizeResult conservative_result =
        BookLiveParagraphNormalizer::normalizeXhtmlText(conservative_source, conservative);
    if (!conservative_result.ok || !conservative_result.changed ||
        conservative_result.text != conservative_expected ||
        conservative_result.text.contains(QStringLiteral("se-bl-normalized")) ||
        conservative_result.afterHash.length() != 64) {
        return fail(QStringLiteral("source-preserving conservative conversion failed: %1")
                        .arg(conservative_result.messages.join(QStringLiteral("; "))));
    }
    const BookLiveParagraphNormalizer::NormalizeResult conservative_second =
        BookLiveParagraphNormalizer::normalizeXhtmlText(conservative_result.text, conservative);
    if (!conservative_second.ok || conservative_second.changed ||
        conservative_second.text != conservative_result.text ||
        conservative_second.before.candidate ||
        !conservative_second.before.candidateRanges.isEmpty()) {
        return fail(QStringLiteral("conservative conversion did not produce an empty second plan"));
    }

    const QString marked_with_new_candidates = xhtml(
        conservativeBody(QStringLiteral("div")), QStringLiteral("se-bl-normalized"));
    const BookLiveParagraphNormalizer::Analysis marked_analysis =
        BookLiveParagraphNormalizer::analyzeXhtmlText(marked_with_new_candidates, conservative);
    if (!marked_analysis.candidate || marked_analysis.convertibleLeaves != 12 ||
        marked_analysis.warnings.isEmpty()) {
        return fail(QStringLiteral("body marker hid newly added candidates"));
    }

    const QString invalid_transparent = xhtml(
        QStringLiteral("<div class=\"main\"><div class=\"content\">"
                       "<div><a href=\"x\"><div>not phrasing</div></a></div>") +
        paragraphs(12) + QStringLiteral("</div></div>"));
    const BookLiveParagraphNormalizer::Analysis invalid_analysis =
        BookLiveParagraphNormalizer::analyzeXhtmlText(invalid_transparent, conservative);
    if (invalid_analysis.pageKind != BookLiveParagraphNormalizer::PageKind::BlockLayout ||
        invalid_analysis.candidate) {
        return fail(QStringLiteral("transparent content with a block descendant did not fail closed"));
    }

    const DivParagraphCssAnalyzer::Result safe_css = DivParagraphCssAnalyzer::analyze({
        { QStringLiteral("safe.css"),
          QStringLiteral("div.para, p.para { margin: 0; }\n"
                         "@media (min-width: 10em) { body > div, body > p { text-indent: 1em; } }\n"
                         "[data-kind='div'] { color: black; }") }
    });
    if (safe_css.reviewRequired || !safe_css.dependencies.isEmpty()) {
        return fail(QStringLiteral("paired CSS selectors were reported as risky"));
    }

    const DivParagraphCssAnalyzer::Result risky_css = DivParagraphCssAnalyzer::analyze({
        { QStringLiteral("risky.css"),
          QStringLiteral("div.para { line-height: 1.8; }\n"
                         "p:nth-of-type(2) { margin-top: 0; }\n"
                         "@supports (display: block) { :is(div, p) + span { color: red; } }") }
    });
    if (!risky_css.reviewRequired || risky_css.dependencies.count() != 3 ||
        risky_css.dependencies.first().sourceId != QStringLiteral("risky.css")) {
        return fail(QStringLiteral("tag-dependent CSS selectors were not fully reported"));
    }

    QString css_risk_source = conservative_source;
    css_risk_source.replace(
        QStringLiteral("</head>"),
        QStringLiteral("<style>div.para { text-indent: 1em; }</style></head>"));
    const BookLiveParagraphNormalizer::Analysis css_risk_analysis =
        BookLiveParagraphNormalizer::analyzeXhtmlText(css_risk_source, conservative);
    if (!css_risk_analysis.candidate || css_risk_analysis.safeToNormalize ||
        !css_risk_analysis.cssReviewRequired ||
        css_risk_analysis.pageKind != BookLiveParagraphNormalizer::PageKind::CssRisk ||
        css_risk_analysis.cssDependencies.count() != 1) {
        return fail(QStringLiteral("CSS risk did not gate automatic normalization"));
    }
    if (BookLiveParagraphNormalizer::normalizeXhtmlText(css_risk_source, conservative).ok ||
        !BookLiveParagraphNormalizer::normalizeXhtmlText(
             css_risk_source, conservative, true).ok) {
        return fail(QStringLiteral("CSS manual-review gate failed"));
    }

    const QVector<DivParagraphCssAnalyzer::Source> safe_stylesheets = {
        { QStringLiteral("Styles/safe.css"),
          QStringLiteral("div.para, p.para { margin: 0; }") }
    };
    const QVector<DivParagraphCssAnalyzer::Source> risky_stylesheets = {
        { QStringLiteral("Styles/risky.css"),
          QStringLiteral("div.para { margin: 0; }") }
    };
    const QVector<DivParagraphNormalizationPlan::Input> plan_inputs = {
        { QStringLiteral("Text/chapter.xhtml"), conservative_source,
          QStringLiteral("revision-1"), safe_stylesheets },
        { QStringLiteral("Text/review.xhtml"), conservative_source,
          QStringLiteral("revision-2"), risky_stylesheets },
        { QStringLiteral("Text/done.xhtml"), conservative_expected,
          QStringLiteral("revision-3"), safe_stylesheets }
    };
    const DivParagraphNormalizationPlan::Result batch_plan =
        DivParagraphNormalizationPlan::build(plan_inputs, conservative);
    if (!batch_plan.ok || batch_plan.planId.length() != 64 ||
        batch_plan.ruleVersion.isEmpty() ||
        batch_plan.presetId != QStringLiteral("conservative-v2") ||
        batch_plan.applyFiles != 1 || batch_plan.reviewFiles != 1 ||
        batch_plan.skippedFiles != 1 || batch_plan.errorFiles != 0 ||
        batch_plan.conversionCount != 12 || batch_plan.entries.first().output != conservative_expected ||
        !DivParagraphNormalizationPlan::revisionConflicts(batch_plan, plan_inputs).isEmpty() ||
        DivParagraphNormalizationPlan::build(plan_inputs, conservative).planId != batch_plan.planId) {
        return fail(QStringLiteral("deterministic batch plan construction failed"));
    }

    QVector<DivParagraphNormalizationPlan::Input> changed_inputs = plan_inputs;
    changed_inputs[0].text += QStringLiteral("\n");
    const QStringList content_conflicts =
        DivParagraphNormalizationPlan::revisionConflicts(batch_plan, changed_inputs);
    if (content_conflicts.count() != 1 ||
        !content_conflicts.first().contains(QStringLiteral("content changed"))) {
        return fail(QStringLiteral("batch plan did not reject changed XHTML"));
    }
    changed_inputs = plan_inputs;
    changed_inputs[0].stylesheets[0].text += QStringLiteral("\nspan { color: red; }");
    const QStringList css_conflicts =
        DivParagraphNormalizationPlan::revisionConflicts(batch_plan, changed_inputs);
    if (css_conflicts.count() != 1 ||
        !css_conflicts.first().contains(QStringLiteral("stylesheet content changed"))) {
        return fail(QStringLiteral("batch plan did not reject changed CSS"));
    }
    changed_inputs = plan_inputs;
    changed_inputs[0].baseRevision = QStringLiteral("revision-new");
    const QStringList revision_conflicts =
        DivParagraphNormalizationPlan::revisionConflicts(batch_plan, changed_inputs);
    if (revision_conflicts.count() != 1 ||
        !revision_conflicts.first().contains(QStringLiteral("revision changed"))) {
        return fail(QStringLiteral("batch plan did not reject a revision conflict"));
    }

    const QString toc = xhtml(
        QStringLiteral("<div class=\"main\"><div><div><div>"
                       "<div>目次</div><div><a href=\"a.xhtml\">一</a></div>"
                       "<div><a href=\"b.xhtml\">二</a></div><div><br/></div>"
                       "<div><a href=\"c.xhtml\">三</a></div></div></div></div></div>"));
    if (BookLiveParagraphNormalizer::analyzeXhtmlText(toc).pageKind !=
        BookLiveParagraphNormalizer::PageKind::TocLike) {
        return fail(QStringLiteral("toc classification failed"));
    }

    const QString image = xhtml(
        QStringLiteral("<div class=\"main\"><p><img src=\"cover.jpg\"/></p></div>"),
        QStringLiteral("p-image"));
    if (BookLiveParagraphNormalizer::analyzeXhtmlText(image).pageKind !=
        BookLiveParagraphNormalizer::PageKind::ImageOrTitlePage) {
        return fail(QStringLiteral("image-page classification failed"));
    }

    const QString short_source = xhtml(
        QStringLiteral("<div class=\"main\"><div><div><div>") + paragraphs(4) +
        QStringLiteral("</div></div></div></div>"));
    const BookLiveParagraphNormalizer::Analysis short_analysis =
        BookLiveParagraphNormalizer::analyzeXhtmlText(short_source);
    if (!short_analysis.candidate || short_analysis.safeToNormalize ||
        BookLiveParagraphNormalizer::normalizeXhtmlText(short_source).ok ||
        !BookLiveParagraphNormalizer::normalizeXhtmlText(short_source, true).ok) {
        return fail(QStringLiteral("manual short-flow gate failed"));
    }

    const QString complex = xhtml(
        QStringLiteral("<div class=\"main\"><div><div><div>") + paragraphs(12) +
        QStringLiteral("<div><div><section>complex</section></div></div>"
                       "</div></div></div></div>"));
    const BookLiveParagraphNormalizer::Analysis complex_analysis =
        BookLiveParagraphNormalizer::analyzeXhtmlText(complex);
    if (complex_analysis.candidate ||
        complex_analysis.pageKind != BookLiveParagraphNormalizer::PageKind::BlockLayout) {
        return fail(QStringLiteral("complex block did not fail closed"));
    }

    if (BookLiveParagraphNormalizer::analyzeXhtmlText(
            QStringLiteral("<html><body><div></body></html>"))
            .pageKind != BookLiveParagraphNormalizer::PageKind::ParseError) {
        return fail(QStringLiteral("malformed XML classification failed"));
    }
    return 0;
}

}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    return runTests();
}
