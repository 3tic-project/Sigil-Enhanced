/************************************************************************
**
**  This file is part of Sigil.
**
**  Sigil is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  any later version.
**
*************************************************************************/

#include "Misc/CheckpointIdentifier.h"

#include <QDomDocument>
#include <QDomElement>
#include <QList>
#include <QSet>
#include <QUuid>

namespace
{

const QString DC_NAMESPACE = QStringLiteral("http://purl.org/dc/elements/1.1/");

QString localName(const QDomElement& element)
{
    if (!element.localName().isEmpty()) {
        return element.localName();
    }
    const QString name = element.tagName();
    const int colon = name.indexOf(QLatin1Char(':'));
    return colon >= 0 ? name.mid(colon + 1) : name;
}

QString normalizedUuid(const QString& content)
{
    // Match OPFResource::GetUUIDIdentifierValue(), which is also used later
    // to find this repository. Values it cannot retrieve must be repaired by
    // appending a canonical lowercase urn:uuid identifier.
    QString value = content;
    value.remove(QStringLiteral("urn:uuid:"));
    const QUuid uuid(value);
    return uuid.isNull() ? QString() : uuid.toString(QUuid::WithoutBraces);
}

bool isIdentifierElement(const QDomElement& element)
{
    return localName(element) == QStringLiteral("identifier")
        && (element.namespaceURI().isEmpty()
            || element.namespaceURI() == DC_NAMESPACE);
}

QList<QDomElement> allElements(const QDomElement& root)
{
    QList<QDomElement> elements;
    QList<QDomElement> pending { root };
    while (!pending.isEmpty()) {
        const QDomElement element = pending.takeLast();
        elements.append(element);
        for (QDomNode child = element.lastChild(); !child.isNull(); child = child.previousSibling()) {
            if (child.isElement()) {
                pending.append(child.toElement());
            }
        }
    }
    return elements;
}

QString uniqueId(const QString& preferred, const QSet<QString>& ids)
{
    QString candidate = preferred.isEmpty()
        ? QStringLiteral("SigilCheckpointUUID") : preferred;
    int suffix = 2;
    while (ids.contains(candidate)) {
        candidate = QStringLiteral("SigilCheckpointUUID%1").arg(suffix++);
    }
    return candidate;
}

}

namespace CheckpointIdentifier
{

Result ensureUuid(const QString& source)
{
    Result result;
    QDomDocument document;
    QString parse_error;
    int parse_line = 0;
    int parse_column = 0;
    if (!document.setContent(source, true, &parse_error, &parse_line, &parse_column)) {
        result.error = QStringLiteral("OPF XML parse failed at %1:%2: %3")
                           .arg(parse_line)
                           .arg(parse_column)
                           .arg(parse_error);
        return result;
    }

    QDomElement package = document.documentElement();
    if (localName(package) != QStringLiteral("package")) {
        result.error = QStringLiteral("OPF package element is missing");
        return result;
    }

    QDomElement metadata;
    const QList<QDomElement> elements = allElements(package);
    QSet<QString> ids;
    for (const QDomElement& element : elements) {
        const QString id = element.attribute(QStringLiteral("id")).trimmed();
        if (!id.isEmpty()) {
            ids.insert(id);
        }
        if (metadata.isNull() && localName(element) == QStringLiteral("metadata")) {
            metadata = element;
        }
        if (isIdentifierElement(element)) {
            const QString uuid = normalizedUuid(element.text());
            if (!uuid.isEmpty()) {
                result.ok = true;
                result.bookId = uuid;
                result.text = source;
                return result;
            }
        }
    }

    if (metadata.isNull()) {
        result.error = QStringLiteral("OPF metadata element is missing");
        return result;
    }

    QString identifier_id;
    const QString referenced_id = package.attribute(QStringLiteral("unique-identifier")).trimmed();
    QDomElement referenced_element;
    if (!referenced_id.isEmpty()) {
        for (const QDomElement& element : elements) {
            if (element.attribute(QStringLiteral("id")) == referenced_id) {
                referenced_element = element;
                break;
            }
        }
    }

    // Reuse a dangling package reference (the common malformed-book case) so
    // adding the UUID also repairs unique-identifier without disturbing a
    // valid ISBN or vendor identifier that the package already references.
    if (!referenced_id.isEmpty() && referenced_element.isNull()) {
        identifier_id = referenced_id;
    } else {
        identifier_id = uniqueId(QStringLiteral("SigilCheckpointUUID"), ids);
        if (referenced_id.isEmpty()
            || (!referenced_element.isNull()
                && !isIdentifierElement(referenced_element))) {
            package.setAttribute(QStringLiteral("unique-identifier"), identifier_id);
        }
    }

    if (metadata.attribute(QStringLiteral("xmlns:dc")).isEmpty()
        && package.attribute(QStringLiteral("xmlns:dc")).isEmpty()) {
        metadata.setAttribute(QStringLiteral("xmlns:dc"), DC_NAMESPACE);
    }

    const QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QDomElement identifier = document.createElementNS(
        DC_NAMESPACE, QStringLiteral("dc:identifier"));
    identifier.setAttribute(QStringLiteral("id"), identifier_id);
    identifier.appendChild(document.createTextNode(QStringLiteral("urn:uuid:") + uuid));
    metadata.appendChild(identifier);

    result.ok = true;
    result.changed = true;
    result.bookId = uuid;
    result.text = document.toString(2);
    return result;
}

}
