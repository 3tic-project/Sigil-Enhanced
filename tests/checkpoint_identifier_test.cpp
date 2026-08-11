#include <QDomDocument>
#include <QDomElement>
#include <QFile>
#include <QList>
#include <QString>
#include <QUuid>

#include <cstdlib>
#include <iostream>

#include "Misc/CheckpointIdentifier.h"

namespace
{

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

QString localName(const QDomElement& element)
{
    if (!element.localName().isEmpty()) {
        return element.localName();
    }
    const QString name = element.tagName();
    const int colon = name.indexOf(QLatin1Char(':'));
    return colon >= 0 ? name.mid(colon + 1) : name;
}

QDomElement findElementById(const QDomElement& root, const QString& id)
{
    QList<QDomElement> pending { root };
    while (!pending.isEmpty()) {
        const QDomElement element = pending.takeLast();
        if (element.attribute(QStringLiteral("id")) == id) {
            return element;
        }
        for (QDomNode child = element.firstChild(); !child.isNull(); child = child.nextSibling()) {
            if (child.isElement()) {
                pending.append(child.toElement());
            }
        }
    }
    return QDomElement();
}

QDomDocument parse(const QString& source)
{
    QDomDocument document;
    require(document.setContent(source, true), "generated OPF must be valid XML");
    return document;
}

}

int main(int argc, char* argv[])
{
    if (argc == 2) {
        QFile file(QString::fromLocal8Bit(argv[1]));
        if (!file.open(QIODevice::ReadOnly)) {
            std::cerr << "could not read OPF\n";
            return 2;
        }
        const auto result = CheckpointIdentifier::ensureUuid(
            QString::fromUtf8(file.readAll()));
        if (!result.ok || result.bookId.isEmpty()) {
            std::cerr << result.error.toStdString() << '\n';
            return 3;
        }
        parse(result.text);
        std::cout << (result.changed ? "added " : "existing ")
                  << result.bookId.toStdString() << '\n';
        return 0;
    }

    const QString existing = QStringLiteral(
        "<?xml version=\"1.0\"?>\n"
        "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"3.0\" unique-identifier=\"BookId\">\n"
        "  <metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">\n"
        "    <dc:identifier id=\"BookId\">urn:uuid:00000000-0000-4000-8000-000000000001</dc:identifier>\n"
        "  </metadata>\n"
        "  <manifest/><spine/>\n"
        "</package>\n");
    const auto existing_result = CheckpointIdentifier::ensureUuid(existing);
    require(existing_result.ok, "an existing UUID must be accepted");
    require(!existing_result.changed, "an existing UUID must not rewrite OPF text");
    require(existing_result.text == existing, "existing UUID OPF must remain byte-identical");
    require(existing_result.bookId == QStringLiteral("00000000-0000-4000-8000-000000000001"),
            "existing UUID must be normalized");

    // Mirrors the reported book: package points to BookId, but only vendor
    // identifiers with the literal value "none" exist.
    const QString dangling = QStringLiteral(
        "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"3.0\" unique-identifier=\"BookId\">"
        "<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">"
        "<dc:title id=\"title\">Test</dc:title>"
        "<dc:identifier id=\"vendor-a\">none</dc:identifier>"
        "<dc:identifier id=\"vendor-b\">none</dc:identifier>"
        "</metadata><manifest/><spine/></package>");
    const auto dangling_result = CheckpointIdentifier::ensureUuid(dangling);
    require(dangling_result.ok && dangling_result.changed,
            "a dangling unique-identifier must be repaired in memory");
    require(!QUuid(dangling_result.bookId).isNull(), "generated checkpoint UUID must be valid");
    const QDomDocument dangling_document = parse(dangling_result.text);
    const QDomElement package = dangling_document.documentElement();
    require(package.attribute(QStringLiteral("unique-identifier")) == QStringLiteral("BookId"),
            "the dangling package reference should be reused");
    const QDomElement book_id = findElementById(package, QStringLiteral("BookId"));
    require(!book_id.isNull() && localName(book_id) == QStringLiteral("identifier"),
            "BookId must now resolve to a dc:identifier");
    require(book_id.text() == QStringLiteral("urn:uuid:") + dangling_result.bookId,
            "the repaired identifier must contain the generated UUID");

    const auto repeated_result = CheckpointIdentifier::ensureUuid(dangling_result.text);
    require(repeated_result.ok && !repeated_result.changed,
            "UUID preparation must be idempotent");
    require(repeated_result.bookId == dangling_result.bookId,
            "repeated preparation must retain repository identity");
    require(repeated_result.text == dangling_result.text,
            "repeated preparation must not reserialize OPF text");

    const QString isbn = QStringLiteral(
        "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"3.0\" unique-identifier=\"isbn\">"
        "<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">"
        "<dc:identifier id=\"isbn\">9780000000000</dc:identifier>"
        "</metadata><manifest/><spine/></package>");
    const auto isbn_result = CheckpointIdentifier::ensureUuid(isbn);
    require(isbn_result.ok && isbn_result.changed, "a non-UUID main identifier needs a UUID sibling");
    const QDomDocument isbn_document = parse(isbn_result.text);
    require(isbn_document.documentElement().attribute(QStringLiteral("unique-identifier"))
                == QStringLiteral("isbn"),
            "a valid non-UUID main identifier must not be replaced");
    require(!findElementById(isbn_document.documentElement(), QStringLiteral("isbn")).isNull(),
            "the existing ISBN identifier must remain");

    const QString invalid_reference = QStringLiteral(
        "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"3.0\" unique-identifier=\"wrong\">"
        "<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">"
        "<dc:title id=\"wrong\">Test</dc:title>"
        "</metadata><manifest/><spine/></package>");
    const auto invalid_result = CheckpointIdentifier::ensureUuid(invalid_reference);
    require(invalid_result.ok && invalid_result.changed, "an invalid package identifier target must be repaired");
    const QDomDocument invalid_document = parse(invalid_result.text);
    const QString repaired_id = invalid_document.documentElement().attribute(
        QStringLiteral("unique-identifier"));
    require(repaired_id != QStringLiteral("wrong"),
            "unique-identifier must no longer point to a title element");
    require(localName(findElementById(invalid_document.documentElement(), repaired_id))
                == QStringLiteral("identifier"),
            "repaired package identity must point to the generated identifier");

    require(!CheckpointIdentifier::ensureUuid(QStringLiteral("<package>")).ok,
            "malformed OPF XML must fail without output");
    require(!CheckpointIdentifier::ensureUuid(
                 QStringLiteral("<package version=\"3.0\"><manifest/><spine/></package>"))
                 .ok,
            "an OPF without metadata must fail without output");

    return 0;
}
