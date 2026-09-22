#include <cstdlib>
#include <iostream>

#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QRawFont>

#include "FontPreview/FontGlyphCoverage.h"
#include "FontPreview/FontPreviewHtml.h"
#include "FontPreview/FontPreviewLanguage.h"
#include "FontPreview/FontPreviewSamples.h"

namespace {

void Require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void ExpectProfile(const QString &tag, FontPreviewProfile profile, const char *message)
{
    const FontPreviewProfile resolved = ResolveFontPreviewProfile(
        tag.isNull() ? QStringList() : QStringList{tag},
        FontPreviewChoice::Auto,
        {});
    Require(resolved == profile, message);
}

}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);

    ExpectProfile(QStringLiteral("zh-CN"), FontPreviewProfile::SimplifiedChinese, "zh-CN");
    ExpectProfile(QStringLiteral("zh-cn"), FontPreviewProfile::SimplifiedChinese, "zh-cn");
    ExpectProfile(QStringLiteral("ZH_CN"), FontPreviewProfile::SimplifiedChinese, "ZH_CN");
    ExpectProfile(QStringLiteral("zh-SG"), FontPreviewProfile::SimplifiedChinese, "zh-SG");
    ExpectProfile(QStringLiteral("zh-Hans"), FontPreviewProfile::SimplifiedChinese, "zh-Hans");
    ExpectProfile(QStringLiteral("zh-Hans-CN"), FontPreviewProfile::SimplifiedChinese, "zh-Hans-CN");
    ExpectProfile(QStringLiteral("zh-TW"), FontPreviewProfile::TraditionalChinese, "zh-TW");
    ExpectProfile(QStringLiteral("zh-HK"), FontPreviewProfile::TraditionalChinese, "zh-HK");
    ExpectProfile(QStringLiteral("zh-Hant"), FontPreviewProfile::TraditionalChinese, "zh-Hant");
    ExpectProfile(QStringLiteral("zh-Hant-HK"), FontPreviewProfile::TraditionalChinese, "zh-Hant-HK");
    ExpectProfile(QStringLiteral("zh"), FontPreviewProfile::ChineseMixed, "bare zh");
    ExpectProfile(QStringLiteral("ja"), FontPreviewProfile::Japanese, "ja");
    ExpectProfile(QStringLiteral("ja-JP"), FontPreviewProfile::Japanese, "ja-JP");
    ExpectProfile(QStringLiteral("en"), FontPreviewProfile::English, "en");
    ExpectProfile(QStringLiteral("en-US"), FontPreviewProfile::English, "en-US");
    ExpectProfile(QStringLiteral("en-GB"), FontPreviewProfile::English, "en-GB");

    const FontPreviewProfile und = ResolveFontPreviewProfile(
        {QStringLiteral("und")}, FontPreviewChoice::Auto, {QFontDatabase::Latin});
    Require(und == FontPreviewProfile::English, "und with a Latin font falls back to English");
    const FontPreviewProfile empty = ResolveFontPreviewProfile(
        {}, FontPreviewChoice::Auto, {QFontDatabase::Japanese, QFontDatabase::Latin});
    Require(empty == FontPreviewProfile::Japanese, "one CJK writing system wins over Latin");
    const FontPreviewProfile pan = ResolveFontPreviewProfile(
        {}, FontPreviewChoice::Auto,
        {QFontDatabase::SimplifiedChinese, QFontDatabase::TraditionalChinese, QFontDatabase::Japanese});
    Require(pan == FontPreviewProfile::Generic, "several CJK systems use the generic sample");
    const FontPreviewProfile manual = ResolveFontPreviewProfile(
        {QStringLiteral("zh-CN")}, FontPreviewChoice::Japanese, {QFontDatabase::Latin});
    Require(manual == FontPreviewProfile::Japanese, "manual choice overrides the book language");

    const FontPreviewSample &simplified = FontPreviewSampleFor(FontPreviewProfile::SimplifiedChinese);
    const FontPreviewSample &traditional = FontPreviewSampleFor(FontPreviewProfile::TraditionalChinese);
    const FontPreviewSample &japanese = FontPreviewSampleFor(FontPreviewProfile::Japanese);
    const FontPreviewSample &english = FontPreviewSampleFor(FontPreviewProfile::English);
    const QString simplifiedText = FontPreviewSpecimenText(simplified);
    const QString traditionalText = FontPreviewSpecimenText(traditional);
    const QString japaneseText = FontPreviewSpecimenText(japanese);
    const QString englishText = FontPreviewSpecimenText(english);
    Require(simplifiedText.contains(QStringLiteral("吃人")) && simplifiedText.contains(QStringLiteral("孔乙己"))
                && simplifiedText.contains(QStringLiteral("云雀")) && simplifiedText.contains(QStringLiteral("学而时习之")),
            "simplified sample must keep several original passages");
    Require(!simplifiedText.contains(QStringLiteral("滿紙荒唐言")),
            "simplified sample must not reuse the traditional poem");
    Require(traditionalText.contains(QStringLiteral("都云作者痴"))
                && !traditionalText.contains(QStringLiteral("都雲作者痴"))
                && traditionalText.contains(QStringLiteral("花謝花飛"))
                && traditionalText.contains(QStringLiteral("我與父親"))
                && traditionalText.contains(QStringLiteral("關關雎鳩")),
            "traditional sample must keep original characters, including 云 not 雲");
    Require(!traditionalText.contains(QStringLiteral("吃人")),
            "traditional sample must not copy the simplified story");
    Require(japaneseText.contains(QStringLiteral("吾輩は猫である"))
                && japaneseText.contains(QStringLiteral("ニャー"))
                && japaneseText.contains(QStringLiteral("イギリス"))
                && japaneseText.contains(QStringLiteral("やうやう"))
                && japaneseText.contains(QStringLiteral("がぎぐげご"))
                && japaneseText.contains(QStringLiteral("パピプペポ")),
            "Japanese sample must include prose, katakana, historical kana, and the kana charts");
    Require(englishText.contains(QStringLiteral("truth universally acknowledged"))
                && englishText.contains(QStringLiteral("Jabberwock"))
                && englishText.contains(QStringLiteral("explicit"))
                && englishText.contains(QStringLiteral("lazy dog"))
                && englishText.contains(QStringLiteral("question")),
            "English sample must keep the literary lines and the pangram");
    Require(!englishText.contains(QStringLiteral("汉字排版")),
            "samples must not keep the composed typography sentence");

    const QString marked = MarkMissingGlyphs(QStringLiteral("A&B<"), {uint('A')},
                                             QStringLiteral("Not present \"here\""));
    Require(marked.contains(QStringLiteral("missing-glyph")) && marked.contains(QStringLiteral("&amp;"))
                && marked.contains(QStringLiteral("&lt;")) && marked.contains(QStringLiteral("&quot;")),
            "missing glyphs must be marked without breaking HTML");
    Require(!marked.mid(marked.indexOf(QStringLiteral(">")) + 1, 1).contains(QLatin1Char('A'))
                || marked.contains(QStringLiteral(">A</span>")),
            "the missing letter itself must remain visible");

    const QFont uiFont = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    const QRawFont raw = QRawFont::fromFont(uiFont);
    Require(raw.isValid(), "the system UI font must load");
    const GlyphCoverageResult coverage = AnalyzeGlyphCoverage(raw, QStringLiteral("A  A\n\uE000"));
    Require(coverage.total == 2, "spaces are ignored and repeated letters count once");
    Require(coverage.missing.contains(uint('A')) == !raw.supportsCharacter(uint('A')),
            "coverage must agree with supportsCharacter");
    if (!raw.supportsCharacter(0xE000)) {
        Require(coverage.missing.contains(0xE000), "a private-use character absent from the font is missing");
    }

    FontPreviewDocument document;
    document.fontUrl = QStringLiteral("font.ttf");
    document.fontWeight = QStringLiteral("400");
    document.fontStyle = QStringLiteral("normal");
    document.description = QStringLiteral("Test");
    document.fileName = QStringLiteral("Test.ttf");
    document.sample = FontPreviewSampleFor(FontPreviewProfile::Generic);
    document.omitFullyMissingLines = true;
    document.coverage = AnalyzeGlyphCoverage(raw, FontPreviewSpecimenText(document.sample));
    const QString html = BuildFontPreviewHtml(document);
    Require(html.contains(QStringLiteral("SigilPreviewFace")) && html.contains(QStringLiteral("Aa Bb Cc")),
            "generic preview must keep the Latin line");
    if (!raw.supportsCharacter(QStringLiteral("汉").at(0).unicode())) {
        Require(!html.contains(QStringLiteral("汉")),
                "a Latin font must not present fallback Chinese as if the font contained it");
    }

    const QString titleFont = QStringLiteral("/tmp/epub-fonts/title.ttf");
    if (QFileInfo::exists(titleFont)) {
        const QRawFont title(titleFont, 32.0);
        Require(title.isValid(), "title.ttf must load");
        Require(PreferredFontFamilyName(title) == QStringLiteral("迷你简书魂"),
                "title.ttf must use its Unicode family name");
        Require(title.supportsCharacter(QStringLiteral("本").at(0).unicode()) && !GlyphHasInk(title, QChar(u'本').unicode()),
                "a cmap hit with an empty outline is not ink");
        const GlyphCoverageResult titleCoverage = AnalyzeGlyphCoverage(
            title, QStringLiteral("满本都写着两个字是“吃人”！"));
        Require(titleCoverage.supported < titleCoverage.total,
                "title.ttf must not report full coverage when glyphs are blank");
        Require(titleCoverage.missing.contains(QChar(u'本').unicode()),
                "the blank 本 glyph must be reported missing");
    }

    return EXIT_SUCCESS;
}
