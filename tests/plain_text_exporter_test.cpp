#include <QCoreApplication>
#include <QFile>
#include <QStringList>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

#include "BuiltinPlugins/PlainTextExporter.h"

using Exporter = BuiltinPlugins::PlainTextExporter;

static void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

static QString xhtml(const QString& body)
{
    return QStringLiteral("<?xml version='1.0' encoding='UTF-8'?><!DOCTYPE html>"
        "<html xmlns='http://www.w3.org/1999/xhtml'><head><script src='a.js'/>"
        "<style/><title>Metadata title</title></head><body>") + body + QStringLiteral("</body></html>");
}

static void expect(const QString& body, const QString& expected)
{
    const auto result = Exporter::render({{QStringLiteral("chapter.xhtml"), xhtml(body)}});
    if (!result.ok() || result.text != expected) {
        std::cerr << "Expected: " << expected.toStdString() << "\nActual: "
                  << result.text.toStdString() << "\nError: " << result.error.toStdString() << '\n';
        throw std::runtime_error("Plain-text extraction changed or omitted content");
    }
}

static QByteArray read(const QString& path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read exported TXT");
    return file.readAll();
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        expect(QStringLiteral("\n <h1>第一章 &amp; &lt;标题&gt;</h1>\n"
                              "<div>前<span>缀</span><p>　中文é𠮷\u00a0</p>尾文</div>"),
               QStringLiteral("第一章 & <标题>\n前缀\n　中文é𠮷\u00a0\n尾文\n"));
        expect(QStringLiteral("<p>彼女は<ruby>漢<rt>かん</rt>字<rp>(</rp><rt>じ</rt><rp>)</rp>"
                              "<rtc><rt>かんじ</rt></rtc></ruby>です。</p>"
                              "<p class='orion-translation'>她是译文。</p>"),
               QStringLiteral("彼女は漢字です。\n她是译文。\n"));
        expect(QStringLiteral("<p>one <em>two</em>\n three<br/>four<br/><br/>five</p>"
                              "<!-- comment --><div><div>six</div><div>seven</div></div>"),
               QStringLiteral("one two three\nfour\n\nfive\nsix\nseven\n"));
        expect(QStringLiteral("<ul><li>一</li><li>二</li></ul>"
                              "<table><tr><th>A</th><th>B</th></tr><tr><td>C</td><td>D</td></tr></table>"),
               QStringLiteral("一\n二\nA\tB\nC\tD\n"));
        expect(QStringLiteral("<p>Before</p><pre>  code\n\tindented  text\n  end</pre><p>After</p>"),
               QStringLiteral("Before\n  code\n\tindented  text\n  end\nAfter\n"));
        expect(QStringLiteral("<p>Visible<script>bad()</script><style>.bad{}</style>"
                              "<span hidden='hidden'>hidden</span><span style='DISPLAY : none !important'>hidden2</span>"
                              "<span style='color:red; visibility: hidden;'>hidden3</span>"
                              "<template><p>template</p></template> text</p><p><img alt='插图说明'/></p>"),
               QStringLiteral("Visible text\n插图说明\n"));
        expect(QStringLiteral("<nav><h2>目录</h2><ol><li>Chapter</li></ol></nav>"
                              "<svg xmlns='http://www.w3.org/2000/svg'><title>Tooltip</title>"
                              "<text>SVG <tspan>文字</tspan></text><text>第二行</text></svg>"),
               QStringLiteral("目录\nChapter\nSVG 文字\n第二行\n"));
        const auto svg = Exporter::render({{"chapter.svg", "<svg xmlns='http://www.w3.org/2000/svg'><text>独立 SVG</text></svg>"}});
        require(svg.ok() && svg.text == QStringLiteral("独立 SVG\n"), "Standalone SVG text was lost");
        const auto prefixed = Exporter::render({{"prefixed.xhtml",
            "<x:html xmlns:x='http://www.w3.org/1999/xhtml'><x:head><x:title>Metadata</x:title></x:head>"
            "<x:body><x:h1>Title</x:h1><x:p>One<x:br/>Two</x:p><x:p>Three</x:p></x:body></x:html>"}});
        require(prefixed.ok() && prefixed.text == "Title\nOne\nTwo\nThree\n",
                "Namespace-prefixed XHTML lost paragraph boundaries or leaked metadata");

        QString longChapter;
        QStringList expectedLines;
        for (int i = 0; i < 12000; ++i) {
            longChapter += QStringLiteral("<div>第%1段<span>正文𠮷</span></div>\n").arg(i);
            expectedLines.append(QStringLiteral("第%1段正文𠮷").arg(i));
        }
        const auto large = Exporter::render({{"long.xhtml", xhtml(longChapter)}});
        require(large.ok() && large.text == expectedLines.join(QLatin1Char('\n')) + QLatin1Char('\n'),
                "A long chapter lost or duplicated paragraphs");

        const auto ordered = Exporter::render({{"z.xhtml", xhtml("<p>First</p>")},
                                               {"empty.xhtml", xhtml("<img src='cover.jpg'/>")},
                                               {"a.xhtml", xhtml("<p>Second</p>")}});
        require(ordered.ok() && ordered.text == "First\n\nSecond\n", "Document boundaries/order changed");
        QTemporaryDir directory;
        require(directory.isValid(), "Cannot create TXT export test directory");
        const QString path = directory.filePath(QStringLiteral("中文 导出.txt"));
        QString error;
        require(Exporter::exportToFile({{"chapter.xhtml", xhtml("<p>原文 &amp; 译文𠮷</p>")}}, path, &error),
                "Cannot export UTF-8 TXT");
        const QByteArray original = read(path);
        require(original == QStringLiteral("原文 & 译文𠮷\n").toUtf8(), "Export changed UTF-8 bytes or added a BOM");
        require(!Exporter::exportToFile({{"empty.xhtml", xhtml("<img src='cover.jpg'/>")}}, path, &error) &&
                !error.isEmpty() && read(path) == original, "Empty export damaged an existing file");
        QString nested;
        for (int i = 0; i < 450; ++i) nested += QStringLiteral("<div>");
        nested += QStringLiteral("must not export a partial book");
        for (int i = 0; i < 450; ++i) nested += QStringLiteral("</div>");
        require(!Exporter::exportToFile({{"too-deep.xhtml", xhtml(nested)}}, path, &error) &&
                error.contains("too-deep.xhtml") && read(path) == original,
                "Parser limit produced a partial TXT or damaged the previous output");
        require(!Exporter::exportToFile({{"chapter.xhtml", xhtml("<p>text</p>")}}, directory.path(), &error) &&
                !error.isEmpty(), "Write failure was reported as success");
        require(Exporter::exportToFile({{"chapter.xhtml", xhtml("<p>Replacement</p>")}}, path, &error) &&
                error.isEmpty() && read(path) == "Replacement\n", "Could not replace the previous TXT");
        std::cout << "Plain-text export extraction and file persistence passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
