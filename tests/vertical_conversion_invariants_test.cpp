#include <QDomDocument>
#include <QDomElement>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include "BuiltinPlugins/VerticalCssTransformer.h"

using BuiltinPlugins::VerticalCssTransformer;

int fail(const QString& message)
{
    QTextStream(stderr) << "vertical_conversion_invariants_test: " << message << '\n';
    return 1;
}

QString localName(const QDomNode& node)
{
    if (!node.isElement()) {
        return QString();
    }
    const QDomElement element = node.toElement();
    const QString local_name = element.localName();
    return (local_name.isEmpty() ? element.tagName() : local_name).toLower();
}

QString semanticText(const QDomNode& node)
{
    if (node.isText() || node.isCDATASection()) {
        const QString text = node.nodeValue();
        return text.trimmed().isEmpty() ? QString() : text;
    }
    if (!node.isElement()) {
        return QString();
    }
    const QString name = localName(node);
    if (name == QStringLiteral("script") || name == QStringLiteral("style")) {
        return QString();
    }
    QString text;
    for (QDomNode child = node.firstChild(); !child.isNull(); child = child.nextSibling()) {
        text += semanticText(child);
    }
    return text;
}

QStringList sortedAttributes(const QDomNode& node, const QStringList& names)
{
    QStringList values;
    if (node.isElement()) {
        const QDomElement element = node.toElement();
        for (const QString& name : names) {
            if (element.hasAttribute(name)) {
                values << QStringLiteral("%1=%2").arg(name, element.attribute(name));
            }
        }
    }
    for (QDomNode child = node.firstChild(); !child.isNull(); child = child.nextSibling()) {
        values << sortedAttributes(child, names);
    }
    values.sort();
    return values;
}

int countElements(const QDomNode& root, const QString& name)
{
    int count = root.isElement() && localName(root) == name ? 1 : 0;
    for (QDomNode child = root.firstChild(); !child.isNull(); child = child.nextSibling()) {
        count += countElements(child, name);
    }
    return count;
}

QString richXhtml()
{
    return QStringLiteral(
        "<html xmlns=\"http://www.w3.org/1999/xhtml\" class=\"vrtl\">"
        "<head><title>章</title>"
        "<link rel=\"stylesheet\" href=\"../Styles/style-standard.css\"/>"
        "<style>p { text-indent: 1em; }</style></head>"
        "<body>"
        "<h1 id=\"chap1\">第一章</h1>"
        "<p id=\"p1\" class=\"tcy\">これは<ruby>漢<rt>かん</rt>字<rt>じ</rt></ruby>"
        "<a href=\"../Text/ch2.xhtml#p2\">次章</a>です。</p>"
        "<p><img src=\"../Images/fig.jpg\" alt=\"図\"/></p>"
        "<a name=\"end\"></a>"
        "</body></html>");
}

bool checkInvariants(const QString& before, const QString& after)
{
    QDomDocument before_doc;
    QDomDocument after_doc;
    if (!before_doc.setContent(before, false) || !after_doc.setContent(after, false)) {
        return false;
    }
    if (semanticText(before_doc) != semanticText(after_doc)) {
        return false;
    }
    if (sortedAttributes(before_doc, QStringList() << QStringLiteral("id") << QStringLiteral("name")) !=
        sortedAttributes(after_doc, QStringList() << QStringLiteral("id") << QStringLiteral("name"))) {
        return false;
    }
    if (sortedAttributes(before_doc, QStringList() << QStringLiteral("href") << QStringLiteral("src")) !=
        sortedAttributes(after_doc, QStringList() << QStringLiteral("href") << QStringLiteral("src"))) {
        return false;
    }
    const QStringList ruby_elems = QStringList() << QStringLiteral("ruby") << QStringLiteral("rt") << QStringLiteral("rp");
    for (const QString& elem : ruby_elems) {
        if (countElements(before_doc, elem) != countElements(after_doc, elem)) {
            return false;
        }
    }
    if (countElements(before_doc, QStringLiteral("img")) != countElements(after_doc, QStringLiteral("img"))) {
        return false;
    }
    if (countElements(before_doc, QStringLiteral("a")) != countElements(after_doc, QStringLiteral("a"))) {
        return false;
    }
    return true;
}

int runTests()
{
    const QString source = richXhtml();

    // ---- 兼容覆盖模式：不变量保持 ----
    {
        VerticalCssTransformer::Options options;
        const auto result = VerticalCssTransformer::transformXhtml(source, options, false);
        if (!result.ok || !result.changed) {
            return fail(QStringLiteral("compat transform failed"));
        }
        if (!checkInvariants(source, result.text)) {
            return fail(QStringLiteral("compat transform violated invariants"));
        }
        // 幂等
        const auto second = VerticalCssTransformer::transformXhtml(result.text, options, false);
        if (!second.ok || second.changed || second.text != result.text) {
            return fail(QStringLiteral("compat transform not idempotent"));
        }
    }

    // ---- 结构化切换模式：不变量保持 ----
    {
        VerticalCssTransformer::Options options;
        options.mode = VerticalCssTransformer::ConversionMode::ProfileAwareRewrite;
        const auto result = VerticalCssTransformer::transformXhtml(source, options, true);
        if (!result.ok || !result.changed) {
            return fail(QStringLiteral("profile transform failed"));
        }
        if (!checkInvariants(source, result.text)) {
            return fail(QStringLiteral("profile transform violated invariants"));
        }
        if (!result.text.contains(QStringLiteral("class=\"hltr se-v2h-converted\""))) {
            return fail(QStringLiteral("profile transform did not switch class"));
        }
    }

    // ---- 转换后文本仍是有效 XML ----
    {
        VerticalCssTransformer::Options options;
        const auto result = VerticalCssTransformer::transformXhtml(source, options, false);
        QDomDocument doc;
        if (!doc.setContent(result.text, false)) {
            return fail(QStringLiteral("transformed output not well-formed"));
        }
    }

    // ---- 横排 → 竖排：不变量保持 + 幂等 ----
    {
        VerticalCssTransformer::Options options;
        options.direction = VerticalCssTransformer::ConversionDirection::HorizontalToVertical;
        const QString horizontal_source = QStringLiteral(
            "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>章</title></head>"
            "<body><h1 id=\"chap1\">第一章</h1>"
            "<p id=\"p1\"><ruby>漢<rt>かん</rt>字<rt>じ</rt></ruby>"
            "<a href=\"../Text/ch2.xhtml#p2\">次章</a>です。</p>"
            "<p><img src=\"../Images/fig.jpg\" alt=\"図\"/></p>"
            "<a name=\"end\"></a></body></html>");
        const auto result = VerticalCssTransformer::transformXhtml(horizontal_source, options, false);
        if (!result.ok || !result.changed) {
            return fail(QStringLiteral("h2v transform failed"));
        }
        if (!checkInvariants(horizontal_source, result.text)) {
            return fail(QStringLiteral("h2v transform violated invariants"));
        }
        if (!result.text.contains(QStringLiteral("class=\"se-h2v-vertical\""))) {
            return fail(QStringLiteral("h2v transform did not add override class"));
        }
        const auto second = VerticalCssTransformer::transformXhtml(result.text, options, false);
        if (!second.ok || second.changed || second.text != result.text) {
            return fail(QStringLiteral("h2v transform not idempotent"));
        }
    }

    return 0;
}

int main()
{
    return runTests();
}
