#include "BookManipulation/ManifestIdRebase.h"

#include <QFileInfo>
#include <QHash>
#include <QList>
#include <QRegularExpression>
#include <QUuid>

#include "BookManipulation/ManifestIdUnidecode.h"
#include "ResourceObjects/OPFSourcePatch.h"

namespace {

using Attrs = QList<QPair<QString, QString>>;

struct Meta {
    QString name;
    QString content;
    Attrs attrs;
};

struct Item {
    QString id;
    QString href;
    QString type;
    Attrs attrs;
};

struct SpineItem {
    QString idref;
    Attrs attrs;
};

struct GuideItem {
    QString type;
    QString title;
    QString href;
};

struct Binding {
    QString type;
    QString handler;
};

struct Package {
    QString version = QStringLiteral("2.0");
    QString uid = QStringLiteral("bookid");
    Attrs attrs;
    Attrs metadataAttrs;
    QList<Meta> metadata;
    QList<Item> manifest;
    Attrs spineAttrs;
    QList<SpineItem> spine;
    QList<GuideItem> guide;
    QList<Binding> bindings;
    QHash<QString, int> usedIds;
    bool sawMetadata = false;
    bool nsRemap = false;
};

QString xmlDecode(QString data)
{
    data.replace(QLatin1String("&quot;"), QLatin1String("\""));
    data.replace(QLatin1String("&gt;"), QLatin1String(">"));
    data.replace(QLatin1String("&lt;"), QLatin1String("<"));
    data.replace(QLatin1String("&amp;"), QLatin1String("&"));
    return data;
}

QString xmlEncode(QString data)
{
    data = xmlDecode(data);
    data.replace(QLatin1Char('&'), QLatin1String("&amp;"));
    data.replace(QLatin1Char('<'), QLatin1String("&lt;"));
    data.replace(QLatin1Char('>'), QLatin1String("&gt;"));
    data.replace(QLatin1Char('"'), QLatin1String("&quot;"));
    return data;
}

int hexValue(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    return -1;
}

QString urlDecode(const QString &part)
{
    const QByteArray raw = part.toUtf8();
    QByteArray bytes;
    for (int index = 0; index < raw.size();) {
        const int high = index + 2 < raw.size() && raw.at(index) == '%' ? hexValue(raw.at(index + 1)) : -1;
        const int low = high >= 0 ? hexValue(raw.at(index + 2)) : -1;
        if (low >= 0) {
            bytes.append(char((high << 4) | low));
            index += 3;
        } else {
            bytes.append(raw.at(index));
            ++index;
        }
    }
    return QString::fromUtf8(bytes);
}

bool needsPercent(uint codepoint)
{
    if (codepoint < 128) {
        const QString safe = QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.-/~");
        return !safe.contains(QChar(codepoint));
    }
    if (codepoint < 0xA0) return true;
    if (codepoint <= 0xD7FF) return false;
    if (codepoint < 0xF900) return true;
    if (codepoint <= 0xFDCF) return false;
    if (codepoint < 0xFDF0) return true;
    if (codepoint <= 0xFFEF) return false;
    if (codepoint < 0x10000) return true;
    if (codepoint <= 0x1FFFD) return false;
    if (codepoint < 0x20000) return true;
    if (codepoint <= 0x2FFFD) return false;
    if (codepoint < 0x30000) return true;
    if (codepoint <= 0x3FFFD) return false;
    return true;
}

QString urlEncode(const QString &part)
{
    QString result;
    for (QChar character : part) {
        if (!needsPercent(character.unicode())) {
            result += character;
            continue;
        }
        const QByteArray bytes = QString(character).toUtf8();
        for (unsigned char byte : bytes) result += QStringLiteral("%%1").arg(byte, 2, 16, QLatin1Char('0')).toUpper();
    }
    return result;
}

QString attr(const Attrs &attrs, const QString &key, const QString &fallback = QString())
{
    for (const auto &item : attrs)
        if (item.first == key) return item.second;
    return fallback;
}

Attrs without(Attrs attrs, const QString &key)
{
    Attrs result;
    for (const auto &item : attrs)
        if (item.first != key) result.append(item);
    return result;
}

class Parser {
public:
    explicit Parser(QString opf) : m_opf(std::move(opf)) { parse(); }

