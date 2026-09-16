#include <QElapsedTimer>
#include <QList>
#include <QString>

#include <iostream>
#include <stdexcept>

#include "Parsers/TagLister.h"
#include "ViewEditors/CodeViewSelectionPolicy.h"

using Result = CodeViewSelectionPolicy::Result;
using Reason = CodeViewSelectionPolicy::Reason;

static void Require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

static Result Select(const QString &source, const QString &needle, int offset = 0)
{
    const int position = source.indexOf(needle);
    Require(position >= 0, "Selection test needle is missing");
    TagLister tags(source);
    return CodeViewSelectionPolicy::FindTextUnit(source, tags, position + offset);
}

static Result SelectSentence(const QString &source, const QString &needle,
                             int offset = 0)
{
    const int position = source.indexOf(needle);
    Require(position >= 0, "Sentence selection test needle is missing");
    TagLister tags(source);
    return CodeViewSelectionPolicy::FindSentence(
        source, tags, position + offset);
}

static void RequireSelection(const QString &source, const QString &needle,
                             const QString &expected, const char *message)
{
    const Result result = Select(source, needle);
    Require(result.hasSelection(), message);
    Require(source.mid(result.start, result.end - result.start) == expected, message);
    Require(result.outerStart >= 0 && result.outerStart < result.start
                && result.outerEnd > result.end,
            "Outer element range is invalid");
}

static void TestParagraphsAndInlineMarkup()
{
    const QString source = QString::fromUtf8(
        "<html><body><p>　春天的<ruby>風<rt>かぜ</rt></ruby>，很轻。</p>"
        "<h2>日本語の見出し</h2></body></html>");
    RequireSelection(source, QString::fromUtf8("春"),
                     QString::fromUtf8("　春天的<ruby>風<rt>かぜ</rt></ruby>，很轻。"),
                     "Chinese paragraph content was not selected exactly");
    RequireSelection(source, QString::fromUtf8("かぜ"),
                     QString::fromUtf8("　春天的<ruby>風<rt>かぜ</rt></ruby>，很轻。"),
                     "Ruby annotation did not resolve to its paragraph");
    RequireSelection(source, QString::fromUtf8("日本語"),
                     QString::fromUtf8("日本語の見出し"),
                     "Japanese heading content was not selected");

    const QString entity = QStringLiteral(
        "<html><body><p>A&amp;B <span>one</span><em>two</em></p></body></html>");
    RequireSelection(entity, QStringLiteral("amp"),
                     QStringLiteral("A&amp;B <span>one</span><em>two</em>"),
                     "Entity or inline markup was split");
}

static void TestElementBoundaries()
{
    const QString same_line = QStringLiteral(
        "<html><body><div>first</div><div>second</div></body></html>");
    RequireSelection(same_line, QStringLiteral("first"), QStringLiteral("first"),
                     "First same-line div expanded into its sibling");
    RequireSelection(same_line, QStringLiteral("second"), QStringLiteral("second"),
                     "Second same-line div expanded into its sibling");

    const QString multiline = QStringLiteral(
        "<html><body><p>\n  first <span>middle</span>\n  last\n</p></body></html>");
    RequireSelection(multiline, QStringLiteral("middle"),
                     QStringLiteral("\n  first <span>middle</span>\n  last\n"),
                     "Multiline source whitespace was not preserved");

    const QString chapter = QStringLiteral(
        "<html><body><div class='main'>before<h1>Title</h1><p>Body</p>after</div></body></html>");
    RequireSelection(chapter, QStringLiteral("Title"), QStringLiteral("Title"),
                     "Heading inside a chapter wrapper was not selected");
    RequireSelection(chapter, QStringLiteral("Body"), QStringLiteral("Body"),
                     "Paragraph inside a chapter wrapper was not selected");
    Require(Select(chapter, QStringLiteral("before")).reason == Reason::NoTextUnit,
            "Structural chapter div was selected as one text unit");
}

