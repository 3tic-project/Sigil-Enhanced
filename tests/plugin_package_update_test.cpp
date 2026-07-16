#include <cstdlib>
#include <iostream>

#include <QDomDocument>

#include "PluginAPI/PluginPackageUpdate.h"

namespace
{

void Require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

const QString PACKAGE = QStringLiteral(
    "<?xml version=\"1.0\"?>"
    "<package xmlns=\"http://www.idpf.org/2007/opf\" "
    "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" version=\"3.0\">"
    "<metadata><dc:title>Old</dc:title></metadata>"
    "<manifest><item id=\"a\" href=\"a.xhtml\" media-type=\"application/xhtml+xml\"/>"
    "<item id=\"b\" href=\"b.xhtml\" media-type=\"application/xhtml+xml\"/></manifest>"
    "<spine page-progression-direction=\"ltr\"><itemref idref=\"a\"/></spine>"
    "</package>");

QDomElement FirstByLocalName(const QDomNode &node, const QString &name)
{
    const QDomElement element = node.toElement();
    if (!element.isNull() && (element.localName() == name || element.tagName() == name)) {
        return element;
    }
    for (QDomNode child = node.firstChild(); !child.isNull(); child = child.nextSibling()) {
        const QDomElement found = FirstByLocalName(child, name);
        if (!found.isNull()) return found;
    }
    return QDomElement();
}

}

int main()
{
    QString updated;
    QString error;
    const QJsonArray metadata {
        QJsonObject {
            { QStringLiteral("name"), QStringLiteral("dc:title") },
            { QStringLiteral("content"), QStringLiteral("A & B") },
            { QStringLiteral("attributes"), QJsonObject {
                { QStringLiteral("id"), QStringLiteral("title") }
            } }
        },
        QJsonObject {
            { QStringLiteral("name"), QStringLiteral("meta") },
            { QStringLiteral("content"), QStringLiteral("Title") },
            { QStringLiteral("attributes"), QJsonObject {
                { QStringLiteral("property"), QStringLiteral("title-type") },
                { QStringLiteral("refines"), QStringLiteral("#title") }
            } }
        }
    };
    Require(PluginApi::ApplyMetadataUpdate(PACKAGE, metadata, &updated, &error),
            "structured metadata update failed");
    QDomDocument document;
    QString parse_error;
    int parse_line = 0;
    int parse_column = 0;
    if (!document.setContent(updated, true, &parse_error, &parse_line, &parse_column)) {
        std::cerr << updated.toStdString() << "\n" << parse_line << ':' << parse_column
                  << ' ' << parse_error.toStdString() << std::endl;
        Require(false, "updated metadata XML is malformed");
    }
    const QDomElement title = FirstByLocalName(document, QStringLiteral("title"));
    Require(title.text() == QStringLiteral("A & B"), "metadata text was not escaped round-trip");
    Require(title.attribute(QStringLiteral("id")) == QStringLiteral("title"),
            "metadata attribute was lost");
    Require(!FirstByLocalName(document, QStringLiteral("meta")).isNull(),
            "metadata child is missing");

    // Round-trip entries that carry per-element xmlns:* (as OPFResource does for
    // dc:date modification stamps with opf:event).
    const QJsonArray metadata_with_xmlns {
        QJsonObject {
            { QStringLiteral("name"), QStringLiteral("dc:title") },
            { QStringLiteral("content"), QStringLiteral("Kept") },
            { QStringLiteral("attributes"), QJsonObject() }
        },
        QJsonObject {
            { QStringLiteral("name"), QStringLiteral("dc:date") },
            { QStringLiteral("content"), QStringLiteral("2024-01-02") },
            { QStringLiteral("attributes"), QJsonObject {
                // Intentionally list opf:event before xmlns:opf to verify pass order.
                { QStringLiteral("opf:event"), QStringLiteral("modification") },
                { QStringLiteral("xmlns:opf"),
                  QStringLiteral("http://www.idpf.org/2007/opf") }
            } }
        }
    };
    Require(PluginApi::ApplyMetadataUpdate(PACKAGE, metadata_with_xmlns, &updated, &error),
            ("xmlns metadata attribute was rejected: " + error).toUtf8().constData());
    Require(document.setContent(updated, true, &parse_error),
            "xmlns metadata update XML is malformed");
    Require(updated.contains(QStringLiteral("opf:event=\"modification\"")),
            "opf:event was lost after xmlns: declaration");
    const QDomElement date = FirstByLocalName(document, QStringLiteral("date"));
    Require(!date.isNull(), "dc:date with xmlns:opf was not written");

    const QJsonArray spine {
        QJsonObject {
            { QStringLiteral("idref"), QStringLiteral("b") },
            { QStringLiteral("linear"), QStringLiteral("no") },
            { QStringLiteral("properties"), QStringLiteral("page-spread-left") }
        },
        QJsonObject {{ QStringLiteral("idref"), QStringLiteral("a") }}
    };
    Require(PluginApi::ApplySpineUpdate(updated, spine,
            QJsonObject {{ QStringLiteral("toc"), QStringLiteral("ncx") }},
            &updated, &error), "structured spine update failed");
    Require(document.setContent(updated, true), "updated spine XML is malformed");
    const QDomElement spine_element = FirstByLocalName(document, QStringLiteral("spine"));
    Require(spine_element.attribute(QStringLiteral("toc")) == QStringLiteral("ncx"),
            "spine attribute was not updated");
    Require(spine_element.attribute(QStringLiteral("page-progression-direction"))
                == QStringLiteral("ltr"),
            "unspecified spine attribute was not preserved");
    const QDomNodeList itemrefs = document.elementsByTagNameNS(
        QStringLiteral("http://www.idpf.org/2007/opf"), QStringLiteral("itemref"));
    Require(itemrefs.size() == 2, "spine items were not replaced");
    Require(itemrefs.at(0).toElement().attribute(QStringLiteral("idref"))
                == QStringLiteral("b"),
            "spine order changed");

    Require(!PluginApi::ApplyMetadataUpdate(PACKAGE,
            QJsonArray { QJsonObject {{ QStringLiteral("name"), QStringLiteral("dc:title") }} },
            &updated, &error), "metadata without content was accepted");
    Require(!PluginApi::ApplySpineUpdate(PACKAGE,
            QJsonArray { QJsonObject {{ QStringLiteral("idref"), 7 }} }, QJsonObject(),
            &updated, &error), "non-string spine idref was accepted");
    Require(!PluginApi::ApplyMetadataUpdate(QStringLiteral("<package>"), metadata,
                                             &updated, &error),
            "malformed package XML was accepted");
    return EXIT_SUCCESS;
}