    Package package;

private:
    QString m_opf;
    int m_pos = 0;
    Attrs m_lastAttrs;

    QPair<QString, QString> nextToken()
    {
        if (m_pos >= m_opf.size()) return {};
        if (m_opf.at(m_pos) != QLatin1Char('<')) {
            const int end = m_opf.indexOf(QLatin1Char('<'), m_pos);
            const int stop = end < 0 ? m_opf.size() : end;
            const QString text = m_opf.mid(m_pos, stop - m_pos);
            m_pos = stop;
            return {text, {}};
        }
        int end = -1;
        if (m_opf.mid(m_pos, 4) == QLatin1String("<!--")) {
            end = m_opf.indexOf(QLatin1String("-->"), m_pos + 1);
            if (end >= 0) end += 2;
        } else {
            end = m_opf.indexOf(QLatin1Char('>'), m_pos + 1);
            const int next = m_opf.indexOf(QLatin1Char('<'), m_pos + 1);
            if (next >= 0 && (end < 0 || next < end)) {
                const QString text = m_opf.mid(m_pos, next - m_pos);
                m_pos = next;
                return {text, {}};
            }
        }
        if (end < 0) end = m_opf.size() - 1;
        const QString tag = m_opf.mid(m_pos, end + 1 - m_pos);
        m_pos = end + 1;
        return {{}, tag};
    }

    struct Tag { QString type; QString name; Attrs attrs; };

    Tag parseTag(const QString &tag)
    {
        Tag result;
        int index = 1;
        const auto space = [](QChar value) {
            return value == QLatin1Char(' ') || value == QLatin1Char('\n') || value == QLatin1Char('\r') || value == QLatin1Char('\t');
        };
        while (index < tag.size() && tag.at(index) == QLatin1Char(' ')) ++index;
        if (index < tag.size() && tag.at(index) == QLatin1Char('/')) {
            result.type = QStringLiteral("end");
            ++index;
            while (index < tag.size() && tag.at(index) == QLatin1Char(' ')) ++index;
        }
        const int nameStart = index;
        if (tag.mid(nameStart).startsWith(QLatin1String("!--"))) {
            result.name = QStringLiteral("!--");
            result.type = QStringLiteral("comment");
            return result;
        }
        while (index < tag.size()) {
            const QChar value = tag.at(index);
            if (value == QLatin1Char('>') || value == QLatin1Char('/') || value == QLatin1Char(' ') || value == QLatin1Char('"')
                || value == QLatin1Char('\'') || value == QLatin1Char('\r') || value == QLatin1Char('\n')) break;
            ++index;
        }
        result.name = tag.mid(nameStart, index - nameStart).toLower();
        if (result.type.isEmpty()) {
            while (tag.indexOf(QLatin1Char('='), index) >= 0) {
                while (index < tag.size() && space(tag.at(index))) ++index;
                const int attrStart = index;
                while (index < tag.size() && tag.at(index) != QLatin1Char('=')) ++index;
                QString name = tag.mid(attrStart, index - attrStart).trimmed();
                ++index;
                while (index < tag.size() && space(tag.at(index))) ++index;
                QString value;
                if (index < tag.size() && (tag.at(index) == QLatin1Char('"') || tag.at(index) == QLatin1Char('\''))) {
                    const QChar quote = tag.at(index);
                    const int valueStart = ++index;
                    while (index < tag.size() && tag.at(index) != QLatin1Char('>') && tag.at(index) != QLatin1Char('<') && tag.at(index) != quote)
                        ++index;
                    value = tag.mid(valueStart, index - valueStart);
                    if (index < tag.size()) ++index;
                } else {
                    const int valueStart = index;
                    while (index < tag.size() && tag.at(index) != QLatin1Char('>') && tag.at(index) != QLatin1Char('/') && tag.at(index) != QLatin1Char(' '))
                        ++index;
                    value = tag.mid(valueStart, index - valueStart);
                }
                result.attrs.append({name, value});
                if (name == QLatin1String("id")) package.usedIds.insert(value, 1);
            }
            result.type = tag.indexOf(QLatin1Char('/'), index) >= 0 ? QStringLiteral("single") : QStringLiteral("begin");
        }
        return result;
    }