static void TestComplexListAndBlockquoteFallbacks()
{
    const QString list = QStringLiteral(
        "<html><body><ul><li>Parent<ul><li>Child</li></ul></li></ul></body></html>");
    Require(Select(list, QStringLiteral("Parent")).reason == Reason::NoTextUnit,
            "List item with a nested list expanded across structure");
    RequireSelection(list, QStringLiteral("Child"), QStringLiteral("Child"),
                     "Leaf nested list item was not selected");

    const QString direct_quote = QStringLiteral(
        "<html><body><blockquote>Direct quote</blockquote></body></html>");
    RequireSelection(direct_quote, QStringLiteral("Direct"), QStringLiteral("Direct quote"),
                     "Direct-text blockquote was not selected");

    const QString paragraphs = QStringLiteral(
        "<html><body><blockquote><p>First</p><p>Second</p></blockquote></body></html>");
    RequireSelection(paragraphs, QStringLiteral("Second"), QStringLiteral("Second"),
                     "Paragraph inside blockquote expanded into multiple paragraphs");
}

static void TestSafeFallbacks()
{
    const QString attribute = QStringLiteral(
        "<html><body><p class='alpha'>text</p></body></html>");
    Require(Select(attribute, QStringLiteral("alpha")).reason == Reason::InMarkup,
            "Attribute click did not preserve markup selection behavior");

    const QString unsafe = QStringLiteral(
        "<html><body><p>safe <code>inline</code></p><pre>raw</pre>"
        "<script>value</script><svg><text>vector</text></svg></body></html>");
    for (const QString &needle : { QStringLiteral("inline"), QStringLiteral("raw"),
                                  QStringLiteral("value"), QStringLiteral("vector") }) {
        Require(Select(unsafe, needle).reason == Reason::UnsafeContainer,
                "Unsafe source container did not fall back");
    }

    const QString special = QStringLiteral(
        "<html><body><p><!-- comment --><![CDATA[raw]]>visible</p></body></html>");
    Require(Select(special, QStringLiteral("comment")).reason == Reason::InMarkup,
            "Comment click did not fall back");
    Require(Select(special, QStringLiteral("raw")).reason == Reason::InMarkup,
            "CDATA click did not fall back");

    const QString empty = QStringLiteral(
        "<html><body><p> \n <br/><img src='x'/> </p></body></html>");
    Require(Select(empty, QStringLiteral(" \n")).reason == Reason::EmptyContent,
            "Empty element expanded to a non-empty ancestor");

    const QString malformed = QStringLiteral("<html><body><p>broken</body></html>");
    Require(!Select(malformed, QStringLiteral("broken")).hasSelection(),
            "Unclosed paragraph produced an expanded selection");
}

static void RequireSentence(const QString &source, const QString &needle,
                            const QString &expected, const char *message)
{
    const Result result = SelectSentence(source, needle);
    Require(result.hasSelection(), message);
    Require(source.mid(result.start, result.end - result.start) == expected,
            message);
}

