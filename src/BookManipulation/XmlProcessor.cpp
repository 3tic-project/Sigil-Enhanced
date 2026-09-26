#include "BookManipulation/XmlProcessor.h"

#include <memory>
#include <string>
#include <vector>

#include <expat.h>

#include "BookManipulation/XmlWellFormed.h"
#include "Misc/ReplaceFunctions.h"

namespace XmlProcessor {

namespace {

using Text = std::u32string;

Text Ucs(const QString &value)
{
    const QList<uint> points = value.toUcs4();
    return Text(points.begin(), points.end());
}

QString Str(const Text &value)
{
    QString result;
    result.reserve(qsizetype(value.size()));
    for (const char32_t point : value) {
        if (QChar::requiresSurrogates(point)) {
            result += QChar(QChar::highSurrogate(point));
            result += QChar(QChar::lowSurrogate(point));
        } else {
            result += QChar(char16_t(point));
        }
    }
    return result;
}

QString PyLower(const QString &value) { return Str(ReplaceFunctions::Lower(Ucs(value))); }

Text PyStrip(const Text &value)
{
    std::size_t begin = 0;
    std::size_t end = value.size();
    while (begin < end && ReplaceFunctions::IsPythonSpace(value[begin])) ++begin;
    while (end > begin && ReplaceFunctions::IsPythonSpace(value[end - 1])) --end;
    return value.substr(begin, end - begin);
}

QString PyStrip(const QString &value) { return Str(PyStrip(Ucs(value))); }

bool IsAsciiHex(char32_t value)
{
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') || (value >= 'A' && value <= 'F');
}

// ---- hrefutils ---------------------------------------------------------------

bool NeedsPercentEncoding(char32_t point)
{
    if (point < 128) {
        const bool safe = (point >= 'A' && point <= 'Z') || (point >= 'a' && point <= 'z') || (point >= '0' && point <= '9')
            || point == '_' || point == '.' || point == '-' || point == '/' || point == '~';
        return !safe;
    }
    if (point < 0xA0) return true;
    if (point <= 0xD7FF) return false;
    if (point < 0xF900) return true;
    if (point <= 0xFDCF) return false;
    if (point < 0xFDF0) return true;
    if (point <= 0xFFEF) return false;
    if (point < 0x10000) return true;
    if (point <= 0x1FFFD) return false;
    if (point < 0x20000) return true;
    if (point <= 0x2FFFD) return false;
    if (point < 0x30000) return true;
    if (point <= 0x3FFFD) return false;
    return true;
}

// CPython's UTF-8 decoder with errors="replace": one U+FFFD per maximal invalid subpart.
Text DecodeUtf8Replace(const std::string &bytes)
{
    Text result;
    std::size_t index = 0;
    const auto at = [&](std::size_t position) { return static_cast<unsigned char>(bytes[position]); };
    while (index < bytes.size()) {
        const unsigned char lead = at(index);
        if (lead < 0x80) {
            result += char32_t(lead);
            ++index;
            continue;
        }
        int count = 0;
        unsigned char low = 0x80;
        unsigned char high = 0xBF;
        char32_t point = 0;
        if (lead >= 0xC2 && lead <= 0xDF) { count = 1; point = lead & 0x1F; }
        else if (lead == 0xE0) { count = 2; low = 0xA0; point = lead & 0x0F; }
        else if ((lead >= 0xE1 && lead <= 0xEC) || lead == 0xEE || lead == 0xEF) { count = 2; point = lead & 0x0F; }
        else if (lead == 0xED) { count = 2; high = 0x9F; point = lead & 0x0F; }
        else if (lead == 0xF0) { count = 3; low = 0x90; point = lead & 0x07; }
        else if (lead >= 0xF1 && lead <= 0xF3) { count = 3; point = lead & 0x07; }
        else if (lead == 0xF4) { count = 3; high = 0x8F; point = lead & 0x07; }
        else {
            result += U'\uFFFD';
            ++index;
            continue;
        }
        std::size_t cursor = index + 1;
        bool valid = true;
        for (int step = 0; step < count; ++step, ++cursor) {
            if (cursor >= bytes.size() || at(cursor) < low || at(cursor) > high) {
                valid = false;
                break;
            }
            point = (point << 6) | (at(cursor) & 0x3F);
            low = 0x80;
            high = 0xBF;
        }
        if (!valid) {
            result += U'\uFFFD';
            index = cursor;
            continue;
        }
        result += point;
        index = cursor;
    }
    return result;
}

std::string UnquoteToBytes(const Text &ascii)
{
    std::string string;
    for (const char32_t point : ascii) string += char(point);
    const auto hex = [](char value) { return IsAsciiHex(char32_t(static_cast<unsigned char>(value))); };
    std::string result;
    std::size_t start = 0;
    std::size_t percent = string.find('%');
    if (percent == std::string::npos) return string;
    result = string.substr(0, percent);
    while (percent != std::string::npos) {
        const std::size_t next = string.find('%', percent + 1);
        const std::string item = string.substr(percent + 1, next == std::string::npos ? std::string::npos : next - percent - 1);
        if (item.size() >= 2 && hex(item[0]) && hex(item[1])) {
            result += char(std::stoi(item.substr(0, 2), nullptr, 16));
            result += item.substr(2);
        } else {
            result += '%';
            result += item;
        }
        percent = next;
    }
    Q_UNUSED(start);
    return result;
}

Text Unquote(const Text &value)
{
    if (value.find(U'%') == Text::npos) return value;
    Text result;
    std::size_t index = 0;
    while (index < value.size()) {
        std::size_t end = index;
        const bool ascii = value[index] < 0x80;
        while (end < value.size() && (value[end] < 0x80) == ascii) ++end;
        const Text run = value.substr(index, end - index);
        result += ascii ? DecodeUtf8Replace(UnquoteToBytes(run)) : run;
        index = end;
    }
    return result;
}

QStringList PySplit(const QString &value, QChar separator)
{
    return value.split(separator, Qt::KeepEmptyParts);
}

QString PyRstrip(QString value, const QString &characters)
{
    while (!value.isEmpty() && characters.contains(value.back())) value.chop(1);
    return value;
}

QString RelativePath(const QString &toBookPath, const QString &startDir)
{
    QStringList destination = PySplit(PyRstrip(toBookPath, "/"), '/');
    QStringList start = PySplit(PyRstrip(startDir, "/"), '/');
    if (destination == QStringList {QString()}) destination.clear();
    if (start == QStringList {QString()}) start.clear();
    int common = 0;
    while (common < destination.size() && common < start.size() && destination.at(common) == start.at(common)) ++common;
    QStringList result;
    for (int index = common; index < start.size(); ++index) result.append("..");
    for (int index = common; index < destination.size(); ++index) result.append(destination.at(index));
    return result.join('/');
}

QString ResolveRelativeSegments(const QString &filePath)
{
    QStringList result;
    for (const QString &segment : PySplit(filePath, '/')) {
        if (segment == ".") continue;
        if (segment == "..") {
            if (!result.isEmpty()) result.removeLast();
        } else {
            result.append(segment);
        }
    }
    return result.join('/');
}

// ---- Beautiful Soup tree as built by sigil_bs4's LXMLTreeBuilderForXML ---------

enum class Kind { Tag, Text, Comment, Instruction, Doctype };

struct Node {
    Kind kind = Kind::Tag;
    QString text;
    QString name;
    QString prefix;
    bool hasPrefix = false;
    QVector<QPair<QString, QString>> attrs;
    std::vector<std::unique_ptr<Node>> contents;
    Node *parent = nullptr;
    bool canBeEmpty = false;
    bool hidden = false;

