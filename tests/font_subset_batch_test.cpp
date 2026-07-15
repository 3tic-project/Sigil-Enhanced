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
        {QStringLiteral("Text/chapter.xhtml"),
         QStringLiteral("application/xhtml+xml"),
         QStringLiteral("<html xmlns=\"http://www.w3.org/1999/xhtml\"><body>"
                        "Hello ffi 中</body></html>")});

    FontSnapshot font;
    font.identifier = QStringLiteral("font-1");
    font.relativePath = QStringLiteral("Fonts/OpenSans-Test.ttf");
    font.mediaType = QStringLiteral("font/ttf");
    font.bytes = ReadFixture();
    snapshot.fonts.append(font);

    const BatchResult batch = FontSubsetController::Analyze(snapshot);
    Require(batch.fonts.size() == 1, "batch did not preserve font count");
    Require(batch.warnings.contains(QStringLiteral("snapshot warning")),
            "batch did not preserve snapshot warnings");
    Require(batch.usage.codepoints.contains(quint32('H')) &&
                batch.usage.codepoints.contains(quint32(0x4e2d)),
            "batch did not collect the document character set");

    const Result& result = batch.fonts.constFirst().result;
    Require(result.success, result.error.toUtf8().constData());
    Require(result.newSize < result.oldSize,
            "batch did not produce a smaller font");
    Require(result.requestedCodepoints.contains(quint32('H')),
            "supported text was not requested from HarfBuzz");
    Require(result.unavailableCodepoints.contains(quint32(0x4e2d)),
            "font-specific unavailable text was not reported");
    Require(result.missingCodepoints.isEmpty(),
            "validated subset is missing requested text");
}

}

int main()
{
    TestBatchAnalysis();
    return EXIT_SUCCESS;
}
