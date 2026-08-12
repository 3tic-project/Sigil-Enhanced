#include <QHash>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include "BuiltinPlugins/VerticalStylesheetResolver.h"

using BuiltinPlugins::VerticalStylesheetResolver;

int fail(const QString& message)
{
    QTextStream(stderr) << "vertical_stylesheet_resolver_test: " << message << '\n';
    return 1;
}

int main()
{
    QHash<QString, QString> css;
    css.insert(QStringLiteral("OEBPS/Styles/main style.css"), QStringLiteral(
        "@import url('nested/layout.css') screen;\nbody { color: black; }"));
    css.insert(QStringLiteral("OEBPS/Styles/nested/layout.css"), QStringLiteral(
        "@import \"../main%20style.css\";\n"
        "@import url(https://example.invalid/remote.css);\n"
        ".vrtl { writing-mode: vertical-rl; }"));
    css.insert(QStringLiteral("OEBPS/Styles/unrelated.css"), QStringLiteral(
        "body { writing-mode: horizontal-tb; }"));

    const QString xhtml = QStringLiteral(
        "<x:html xmlns:x=\"http://www.w3.org/1999/xhtml\"><x:head>"
        "<x:link rel=\"alternate STYLESHEET\" "
        "href=\"../Styles/main%20style.css?rev=1#sheet\"/>"
        "</x:head><x:body/></x:html>");
    const QStringList resolved = VerticalStylesheetResolver::resolve(
        xhtml, QStringLiteral("OEBPS/Text/chapter.xhtml"), css);
    if (resolved != QStringList {
            QStringLiteral("OEBPS/Styles/main style.css"),
            QStringLiteral("OEBPS/Styles/nested/layout.css") }) {
        return fail(QStringLiteral("encoded/imported stylesheet graph mismatch: %1")
                        .arg(resolved.join(QStringLiteral(", "))));
    }

    const QString remote = QStringLiteral(
        "<html><head><link rel=\"stylesheet\" href=\"https://example.invalid/a.css\"/>"
        "<link rel=\"stylesheet\" href=\"urn:example:stylesheet\"/>"
        "</head><body/></html>");
    if (!VerticalStylesheetResolver::resolve(
             remote, QStringLiteral("OEBPS/Text/chapter.xhtml"), css).isEmpty()) {
        return fail(QStringLiteral("remote stylesheet must not resolve to a book path"));
    }

    if (!VerticalStylesheetResolver::resolve(
             QStringLiteral("<html>"), QStringLiteral("OEBPS/Text/chapter.xhtml"), css)
             .isEmpty()) {
        return fail(QStringLiteral("malformed XHTML must yield an empty graph"));
    }

    return 0;
}