    QString attr(const QString &key, bool *found) const
    {
        for (const auto &item : attrs) {
            if (item.first == key) {
                *found = true;
                return item.second;
            }
        }
        *found = false;
        return QString();
    }

    void setAttr(const QString &key, const QString &value)
    {
        for (auto &item : attrs) {
            if (item.first == key) {
                item.second = value;
                return;
            }
        }
        attrs.append({key, value});
    }

    const Node *nextSibling() const
    {
        if (!parent) return nullptr;
        for (std::size_t index = 0; index + 1 < parent->contents.size(); ++index)
            if (parent->contents[index].get() == this) return parent->contents[index + 1].get();
        return nullptr;
    }
};

const QString XMLNS = QStringLiteral("http://www.w3.org/2000/xmlns/");
const QString XML_NS = QStringLiteral("http://www.w3.org/XML/1998/namespace");

// A prefix of null means the default namespace, as Python None.
struct Prefix {
    bool isNull = true;
    QString value;
    bool operator==(const Prefix &other) const { return isNull == other.isNull && (isNull || value == other.value); }
};

using InvertedMap = QVector<QPair<QString, Prefix>>;

QString StandardPrefix(const QString &uri, bool *found)
{
    static const QHash<QString, QString> prefixes {
        {"http://www.idpf.org/2007/opf", "opf"},
        {"http://purl.org/dc/elements/1.1/", "dc"},
        {"http://purl.org/dc/terms/", "dcterms"},
        {"http://id.loc.gov/vocabulary/", "marc"},
        {"http://www.idpf.org/vocab/rendition/#", "rendition"},
        {"http://www.editeur/org/ONIX/book/codelists/current.html#", "onix"},
        {"http://www.idpf.org/epub/vocab/overlays/#", "media"},
        {"http://www.idpf.org/2007/ops", "epub"},
    };
    *found = prefixes.contains(uri);
    return prefixes.value(uri);
}

class SoupBuilder {
public:
    SoupBuilder(const QStringList &voidTags) : m_voidTags(voidTags)
    {
        m_root = std::make_unique<Node>();
        m_root->name = QStringLiteral("[document]");
        m_root->hidden = true;
        m_stack.append(m_root.get());
        InvertedMap defaults;
        defaults.append({XML_NS, Prefix {false, QStringLiteral("xml")}});
        m_nsmaps.append({true, defaults});
    }

    bool parse(const QString &source)
    {
        const QByteArray bytes = source.toUtf8();
        XML_Parser parser = XML_ParserCreateNS("UTF-8", '|');
        if (!parser) return false;
        XML_SetUserData(parser, this);
        XML_SetStartNamespaceDeclHandler(parser, [](void *user, const XML_Char *prefix, const XML_Char *uri) {
            static_cast<SoupBuilder *>(user)->m_pendingNs.append(
                {prefix ? Prefix {false, QString::fromUtf8(prefix)} : Prefix {}, uri ? QString::fromUtf8(uri) : QString()});
        });
        XML_SetElementHandler(parser,
            [](void *user, const XML_Char *name, const XML_Char **atts) { static_cast<SoupBuilder *>(user)->start(name, atts); },
            [](void *user, const XML_Char *name) { static_cast<SoupBuilder *>(user)->end(name); });
        XML_SetCharacterDataHandler(parser, [](void *user, const XML_Char *text, int length) {
            static_cast<SoupBuilder *>(user)->m_data += QString::fromUtf8(text, length);
            static_cast<SoupBuilder *>(user)->m_hasData = true;
        });
        XML_SetCommentHandler(parser, [](void *user, const XML_Char *text) {
            auto *self = static_cast<SoupBuilder *>(user);
            self->endData();
            self->m_data = QString::fromUtf8(text);
            self->m_hasData = true;
            self->endData(Kind::Comment);
        });
        XML_SetProcessingInstructionHandler(parser, [](void *user, const XML_Char *target, const XML_Char *data) {
            auto *self = static_cast<SoupBuilder *>(user);
            self->endData();
            self->m_data = QString::fromUtf8(target) + ' ' + QString::fromUtf8(data);
            self->m_hasData = true;
            self->endData(Kind::Instruction);
        });
        XML_SetStartDoctypeDeclHandler(parser, [](void *user, const XML_Char *name, const XML_Char *system,
                                                  const XML_Char *publicId, int) {
            static_cast<SoupBuilder *>(user)->doctype(name, publicId, system);
        });
        XML_SetSkippedEntityHandler(parser, [](void *user, const XML_Char *, int) {
            static_cast<SoupBuilder *>(user)->m_unsupported = true;
        });
        XML_SetExternalEntityRefHandler(parser, [](XML_Parser, const XML_Char *, const XML_Char *, const XML_Char *,
                                                   const XML_Char *) { return int(XML_STATUS_ERROR); });
        m_parser = parser;
        const XML_Status status = XML_Parse(parser, bytes.constData(), int(bytes.size()), XML_TRUE);
        XML_ParserFree(parser);
        m_parser = nullptr;
        if (status != XML_STATUS_OK || m_unsupported) return false;
        endData();
        return true;
    }