static void TestSentenceProjectionAndBoundaries()
{
    const QString cjk = QString::fromUtf8(
        "<html><body><p>第一句。第二句含<ruby>風<rt>かぜ</rt></ruby>！第三句？</p>"
        "</body></html>");
    RequireSentence(cjk, QString::fromUtf8("第二"),
                    QString::fromUtf8(
                        "第二句含<ruby>風<rt>かぜ</rt></ruby>！"),
                    "CJK sentence selection split Ruby or sentence punctuation");
    RequireSentence(cjk, QString::fromUtf8("かぜ"),
                    QString::fromUtf8(
                        "第二句含<ruby>風<rt>かぜ</rt></ruby>！"),
                    "Ruby annotation did not map back to its source sentence");

    const QString inline_wrapper = QStringLiteral(
        "<html><body><p><span>Wrapped sentence.</span> Plain next.</p>"
        "</body></html>");
    RequireSentence(inline_wrapper, QStringLiteral("Wrapped"),
                    QStringLiteral("<span>Wrapped sentence.</span>"),
                    "A fully selected inline wrapper was not preserved");

    const QString partial_wrapper = QStringLiteral(
        "<html><body><p><span>First sentence. Second remains.</span> Third.</p>"
        "</body></html>");
    RequireSentence(partial_wrapper, QStringLiteral("First"),
                    QStringLiteral("First sentence."),
                    "A partial inline wrapper expanded past the sentence boundary");

    const QString entities = QString::fromUtf8(
        "<html><body><p>First&#x3002;Second &amp; final! “Third?”</p>"
        "</body></html>");
    RequireSentence(entities, QStringLiteral("First"),
                    QStringLiteral("First&#x3002;"),
                    "Numeric punctuation entity was split or ignored");
    RequireSentence(entities, QStringLiteral("amp"),
                    QStringLiteral("Second &amp; final!"),
                    "A click inside an entity did not select its exact sentence source");
    RequireSentence(entities, QStringLiteral("Third"),
                    QString::fromUtf8("“Third?”"),
                    "Trailing sentence quote was not preserved");

    const QString english = QStringLiteral(
        "<html><body><p>Value 3.14 stays. Visit example.com now. Last.</p>"
        "</body></html>");
    RequireSentence(english, QStringLiteral("3.14"),
                    QStringLiteral("Value 3.14 stays."),
                    "Decimal punctuation was treated as a sentence boundary");
    RequireSentence(english, QStringLiteral("example.com"),
                    QStringLiteral("Visit example.com now."),
                    "URL punctuation was treated as a sentence boundary");

    const QString unbalanced = QStringLiteral(
        "<html><body><p><em>First sentence. Second crosses</em> outside.</p>"
        "</body></html>");
    const Result fallback = SelectSentence(unbalanced, QStringLiteral("Second"));
    Require(fallback.reason == Reason::SentenceFallbackElement
                && unbalanced.mid(fallback.start, fallback.end - fallback.start)
                    == QStringLiteral(
                        "<em>First sentence. Second crosses</em> outside."),
            "A sentence crossing half an inline element did not fall back safely");

    const QString unsafe = QStringLiteral(
        "<html><body><p>Safe sentence. <code>raw</code> Next.</p></body></html>");
    Require(SelectSentence(unsafe, QStringLiteral("Safe")).reason
                == Reason::UnsafeContainer,
            "Sentence mode expanded a text unit containing unsafe source markup");
}