    void parse()
    {
        QStringList prefix;
        QString content;
        while (m_pos < m_opf.size()) {
            const auto token = nextToken();
            if (token.first.isNull() && token.second.isNull()) break;
            if (!token.first.isNull()) {
                content = token.first;
                content.remove(QRegularExpression(QStringLiteral("[ \\r\\n]+$")));
                continue;
            }
            Tag tag = parseTag(token.second);
            if (tag.name.startsWith(QLatin1String("opf:"))) {
                package.nsRemap = true;
                tag.name = tag.name.mid(4);
            }
            if (tag.type == QLatin1String("begin")) {
                content.clear();
                prefix.append(tag.name);
                if (tag.name == QLatin1String("package") || tag.name == QLatin1String("metadata") || tag.name == QLatin1String("spine")
                    || tag.name == QLatin1String("manifest") || tag.name == QLatin1String("guide") || tag.name == QLatin1String("bindings")
                    || tag.name == QLatin1String("dc-metadata") || tag.name == QLatin1String("x-metadata") || tag.name == QLatin1String("tours")) {
                    accept(prefix.join(QLatin1Char('.')), tag.name, tag.attrs, QString());
                } else {
                    m_lastAttrs = tag.attrs;
                }
            } else {
                if (tag.type == QLatin1String("end")) {
                    if (!prefix.isEmpty()) prefix.removeLast();
                    tag.attrs = m_lastAttrs;
                    m_lastAttrs.clear();
                } else if (tag.type == QLatin1String("single")) {
                    content.clear();
                }
                const bool parent = tag.name == QLatin1String("package") || tag.name == QLatin1String("metadata") || tag.name == QLatin1String("spine")
                    || tag.name == QLatin1String("manifest") || tag.name == QLatin1String("guide") || tag.name == QLatin1String("bindings");
                if (tag.type == QLatin1String("single") || (tag.type == QLatin1String("end") && !parent))
                    accept(prefix.join(QLatin1Char('.')), tag.name, tag.attrs, content);
                content.clear();
            }
        }
    }

