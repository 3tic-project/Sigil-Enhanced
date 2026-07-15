#include <cstdlib>
#include <iostream>

#include "ChineseConversion/ChineseTextConversionPlan.h"
#include "ChineseConversion/OpenCCConverter.h"

namespace
{

void Require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

OpenCCConverter MakeConverter()
{
    return OpenCCConverter(
        ChineseConversionProfile::ForMode(ChineseConversionMode::S2T),
        QStringLiteral(SIGIL_OPENCC_TEST_DATA_DIR));
}

}

int main()
{
    OpenCCConverter converter = MakeConverter();
    Require(converter.IsValid(), "OpenCC converter is not valid");

    const QString xhtml = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head>\n"
        "  <title>汉字标题</title></head><body id=\"汉字标识\">\n"
        "  <p class=\"汉字类\" title='简体提示'>汉&amp;字"
        "<a href=\"汉字.xhtml#章节\">汉字</a></p>\n"
        "  <p lang=\"ja-JP\">汉字日文</p><code>汉字代码</code>\n"
        "  <pre>汉字诗歌</pre><script>var label = '汉字脚本';</script>\n"
        "  <ruby>汉字<rt>かんじ</rt></ruby>\n"
        "</body></html>\n");

    ChineseConversionOptions options;
    const ChineseTextConversionPlan plan = ChineseTextConversionPlan::Build(
        xhtml, ChineseDocumentKind::Xhtml, options, converter);
    Require(plan.IsValid(), "XHTML conversion plan is invalid");
    Require(plan.HasChanges(), "XHTML conversion plan is empty");
    Require(plan.SkippedJapaneseSegments() == 1,
            "Japanese language inheritance was not recorded");
    Require(plan.SkippedProtectedSegments() >= 2,
            "protected elements were not recorded");

    QString applyError;
    const QString converted = plan.Apply(&applyError);
    Require(applyError.isEmpty(), "XHTML plan apply failed");
    Require(converted.startsWith(QStringLiteral(
                "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n")),
            "XML declaration or leading formatting changed");
    Require(converted.contains(QStringLiteral("<title>汉字标题</title>")),
            "head title was converted without metadata opt-in");
    Require(converted.contains(QStringLiteral("id=\"汉字标识\"")),
            "id attribute was converted");
    Require(converted.contains(QStringLiteral("class=\"汉字类\"")),
            "class attribute was converted");
    Require(converted.contains(QStringLiteral("href=\"汉字.xhtml#章节\"")),
            "href attribute was converted");
    Require(converted.contains(QStringLiteral("title='簡體提示'")),
            "allowed title attribute was not converted or quote style changed");
    Require(converted.contains(QStringLiteral("漢&amp;字")),
            "text entity was not preserved safely");
    Require(converted.contains(QStringLiteral(">漢字</a>")),
            "link text was not converted");
    Require(converted.contains(QStringLiteral("lang=\"ja-JP\">汉字日文")),
            "Japanese text was converted");
    Require(converted.contains(QStringLiteral("<code>汉字代码</code>")),
            "code element was converted");
    Require(converted.contains(QStringLiteral("<pre>漢字詩歌</pre>")),
            "pre element should be converted by default");
    Require(converted.contains(QStringLiteral("'汉字脚本'")),
            "script element was converted");
    Require(converted.contains(QStringLiteral("<ruby>漢字<rt>かんじ</rt></ruby>")),
            "ruby structure or text conversion is incorrect");

    ChineseConversionOptions textOnlyOptions;
    textOnlyOptions.includeAltText = false;
    textOnlyOptions.includeTitleAttributes = false;
    textOnlyOptions.includeAriaLabels = false;
    const QString textOnly = ChineseTextConversionPlan::Build(
        xhtml, ChineseDocumentKind::Xhtml, textOnlyOptions, converter).Apply();
    Require(textOnly.contains(QStringLiteral("title='简体提示'")),
            "disabled title attribute conversion was ignored");

    const QString twoSegments = QStringLiteral(
        "<html><body><p>汉字</p><p>转换</p></body></html>");
    const ChineseTextConversionPlan partialPlan = ChineseTextConversionPlan::Build(
        twoSegments, ChineseDocumentKind::Xhtml, options, converter);
    Require(partialPlan.Changes().size() == 2,
            "partial preview fixture did not produce two changes");
    const QString partiallyConverted = partialPlan.Apply(QSet<int> { 0 });
    Require(partiallyConverted.contains(QStringLiteral("<p>漢字</p>")),
            "enabled preview change was not applied");
    Require(partiallyConverted.contains(QStringLiteral("<p>转换</p>")),
            "disabled preview change was applied");

    const QString svg = QStringLiteral(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" id=\"汉字图\" "
        "aria-label=\"汉字插图\"><title>汉字标题</title>"
        "<path id=\"汉字路径\" d=\"M 0 0 L 1 1\"/>"
        "<text>汉字<tspan>转换</tspan></text><metadata>汉字机器数据</metadata></svg>");
    const ChineseTextConversionPlan svgPlan = ChineseTextConversionPlan::Build(
        svg, ChineseDocumentKind::Svg, options, converter);
    Require(svgPlan.IsValid(), "SVG conversion plan is invalid");
    const QString convertedSvg = svgPlan.Apply();
    Require(convertedSvg.contains(QStringLiteral("aria-label=\"漢字插圖\"")),
            "SVG accessible label was not converted");
    Require(convertedSvg.contains(QStringLiteral("<title>漢字標題</title>")),
            "SVG title was not converted");
    Require(convertedSvg.contains(QStringLiteral("<text>漢字<tspan>轉換</tspan></text>")),
            "SVG text was not converted");
    Require(convertedSvg.contains(QStringLiteral("id=\"汉字路径\"")),
            "SVG id was converted");
    Require(convertedSvg.contains(QStringLiteral("<metadata>汉字机器数据</metadata>")),
            "SVG metadata was converted");
    Require(convertedSvg.contains(QStringLiteral("d=\"M 0 0 L 1 1\"")),
            "SVG path data changed");

    return EXIT_SUCCESS;
}
