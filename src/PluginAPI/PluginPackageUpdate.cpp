/************************************************************************
**
**  This file is part of Sigil.
**
*************************************************************************/

#include "PluginAPI/PluginPackageUpdate.h"

#include <QDomDocument>
#include <QHash>

#include <algorithm>
#include <utility>

namespace
{

QDomElement DirectChildElement(const QDomElement &parent, const QString &local_name)
{
    for (QDomNode child = parent.firstChild(); !child.isNull(); child = child.nextSibling()) {
        const QDomElement element = child.toElement();
        if (!element.isNull() && (element.localName() == local_name
                                  || element.tagName() == local_name)) {
            return element;
        }
    }
    return QDomElement();
}

bool ParsePackageDom(const QString &source, QDomDocument *document, QString *error)
{
    QString message;
    int line = 0;
    int column = 0;
    if (!document->setContent(source, true, &message, &line, &column)) {
        if (error) {
            *error = QStringLiteral("Package XML is not well formed at %1:%2: %3")
                .arg(line).arg(column).arg(message);
        }
        return false;
    }
    return true;
}

const QString XMLNS_NAMESPACE =
    QStringLiteral("http://www.w3.org/2000/xmlns/");

void RegisterNamespaceDeclaration(const QString &name, const QString &uri,
                                  QHash<QString, QString> *namespaces)
{
    if (name == QStringLiteral("xmlns")) {
        // Default namespace declaration — tracked under an empty key for lookups.
        namespaces->insert(QString(), uri);
        return;
    }
    if (name.startsWith(QStringLiteral("xmlns:"))) {
        const QString declared_prefix = name.mid(6);
        if (!declared_prefix.isEmpty()) {
            namespaces->insert(declared_prefix, uri);
        }
    }
}

void CollectNamespaces(const QDomNode &node, QHash<QString, QString> *namespaces)
{
    const QDomElement element = node.toElement();
    if (!element.isNull() && !element.prefix().isEmpty() && !element.namespaceURI().isEmpty()) {
        namespaces->insert(element.prefix(), element.namespaceURI());
    }
    const QDomNamedNodeMap attributes = node.attributes();
    for (int index = 0; index < attributes.size(); ++index) {
        const QDomAttr attribute = attributes.item(index).toAttr();
        // Namespace declarations: xmlns="..." or xmlns:prefix="..."
        // With namespace processing enabled, QDom reports these under the xmlns NS.
        if (attribute.namespaceURI() == XMLNS_NAMESPACE
            || attribute.name() == QStringLiteral("xmlns")
            || attribute.prefix() == QStringLiteral("xmlns")
            || attribute.name().startsWith(QStringLiteral("xmlns:"))) {
            if (attribute.name() == QStringLiteral("xmlns")
                || (attribute.localName() == QStringLiteral("xmlns")
                    && attribute.prefix().isEmpty())) {
                RegisterNamespaceDeclaration(QStringLiteral("xmlns"), attribute.value(),
                                             namespaces);
            } else {
                const QString declared = attribute.prefix() == QStringLiteral("xmlns")
                    ? attribute.localName()
                    : attribute.name().section(QLatin1Char(':'), 1);
                if (!declared.isEmpty()) {
                    namespaces->insert(declared, attribute.value());
                }
            }
            continue;
        }
        if (!attribute.prefix().isEmpty() && !attribute.namespaceURI().isEmpty()
            && attribute.prefix() != QStringLiteral("xmlns")) {
            namespaces->insert(attribute.prefix(), attribute.namespaceURI());
        }
    }
    for (QDomNode child = node.firstChild(); !child.isNull(); child = child.nextSibling()) {
        CollectNamespaces(child, namespaces);
    }
}

bool SplitQualifiedName(const QString &name, QString *prefix, QString *local_name,
                        QString *error)
{
    const int colon = name.indexOf(QLatin1Char(':'));
    if (colon == 0 || colon == name.size() - 1
        || (colon >= 0 && name.indexOf(QLatin1Char(':'), colon + 1) >= 0)) {
        if (error) *error = QStringLiteral("XML qualified name is invalid: %1").arg(name);
        return false;
    }
    *prefix = colon < 0 ? QString() : name.left(colon);
    *local_name = colon < 0 ? name : name.mid(colon + 1);
    return !local_name->isEmpty();
}

bool ApplyAttribute(QDomElement &element, const QString &name,
                    const QString &value, QHash<QString, QString> *namespaces,
                    QString *error)
{
    // xmlns / xmlns:prefix declare namespaces; they are not ordinary prefixed attrs.
    // Write them as literal attribute names so QDom does not invent xmlns:xmlns.
    if (name == QStringLiteral("xmlns")
        || name.startsWith(QStringLiteral("xmlns:"))) {
        RegisterNamespaceDeclaration(name, value, namespaces);
        element.setAttribute(name, value);
        return true;
    }
    QString attribute_prefix;
    QString attribute_name;
    if (!SplitQualifiedName(name, &attribute_prefix, &attribute_name, error)) {
        return false;
    }
    if (!attribute_prefix.isEmpty()) {
        if (!namespaces->contains(attribute_prefix)) {
            if (error) {
                *error = QStringLiteral("Metadata attribute prefix is undeclared: %1")
                    .arg(attribute_prefix);
            }
            return false;
        }
        // Prefer the literal qualified name so round-trips match OPFParser TagAtts
        // (e.g. opf:event, xml:lang) without rewriting namespace nodes.
        element.setAttribute(name, value);
    } else {
        element.setAttribute(attribute_name, value);
    }
    return true;
}

}