    Node *root() const { return m_root.get(); }

private:
    static void splitName(const QString &expanded, QString *uri, QString *local, bool *namespaced)
    {
        const int bar = expanded.indexOf('|');
        *namespaced = bar >= 0;
        *uri = *namespaced ? expanded.left(bar) : QString();
        *local = *namespaced ? expanded.mid(bar + 1) : expanded;
    }

    Prefix attrPrefix(const QString &uri) const
    {
        for (int index = m_nsmaps.size() - 1; index >= 0; --index) {
            if (!m_nsmaps.at(index).first) continue;
            for (const auto &item : m_nsmaps.at(index).second)
                if (item.first == uri) return item.second;
        }
        return Prefix {};
    }

    Prefix tagPrefix(const QString &uri) const
    {
        QVector<Prefix> prefixes;
        for (const auto &map : m_nsmaps) {
            if (!map.first) continue;
            for (const auto &item : map.second)
                if (item.first == uri) prefixes.append(item.second);
        }
        for (const Prefix &prefix : prefixes)
            if (prefix.isNull) return Prefix {};
        return prefixes.isEmpty() ? Prefix {} : prefixes.last();
    }

    void start(const XML_Char *expandedName, const XML_Char **atts)
    {
        const int specified = XML_GetSpecifiedAttributeCount(m_parser);
        QVector<QPair<QString, QString>> attrs;
        for (int index = 0; index < specified && atts[index]; index += 2) {
            QString key = QString::fromUtf8(atts[index]);
            const int bar = key.indexOf('|');
            if (bar >= 0) key = '{' + key.left(bar) + '}' + key.mid(bar + 1);
            attrs.append({key, QString::fromUtf8(atts[index + 1])});
        }
        QVector<QPair<Prefix, QString>> nsmap;
        for (auto item : m_pendingNs) {
            if (!item.first.isNull && item.first.value.isEmpty()) item.first = Prefix {};
            bool replaced = false;
            for (auto &existing : nsmap) {
                if (existing.first == item.first) {
                    existing.second = item.second;
                    replaced = true;
                }
            }
            if (!replaced) nsmap.append(item);
        }
        m_pendingNs.clear();
        if (!nsmap.isEmpty()) {
            QVector<QPair<Prefix, QString>> remapped;
            for (const auto &item : nsmap) {
                bool known = false;
                const QString standard = StandardPrefix(item.second, &known);
                Prefix prefix = known ? Prefix {false, standard} : item.first;
                if (!prefix.isNull && prefix.value == QLatin1String("opf") && item.first.isNull) prefix = Prefix {};
                bool replaced = false;
                for (auto &existing : remapped) {
                    if (existing.first == prefix) {
                        existing.second = item.second;
                        replaced = true;
                    }
                }
                if (!replaced) remapped.append({prefix, item.second});
            }
            InvertedMap inverted;
            for (const auto &item : remapped) {
                bool replaced = false;
                for (auto &existing : inverted) {
                    if (existing.first == item.second) {
                        existing.second = item.first;
                        replaced = true;
                    }
                }
                if (!replaced) inverted.append({item.second, item.first});
            }
            m_nsmaps.append({true, inverted});
            for (const auto &item : remapped) {
                const QString key = item.first.isNull ? QStringLiteral("xmlns") : QStringLiteral("xmlns:") + item.first.value;
                assign(attrs, key, item.second);
            }
        } else if (m_nsmaps.size() > 1) {
            m_nsmaps.append(qMakePair(false, InvertedMap()));
        }
        QVector<QPair<QString, QString>> finalAttrs;
        for (const auto &item : attrs) {
            QString key = item.first;
            if (key.startsWith('{')) {
                const int close = key.indexOf('}');
                const QString uri = key.mid(1, close - 1);
                const QString local = key.mid(close + 1);
                const Prefix prefix = attrPrefix(uri);
                key = prefix.isNull ? local : prefix.value + ':' + local;
            }
            assign(finalAttrs, key, item.second);
        }
        QString uri;
        QString local;
        bool namespaced = false;
        splitName(QString::fromUtf8(expandedName), &uri, &local, &namespaced);
        const Prefix prefix = namespaced ? tagPrefix(uri) : Prefix {};
        endData();
        auto tag = std::make_unique<Node>();
        tag->kind = Kind::Tag;
        tag->name = local;
        tag->hasPrefix = !prefix.isNull;
        tag->prefix = prefix.value;
        tag->attrs = finalAttrs;
        tag->canBeEmpty = m_voidTags.contains(local);
        Node *parent = m_stack.last();
        tag->parent = parent;
        Node *raw = tag.get();
        parent->contents.push_back(std::move(tag));
        m_stack.append(raw);
    }

    void end(const XML_Char *expandedName)
    {
        endData();
        QString uri;
        QString local;
        bool namespaced = false;
        splitName(QString::fromUtf8(expandedName), &uri, &local, &namespaced);
        const Prefix prefix = namespaced ? tagPrefix(uri) : Prefix {};
        for (int index = m_stack.size() - 1; index > 0; --index) {
            Node *tag = m_stack.last();
            m_stack.removeLast();
            if (tag->name == local && tag->hasPrefix == !prefix.isNull && (prefix.isNull || tag->prefix == prefix.value)) break;
        }
        if (m_nsmaps.size() > 1) m_nsmaps.removeLast();
    }

