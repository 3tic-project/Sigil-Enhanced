#include <cstdlib>
#include <iostream>
#include <stdexcept>

#include <QString>
#include <QVector>

#include "BuiltinPlugins/BookLiveParagraphNormalizer.h"
#include "BuiltinPlugins/CmoaParagraphNormalizer.h"
#include "BuiltinPlugins/DivParagraphNormalizationPlan.h"

using BuiltinPlugins::BookLiveParagraphNormalizer;
using BuiltinPlugins::CmoaParagraphNormalizer;
using BuiltinPlugins::DivParagraphCssAnalyzer;
using BuiltinPlugins::DivParagraphNormalizationPlan;

namespace
{

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

QString fixture()
{
    QString paragraphs;
    for (int index = 0; index < 13; ++index) {
        paragraphs += QStringLiteral(
            "      <div class='css_body'>　本文%1<ruby>漢<rt>かん</rt></ruby>字。</div>")
            .arg(index);
        if (index == 6) {
            paragraphs += QLatin1Char('\n');
        }
    }
    return QStringLiteral(R"XHTML(<?xml version='1.0' encoding='UTF-8'?>
<!DOCTYPE html>
<html xmlns='http://www.w3.org/1999/xhtml' xml:lang='ja' class='vrtl'>
 <head><title>Cmoa</title><link rel='stylesheet' href='../style/reset.css'/></head>
 <body class='p-002.xhtml'>
  <div class='main'>
   <div class='layout'><div class='flow'>
      <!-- source spacing must stay exactly here -->
      <div class='blank'><br/></div>
      <div class='heading'><a name='chapter'/><h1>第一章</h1></div>
%1
   </div></div>
  </div>
 </body>
</html>)XHTML").arg(paragraphs);
}

QVector<DivParagraphCssAnalyzer::Source> safeStyles()
{
    return {
        { QStringLiteral("style/reset.css"), QStringLiteral(R"CSS(
div,p { display:block; margin:0; padding:0; }
body,div,p { text-indent:0; }
body > p,
div > p { text-indent:inherit; }
.css_body { line-height:1.75; margin-top:0; margin-bottom:0; }
)CSS") },
        { QStringLiteral("style/title.css"), QStringLiteral(
            ".p-titlepage .author p { margin: 0.5em 0 0 0; }") }
    };
}

}

int main()
{
    try {
        const QString source = fixture();
        const auto options = CmoaParagraphNormalizer::defaultOptions();
        const auto analysis = CmoaParagraphNormalizer::analyzeXhtmlText(
            source, options, safeStyles());
        require(analysis.ok && analysis.candidate && analysis.safeToNormalize,
                "Cmoa fixture was not accepted by the dedicated safe profile");
        require(analysis.convertibleLeaves == 13 && analysis.spacerBrLeaves == 1 &&
                    analysis.protectedHeadingBlocks == 1,
                "Cmoa candidate/protected counts changed");
        require(analysis.presetId == QLatin1String("cmoa-conservative-v1"),
                "Cmoa preset identity changed");

        const auto normalized = CmoaParagraphNormalizer::normalizeXhtmlText(
            source, options, safeStyles());
        require(normalized.ok && normalized.changed,
                "Cmoa source-preserving normalization failed");
        require(normalized.text.count(QStringLiteral("<p class='css_body'>")) == 13 &&
                    normalized.text.contains(
                        QStringLiteral("<div class='blank'><br/></div>")) &&
                    normalized.text.contains(
                        QStringLiteral("<div class='heading'><a name='chapter'/><h1>第一章</h1></div>")),
                "Cmoa normalization changed protected or blank structures");
        require(normalized.text.count(QStringLiteral("<ruby>漢<rt>かん</rt></ruby>")) == 13,
                "Cmoa normalization changed Ruby order/content");
        require(normalized.text.contains(
                    QStringLiteral("<!-- source spacing must stay exactly here -->")) &&
                    !normalized.text.contains(QStringLiteral("se-bl-")),
                "Cmoa normalization serialized the document or injected BookLive styles");

        const auto second = CmoaParagraphNormalizer::normalizeXhtmlText(
            normalized.text, options, safeStyles());
        require(second.ok && !second.changed && second.text == normalized.text,
                "Cmoa normalization is not idempotent");

        auto risky_styles = safeStyles();
        risky_styles << DivParagraphCssAnalyzer::Source {
            QStringLiteral("style/risky.css"),
            QStringLiteral("div.css_body { color: red; }")
        };
        const auto risky = CmoaParagraphNormalizer::analyzeXhtmlText(
            source, options, risky_styles);
        require(risky.candidate && !risky.safeToNormalize && risky.cssReviewRequired,
                "Unknown Cmoa tag-dependent CSS did not fail closed");

        const QVector<DivParagraphNormalizationPlan::Input> inputs = {
            { QStringLiteral("Text/chapter.xhtml"), source,
              QStringLiteral("revision-1"), safeStyles() }
        };
        const auto plan = DivParagraphNormalizationPlan::buildCmoa(inputs, options);
        require(plan.ok && plan.applyFiles == 1 && plan.conversionCount == 13 &&
                    plan.presetId == QLatin1String("cmoa-conservative-v1"),
                "Cmoa batch plan did not use the dedicated normalizer");

        require(BookLiveParagraphNormalizer::Options::bookLiveCompatibility().presetId() ==
                    QLatin1String("booklive-compat-v1"),
                "Cmoa support changed the BookLive compatibility preset");

        std::cout << "Cmoa paragraph normalizer checks passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
