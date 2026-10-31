#include "BuiltinPlugins/PlainTextExporter.h"

#include <QRegularExpression>
#include <QSaveFile>
#include <QStringList>
#include <memory>

#include "gumbo.h"

namespace
{

bool htmlSpace(QChar ch)
{
    return ch == QLatin1Char(' ') || ch == QLatin1Char('\t') || ch == QLatin1Char('\n') ||
           ch == QLatin1Char('\r') || ch == QLatin1Char('\f');
}

bool blockTag(GumboTag tag)
{
    switch (tag) {
    case GUMBO_TAG_ADDRESS: case GUMBO_TAG_ARTICLE: case GUMBO_TAG_ASIDE:
    case GUMBO_TAG_BLOCKQUOTE: case GUMBO_TAG_CAPTION: case GUMBO_TAG_DD:
    case GUMBO_TAG_DETAILS: case GUMBO_TAG_DIV: case GUMBO_TAG_DL: case GUMBO_TAG_DT:
    case GUMBO_TAG_FIELDSET: case GUMBO_TAG_FIGCAPTION: case GUMBO_TAG_FIGURE:
    case GUMBO_TAG_FOOTER: case GUMBO_TAG_FORM: case GUMBO_TAG_H1: case GUMBO_TAG_H2:
    case GUMBO_TAG_H3: case GUMBO_TAG_H4: case GUMBO_TAG_H5: case GUMBO_TAG_H6:
    case GUMBO_TAG_HEADER: case GUMBO_TAG_HGROUP: case GUMBO_TAG_HR:
    case GUMBO_TAG_LI: case GUMBO_TAG_MAIN: case GUMBO_TAG_NAV: case GUMBO_TAG_OL:
    case GUMBO_TAG_P: case GUMBO_TAG_PRE: case GUMBO_TAG_SECTION: case GUMBO_TAG_SUMMARY:
    case GUMBO_TAG_TABLE: case GUMBO_TAG_TR: case GUMBO_TAG_UL: case GUMBO_TAG_TEXT:
        return true;
    default:
        return false;
    }
}

GumboTag localTag(const GumboElement& element)
{
    if (element.tag != GUMBO_TAG_UNKNOWN) return element.tag;
    // XML permits XHTML/SVG namespace prefixes; Gumbo keeps these as unknown
    // tag names. Use the local name for text boundaries and excluded elements.
    GumboStringPiece original = element.original_tag;
    gumbo_tag_from_original_text(&original);
    const QByteArray name(original.data, original.length);
    const qsizetype colon = name.lastIndexOf(':');
    if (colon < 0) return element.tag;
    return gumbo_tagn_enum(name.constData() + colon + 1, name.size() - colon - 1);
}

bool ignored(const GumboElement& element, GumboTag tag)
{
    switch (tag) {
    case GUMBO_TAG_HEAD: case GUMBO_TAG_TITLE: case GUMBO_TAG_SCRIPT: case GUMBO_TAG_STYLE:
    case GUMBO_TAG_TEMPLATE: case GUMBO_TAG_RT: case GUMBO_TAG_RP: case GUMBO_TAG_RTC:
        return true;
    default:
        break;
    }
    if (gumbo_get_attribute(&element.attributes, "hidden")) return true;
    if (const auto* style = gumbo_get_attribute(&element.attributes, "style")) {
        static const QRegularExpression invisible(
            QStringLiteral("(?:^|;)\\s*(?:display\\s*:\\s*none|visibility\\s*:\\s*(?:hidden|collapse))"
                           "\\s*(?:!\\s*important)?\\s*(?:;|$)"),
            QRegularExpression::CaseInsensitiveOption);
        if (invisible.match(QString::fromUtf8(style->value)).hasMatch()) return true;
    }
    return false;
}

class TextWriter
{
public:
    void boundary()
    {
        pendingSpace = false;
        if (!text.isEmpty() && !text.endsWith(QLatin1Char('\n'))) text += QLatin1Char('\n');
    }