    void doctype(const XML_Char *name, const XML_Char *publicId, const XML_Char *system)
    {
        endData();
        QString value = name ? QString::fromUtf8(name) : QString();
        if (publicId) {
            value += QStringLiteral(" PUBLIC \"%1\"").arg(QString::fromUtf8(publicId));
            if (system) value += QStringLiteral("\n \"%1\"").arg(QString::fromUtf8(system));
        } else if (system) {
            value += QStringLiteral(" SYSTEM \"%1\"").arg(QString::fromUtf8(system));
        }
        addString(Kind::Doctype, value);
    }

    void endData(Kind kind = Kind::Text)
    {
        if (!m_hasData) return;
        QString data = m_data;
        m_data.clear();
        m_hasData = false;
        bool strippable = true;
        for (const QChar value : data) {
            if (!QStringLiteral(" \n\t\x0c\r").contains(value)) {
                strippable = false;
                break;
            }
        }
        if (strippable) data = data.contains('\n') ? QStringLiteral("\n") : QStringLiteral(" ");
        addString(kind, data);
    }

    void addString(Kind kind, const QString &value)
    {
        auto node = std::make_unique<Node>();
        node->kind = kind;
        node->text = value;
        node->parent = m_stack.last();
        m_stack.last()->contents.push_back(std::move(node));
    }

    static void assign(QVector<QPair<QString, QString>> &attrs, const QString &key, const QString &value)
    {
        for (auto &item : attrs) {
            if (item.first == key) {
                item.second = value;
                return;
            }
        }
        attrs.append({key, value});
    }

