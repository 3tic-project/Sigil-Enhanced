#include <cstdlib>
#include <iostream>

#include <QFile>

#include "BookManipulation/FontSubset/FontSubsetController.h"

namespace
{

using namespace FontSubset;

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

QByteArray ReadFixture()
{
    QFile file(QStringLiteral(SIGIL_FONT_TEST_FIXTURE));
    Require(file.open(QIODevice::ReadOnly), "could not read font fixture");
    return file.readAll();
}

void TestBatchAnalysis()
{
    BookSnapshot snapshot;
    snapshot.warnings.append(QStringLiteral("snapshot warning"));
    snapshot.textSources.append(
        {QStringLiteral("Styles/Fonts.css"), QStringLiteral("text/css"),
         QStringLiteral("@font-face { font-family: \"Song\"; src: url(../Fonts/Missing.ttf); }"
                        "@font-face { font-family: \"fzxbsf\"; src: url(../Fonts/OpenSans-Test.ttf); }"
                        "@font-face { font-family: \"kxzd\"; src: url(../Fonts/Kxzd.ttf); }")});
    snapshot.textSources.append(
        {QStringLiteral("Styles/Main.css"), QStringLiteral("text/css"),
         QStringLiteral("p { font-family: \"Song\"; }"
                        "span.background-text { font-family: \"fzxbsf\"; }"
                        "li.background-text { font-family: \"fzxbsf\"; }"
                        "div.people { font-family: \"kxzd\"; }"
                        "div.people > i { font-family: \"fzxbsf\"; }"
                        ".song { font-family: \"Song\"; }"
                        ".mix { font-family: \"Song\", \"fzxbsf\"; }"
                        "em { font-family: \"fzxbsf\" !important; }"
                        "span.background-text + span { font-family: \"kxzd\"; }"
                        "p::before { content: \"IGNORED\"; font-family: \"fzxbsf\"; }")});
    snapshot.textSources.append(
        {QStringLiteral("Text/chapter.xhtml"),
         QStringLiteral("application/xhtml+xml"),
         QStringLiteral("<html xmlns=\"http://www.w3.org/1999/xhtml\"><head>"
                        "<link rel=\"stylesheet\" href=\"../Styles/Fonts.css\"/>"
                        "<link rel=\"stylesheet\" href=\"../Styles/Main.css\"/>"
                        "</head><body>"
                        "<p>Hello<span class=\"background-text\">Hi中</span><span>W</span>"
                        "<em style=\"font-family: kxzd\">Y</em></p>"
                        "<div class=\"people\">Z<i>Q</i><b>e</b><span class=\"song\">NO</span></div>"
                        "<p class=\"mix\">AB</p>"
                        "<ol><li class=\"background-text\"></li><li class=\"background-text\"></li></ol>"
                        "</body></html>")});

    const QByteArray fontBytes = ReadFixture();
    FontSnapshot title;
    title.identifier = QStringLiteral("fzxbsf");
    title.relativePath = QStringLiteral("Fonts/OpenSans-Test.ttf");
    title.mediaType = QStringLiteral("font/ttf");
    title.bytes = fontBytes;
    FontSnapshot names;
    names.identifier = QStringLiteral("kxzd");
    names.relativePath = QStringLiteral("Fonts/Kxzd.ttf");
    names.mediaType = QStringLiteral("font/ttf");
    names.bytes = fontBytes;
    FontSnapshot unused;
    unused.identifier = QStringLiteral("unused");
    unused.relativePath = QStringLiteral("Fonts/Unused.ttf");
    unused.mediaType = QStringLiteral("font/ttf");
    unused.bytes = fontBytes;
    snapshot.fonts = {title, names, unused};

    const BatchResult batch = FontSubsetController::Analyze(snapshot);
    Require(batch.fonts.size() == 3, "batch did not preserve font count");
    Require(batch.warnings.contains(QStringLiteral("snapshot warning")),
            "batch did not preserve snapshot warnings");
    Require(!batch.warnings.filter(QStringLiteral("pseudo")).isEmpty(),
            "pseudo-element selector was not reported");

    const Result& titleResult = batch.fonts.at(0).result;
    Require(titleResult.success, titleResult.error.toUtf8().constData());
    for (quint32 codepoint : {quint32('H'), quint32('i'), quint32('A'), quint32('B'),
                              quint32('Q'), quint32('Y'), quint32('1'), quint32('2'),
                              quint32(0x4e2d)}) {
        Require(titleResult.inputCodepoints.contains(codepoint),
                "styled text was not assigned to its font");
    }
    Require(!titleResult.inputCodepoints.contains(quint32('e')) &&
                !titleResult.inputCodepoints.contains(quint32('W')) &&
                !titleResult.inputCodepoints.contains(quint32('N')),
            "text for another face was copied into this font");
    Require(titleResult.requestedCodepoints.contains(quint32('H')) &&
                titleResult.unavailableCodepoints.contains(quint32(0x4e2d)),
            "coverage was not reported per assigned font");

    const Result& nameResult = batch.fonts.at(1).result;
    Require(nameResult.success, nameResult.error.toUtf8().constData());
    Require(nameResult.requestedCodepoints.contains(quint32('Z')) &&
                nameResult.requestedCodepoints.contains(quint32('e')) &&
                nameResult.requestedCodepoints.contains(quint32('W')),
            "inherited, child, and sibling faces were not resolved");
    Require(!nameResult.inputCodepoints.contains(quint32('H')) &&
                !nameResult.inputCodepoints.contains(quint32('Y')) &&
                !nameResult.inputCodepoints.contains(quint32('Q')),
            "higher-priority face leaked into the parent font");

    const Result& unusedResult = batch.fonts.at(2).result;
    Require(!unusedResult.success &&
                unusedResult.error == QStringLiteral("No text is assigned to this font."),
            "unreferenced font received book text");
}

void TestUnicodeSetReplacesMatchedText()
{
    BookSnapshot snapshot;
    snapshot.textSources.append(
        {QStringLiteral("Styles/Fonts.css"), QStringLiteral("text/css"),
         QStringLiteral("@font-face { font-family: \"fzxbsf\"; "
                        "src: url(../Fonts/OpenSans-Test.ttf); unicode-set: AB; }")});
    snapshot.textSources.append(
        {QStringLiteral("Text/chapter.xhtml"),
         QStringLiteral("application/xhtml+xml"),
         QStringLiteral("<html xmlns=\"http://www.w3.org/1999/xhtml\"><head>"
                        "<link rel=\"stylesheet\" href=\"../Styles/Fonts.css\"/>"
                        "<style>body { font-family: \"fzxbsf\"; }</style>"
                        "</head><body>Hello</body></html>")});
    FontSnapshot font;
    font.identifier = QStringLiteral("fzxbsf");
    font.relativePath = QStringLiteral("Fonts/OpenSans-Test.ttf");
    font.bytes = ReadFixture();
    snapshot.fonts.append(font);

    const Result result = FontSubsetController::Analyze(snapshot).fonts.constFirst().result;
    Require(result.success, result.error.toUtf8().constData());
    Require(result.requestedCodepoints.contains(quint32('A')) &&
                result.requestedCodepoints.contains(quint32('B')) &&
                !result.inputCodepoints.contains(quint32('H')),
            "unicode-set did not replace the matched characters");
}

}

int main()
{
    TestBatchAnalysis();
    TestUnicodeSetReplacesMatchedText();
    return EXIT_SUCCESS;
}