namespace PluginApi
{

bool ApplyMetadataUpdate(const QString &source, const QJsonArray &entries,
                         QString *updated, QString *error)
{
    QDomDocument document;
    if (!ParsePackageDom(source, &document, error)) return false;
    QDomElement metadata = DirectChildElement(document.documentElement(),
                                              QStringLiteral("metadata"));
    if (metadata.isNull()) {
        if (error) *error = QStringLiteral("Package metadata element is missing");
        return false;
    }
    QHash<QString, QString> namespaces;
    namespaces.insert(QStringLiteral("xml"),
                      QStringLiteral("http://www.w3.org/XML/1998/namespace"));
    CollectNamespaces(document, &namespaces);
    while (!metadata.firstChild().isNull()) metadata.removeChild(metadata.firstChild());
    for (const QJsonValue &value : entries) {
        const QJsonObject entry = value.toObject();
        const QString name = entry.value(QStringLiteral("name")).toString();
        if (name.isEmpty() || !value.isObject()
            || !entry.value(QStringLiteral("content")).isString()) {
            if (error) *error = QStringLiteral("Metadata entries require name and content strings");
            return false;
        }
        QString prefix;
        QString local_name;
        if (!SplitQualifiedName(name, &prefix, &local_name, error)) return false;
        if (!prefix.isEmpty() && !namespaces.contains(prefix)) {
            if (error) *error = QStringLiteral("Metadata namespace prefix is undeclared: %1")
                .arg(prefix);
            return false;
        }
        QDomElement element = prefix.isEmpty()
            ? document.createElementNS(metadata.namespaceURI(), local_name)
            : document.createElementNS(namespaces.value(prefix), name);
        const QJsonObject attributes = entry.value(QStringLiteral("attributes")).toObject();
        // Two passes so per-element xmlns:* declarations are available for opf:/etc. attrs.
        for (int pass = 0; pass < 2; ++pass) {
            for (auto it = attributes.constBegin(); it != attributes.constEnd(); ++it) {
                if (!it.value().isString()) {
                    if (error) *error = QStringLiteral("Metadata attributes must be strings");
                    return false;
                }
                const bool is_xmlns = it.key() == QStringLiteral("xmlns")
                    || it.key().startsWith(QStringLiteral("xmlns:"));
                if (pass == 0 ? !is_xmlns : is_xmlns) continue;
                if (!ApplyAttribute(element, it.key(), it.value().toString(),
                                    &namespaces, error)) {
                    return false;
                }
            }
        }
        element.appendChild(document.createTextNode(entry.value(QStringLiteral("content")).toString()));
        metadata.appendChild(element);
    }
    *updated = document.toString(-1);
    return true;
}

bool ApplyManifestAdditions(const QString &source,
                            const QList<PackageManifestAddition> &additions,
                            QString *updated,
                            QString *error)
{
    QDomDocument document;
    if (!ParsePackageDom(source, &document, error)) return false;
    QDomElement manifest = DirectChildElement(document.documentElement(),
                                              QStringLiteral("manifest"));
    if (manifest.isNull()) {
        if (error) *error = QStringLiteral("Package manifest element is missing");
        return false;
    }

    QHash<QString, QDomElement> items_by_id;
    QHash<QString, QString> ids_by_href;
    for (QDomNode child = manifest.firstChild(); !child.isNull();
         child = child.nextSibling()) {
        const QDomElement item = child.toElement();
        if (item.isNull() || (item.localName() != QStringLiteral("item")
                              && item.tagName() != QStringLiteral("item"))) {
            continue;
        }
        const QString id = item.attribute(QStringLiteral("id"));
        const QString href = item.attribute(QStringLiteral("href"));
        if (!id.isEmpty()) items_by_id.insert(id, item);
        if (!href.isEmpty()) ids_by_href.insert(href, id);
    }

    QList<PackageManifestAddition> ordered = additions;
    std::sort(ordered.begin(), ordered.end(),
              [](const PackageManifestAddition &left,
                 const PackageManifestAddition &right) {
        return left.href < right.href;
    });
    for (const PackageManifestAddition &addition : std::as_const(ordered)) {
        if (addition.manifestId.isEmpty() || addition.href.isEmpty()
            || addition.mediaType.isEmpty()) {
            if (error) {
                *error = QStringLiteral("Manifest additions require id, href, and media type");
            }
            return false;
        }
        QDomElement item = items_by_id.value(addition.manifestId);
        if (!item.isNull()) {
            if (item.attribute(QStringLiteral("href")) != addition.href) {
                if (error) *error = QStringLiteral("A manifest ID already uses another href");
                return false;
            }
        } else {
            if (ids_by_href.contains(addition.href)) {
                if (error) *error = QStringLiteral("A manifest href already uses another ID");
                return false;
            }
            item = document.createElementNS(manifest.namespaceURI(),
                                            QStringLiteral("item"));
            item.setAttribute(QStringLiteral("id"), addition.manifestId);
            item.setAttribute(QStringLiteral("href"), addition.href);
            manifest.appendChild(item);
            items_by_id.insert(addition.manifestId, item);
            ids_by_href.insert(addition.href, addition.manifestId);
        }
        item.setAttribute(QStringLiteral("media-type"), addition.mediaType);
        for (const auto &attribute : {
                 std::pair<QString, QString>(QStringLiteral("properties"), addition.properties),
                 std::pair<QString, QString>(QStringLiteral("fallback"), addition.fallback),
                 std::pair<QString, QString>(QStringLiteral("media-overlay"), addition.overlay) }) {
            if (attribute.second.isEmpty()) {
                item.removeAttribute(attribute.first);
            } else {
                item.setAttribute(attribute.first, attribute.second);
            }
        }
    }
    *updated = document.toString(-1);
    return true;
}

bool ApplySpineUpdate(const QString &source, const QJsonArray &items,
                      const QJsonObject &attributes, QString *updated, QString *error)
{
    QDomDocument document;
    if (!ParsePackageDom(source, &document, error)) return false;
    QDomElement spine = DirectChildElement(document.documentElement(), QStringLiteral("spine"));
    if (spine.isNull()) {
        if (error) *error = QStringLiteral("Package spine element is missing");
        return false;
    }
    while (!spine.firstChild().isNull()) spine.removeChild(spine.firstChild());
    for (auto it = attributes.constBegin(); it != attributes.constEnd(); ++it) {
        if (!it.value().isString()) {
            if (error) *error = QStringLiteral("Spine attributes must be strings");
            return false;
        }
        spine.setAttribute(it.key(), it.value().toString());
    }
    for (const QJsonValue &value : items) {
        const QJsonObject item = value.toObject();
        const QString idref = item.value(QStringLiteral("idref")).toString();
        if (!value.isObject() || idref.isEmpty()) {
            if (error) *error = QStringLiteral("Spine items require an idref string");
            return false;
        }
        QDomElement element = document.createElementNS(spine.namespaceURI(),
                                                       QStringLiteral("itemref"));
        element.setAttribute(QStringLiteral("idref"), idref);
        for (const QString &name : { QStringLiteral("id"), QStringLiteral("linear"),
                                     QStringLiteral("properties") }) {
            const QJsonValue attribute = item.value(name);
            if (!attribute.isUndefined()) {
                if (!attribute.isString()) {
                    if (error) *error = QStringLiteral("Spine item attributes must be strings");
                    return false;
                }
                element.setAttribute(name, attribute.toString());
            }
        }
        spine.appendChild(element);
    }
    *updated = document.toString(-1);
    return true;
}

} // namespace PluginApi