    QStringList m_voidTags;
    std::unique_ptr<Node> m_root;
    QVector<Node *> m_stack;
    QVector<QPair<bool, InvertedMap>> m_nsmaps;
    QVector<QPair<Prefix, QString>> m_pendingNs;
    QString m_data;
    bool m_hasData = false;
    bool m_unsupported = false;
    XML_Parser m_parser = nullptr;
};

// EntitySubstitution.substitute_xml_containing_entities
QString SubstituteXml(const QString &value)
{
    const Text text = Ucs(value);
    Text result;
    for (std::size_t index = 0; index < text.size(); ++index) {
        const char32_t point = text[index];
        if (point == U'<') { result += U"&lt;"; continue; }
        if (point == U'>') { result += U"&gt;"; continue; }
        if (point == U'\u00A0') { result += U"&#160;"; continue; }
        if (point != U'&') { result += point; continue; }
        std::size_t cursor = index + 1;
        bool entity = false;
        if (cursor < text.size() && text[cursor] == U'#') {
            std::size_t digits = cursor + 1;
            while (digits < text.size() && ReplaceFunctions::IsPythonDigit(text[digits])) ++digits;
            if (digits > cursor + 1 && digits < text.size() && text[digits] == U';') entity = true;
            if (!entity && cursor + 1 < text.size() && text[cursor + 1] == U'x') {
                std::size_t hex = cursor + 2;
                while (hex < text.size() && IsAsciiHex(text[hex])) ++hex;
                if (hex > cursor + 2 && hex < text.size() && text[hex] == U';') entity = true;
            }
        }
        if (!entity) {
            std::size_t word = cursor;
            while (word < text.size() && ReplaceFunctions::IsPythonWord(text[word])) ++word;
            if (word > cursor && word < text.size() && text[word] == U';') entity = true;
        }
        result += entity ? Text(1, U'&') : Text(U"&amp;");
    }
    return Str(result);
}

QString QuotedAttribute(QString value)
{
    value.replace('"', QLatin1String("&quot;"));
    return '"' + value + '"';
}

// element.py IS_ENTITY pass: only the three base XML entities survive.
Text EscapeNonBaseEntities(const Text &text)
{
    Text result;
    std::size_t index = 0;
    while (index < text.size()) {
        if (text[index] != U'&') {
            result += text[index++];
            continue;
        }
        std::size_t end = 0;
        std::size_t cursor = index + 1;
        if (cursor < text.size() && text[cursor] == U'#') {
            std::size_t digits = cursor + 1;
            while (digits < text.size() && ReplaceFunctions::IsPythonDigit(text[digits])) ++digits;
            if (digits > cursor + 1 && digits < text.size() && text[digits] == U';') end = digits + 1;
            if (!end && cursor + 1 < text.size() && text[cursor + 1] == U'x') {
                std::size_t hex = cursor + 2;
                while (hex < text.size() && IsAsciiHex(text[hex])) ++hex;
                if (hex > cursor + 2 && hex < text.size() && text[hex] == U';') end = hex + 1;
            }
        }
        if (!end) {
            std::size_t word = cursor;
            while (word < text.size() && ReplaceFunctions::IsPythonWord(text[word])) ++word;
            if (word > cursor && word < text.size() && text[word] == U';') end = word + 1;
        }
        if (!end) {
            result += text[index++];
            continue;
        }
        const Text piece = text.substr(index, end - index);
        result += piece == U"&lt;" || piece == U"&gt;" || piece == U"&amp;" ? piece : U"&amp;" + piece.substr(1);
        index = end;
    }
    return result;
}

bool IsXmlParent(const QString &name)
{
    static const QStringList parents {"package", "metadata", "manifest", "spine", "guide", "ncx", "head", "doctitle",
                                      "docauthor", "navmap", "navpoint", "navlabel", "pagelist", "pagetarget"};
    return parents.contains(PyLower(name));
}

QString Repeat(const QString &value, int count) { return count > 0 ? value.repeated(count) : QString(); }

// Tag.string: a lone child string, or the lone child's own string.
const Node *SingleString(const Node *tag)
{
    if (tag->contents.size() != 1) return nullptr;
    const Node *child = tag->contents.front().get();
    return child->kind == Kind::Tag ? SingleString(child) : child;
}

QString DecodeXml(Node *tag, int indentLevel, const QString &indentChars);

QString DecodeContents(Node *tag, int indentLevel, const QString &indentChars)
{
    const bool parent = IsXmlParent(tag->name);
    QString result;
    bool appended = false;
    for (const auto &child : tag->contents) {
        QString text;
        switch (child->kind) {
        case Kind::Tag:
            result += DecodeXml(child.get(), indentLevel, indentChars);
            appended = true;
            continue;
        case Kind::Text: text = SubstituteXml(child->text); break;
        case Kind::Comment: text = "<!--" + child->text + "-->"; break;
        case Kind::Instruction: text = "<?" + child->text + ">"; break;
        case Kind::Doctype: text = "<!DOCTYPE " + child->text + ">\n"; break;
        }
        if (text.isEmpty()) continue;
        text = Str(EscapeNonBaseEntities(PyStrip(Ucs(text))));
        if (text.isEmpty()) continue;
        if (parent && !appended) result += Repeat(indentChars, indentLevel - 1);
        result += text;
        appended = true;
    }
    return result;
}

QString DecodeXml(Node *tag, int indentLevel, const QString &indentChars)
{
    const bool parent = IsXmlParent(tag->name);
    QStringList attrs;
    for (const auto &item : tag->attrs) attrs.append(item.first + '=' + QuotedAttribute(SubstituteXml(item.second)));
    const QString prefix = tag->hasPrefix && !tag->prefix.isEmpty() ? tag->prefix + ':' : QString();
    if (tag->canBeEmpty) {
        const Node *string = SingleString(tag);
        if (string && PyStrip(string->text).isEmpty()) tag->contents.clear();
    }
    const bool empty = tag->contents.empty() && tag->canBeEmpty;
    const QString close = empty ? QStringLiteral("/") : QString();
    const QString closeTag = empty ? QString() : "</" + prefix + tag->name + '>';
    const QString indent = Repeat(indentChars, indentLevel - 1);
    const QString contents = DecodeContents(tag, parent || tag->hidden ? indentLevel + 1 : indentLevel, indentChars);
    if (tag->hidden) return contents;
    QString result = indent + '<' + prefix + tag->name + (attrs.isEmpty() ? QString() : ' ' + attrs.join(' ')) + close + '>';
    if (parent) result += '\n';
    result += contents;
    if ((!contents.isEmpty() && contents.back() != '\n' && parent) || empty) result += '\n';
    if (!closeTag.isEmpty() && parent) result += indent;
    result += closeTag;
    if (!closeTag.isEmpty() && tag->nextSibling()) result += '\n';
    return result;
}

// xmlprocessor._remove_xml_header: the first <?xml ...?> anywhere, with trailing space.
QString RemoveXmlHeader(const QString &data)
{
    const Text text = Ucs(data);
    const auto space = [&](std::size_t index) { return index < text.size() && ReplaceFunctions::IsPythonSpace(text[index]); };
    for (std::size_t start = 0; start < text.size(); ++start) {
        if (text[start] != U'<') continue;
        std::size_t cursor = start + 1;
        while (space(cursor)) ++cursor;
        if (cursor >= text.size() || text[cursor] != U'?') continue;
        ++cursor;
        if (cursor + 3 > text.size() || (text[cursor] | 0x20) != U'x' || (text[cursor + 1] | 0x20) != U'm'
            || (text[cursor + 2] | 0x20) != U'l') continue;
        cursor += 3;
        while (space(cursor)) ++cursor;
        while (cursor < text.size() && text[cursor] != U'?' && text[cursor] != U'>') ++cursor;
        while (cursor < text.size() && text[cursor] == U'?') ++cursor;
        if (cursor >= text.size() || text[cursor] != U'>') continue;
        ++cursor;
        while (space(cursor)) ++cursor;
        return Str(text.substr(0, start) + text.substr(cursor));
    }
    return data;
}

std::unique_ptr<SoupBuilder> ParseSoup(const QString &data, const QStringList &voidTags)
{
    if (XmlWellFormed::Check(data).line != -1) return nullptr;
    auto builder = std::make_unique<SoupBuilder>(voidTags);
    if (!builder->parse(data)) return nullptr;
    return builder;
}

QString Serialize(Node *root)
{
    QString result = DecodeXml(root, 0, QStringLiteral("  "));
    if (result.startsWith(QLatin1String("<?xml "))) result = RemoveXmlHeader(result);
    return QStringLiteral("<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n") + result;
}

bool NameMatches(const Node *tag, const QStringList &names)
{
    return names.contains(tag->name) || (tag->hasPrefix && names.contains(tag->prefix + ':' + tag->name));
}

template <typename Visit>
void FindAll(Node *node, const QStringList &names, Visit visit)
{
    for (const auto &child : node->contents) {
        if (child->kind != Kind::Tag) continue;
        if (NameMatches(child.get(), names)) visit(child.get());
        FindAll(child.get(), names, visit);
    }
}

const QStringList EBOOK_EMPTY = {"meta", "item", "itemref", "reference", "content"};

bool UpdateReferences(const QString &data, const QStringList &voidTags, const QStringList &names,
                      const QStringList &attributes, const QString &newBookPath, const QString &oldBookPath,
                      const QHash<QString, QString> &updates, QString *out)
{
    auto soup = ParseSoup(RemoveXmlHeader(data), voidTags);
    if (!soup) return false;
    FindAll(soup->root(), names, [&](Node *tag) {
        for (const QString &attribute : attributes) {
            bool found = false;
            const QString reference = tag->attr(attribute, &found);
            if (!found || reference.contains(':')) continue;
            const QStringList parts = PySplit(reference, '#');
            const QString path = UrlDecodePart(parts.at(0));
            const QString fragment = parts.size() > 1 ? UrlDecodePart(parts.at(1)) : QString();
            const QString oldTarget = BuildBookPath(path, StartingDir(oldBookPath));
            const QString newTarget = updates.value(oldTarget, oldTarget);
            QString value = UrlEncodePart(BuildRelativePath(newBookPath, newTarget));
            if (!fragment.isEmpty()) value += '#' + UrlEncodePart(fragment);
            tag->setAttr(attribute, value);
        }
    });
    *out = Serialize(soup->root());
    return true;
}

// ---- opf_newparser -------------------------------------------------------------

QString XmlDecode(QString value)
{
    return value.replace("&quot;", "\"").replace("&gt;", ">").replace("&lt;", "<").replace("&amp;", "&");
}

QString XmlEncode(const QString &value)
{
    return XmlDecode(value).replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;").replace('"', "&quot;");
}

using Attributes = QVector<QPair<QString, QString>>;

QString Pop(Attributes &attrs, const QString &key, const QString &fallback)
{
    for (int index = 0; index < attrs.size(); ++index) {
        if (attrs.at(index).first == key) return attrs.takeAt(index).second;
    }
    return fallback;
}

bool Has(const Attributes &attrs, const QString &key)
{
    for (const auto &item : attrs)
        if (item.first == key) return true;
    return false;
}

void Set(Attributes &attrs, const QString &key, const QString &value)
{
    for (auto &item : attrs) {
        if (item.first == key) {
            item.second = value;
            return;
        }
    }
    attrs.append({key, value});
}

class OpfParser {
public:
    explicit OpfParser(const QString &data) : m_opf(data) { parse(); }

