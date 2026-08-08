#include <QCoreApplication>
#include <QDomDocument>
#include <QRegularExpression>
#include <QString>
#include <QTextStream>

#include "BuiltinPlugins/BookLiveParagraphNormalizer.h"

using BuiltinPlugins::BookLiveParagraphNormalizer;

namespace
{

QString localName(const QDomNode& node)
{
    if (!node.isElement()) {
        return QString();
    }
    const QDomElement element = node.toElement();
    const QString local_name = element.localName();
    return (local_name.isEmpty() ? element.tagName() : local_name).toLower();
}

bool hasClass(const QDomElement& element, const QString& class_name)
{
    return element.attribute(QStringLiteral("class"))
        .split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts)
        .contains(class_name);
}

QDomElement findClass(const QDomNode& root, const QString& class_name)
{
    if (root.isElement() && hasClass(root.toElement(), class_name)) {
        return root.toElement();
    }
    for (QDomNode child = root.firstChild(); !child.isNull(); child = child.nextSibling()) {
        const QDomElement found = findClass(child, class_name);
        if (!found.isNull()) {
            return found;
        }
    }
    return QDomElement();
}

int countElements(const QDomNode& root, const QString& name)
{
    int count = root.isElement() && localName(root) == name ? 1 : 0;
    for (QDomNode child = root.firstChild(); !child.isNull(); child = child.nextSibling()) {
        count += countElements(child, name);
    }
    return count;
}

bool generatedParagraphContainsDiv(const QDomNode& root)
{
    if (root.isElement()) {
        const QDomElement element = root.toElement();
        if (localName(element) == QStringLiteral("p") &&
            hasClass(element, QStringLiteral("se-bl-paragraph")) &&
            countElements(element, QStringLiteral("div")) > 0) {
            return true;
        }
    }
    for (QDomNode child = root.firstChild(); !child.isNull(); child = child.nextSibling()) {
        if (generatedParagraphContainsDiv(child)) {
            return true;
        }
    }
    return false;
}

QString paragraphs(int count)
{
    QString result;
    for (int i = 0; i < count; ++i) {
        result += QStringLiteral("<div class=\"para\" style=\"line-height:1.75\">"
                                 "　正文%1<ruby>字<rt>じ</rt></ruby>。</div>")
                      .arg(i);
    }
    return result;
}

QString xhtml(const QString& body, const QString& body_class = QString())
{
    return QStringLiteral(
        "<!DOCTYPE html><html xmlns=\"http://www.w3.org/1999/xhtml\" class=\"vrtl\">"
        "<head><meta charset=\"UTF-8\"/><title>x</title>"
        "<link rel=\"stylesheet\" href=\"../style/generated_styles.css\"/></head>"
        "<body class=\"%1\">%2</body></html>")
        .arg(body_class, body);
}

int fail(const QString& message)
{
    QTextStream(stderr) << message << '\n';
    return 1;
}

