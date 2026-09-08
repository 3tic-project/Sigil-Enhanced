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
}

int main()
{
    try {
        TestParagraphsAndInlineMarkup();
        TestElementBoundaries();
        TestComplexListAndBlockquoteFallbacks();
        TestSafeFallbacks();
        TestUnicodeAndWarmIndexPerformance();
        std::cout << "Code View selection policy checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