    void append(const QString& value, bool preformatted)
    {
        if (preformatted) {
            QString normalized = value;
            normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
            normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));
            text += normalized;
            return;
        }
        for (QChar ch : value) {
            if (htmlSpace(ch)) {
                pendingSpace = true;
            } else {
                if (pendingSpace && !text.isEmpty() && !text.endsWith(QLatin1Char('\n')) &&
                    !text.endsWith(QLatin1Char('\t'))) text += QLatin1Char(' ');
                pendingSpace = false;
                text += ch;
            }
        }
    }

    void visit(const GumboNode* node, bool preformatted = false)
    {
        if (node->type == GUMBO_NODE_TEXT || node->type == GUMBO_NODE_WHITESPACE ||
            node->type == GUMBO_NODE_CDATA) {
            append(QString::fromUtf8(node->v.text.text), preformatted);
            return;
        }
        if (node->type != GUMBO_NODE_ELEMENT) return;
        const GumboElement& element = node->v.element;
        const GumboTag tag = localTag(element);
        if (ignored(element, tag)) return;
        if (tag == GUMBO_TAG_BR) {
            pendingSpace = false;
            text += QLatin1Char('\n');
            return;
        }
        if (tag == GUMBO_TAG_IMG) {
            if (const auto* alt = gumbo_get_attribute(&element.attributes, "alt")) {
                append(QString::fromUtf8(alt->value), preformatted);
            }
            return;
        }
        const bool block = blockTag(tag);
        if (block) boundary();
        if ((tag == GUMBO_TAG_TD || tag == GUMBO_TAG_TH) &&
            !text.isEmpty() && !text.endsWith(QLatin1Char('\n'))) {
            pendingSpace = false;
            text += QLatin1Char('\t');
        }
        const bool preserve = preformatted || tag == GUMBO_TAG_PRE;
        for (unsigned int i = 0; i < element.children.length; ++i) {
            visit(static_cast<GumboNode*>(element.children.data[i]), preserve);
        }
        if (block) boundary();
    }

    QString finish() const
    {
        qsizetype start = 0;
        qsizetype end = text.size();
        while (start < end && text.at(start) == QLatin1Char('\n')) ++start;
        while (end > start && text.at(end - 1) == QLatin1Char('\n')) --end;
        return text.mid(start, end - start);
    }

private:
    QString text;
    bool pendingSpace = false;
};

const GumboNode* bodyNode(const GumboNode* root)
{
    if (!root || root->type != GUMBO_NODE_ELEMENT) return nullptr;
    for (unsigned int i = 0; i < root->v.element.children.length; ++i) {
        const auto* child = static_cast<GumboNode*>(root->v.element.children.data[i]);
        if (child->type == GUMBO_NODE_ELEMENT && child->v.element.tag == GUMBO_TAG_BODY) return child;
    }
    return nullptr;
}

}

namespace BuiltinPlugins
{

PlainTextExporter::Result PlainTextExporter::render(const QList<Document>& documents)
{
    QStringList sections;
    for (const Document& document : documents) {
        if (document.xhtml.isEmpty()) continue;
        // Use Sigil's XHTML-aware Gumbo rules, including self-closing script/style
        // handling. The buffer must outlive the tree's original-source pointers.
        const QByteArray source = document.xhtml.toUtf8();
        GumboOptions options = kGumboDefaultOptions;
        options.use_xhtml_rules = true;
        const std::unique_ptr<GumboOutput, decltype(&gumbo_destroy_output)> output(
            gumbo_parse_with_options(&options, source.constData(), source.size()), gumbo_destroy_output);
        if (!output || output->status != GUMBO_STATUS_OK) {
            return {{}, tr("Cannot extract all text from %1: the document exceeds the parser limits.")
                            .arg(document.bookPath)};
        }
        TextWriter writer;
        if (const auto* body = bodyNode(output->root)) writer.visit(body);
        const QString text = writer.finish();
        if (!text.trimmed().isEmpty()) sections.append(text);
    }
    if (sections.isEmpty()) return {{}, tr("This book contains no exportable text.")};
    return {sections.join(QStringLiteral("\n\n")) + QLatin1Char('\n'), {}};
}

bool PlainTextExporter::exportToFile(const QList<Document>& documents, const QString& destination,
                                    QString* error)
{
    const Result result = render(documents);
    if (!result.ok()) {
        if (error) *error = result.error;
        return false;
    }
    QSaveFile file(destination);
    const QByteArray bytes = result.text.toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) *error = tr("Cannot write TXT file %1: %2").arg(destination, file.errorString());
        return false;
    }
    if (error) error->clear();
    return true;
}

}
