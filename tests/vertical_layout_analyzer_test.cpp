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

    // ---- analyzeCss: 关闭的 vert/vrt2 不是纵排风险，大小写 important 可识别 ----
    {
        const auto disabled = VerticalLayoutAnalyzer::analyzeCss(QStringLiteral(
            "p { font-feature-settings: 'vert' 0, \"vrt2\" off, \"vertical\" 1; }\n"));
        const auto important = VerticalLayoutAnalyzer::analyzeCss(QStringLiteral(
            "p { position: absolute ! IMPORTANT; writing-mode: vertical-rl !IMPORTANT; }\n"));
        if (!disabled.ok || disabled.hasVertFeature
            || !important.ok || !important.hasAbsolutePositioning
            || !important.hasVerticalWritingMode) {
            return fail(QStringLiteral("disabled vertical feature / important normalization failed"));
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

    // ---- 格式化空白不应把图片页伪装成短文本页 ----
    {
        const auto xhtml = VerticalLayoutAnalyzer::analyzeXhtml(QStringLiteral(
            "<html xmlns=\"http://www.w3.org/1999/xhtml\">\n  <head/>\n  <body>\n    "
            "<img src=\"cover.jpg\" alt=\"cover\"/>\n  </body>\n</html>"));
        if (xhtml.visibleTextLength != 0
            || xhtml.pageKind != VerticalLayoutAnalyzer::PageKind::ImageOnly) {
            return fail(QStringLiteral("formatted image-only page classification failed"));
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

    // ---- 普通章节中的 HTML nav 不是 EPUB 导航文档 ----
    {
        const auto xhtml = VerticalLayoutAnalyzer::analyzeXhtml(QStringLiteral(
            "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head/><body>"
            "<nav aria-label=\"章节内导航\"><a href=\"#next\">下一节</a></nav>"
            "<p id=\"next\">正文</p></body></html>"));
        if (xhtml.isNavDocument
            || xhtml.pageKind == VerticalLayoutAnalyzer::PageKind::TocOrNav) {
            return fail(QStringLiteral("generic HTML nav was misclassified as EPUB navigation"));
        }
    }

    // ---- inline !important writing-mode 风险信号 ----
    {
        const auto xhtml = VerticalLayoutAnalyzer::analyzeXhtml(QStringLiteral(
            "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head/><body "
            "style=\"writing-mode: horizontal-tb !IMPORTANT\"><p "
            "style=\"-epub-writing-mode: vertical-rl !important\">正文</p>"
            "</body></html>"));
        if (!xhtml.hasImportantRootHorizontalStyle
            || !xhtml.hasImportantInlineVerticalStyle) {
            return fail(QStringLiteral("important inline writing-mode detection failed"));
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

    // ---- conversion provenance markers are detected but do not change direction ----
    {
        const auto css = VerticalLayoutAnalyzer::analyzeCss(QStringLiteral(
            ".vrtl { writing-mode: vertical-rl; } .hltr { writing-mode: horizontal-tb; }"));
        const auto xhtml = VerticalLayoutAnalyzer::analyzeXhtml(QStringLiteral(
            "<html xmlns=\"http://www.w3.org/1999/xhtml\" "
            "class=\"hltr se-v2h-converted\"><head/><body><p>横</p></body></html>"));
        if (!xhtml.hasV2hConversionMarker || xhtml.hasH2vConversionMarker
            || VerticalLayoutAnalyzer::effectiveWritingMode(css, xhtml)
                != VerticalLayoutAnalyzer::WritingMode::Horizontal) {
            return fail(QStringLiteral("conversion provenance marker analysis failed"));
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

    // ---- 页内 style CSS 解析错误必须暴露给编排器 ----
    {
        const auto xhtml = VerticalLayoutAnalyzer::analyzeXhtml(QStringLiteral(
            "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><style>"
            "body { writing-mode: vertical-rl; @#$% }</style></head><body><p>本文</p>"
            "</body></html>"));
        if (!xhtml.ok || xhtml.inlineCssParseErrorCount == 0) {
            return fail(QStringLiteral("inline CSS parse errors were not reported"));
        }
    }

    // ---- 带命名空间前缀的 XHTML 根元素可分析；非 XHTML 根元素拒绝 ----
    {
        const auto prefixed = VerticalLayoutAnalyzer::analyzeXhtml(QStringLiteral(
            "<x:html xmlns:x=\"http://www.w3.org/1999/xhtml\"><x:head/>"
            "<x:body style=\"writing-mode: vertical-rl\"><x:p>本文</x:p></x:body></x:html>"));
        const auto wrong_root = VerticalLayoutAnalyzer::analyzeXhtml(QStringLiteral(
            "<svg xmlns=\"http://www.w3.org/2000/svg\"><text>本文</text></svg>"));
        if (!prefixed.ok || !prefixed.hasVerticalWritingMode
            || wrong_root.ok || wrong_root.pageKind != VerticalLayoutAnalyzer::PageKind::ParseError) {
            return fail(QStringLiteral("XHTML root/namespace validation failed"));
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

    // ---- OPF itemref 局部 fixed-layout 必须映射回 manifest href ----
    {
        const auto opf = VerticalLayoutAnalyzer::analyzeOpf(QStringLiteral(
            "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"3.0\">"
            "<metadata/><manifest>"
            "<item id=\"flow\" href=\"Text/flow.xhtml\" media-type=\"application/xhtml+xml\"/>"
            "<item id=\"fixed\" href=\"Text/fixed.xhtml\" media-type=\"application/xhtml+xml\"/>"
            "</manifest><spine><itemref idref=\"flow\"/>"
            "<itemref idref=\"fixed\" properties=\"rendition:layout-pre-paginated\"/>"
            "</spine></package>"));
        if (!opf.ok || opf.fixedLayoutCount != 1
            || !opf.fixedLayoutHrefs.contains(QStringLiteral("Text/fixed.xhtml"))) {
            return fail(QStringLiteral("itemref fixed-layout href mapping failed"));
        }
    }

    // ---- OPF rendition:layout refinement 只作用于对应 itemref ----
    {
        const auto opf = VerticalLayoutAnalyzer::analyzeOpf(QStringLiteral(
            "<opf:package xmlns:opf=\"http://www.idpf.org/2007/opf\" version=\"3.0\">"
            "<opf:metadata><opf:meta property=\"rendition:layout\">reflowable</opf:meta>"
            "<opf:meta property=\"rendition:layout\" refines=\"#fixed-ref\">"
            "pre-paginated</opf:meta></opf:metadata><opf:manifest>"
            "<opf:item id=\"flow\" href=\"Text/flow.xhtml\" media-type=\"application/xhtml+xml\"/>"
            "<opf:item id=\"fixed\" href=\"Text/fixed.xhtml\" media-type=\"application/xhtml+xml\"/>"
            "</opf:manifest><opf:spine><opf:itemref id=\"flow-ref\" idref=\"flow\"/>"
            "<opf:itemref id=\"fixed-ref\" idref=\"fixed\"/>"
            "</opf:spine></opf:package>"));
        if (!opf.ok || opf.renditionLayout != QStringLiteral("reflowable")
            || opf.fixedLayoutCount != 1
            || opf.fixedLayoutHrefs != QStringList { QStringLiteral("Text/fixed.xhtml") }) {
            return fail(QStringLiteral("refined fixed-layout scoping failed"));
        }
    }

    // ---- itemref 的 reflowable 覆盖整书 pre-paginated ----
    {
        const auto opf = VerticalLayoutAnalyzer::analyzeOpf(QStringLiteral(
            "<package><metadata><meta property=\"rendition:layout\">pre-paginated</meta>"
            "<meta property=\"rendition:layout\" refines=\"#flow-ref\">reflowable</meta>"
            "</metadata><manifest><item id=\"flow\" href=\"flow.xhtml\" "
            "media-type=\"application/xhtml+xml\"/><item id=\"fixed\" href=\"fixed.xhtml\" "
            "media-type=\"application/xhtml+xml\"/></manifest><spine>"
            "<itemref id=\"flow-ref\" idref=\"flow\"/><itemref id=\"fixed-ref\" idref=\"fixed\"/>"
            "</spine></package>"));
        if (!opf.ok || opf.fixedLayoutCount != 1
            || opf.fixedLayoutHrefs != QStringList { QStringLiteral("fixed.xhtml") }) {
            return fail(QStringLiteral("refined reflowable override failed"));
        }
    }

    // ---- EPUB 2 / iBooks legacy fixed-layout metadata ----
    {
        const auto opf = VerticalLayoutAnalyzer::analyzeOpf(QStringLiteral(
            "<package><metadata><meta name=\"fixed-layout\" content=\"true\"/>"
            "</metadata><manifest><item id=\"p\" href=\"p.xhtml\" "
            "media-type=\"application/xhtml+xml\"/></manifest>"
            "<spine><itemref idref=\"p\"/></spine></package>"));
        if (!opf.ok || opf.renditionLayout != QStringLiteral("pre-paginated")
            || opf.fixedLayoutCount != 1) {
            return fail(QStringLiteral("legacy fixed-layout metadata detection failed"));
        }
    }

    // ---- 缺少 spine 或非法翻页方向的 OPF 必须分析失败 ----
    {
        const auto no_spine = VerticalLayoutAnalyzer::analyzeOpf(QStringLiteral(
            "<package><metadata/><manifest/></package>"));
        const auto bad_progression = VerticalLayoutAnalyzer::analyzeOpf(QStringLiteral(
            "<package><metadata/><manifest/><spine page-progression-direction=\"sideways\"/>"
            "</package>"));
        if (no_spine.ok || bad_progression.ok) {
            return fail(QStringLiteral("invalid OPF semantics were accepted"));
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