static void TestUnicodeAndWarmIndexPerformance()
{
    const QString unicode = QString::fromUtf8(
        "<html><body><p>👨‍👩‍👧‍👦 𠮷 é</p></body></html>");
    RequireSelection(unicode, QString::fromUtf8("👨"), QString::fromUtf8("👨‍👩‍👧‍👦 𠮷 é"),
                     "Unicode text unit was split");

    QString large = QStringLiteral("<html><body>");
    large.reserve(1024 * 1024);
    for (int index = 0; index < 20000; ++index) {
        large += QStringLiteral("<p>entry-%1-abcdefghijklmnopqrstuvwxyz</p>").arg(index);
    }
    large += QStringLiteral("</body></html>");
    TagLister tags(large);
    const int position = large.lastIndexOf(QStringLiteral("entry-19999"));
    QList<qint64> samples;
    for (int sample = 0; sample < 20; ++sample) {
        QElapsedTimer timer;
        timer.start();
        const Result result = CodeViewSelectionPolicy::FindTextUnit(large, tags, position);
        samples.append(timer.nsecsElapsed());
        Require(result.hasSelection()
                    && large.mid(result.start, result.end - result.start)
                        == QStringLiteral("entry-19999-abcdefghijklmnopqrstuvwxyz"),
                "Large warm index selected the wrong paragraph");
    }
    std::sort(samples.begin(), samples.end());
    const qint64 p95_milliseconds = samples.at(18) / 1000000;
    Require(p95_milliseconds <= 50,
            "Warm 20,000-tag selection P95 exceeded 50 ms");

    QList<qint64> sentence_samples;
    for (int sample = 0; sample < 20; ++sample) {
        QElapsedTimer timer;
        timer.start();
        const Result result = CodeViewSelectionPolicy::FindSentence(
            large, tags, position);
        sentence_samples.append(timer.nsecsElapsed());
        Require(result.hasSelection()
                    && large.mid(result.start, result.end - result.start)
                        == QStringLiteral(
                            "entry-19999-abcdefghijklmnopqrstuvwxyz"),
                "Large warm sentence selection chose the wrong paragraph");
    }
    std::sort(sentence_samples.begin(), sentence_samples.end());
    const qint64 sentence_p95_milliseconds = sentence_samples.at(18) / 1000000;
    Require(sentence_p95_milliseconds <= 50,
            "Warm 20,000-tag sentence selection P95 exceeded 50 ms");

    QString inline_heavy = QStringLiteral("<html><body><p>");
    inline_heavy.reserve(80000);
    for (int index = 0; index < 2000; ++index) {
        inline_heavy += QStringLiteral("<span>chunk-%1 </span>").arg(index);
    }
    inline_heavy += QStringLiteral("Final sentence.</p></body></html>");
    TagLister inline_tags(inline_heavy);
    const int inline_position = inline_heavy.indexOf(
        QStringLiteral("Final sentence"));
    QList<qint64> inline_samples;
    for (int sample = 0; sample < 5; ++sample) {
        QElapsedTimer timer;
        timer.start();
        const Result result = CodeViewSelectionPolicy::FindSentence(
            inline_heavy, inline_tags, inline_position);
        inline_samples.append(timer.nsecsElapsed());
        Require(result.hasSelection()
                    && result.start == inline_heavy.indexOf(QStringLiteral("<span>"))
                    && inline_heavy.mid(result.start, result.end - result.start)
                           .endsWith(QStringLiteral("Final sentence.")),
                "Inline-heavy sentence selection did not preserve complete markup");
    }
    std::sort(inline_samples.begin(), inline_samples.end());
    Require(inline_samples.constLast() / 1000000 <= 100,
            "Inline-heavy warm sentence selection exceeded 100 ms");

    const QString oversized_sentence = QStringLiteral("<html><body><p>")
        + QString(150000, QLatin1Char('a'))
        + QStringLiteral(".</p></body></html>");
    TagLister oversized_tags(oversized_sentence);
    const int oversized_position = oversized_sentence.indexOf(
        QString(32, QLatin1Char('a')), 70000);
    QElapsedTimer oversized_timer;
    oversized_timer.start();
    const Result oversized_result = CodeViewSelectionPolicy::FindSentence(
        oversized_sentence, oversized_tags, oversized_position);
    const qint64 oversized_milliseconds = oversized_timer.elapsed();
    Require(!oversized_result.hasSelection()
                && oversized_result.reason == Reason::NoSentence,
            "An oversized ambiguous sentence did not fall back to word selection");
    Require(oversized_milliseconds <= 100,
            "Oversized sentence fallback exceeded 100 ms");

    QString megabyte = QStringLiteral("<html><body><p>");
    megabyte += QStringLiteral("A short sentence. ").repeated(60000);
    megabyte += QStringLiteral("Final megabyte sentence.</p></body></html>");
    TagLister megabyte_tags(megabyte);
    const int megabyte_position = megabyte.indexOf(
        QStringLiteral("Final megabyte"));
    QElapsedTimer megabyte_timer;
    megabyte_timer.start();
    const Result megabyte_result = CodeViewSelectionPolicy::FindSentence(
        megabyte, megabyte_tags, megabyte_position);
    const qint64 megabyte_milliseconds = megabyte_timer.elapsed();
    Require(megabyte_result.hasSelection()
                && megabyte.mid(megabyte_result.start,
                                megabyte_result.end - megabyte_result.start)
                    == QStringLiteral("Final megabyte sentence."),
            "Megabyte sentence selection chose the wrong source range");
    Require(megabyte_milliseconds <= 100,
            "Megabyte warm sentence selection exceeded 100 ms");
}

int main()
{
    try {
        TestParagraphsAndInlineMarkup();
        TestElementBoundaries();
        TestComplexListAndBlockquoteFallbacks();
        TestSafeFallbacks();
        TestSentenceProjectionAndBoundaries();
        TestUnicodeAndWarmIndexPerformance();
        std::cout << "Code View selection policy checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
