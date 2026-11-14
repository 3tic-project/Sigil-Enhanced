#include <QCoreApplication>
#include <QDebug>
#include "BuiltinPlugins/Translation/TranslationText.h"

using namespace BuiltinPlugins::Translation;

static void require(bool ok, const char *message)
{
    if (!ok) qFatal("%s", message);
}

static QString book(const QString &body)
{
    return QStringLiteral("<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>T</title></head><body>")
        + body + QStringLiteral("</body></html>");
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString source = book(QStringLiteral(
        "<p id=\"one\" class=\"indent\">「<ruby>名前<rt>な<br/>まえ</rt><rp>（</rp></ruby><em>です</em>」😀&amp;</p>"
        "<p>同じ</p><p>同じ</p><p><img src=\"pic.jpg\" alt=\"\"/></p>"));
    auto parsed = extract(source);
    require(parsed.error.isEmpty(), "parse valid XHTML");
    require(parsed.paragraphs.size() == 3, "ruby is a whole paragraph, image-only page skipped");
    require(parsed.paragraphs[0].text == QStringLiteral("「名前です」😀&"), "ruby reading excluded from complete source");
    require(parsed.paragraphs[0].readings.contains(QStringLiteral("なまえ")), "reading retained independently");
    QList<Replacement> edits;
    for (int i = 0; i < 3; ++i) edits.append({i, parsed.paragraphs[i].sourceKey, {}, QStringLiteral("译文%1<&").arg(i)});
    auto written = writePairs(source, edits);
    require(written.error.isEmpty() && written.applied.size() == 3, "batch inserted without model markup");
    require(written.text.contains(QStringLiteral("<ruby>名前<rt>な<br/>まえ</rt>")), "original ruby unchanged");
    require(written.text.count(QStringLiteral("id=\"one\"")) == 1, "source ID not copied");
    require(written.text.contains(QStringLiteral("译文0&lt;&amp;")), "translation escaped");
    parsed = extract(written.text);
    require(parsed.paragraphs.size() == 3 && parsed.paragraphs[2].translation == QStringLiteral("译文2<&"), "duplicates paired by location");
    edits.clear();
    edits.append({1, parsed.paragraphs[1].sourceKey, parsed.paragraphs[1].translation, QStringLiteral("校正")});
    auto revised = writePairs(written.text, edits);
    require(revised.error.isEmpty() && revised.applied.size() == 1, "update existing translation");
    require(revised.text.count(QStringLiteral("se-translation\"")) == 3, "no second translation inserted");
    require(revised.text.count(QStringLiteral("id=\"sigil-translation-style\"")) == 1, "stylesheet injected once");
    auto conflict = writePairs(revised.text, edits);
    require(conflict.applied.isEmpty() && conflict.conflicts.size() == 1, "human/current translation conflict preserved");

    const QString flow = book(QStringLiteral("<p>A<span>B<br/>C</span>D</p><ul><li>項目</li></ul>"));
    parsed = extract(flow);
    require(parsed.paragraphs.size() == 3 && parsed.paragraphs[0].text == QStringLiteral("AB")
            && parsed.paragraphs[1].text == QStringLiteral("CD"), "br across inline spans splits complete reading units");
    edits.clear();
    for (int i = 0; i < 3; ++i) edits.append({i, parsed.paragraphs[i].sourceKey, {}, QStringLiteral("译%1").arg(i)});
    written = writePairs(flow, edits);
    require(written.error.isEmpty(), "inline insertion remains well formed");
    auto after = extract(written.text);
    require(after.paragraphs.size() == 3, "inserted inline translations excluded from source");
    for (int i = 0; i < 3; ++i) {
        require(after.paragraphs[i].sourceKey == parsed.paragraphs[i].sourceKey, "source projection stable after insert");
        require(after.paragraphs[i].translation == QStringLiteral("译%1").arg(i), "inline pair recovered");
    }

    const QString toc = book(QStringLiteral("<nav epub:type=\"toc\" xmlns:epub=\"http://www.idpf.org/2007/ops\">"
        "<ol><li><a href=\"c.xhtml#c\"><span>序</span><ruby>章<rt>しょう</rt></ruby></a></li></ol></nav>"));
    parsed = extract(toc);
    require(parsed.paragraphs.size() == 1 && parsed.paragraphs[0].toc, "TOC anchor one unit");
    written = writePairs(toc, {{0, parsed.paragraphs[0].sourceKey, {}, QStringLiteral("序章")}});
    require(written.error.isEmpty() && written.text.contains(QStringLiteral("href=\"c.xhtml#c\"")), "TOC keeps target");
    require(written.text.indexOf(QStringLiteral(" / 序章")) < written.text.indexOf(QStringLiteral("</a>")), "translation inside original link");
    after = extract(written.text);
    require(after.paragraphs.size() == 1 && after.paragraphs[0].text == QStringLiteral("序章")
            && after.paragraphs[0].translation == QStringLiteral("序章"), "TOC source and translation separated");
    require(after.paragraphs[0].sourceKey == parsed.paragraphs[0].sourceKey, "TOC source projection stable");
    require(!extract(book(QStringLiteral("<p>broken</div>"))).error.isEmpty(), "invalid source rejected");
    parsed = extract(book(QStringLiteral("裸文本<span>一</span><p> </p>裸文本二<br/>裸文本三")));
    require(parsed.error.isEmpty() && parsed.paragraphs.size() == 3, "mixed spacer/body flow does not silently drop text");
    edits.clear();
    for (int i = 0; i < 3; ++i) edits.append({i, parsed.paragraphs[i].sourceKey, {}, QStringLiteral("中文%1").arg(i)});
    written = writePairs(book(QStringLiteral("裸文本<span>一</span><p> </p>裸文本二<br/>裸文本三")), edits);
    require(written.error.isEmpty() && extract(written.text).paragraphs.size() == 3, "mixed-flow inline pairs stable");
    require(normalizedTranslation(QStringLiteral("「日」『本』"), QStringLiteral("“中”‘文’")) == QStringLiteral("「中」『文』"), "safe quote normalization");
    require(normalizedTranslation(QStringLiteral("カナ"), QStringLiteral("カナ译文")) == QStringLiteral("カナ译文"), "kana never deleted");
    const QString indented = book(QStringLiteral("<p>　原文</p>"));
    parsed = extract(indented);
    written = writePairs(indented, {{0, parsed.paragraphs[0].sourceKey, {}, QStringLiteral("译文")}});
    require(written.error.isEmpty() && written.text.contains(QStringLiteral(">　译文</p>")), "fullwidth paragraph indentation preserved");
    written = writePairs(indented, {{0, parsed.paragraphs[0].sourceKey, {}, QStringLiteral("译文")}}, "zh-Hans", nullptr, 1.75);
    require(written.text.contains("margin-bottom: 1.75rem"), "configured spacing reaches generated stylesheet");
    const QString ncx = QStringLiteral("<?xml version=\"1.0\"?><ncx xmlns=\"http://www.daisy.org/z3986/2005/ncx/\" version=\"2005-1\"><head/><docTitle><text>Title</text></docTitle><navMap><navPoint id=\"n1\" playOrder=\"1\"><navLabel><text>序章😀 &amp; 続き</text></navLabel><content src=\"c.xhtml#start\"/></navPoint></navMap></ncx>");
    parsed = extract(ncx);
    require(parsed.ncx && parsed.paragraphs.size() == 1 && parsed.paragraphs.first().text == "序章😀 & 続き", "NCX reads visible navigation labels");
    written = writePairs(ncx, {{0, parsed.paragraphs.first().sourceKey, {}, "序章与后续"}});
    require(written.error.isEmpty() && written.text.contains("c.xhtml#start") && written.text.contains("playOrder=\"1\""), "NCX translation preserves hierarchy and targets");
    after = extract(written.text);
    require(after.paragraphs.first().sourceKey == parsed.paragraphs.first().sourceKey && after.paragraphs.first().translation == "序章与后续", "NCX bilingual source remains identifiable");
    revised = writePairs(written.text, {{0, after.paragraphs.first().sourceKey, "序章与后续", "修订标题"}});
    require(revised.error.isEmpty() && revised.text.count("<!--sigil-translation-->") == 1 && extract(revised.text).paragraphs.first().translation == "修订标题", "NCX review replaces the existing translation");
    qInfo() << "Translation text tests passed";
}