    bool complete() const { return m_hasPackage && m_hasMetadata; }

    QString rebuild() const
    {
        QString result = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
        result += QStringLiteral("<package version=\"%1\" unique-identifier=\"%2\"").arg(m_version, m_uid);
        result += attributes(m_packageAttrs) + ">\n";
        result += "  <metadata" + attributes(m_metadataAttrs) + ">\n";
        for (const auto &entry : m_metadata) {
            result += "    <" + entry.name + attributes(entry.attrs);
            if (entry.content.isNull() || entry.content.isEmpty()) result += "/>\n";
            else result += '>' + XmlEncode(entry.content) + "</" + entry.name + ">\n";
        }
        result += "  </metadata>\n  <manifest>\n";
        for (const auto &item : m_manifest)
            result += QStringLiteral("    <item id=\"%1\" href=\"%2\" media-type=\"%3\"").arg(item.id, item.href, item.mediaType)
                + attributes(item.attrs) + "/>\n";
        result += "  </manifest>\n  <spine" + attributes(m_spineAttrs) + ">\n";
        for (const auto &item : m_spine)
            result += QStringLiteral("    <itemref idref=\"%1\"").arg(item.first) + attributes(item.second) + "/>\n";
        result += "  </spine>\n";
        if (!m_guide.isEmpty()) {
            result += "  <guide>\n";
            for (const auto &reference : m_guide)
                result += QStringLiteral("    <reference type=\"%1\" title=\"%2\" href=\"%3\"/>\n")
                    .arg(reference.at(0), reference.at(1), reference.at(2));
            result += "  </guide>\n";
        }
        if (!m_bindings.isEmpty() && m_version.startsWith('3')) {
            result += "  <bindings>\n";
            for (const auto &binding : m_bindings)
                result += QStringLiteral("  <mediaType media-type=\"%1\" handler=\"%2\"/>\n").arg(binding.first, binding.second);
            result += "  </bindings>\n";
        }
        return result + "</package>\n";
    }

private:
    struct Meta {
        QString name;
        QString content;
        Attributes attrs;
    };
    struct Item {
        QString id;
        QString href;
        QString mediaType;
        Attributes attrs;
    };
    struct Parsed {
        QString type;
        QString name;
        Attributes attrs;
    };

    static QString attributes(const Attributes &attrs)
    {
        QString result;
        for (const auto &item : attrs) result += ' ' + item.first + "=\"" + XmlEncode(item.second) + '"';
        return result;
    }

    // Returns false at the end; otherwise either *text or *tag is set.
    bool next(QString *text, QString *tag)
    {
        const int p = m_position;
        if (p >= m_opf.size()) return false;
        if (m_opf.at(p) != '<') {
            int end = m_opf.indexOf('<', p);
            if (end == -1) end = m_opf.size();
            m_position = end;
            *text = m_opf.mid(p, end - p);
            *tag = QString();
            m_isText = true;
            return true;
        }
        int end;
        if (m_opf.mid(p, 4) == QLatin1String("<!--")) {
            end = m_opf.indexOf("-->", p + 1);
            if (end != -1) end += 2;
        } else {
            end = m_opf.indexOf('>', p + 1);
            const int nextStart = m_opf.indexOf('<', p + 1);
            if (nextStart != -1 && nextStart < end) {
                m_position = nextStart;
                *text = m_opf.mid(p, nextStart - p);
                m_isText = true;
                return true;
            }
        }
        // The legacy parser loops forever on an unterminated tag; its input is always well formed.
        if (end == -1) return false;
        m_position = end + 1;
        *tag = m_opf.mid(p, end + 1 - p);
        m_isText = false;
        return true;
    }

