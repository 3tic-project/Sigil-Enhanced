#include <QString>
#include <QTextStream>

#include "BuiltinPlugins/VerticalLayoutAnalyzer.h"

using BuiltinPlugins::VerticalLayoutAnalyzer;

int fail(const QString& message)
{
    QTextStream(stderr) << "vertical_layout_analyzer_test: " << message << '\n';
    return 1;
}

int runTests()
{
    // ---- analyzeCss: 标准 + 前缀 writing-mode ----
    {
        const auto css = VerticalLayoutAnalyzer::analyzeCss(QStringLiteral(
            "body { writing-mode: vertical-rl; }\n"
            ".vrtl p { -epub-writing-mode: vertical-rl; }\n"
            ".hltr p { -webkit-writing-mode: horizontal-tb; }\n"));
        if (!css.ok || !css.hasVerticalWritingMode || css.verticalWritingModeCount != 2 ||
            !css.hasHorizontalWritingMode) {
            return fail(QStringLiteral("standard/prefix writing-mode detection failed"));
        }
    }

    // ---- analyzeCss: .vrtl/.hltr 成对 + tcy/upright ----
    {
        const auto css = VerticalLayoutAnalyzer::analyzeCss(QStringLiteral(
            ".vrtl p { writing-mode: vertical-rl; }\n"
            ".hltr p { writing-mode: horizontal-tb; }\n"
            ".tcy { text-combine-upright: all; }\n"
            ".upright-1 { text-orientation: upright; }\n"));
        if (!css.hasVrtlClass || !css.hasHltrClass || !css.hasPairedVrtlHltr ||
            !css.hasTcy || !css.hasUpright || !css.hasTextCombine || !css.hasTextOrientation) {
            return fail(QStringLiteral("vrtl/hltr/tcy/upright detection failed"));
        }
    }

    // ---- analyzeCss: OpenType vert/vrt2 ----
    {
        const auto css = VerticalLayoutAnalyzer::analyzeCss(QStringLiteral(
            "p { font-feature-settings: \"vert\" 1, \"kern\" 1; }\n"));
        if (!css.hasVertFeature) {
            return fail(QStringLiteral("font-feature-settings vert detection failed"));
        }
    }

    // ---- analyzeCss: 风险信号 ----
    {
        const auto css = VerticalLayoutAnalyzer::analyzeCss(QStringLiteral(
            "p { position: absolute; }\n"
            "img { transform: rotate(90deg); }\n"));
        if (!css.hasAbsolutePositioning || !css.hasTransformRotate) {
            return fail(QStringLiteral("absolute/rotate risk signals failed"));
        }
    }

    // ---- analyzeCss: 物理方向 utility class ----
    {
        const auto css = VerticalLayoutAnalyzer::analyzeCss(QStringLiteral(
            ".start-1 { margin-top: 1em; }\n"
            ".end-0 { margin-bottom: 0; }\n"
            ".normal { margin-left: 2em; }\n"));
        if (css.physicalSideUtilityCount < 2) {
            return fail(QStringLiteral("physical side utility detection failed: %1")
                            .arg(css.physicalSideUtilityCount));
        }
    }

    // ---- analyzeCss: 解析错误 ----
    {
        const auto css = VerticalLayoutAnalyzer::analyzeCss(QStringLiteral(
            "body { writing-mode: vertical-rl; }\n"
            "p { color: red; @#$% }\n"));
        if (css.ok || css.parseErrorCount == 0) {
            return fail(QStringLiteral("malformed css should set parseErrorCount"));
        }
    }

    // ---- analyzeXhtml: 内联 style 纵向 + ruby ----
    {
        const auto xhtml = VerticalLayoutAnalyzer::analyzeXhtml(QStringLiteral(
            "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head>"
            "<style>body { writing-mode: vertical-rl; }</style></head>"
            "<body><p>これはテストです。</p><ruby>漢<rt>かん</rt></ruby></body></html>"));
        if (!xhtml.ok || !xhtml.hasVerticalWritingMode ||
            xhtml.pageKind != VerticalLayoutAnalyzer::PageKind::ReflowVerticalSafe ||
            !xhtml.hasRuby || xhtml.rubyCount != 2) {
            return fail(QStringLiteral("inline vertical + ruby analysis failed: %1")
                            .arg(VerticalLayoutAnalyzer::pageKindName(xhtml.pageKind)));
        }
    }

    // ---- analyzeXhtml: 图片页 ----
    {
        const auto xhtml = VerticalLayoutAnalyzer::analyzeXhtml(QStringLiteral(
            "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head/><body>"
            "<img src=\"cover.jpg\" alt=\"cover\"/></body></html>"));
        if (xhtml.pageKind != VerticalLayoutAnalyzer::PageKind::ImageOnly) {
            return fail(QStringLiteral("image-only classification failed"));
        }
    }

    // ---- analyzeXhtml: nav ----
    {
        const auto xhtml = VerticalLayoutAnalyzer::analyzeXhtml(QStringLiteral(
            "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head/><body>"
            "<nav epub:type=\"toc\"><ol><li><a href=\"c1.xhtml\">章</a></li></ol></nav>"
            "</body></html>"));
        if (!xhtml.isNavDocument || xhtml.pageKind != VerticalLayoutAnalyzer::PageKind::TocOrNav) {
            return fail(QStringLiteral("nav classification failed"));
        }
    }

    // ---- analyzeXhtml: 纵横混排 ----
    {
        const auto xhtml = VerticalLayoutAnalyzer::analyzeXhtml(QStringLiteral(
            "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head/><body "
            "style=\"writing-mode: vertical-rl\">"
            "<p>縦書き本文</p>"
            "<table style=\"writing-mode: horizontal-tb\"><tr><td>横</td></tr></table>"
            "</body></html>"));
        if (!xhtml.hasVerticalWritingMode || !xhtml.hasHorizontalWritingMode ||
            xhtml.pageKind != VerticalLayoutAnalyzer::PageKind::MixedWritingMode) {
            return fail(QStringLiteral("mixed writing mode classification failed: %1")
                            .arg(VerticalLayoutAnalyzer::pageKindName(xhtml.pageKind)));
        }
    }

    // ---- effectiveWritingMode: DPFJ 共用样式表以页面根 class 为准 ----
    {
        const auto css = VerticalLayoutAnalyzer::analyzeCss(QStringLiteral(
            ".vrtl { writing-mode: vertical-rl; }\n"
            ".hltr { writing-mode: horizontal-tb; }\n"));
        const auto vertical = VerticalLayoutAnalyzer::analyzeXhtml(QStringLiteral(
            "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head/>"
            "<body class=\"vrtl\"><p>縦</p></body></html>"));
        const auto horizontal = VerticalLayoutAnalyzer::analyzeXhtml(QStringLiteral(
            "<html xmlns=\"http://www.w3.org/1999/xhtml\" class=\"hltr\"><head/>"
            "<body><p>横</p></body></html>"));
        if (VerticalLayoutAnalyzer::effectiveWritingMode(css, vertical)
                != VerticalLayoutAnalyzer::WritingMode::Vertical
            || VerticalLayoutAnalyzer::effectiveWritingMode(css, horizontal)
                != VerticalLayoutAnalyzer::WritingMode::Horizontal) {
            return fail(QStringLiteral("paired profile root class direction failed"));
        }
    }

    // ---- effectiveWritingMode: compatibility override class wins ----
    {
        const auto vertical_css = VerticalLayoutAnalyzer::analyzeCss(QStringLiteral(
            "body { writing-mode: vertical-rl; }"));
        const auto xhtml = VerticalLayoutAnalyzer::analyzeXhtml(QStringLiteral(
            "<html xmlns=\"http://www.w3.org/1999/xhtml\" class=\"vrtl se-v2h-horizontal\">"
            "<head/><body><p>横</p></body></html>"));
        if (!xhtml.hasV2hOverrideClass
            || VerticalLayoutAnalyzer::effectiveWritingMode(vertical_css, xhtml)
                != VerticalLayoutAnalyzer::WritingMode::Horizontal) {
            return fail(QStringLiteral("compatibility override direction failed"));
        }
    }

    // ---- analyzeXhtml: 固定 viewport ----
    {
        const auto xhtml = VerticalLayoutAnalyzer::analyzeXhtml(QStringLiteral(
            "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head>"
            "<meta name=\"viewport\" content=\"width=1024, height=768\"/>"
            "</head><body><p>fixed</p></body></html>"));
        if (!xhtml.fixedViewport || xhtml.pageKind != VerticalLayoutAnalyzer::PageKind::FixedLayout) {
            return fail(QStringLiteral("fixed viewport classification failed"));
        }
    }

    // ---- analyzeXhtml: XML 解析失败 ----
    {
        const auto xhtml = VerticalLayoutAnalyzer::analyzeXhtml(QStringLiteral(
            "<html><body><p>unclosed"));
        if (xhtml.ok || xhtml.pageKind != VerticalLayoutAnalyzer::PageKind::ParseError) {
            return fail(QStringLiteral("parse-error classification failed"));
        }
    }

    // ---- analyzeOpf ----
    {
        const auto opf = VerticalLayoutAnalyzer::analyzeOpf(QStringLiteral(
            "<?xml version=\"1.0\"?>"
            "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"3.0\" unique-identifier=\"uid\">"
            "<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">"
            "<dc:title>t</dc:title><dc:language>ja</dc:language>"
            "<meta property=\"rendition:layout\">reflowable</meta></metadata>"
            "<manifest>"
            "<item id=\"c1\" href=\"Text/c1.xhtml\" media-type=\"application/xhtml+xml\"/>"
            "<item id=\"i1\" href=\"Images/p.jpg\" media-type=\"image/jpeg\"/>"
            "</manifest>"
            "<spine page-progression-direction=\"rtl\">"
            "<itemref idref=\"c1\"/><itemref idref=\"i1\"/></spine></package>"));
        if (!opf.ok || opf.epubVersion != QStringLiteral("3.0") ||
            !opf.languages.contains(QStringLiteral("ja")) ||
            opf.pageProgression != QStringLiteral("rtl") ||
            opf.renditionLayout != QStringLiteral("reflowable") ||
            opf.spineItemCount != 2 || opf.imageOnlyCount != 1) {
            return fail(QStringLiteral("opf analysis failed"));
        }
    }

    // ---- 风险分级 ----
    if (VerticalLayoutAnalyzer::riskLevelName(10) != QStringLiteral("auto-safe") ||
        VerticalLayoutAnalyzer::riskLevelName(30) != QStringLiteral("auto-safe-with-warnings") ||
        VerticalLayoutAnalyzer::riskLevelName(60) != QStringLiteral("manual-review") ||
        VerticalLayoutAnalyzer::riskLevelName(80) != QStringLiteral("unsupported")) {
        return fail(QStringLiteral("risk level names failed"));
    }

    // ---- 组合风险分 ----
    {
        VerticalLayoutAnalyzer::CssAnalysis css;
        css.hasTransformRotate = true;
        VerticalLayoutAnalyzer::XhtmlAnalysis xhtml;
        xhtml.ok = true;
        xhtml.hasVerticalWritingMode = true;
        xhtml.visibleTextLength = 500;
        const int score = VerticalLayoutAnalyzer::combinedRiskScore(css, xhtml, false);
        if (score != 5) {  // transform rotate +15，纯文本 reflow 折扣 -10 → 5
            return fail(QStringLiteral("combined risk score mismatch: %1").arg(score));
        }
    }

    return 0;
}

int main()
{
    return runTests();
}