int runTests()
{
    const QString body =
        QStringLiteral("<div class=\"main\"><div class=\"outer\"><div class=\"justify\">"
                       "<div class=\"content\"><a name=\"start\"></a>"
                       "<div class=\"image-box\"><img class=\"fit\" src=\"image.jpg\"/></div>"
                       "<div class=\"spacer\"><br/></div>"
                       "<div class=\"title-outer\" style=\"margin-top:2em\">"
                       "<div class=\"title-inner\">小节标题</div></div>") +
        paragraphs(12) +
        QStringLiteral("<div class=\"scene\">　◆　◆　◆</div>"
                       "</div></div></div></div>");
    const QString source = xhtml(body, QStringLiteral("chapter-page"));
    const BookLiveParagraphNormalizer::Analysis analysis =
        BookLiveParagraphNormalizer::analyzeXhtmlText(source);
    if (!analysis.safeToNormalize || analysis.paragraphLeaves != 12 ||
        analysis.wrappedBlockLeaves != 1 || analysis.spacerBrLeaves != 1 ||
        analysis.sceneBreaks != 1 || analysis.imageLeaves != 1 || analysis.anchorOnly != 1) {
        return fail(QStringLiteral("normal flow analysis failed: %1").arg(analysis.message));
    }

    const BookLiveParagraphNormalizer::NormalizeResult result =
        BookLiveParagraphNormalizer::normalizeXhtmlText(source);
    if (!result.ok || !result.changed ||
        result.after.pageKind != BookLiveParagraphNormalizer::PageKind::AlreadyNormalized) {
        return fail(QStringLiteral("normalization failed: %1")
                        .arg(result.messages.join(QStringLiteral("; "))));
    }

    QDomDocument output;
    if (!output.setContent(result.text)) {
        return fail(QStringLiteral("normalized output is not well-formed XML"));
    }
    const QDomElement title_outer = findClass(output, QStringLiteral("title-outer"));
    const QDomElement title_inner = findClass(output, QStringLiteral("title-inner"));
    const QDomElement spacer = findClass(output, QStringLiteral("spacer"));
    if (findClass(output, QStringLiteral("outer")).isNull() ||
        findClass(output, QStringLiteral("justify")).isNull() ||
        findClass(output, QStringLiteral("content")).isNull() ||
        localName(title_outer) != QStringLiteral("p") ||
        title_outer.attribute(QStringLiteral("style")) != QStringLiteral("margin-top:2em") ||
        localName(title_inner) != QStringLiteral("span") ||
        !hasClass(title_inner, QStringLiteral("se-bl-inner-block")) ||
        localName(spacer) != QStringLiteral("p") || countElements(spacer, QStringLiteral("br")) != 1) {
        return fail(QStringLiteral("layout, title style, or spacer was not preserved"));
    }
    if (generatedParagraphContainsDiv(output) ||
        result.text.contains(QStringLiteral("se-bl-credit")) ||
        result.text.contains(QStringLiteral("margin-top: 5em")) ||
        countElements(output, QStringLiteral("ruby")) != 12 ||
        countElements(output, QStringLiteral("rt")) != 12 ||
        !result.text.contains(QStringLiteral("name=\"start\"")) ||
        !result.text.contains(QStringLiteral("src=\"image.jpg\""))) {
        return fail(QStringLiteral("flattening or semantic preservation failed"));
    }

    const BookLiveParagraphNormalizer::NormalizeResult second =
        BookLiveParagraphNormalizer::normalizeXhtmlText(result.text);
    if (!second.ok || second.changed || second.text != result.text) {
        return fail(QStringLiteral("normalization is not idempotent"));
    }

    const QString toc = xhtml(
        QStringLiteral("<div class=\"main\"><div><div><div>"
                       "<div>目次</div><div><a href=\"a.xhtml\">一</a></div>"
                       "<div><a href=\"b.xhtml\">二</a></div><div><br/></div>"
                       "<div><a href=\"c.xhtml\">三</a></div></div></div></div></div>"));
    if (BookLiveParagraphNormalizer::analyzeXhtmlText(toc).pageKind !=
        BookLiveParagraphNormalizer::PageKind::TocLike) {
        return fail(QStringLiteral("toc classification failed"));
    }

    const QString image = xhtml(
        QStringLiteral("<div class=\"main\"><p><img src=\"cover.jpg\"/></p></div>"),
        QStringLiteral("p-image"));
    if (BookLiveParagraphNormalizer::analyzeXhtmlText(image).pageKind !=
        BookLiveParagraphNormalizer::PageKind::ImageOrTitlePage) {
        return fail(QStringLiteral("image-page classification failed"));
    }

    const QString short_source = xhtml(
        QStringLiteral("<div class=\"main\"><div><div><div>") + paragraphs(4) +
        QStringLiteral("</div></div></div></div>"));
    const BookLiveParagraphNormalizer::Analysis short_analysis =
        BookLiveParagraphNormalizer::analyzeXhtmlText(short_source);
    if (!short_analysis.candidate || short_analysis.safeToNormalize ||
        BookLiveParagraphNormalizer::normalizeXhtmlText(short_source).ok ||
        !BookLiveParagraphNormalizer::normalizeXhtmlText(short_source, true).ok) {
        return fail(QStringLiteral("manual short-flow gate failed"));
    }

    const QString complex = xhtml(
        QStringLiteral("<div class=\"main\"><div><div><div>") + paragraphs(12) +
        QStringLiteral("<div><div><section>complex</section></div></div>"
                       "</div></div></div></div>"));
    const BookLiveParagraphNormalizer::Analysis complex_analysis =
        BookLiveParagraphNormalizer::analyzeXhtmlText(complex);
    if (complex_analysis.candidate ||
        complex_analysis.pageKind != BookLiveParagraphNormalizer::PageKind::BlockLayout) {
        return fail(QStringLiteral("complex block did not fail closed"));
    }

    if (BookLiveParagraphNormalizer::analyzeXhtmlText(
            QStringLiteral("<html><body><div></body></html>"))
            .pageKind != BookLiveParagraphNormalizer::PageKind::ParseError) {
        return fail(QStringLiteral("malformed XML classification failed"));
    }
    return 0;
}

}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    return runTests();
}