    void accept(const QString &prefix, const QString &name, Attrs attrs, const QString &content)
    {
        if (name == QLatin1String("package")) {
            package.version = attr(attrs, QStringLiteral("version"), QStringLiteral("2.0"));
            package.uid = attr(attrs, QStringLiteral("unique-identifier"), QStringLiteral("bookid"));
            attrs = without(without(attrs, QStringLiteral("version")), QStringLiteral("unique-identifier"));
            if (package.nsRemap) {
                bool hadOpf = false;
                Attrs kept;
                for (const auto &item : attrs) {
                    if (item.first == QLatin1String("xmlns:opf")) hadOpf = true;
                    else kept.append(item);
                }
                if (hadOpf) kept.append({QStringLiteral("xmlns"), QStringLiteral("http://www.idpf.org/2007/opf")});
                attrs = kept;
            }
            package.attrs = attrs;
            return;
        }
        if (name == QLatin1String("metadata")) {
            if (package.nsRemap && attr(attrs, QStringLiteral("xmlns:opf")).isNull())
                attrs.append({QStringLiteral("xmlns:opf"), QStringLiteral("http://www.idpf.org/2007/opf")});
            package.metadataAttrs = attrs;
            package.sawMetadata = true;
            return;
        }
        if ((name == QLatin1String("meta") || name == QLatin1String("link") || name.startsWith(QLatin1String("dc:")))
            && prefix.contains(QLatin1String("metadata"))) {
            package.metadata.append({name, content, attrs});
            return;
        }
        if (name == QLatin1String("item") && prefix.contains(QLatin1String("manifest"))) {
            QString id = attr(attrs, QStringLiteral("id"), QStringLiteral("xid%1").arg(package.manifest.size(), 3, 10, QLatin1Char('0')));
            QString href = attr(attrs, QStringLiteral("href"));
            if (!href.contains(QLatin1Char(':'))) href = urlEncode(urlDecode(href));
            package.manifest.append({id, href, attr(attrs, QStringLiteral("media-type")),
                                     without(without(without(attrs, QStringLiteral("id")), QStringLiteral("href")), QStringLiteral("media-type"))});
            return;
        }
        if (name == QLatin1String("spine")) {
            package.spineAttrs = attrs;
            return;
        }
        if (name == QLatin1String("itemref") && prefix.contains(QLatin1String("spine"))) {
            package.spine.append({attr(attrs, QStringLiteral("idref")), without(attrs, QStringLiteral("idref"))});
            return;
        }
        if (name == QLatin1String("reference") && prefix.contains(QLatin1String("guide"))) {
            package.guide.append({attr(attrs, QStringLiteral("type")), attr(attrs, QStringLiteral("title")), attr(attrs, QStringLiteral("href"))});
            return;
        }
        if ((name == QLatin1String("mediaType") || name == QLatin1String("mediatype")) && prefix.contains(QLatin1String("bindings")))
            package.bindings.append({attr(attrs, QStringLiteral("media-type")), attr(attrs, QStringLiteral("handler"))});
    }
};

QString validId(QString value)
{
    QString filtered;
    const QString letters = QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
    const QString chars = letters + QStringLiteral("_.-0123456789");
    for (QChar character : value)
        if (chars.contains(character)) filtered += character;
    if (filtered.isEmpty()) filtered = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!letters.contains(filtered.at(0))) filtered.prepend(QLatin1Char('x'));
    return filtered;
}

QString baseId(const QString &href)
{
    const QString fileName = QFileInfo(href).fileName();
    const int dot = fileName.lastIndexOf(QLatin1Char('.'));
    QString name = dot > 0 ? fileName.left(dot) + QLatin1Char('_') + fileName.mid(dot + 1) : fileName;
    return ManifestIdUnidecode::transliterate(name.trimmed());
}

QString uniqueId(QString value, QHash<QString, int> &used)
{
    const QString base = value;
    int count = 0;
    while (used.contains(value)) {
        ++count;
        value = base + QStringLiteral("%1").arg(count, 4, 10, QLatin1Char('0'));
        if (count >= 10000) {
            value = QLatin1Char('x') + QUuid::createUuid().toString(QUuid::WithoutBraces);
            break;
        }
    }
    return value;
}

QString attributes(const Attrs &attrs)
{
    QString result;
    for (const auto &item : attrs) result += QStringLiteral(" %1=\"%2\"").arg(item.first, xmlEncode(item.second));
    return result;
}