    static Parsed parseTag(const QString &s)
    {
        static const QString whitespace = QStringLiteral(" \n\r\t");
        const int n = s.size();
        int p = 1;
        Parsed result;
        const auto at = [&](int index) { return index >= 0 && index < n ? s.at(index) : QChar(); };
        while (p < n && at(p) == ' ') ++p;
        if (at(p) == '/') {
            result.type = "end";
            ++p;
            while (p < n && at(p) == ' ') ++p;
        }
        int b = p;
        if (s.mid(b).startsWith(QLatin1String("!--"))) {
            result.name = "!--";
            result.type = "comment";
            result.attrs.append({"special", PyStrip(PySlice(s, b + 3, -3))});
            return result;
        }
        while (p < n && !QStringLiteral(">/ \"'\r\n").contains(at(p))) ++p;
        result.name = PyLower(s.mid(b, p - b));
        if (result.name == QLatin1String("!doctype")) result.name = "!DOCTYPE";
        if (result.name == QLatin1String("?xml") || result.name == QLatin1String("!DOCTYPE")) {
            result.type = result.name == QLatin1String("?xml") ? "xmlheader" : "doctype";
            result.attrs.append({"special", PySlice(s, p, -1)});
        }
        if (result.type.isEmpty()) {
            while (s.indexOf('=', p) != -1) {
                while (p < n && whitespace.contains(at(p))) ++p;
                b = p;
                while (p < n && at(p) != '=') ++p;
                QString name = PyLower(s.mid(b, p - b));
                while (!name.isEmpty() && whitespace.contains(name.back())) name.chop(1);
                ++p;
                while (p < n && whitespace.contains(at(p))) ++p;
                QString value;
                if (at(p) == '"' || at(p) == '\'') {
                    const QChar quote = at(p);
                    ++p;
                    b = p;
                    while (p < n && at(p) != '>' && at(p) != '<' && at(p) != quote) ++p;
                    value = s.mid(b, p - b);
                    ++p;
                } else {
                    b = p;
                    while (p < n && at(p) != '>' && at(p) != '/' && at(p) != ' ') ++p;
                    value = s.mid(b, p - b);
                }
                Set(result.attrs, name, value);
            }
        }
        if (result.type.isEmpty()) result.type = s.indexOf('/', p) >= 0 ? "single" : "begin";
        return result;
    }

    static QString PySlice(const QString &s, int start, int end)
    {
        const int n = s.size();
        if (start < 0) start = qMax(0, start + n);
        if (end < 0) end = qMax(0, end + n);
        start = qMin(start, n);
        end = qMin(end, n);
        return end <= start ? QString() : s.mid(start, end - start);
    }

    void parse()
    {
        static const QStringList parents {"package", "metadata", "dc-metadata", "x-metadata", "manifest", "spine",
                                          "tours", "guide", "bindings"};
        QStringList prefix;
        QString content;
        bool hasContent = false;
        Attributes lastAttrs;
        bool hasLast = false;
        int count = 0;
        QString text;
        QString tag;
        while (next(&text, &tag)) {
            if (m_isText) {
                content = PyRstrip(text, " \r\n");
                hasContent = true;
                continue;
            }
            Parsed parsed = parseTag(tag);
            if (parsed.name.startsWith(QLatin1String("opf:"))) {
                m_nsRemap = true;
                parsed.name = parsed.name.mid(4);
            }
            if (parsed.type == QLatin1String("begin")) {
                hasContent = false;
                prefix.append(parsed.name);
                if (parents.contains(parsed.name)) handle(prefix, parsed.name, parsed.attrs, QString(), false, &count);
                else {
                    lastAttrs = parsed.attrs;
                    hasLast = true;
                }
                continue;
            }
            Attributes attrs = parsed.attrs;
            if (parsed.type == QLatin1String("end")) {
                if (!prefix.isEmpty()) prefix.removeLast();
                attrs = hasLast ? lastAttrs : Attributes();
                hasLast = false;
            } else if (parsed.type == QLatin1String("single")) {
                hasContent = false;
            }
            if (parsed.type == QLatin1String("single") || (parsed.type == QLatin1String("end") && !parents.contains(parsed.name)))
                handle(prefix, parsed.name, attrs, content, hasContent, &count);
            hasContent = false;
        }
    }

    void handle(const QStringList &prefix, const QString &name, Attributes attrs, const QString &content, bool hasContent, int *count)
    {
        if (name == QLatin1String("package")) {
            m_version = Pop(attrs, "version", "2.0");
            m_uid = Pop(attrs, "unique-identifier", "bookid");
            if (m_nsRemap && Has(attrs, "xmlns:opf")) {
                Pop(attrs, "xmlns:opf", QString());
                Set(attrs, "xmlns", "http://www.idpf.org/2007/opf");
            }
            m_packageAttrs = attrs;
            m_hasPackage = true;
            return;
        }
        if (name == QLatin1String("dc-metadata") || name == QLatin1String("x-metadata")) return;
        if (name == QLatin1String("metadata")) {
            if (m_nsRemap && !Has(attrs, "xmlns:opf")) Set(attrs, "xmlns:opf", "http://www.idpf.org/2007/opf");
            m_metadataAttrs = attrs;
            m_hasMetadata = true;
            return;
        }
        if (prefix.contains(QStringLiteral("metadata"))) {
            m_metadata.append({name, hasContent ? content : QString(), attrs});
            return;
        }
        if (name == QLatin1String("item") && prefix.contains(QStringLiteral("manifest"))) {
            const QString fallback = QStringLiteral("xid%1").arg(*count, 3, 10, QLatin1Char('0'));
            ++*count;
            Item item;
            item.id = Pop(attrs, "id", fallback);
            item.href = Pop(attrs, "href", QString());
            if (!item.href.contains(':')) item.href = UrlEncodePart(UrlDecodePart(item.href));
            item.mediaType = Pop(attrs, "media-type", QString());
            item.attrs = attrs;
            m_manifest.append(item);
            return;
        }
        if (name == QLatin1String("spine")) {
            m_spineAttrs = attrs;
            return;
        }
        if (name == QLatin1String("itemref") && prefix.contains(QStringLiteral("spine"))) {
            const QString idref = Pop(attrs, "idref", QString());
            m_spine.append({idref, attrs});
            return;
        }
        if (name == QLatin1String("reference") && prefix.contains(QStringLiteral("guide"))) {
            const QString type = Pop(attrs, "type", QString());
            const QString title = Pop(attrs, "title", QString());
            m_guide.append({type, title, Pop(attrs, "href", QString())});
            return;
        }
        if ((name == QLatin1String("mediatype") || name == QLatin1String("mediaType")) && prefix.contains(QStringLiteral("bindings"))) {
            const QString type = Pop(attrs, "media-type", QString());
            m_bindings.append({type, Pop(attrs, "handler", QString())});
        }
    }