QString legacyXml(Package package)
{
    QHash<QString, QString> changed;
    for (Item &item : package.manifest) {
        QString generated = validId(baseId(item.href));
        if (generated == item.id) continue;
        if (package.usedIds.contains(generated)) generated = uniqueId(generated, package.usedIds);
        changed.insert(item.id, generated);
        if (!changed.values().contains(item.id)) package.usedIds.remove(item.id);
        package.usedIds.insert(generated, 1);
        item.id = generated;
    }
    for (auto &item : package.spineAttrs)
        if (item.first == QLatin1String("toc")) item.second = changed.value(item.second, item.second);
    for (auto &item : package.spine) item.idref = changed.value(item.idref, item.idref);
    for (auto &item : package.bindings) item.handler = changed.value(item.handler, item.handler);
    for (Meta &meta : package.metadata) {
        if (meta.name != QLatin1String("meta")) continue;
        bool cover = false;
        bool property = false;
        bool refines = false;
        for (const auto &item : meta.attrs) {
            if (item.first == QLatin1String("name") && item.second == QLatin1String("cover")) cover = true;
            if (item.first == QLatin1String("property")) property = true;
            if (item.first == QLatin1String("refines")) refines = true;
        }
        if (!cover && !(property && refines)) continue;
        for (auto &item : meta.attrs) {
            if (cover && item.first == QLatin1String("content")) item.second = changed.value(item.second, item.second);
            else if (property && refines && item.first == QLatin1String("refines") && item.second.startsWith(QLatin1Char('#'))) {
                const QString id = item.second.mid(1);
                item.second = QLatin1Char('#') + changed.value(id, id);
            }
        }
    }
    for (Item &item : package.manifest) {
        for (auto &attr : item.attrs) {
            if (attr.first == QLatin1String("media-overlay") || attr.first == QLatin1String("fallback"))
                attr.second = changed.value(attr.second, attr.second);
        }
    }

    QString xml = QStringLiteral("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
    xml += QStringLiteral("<package version=\"") + package.version + QStringLiteral("\" unique-identifier=\"") + package.uid + QLatin1Char('"');
    xml += attributes(package.attrs) + QStringLiteral(">\n");
    xml += QStringLiteral("  <metadata") + attributes(package.metadataAttrs) + QStringLiteral(">\n");
    for (const Meta &meta : package.metadata) {
        xml += QStringLiteral("    <%1").arg(meta.name) + attributes(meta.attrs);
        xml += meta.content.isEmpty() ? QStringLiteral("/>\n")
                                      : QStringLiteral(">") + xmlEncode(meta.content) + QStringLiteral("</") + meta.name + QStringLiteral(">\n");
    }
    xml += QStringLiteral("  </metadata>\n  <manifest>\n");
    for (const Item &item : package.manifest) {
        xml += QStringLiteral("    <item id=\"") + item.id + QStringLiteral("\" href=\"") + item.href
            + QStringLiteral("\" media-type=\"") + item.type + QLatin1Char('"');
        xml += attributes(item.attrs) + QStringLiteral("/>\n");
    }
    xml += QStringLiteral("  </manifest>\n  <spine") + attributes(package.spineAttrs) + QStringLiteral(">\n");
    for (const SpineItem &item : package.spine) {
        xml += QStringLiteral("    <itemref idref=\"") + item.idref + QLatin1Char('"');
        xml += attributes(item.attrs) + QStringLiteral("/>\n");
    }
    xml += QStringLiteral("  </spine>\n");
    if (!package.guide.isEmpty()) {
        xml += QStringLiteral("  <guide>\n");
        for (const GuideItem &item : package.guide) {
            xml += QStringLiteral("    <reference type=\"") + item.type + QStringLiteral("\" title=\"")
                + item.title + QStringLiteral("\" href=\"") + item.href + QStringLiteral("\"/>\n");
        }
        xml += QStringLiteral("  </guide>\n");
    }
    if (!package.bindings.isEmpty() && package.version.startsWith(QLatin1Char('3'))) {
        xml += QStringLiteral("  <bindings>\n");
        for (const Binding &item : package.bindings) {
            xml += QStringLiteral("  <mediaType media-type=\"") + item.type + QStringLiteral("\" handler=\"")
                + item.handler + QStringLiteral("\"/>\n");
        }
        xml += QStringLiteral("  </bindings>\n");
    }
    xml += QStringLiteral("</package>\n");
    return xml;
}

QHash<QString, QString> changedIds(const Package &package)
{
    QHash<QString, QString> changed;
    QHash<QString, int> used = package.usedIds;
    for (const Item &item : package.manifest) {
        QString generated = validId(baseId(item.href));
        if (generated == item.id) continue;
        if (used.contains(generated)) generated = uniqueId(generated, used);
        changed.insert(item.id, generated);
        if (!changed.values().contains(item.id)) used.remove(item.id);
        used.insert(generated, 1);
    }
    return changed;
}

}

namespace ManifestIdRebase {

QString Legacy(const QString &opf)
{
    return legacyXml(Parser(opf).package);
}

QString Preserving(const QString &opf)
{
    return OPFSourcePatch::MapIdentifiers(opf, changedIds(Parser(opf).package));
}

}