    QString m_opf;
    int m_position = 0;
    bool m_isText = false;
    bool m_nsRemap = false;
    bool m_hasPackage = false;
    bool m_hasMetadata = false;
    QString m_version;
    QString m_uid;
    Attributes m_packageAttrs;
    Attributes m_metadataAttrs;
    Attributes m_spineAttrs;
    QVector<Meta> m_metadata;
    QVector<Item> m_manifest;
    QVector<QPair<QString, Attributes>> m_spine;
    QVector<QStringList> m_guide;
    QVector<QPair<QString, QString>> m_bindings;
};

}

QString UrlEncodePart(const QString &part)
{
    QString result;
    for (const char32_t point : Ucs(part)) {
        if (!NeedsPercentEncoding(point)) {
            result += Str(Text(1, point));
            continue;
        }
        for (const char byte : Str(Text(1, point)).toUtf8())
            result += QStringLiteral("%%1").arg(uint(static_cast<unsigned char>(byte)), 2, 16, QLatin1Char('0')).toUpper();
    }
    return result;
}

QString UrlDecodePart(const QString &part) { return Str(Unquote(Ucs(part))); }

QString StartingDir(const QString &filePath)
{
    QStringList segments = PySplit(filePath, '/');
    segments.removeLast();
    return segments.join('/');
}

QString BuildBookPath(const QString &destination, const QString &startFolder)
{
    if (startFolder.isEmpty() || PyStrip(startFolder).isEmpty()) return destination;
    return ResolveRelativeSegments(PyRstrip(startFolder, "/") + '/' + destination);
}

QString BuildRelativePath(const QString &fromBookPath, const QString &toBookPath)
{
    if (fromBookPath == toBookPath) return QString();
    return RelativePath(toBookPath, StartingDir(fromBookPath));
}

bool RebuildOpfXml(const QString &data, QString *out)
{
    const OpfParser parser(data);
    if (!parser.complete()) return false;
    *out = parser.rebuild();
    return true;
}

bool RepairXML(const QString &data, const QString &mediaType, QString *out)
{
    const QString body = RemoveXmlHeader(data);
    const bool opf = mediaType == QLatin1String("application/oebps-package+xml");
    if (!opf) {
        if (XmlWellFormed::Check(body).line != -1) return false;
        *out = data;
        return true;
    }
    auto soup = ParseSoup(body, {"item", "itemref", "mediatype", "mediaType", "reference"});
    if (!soup) return false;
    return RebuildOpfXml(Serialize(soup->root()), out);
}

bool PerformOPFSourceUpdates(const QString &data, const QString &newBookPath, const QString &oldBookPath,
                             const QHash<QString, QString> &updates, QString *out)
{
    return UpdateReferences(data, EBOOK_EMPTY, {"link", "item", "reference", "site"}, {"href"},
                            newBookPath, oldBookPath, updates, out);
}

bool PerformNCXSourceUpdates(const QString &data, const QString &newBookPath, const QString &oldBookPath,
                             const QHash<QString, QString> &updates, QString *out)
{
    return UpdateReferences(data, EBOOK_EMPTY, {"content"}, {"src"}, newBookPath, oldBookPath, updates, out);
}

bool PerformSMILUpdates(const QString &data, const QString &newBookPath, const QString &oldBookPath,
                        const QHash<QString, QString> &updates, QString *out)
{
    return UpdateReferences(data, {"text", "audio"}, {"body", "seq", "text", "audio", "smil", "par"},
                            {"src", "epub:textref"}, newBookPath, oldBookPath, updates, out);
}

bool PerformPageMapUpdates(const QString &data, const QString &newBookPath, const QString &oldBookPath,
                           const QHash<QString, QString> &updates, QString *out)
{
    return UpdateReferences(data, {"page"}, {"page"}, {"href"}, newBookPath, oldBookPath, updates, out);
}

bool AnchorNCXUpdates(const QString &data, const QString &ncxBookPath, const QString &originatingBookPath,
                      const QHash<QString, QString> &idLocations, QString *out)
{
    auto soup = ParseSoup(RemoveXmlHeader(data), EBOOK_EMPTY);
    if (!soup) return false;
    const QString start = StartingDir(ncxBookPath);
    FindAll(soup->root(), {"content"}, [&](Node *tag) {
        bool found = false;
        const QString source = tag->attr("src", &found);
        if (!found || source.contains(':')) return;
        const QStringList parts = PySplit(source, '#');
        QString target = BuildBookPath(UrlDecodePart(parts.at(0)), start);
        if (parts.size() > 1 && target == originatingBookPath && !parts.at(1).isEmpty()) {
            const QString fragment = UrlDecodePart(parts.at(1));
            if (idLocations.contains(fragment)) {
                target = idLocations.value(fragment);
                tag->setAttr("src", UrlEncodePart(BuildRelativePath(ncxBookPath, target)) + '#' + UrlEncodePart(fragment));
            }
        }
    });
    *out = Serialize(soup->root());
    return true;
}

bool AnchorNCXUpdatesAfterMerge(const QString &data, const QString &ncxBookPath, const QString &sinkBookPath,
                                const QStringList &mergedBookPaths, QString *out)
{
    auto soup = ParseSoup(RemoveXmlHeader(data), EBOOK_EMPTY);
    if (!soup) return false;
    const QString start = StartingDir(ncxBookPath);
    FindAll(soup->root(), {"content"}, [&](Node *tag) {
        bool found = false;
        const QString source = tag->attr("src", &found);
        if (!found || source.contains(':')) return;
        const QStringList parts = PySplit(source, '#');
        if (!mergedBookPaths.contains(BuildBookPath(UrlDecodePart(parts.at(0)), start))) return;
        QString value = UrlEncodePart(BuildRelativePath(ncxBookPath, sinkBookPath));
        if (parts.size() > 1 && !parts.at(1).isEmpty()) value += '#' + UrlEncodePart(parts.at(1));
        tag->setAttr("src", value);
    });
    *out = Serialize(soup->root());
    return true;
}

}
