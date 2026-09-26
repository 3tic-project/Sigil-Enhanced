#include "ResourceObjects/OPFSourcePatch.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include <QHash>
#include <QJsonArray>
#include <QJsonValue>
#include <QRegularExpression>
#include <QStringList>
#include <QVector>

#include <expat.h>

#include "sigil_exception.h"

namespace {

const QString OPF = QStringLiteral("http://www.idpf.org/2007/opf");
const QString DC = QStringLiteral("http://purl.org/dc/elements/1.1/");
const QString XMLNS = QStringLiteral("http://www.w3.org/2000/xmlns/");
const QString XML = QStringLiteral("http://www.w3.org/XML/1998/namespace");

struct Span {
    qint64 start = 0;
    qint64 end = 0;
    qint64 valueStart = 0;
    qint64 valueEnd = 0;
    QChar quote;
    QString rawName;
};

class PrefixMap {
public:
    QString value(const QString &key, const QString &fallback = QString()) const
    {
        const auto found = m_index.constFind(key);
        return found == m_index.constEnd() ? fallback : m_items.at(found.value()).second;
    }

    bool contains(const QString &key) const { return m_index.contains(key); }

    void insertOrAssign(const QString &key, const QString &uri)
    {
        const auto found = m_index.constFind(key);
        if (found == m_index.constEnd()) {
            m_index.insert(key, m_items.size());
            m_items.append({key, uri});
        } else {
            m_items[found.value()].second = uri;
        }
    }

    const QVector<QPair<QString, QString>> &items() const { return m_items; }

private:
    QVector<QPair<QString, QString>> m_items;
    QHash<QString, int> m_index;
};

struct Node {
    QString name;
    QHash<QString, QString> attrs;
    QStringList attrOrder;
    qint64 start = 0;
    qint64 openEnd = 0;
    qint64 closeStart = 0;
    qint64 end = 0;
    QString qualified;
    PrefixMap namespaces;
    QVector<QPair<QString, QString>> declared;
    QHash<QString, Span> spans;
    std::vector<std::unique_ptr<Node>> children;
    QString text;
    bool empty = false;

    QVector<Node *> childPointers() const
    {
        QVector<Node *> result;
        result.reserve(int(children.size()));
        for (const auto &child : children) result.append(child.get());
        return result;
    }
};

struct Document {
    QByteArray data;
    std::unique_ptr<Node> root;

    QByteArray raw(const Node *node) const { return data.mid(int(node->start), int(node->end - node->start)); }
};

struct Edit {
    qint64 left = 0;
    qint64 right = 0;
    QByteArray value;
};

QString Expanded(const QString &name, const PrefixMap &namespaces, bool attribute)
{
    if (name == QLatin1String("xmlns")) return XMLNS + QLatin1String("|");
    if (name.startsWith(QLatin1String("xmlns:"))) return XMLNS + QLatin1String("|") + name.mid(6);
    const int colon = name.indexOf(QLatin1Char(':'));
    if (colon >= 0) {
        const QString prefix = name.left(colon);
        if (!namespaces.contains(prefix)) throw ErrorParsingXml("Cannot index package XML: unknown namespace prefix");
        return namespaces.value(prefix) + QLatin1String("|") + name.mid(colon + 1);
    }
    const QString uri = attribute ? QString() : namespaces.value(QString());
    return uri.isEmpty() ? name : uri + QLatin1String("|") + name;
}

bool XmlSpace(char value)
{
    return value == ' ' || value == '\t' || value == '\n' || value == '\r';
}

void ScanAttributes(const QByteArray &raw, qint64 base, const PrefixMap &namespaces, Node *node)
{
    int index = 0;
    while (index < raw.size()) {
        if (!XmlSpace(raw.at(index))) {
            ++index;
            continue;
        }
        const int whitespace = index;
        while (index < raw.size() && XmlSpace(raw.at(index))) ++index;
        const int nameStart = index;
        while (index < raw.size()) {
            const char value = raw.at(index);
            if (XmlSpace(value) || value == '=' || value == '/' || value == '>') break;
            ++index;
        }
        if (index == nameStart) {
            index = whitespace + 1;
            continue;
        }
        const int nameEnd = index;
        while (index < raw.size() && XmlSpace(raw.at(index))) ++index;
        if (index >= raw.size() || raw.at(index) != '=') {
            index = nameStart;
            continue;
        }
        ++index;
        while (index < raw.size() && XmlSpace(raw.at(index))) ++index;
        if (index >= raw.size() || (raw.at(index) != '"' && raw.at(index) != '\'')) {
            index = nameStart;
            continue;
        }
        const char quote = raw.at(index);
        const int valueStart = ++index;
        while (index < raw.size() && raw.at(index) != quote) ++index;
        if (index >= raw.size()) break;
        const int valueEnd = index++;
        const QString rawName = QString::fromUtf8(raw.constData() + nameStart, nameEnd - nameStart);
        Span span;
        span.start = base + whitespace;
        span.end = base + index;
        span.valueStart = base + valueStart;
        span.valueEnd = base + valueEnd;
        span.quote = QChar(quote);
        span.rawName = rawName;
        node->spans.insert(Expanded(rawName, namespaces, true), span);
    }
}

struct ParseState {
    QByteArray *data = nullptr;
    Document *document = nullptr;
    QVector<Node *> stack;
    PrefixMap declarations;
    QString error;
    XML_Parser parser = nullptr;
};

void Fail(ParseState *state, const QString &message)
{
    if (state->error.isEmpty()) state->error = message;
    XML_StopParser(state->parser, XML_FALSE);
}

void XMLCALL StartNamespace(void *user, const XML_Char *prefix, const XML_Char *uri)
{
    auto *state = static_cast<ParseState *>(user);
    state->declarations.insertOrAssign(prefix ? QString::fromUtf8(prefix) : QString(),
                                       uri ? QString::fromUtf8(uri) : QString());
}

void XMLCALL StartElement(void *user, const XML_Char *name, const XML_Char **attributes)
{
    auto *state = static_cast<ParseState *>(user);
    if (!state->error.isEmpty()) return;
    const qint64 offset = XML_GetCurrentByteIndex(state->parser);
    qint64 end = offset;
    char quote = 0;
    const QByteArray &data = *state->data;
    while (end < data.size()) {
        const char value = data.at(int(end));
        if (quote) {
            if (value == quote) quote = 0;
        } else if (value == '"' || value == '\'') {
            quote = value;
        } else if (value == '>') {
            break;
        }
        ++end;
    }
    if (end == data.size()) {
        Fail(state, QStringLiteral("Unterminated XML start tag"));
        return;
    }
    ++end;
    const QByteArray raw = data.mid(int(offset), int(end - offset));
    const int nameStart = 1;
    int nameEnd = nameStart;
    while (nameEnd < raw.size()) {
        const char value = raw.at(nameEnd);
        if (XmlSpace(value) || value == '/' || value == '>') break;
        ++nameEnd;
    }
    try {
        auto node = std::make_unique<Node>();
        node->name = QString::fromUtf8(name);
        node->start = offset;
        node->openEnd = end;
        node->qualified = QString::fromUtf8(raw.constData() + nameStart, nameEnd - nameStart);
        node->namespaces = state->stack.isEmpty() ? PrefixMap() : state->stack.last()->namespaces;
        if (state->stack.isEmpty()) node->namespaces.insertOrAssign(QStringLiteral("xml"), XML);
        for (const auto &item : state->declarations.items()) node->namespaces.insertOrAssign(item.first, item.second);
        node->declared = state->declarations.items();
        state->declarations = PrefixMap();
        node->empty = raw.endsWith("/>");
        for (const XML_Char **cursor = attributes; cursor && *cursor; cursor += 2) {
            const QString key = QString::fromUtf8(cursor[0]);
            const QString value = QString::fromUtf8(cursor[1]);
            if (!node->attrs.contains(key)) node->attrOrder.append(key);
            node->attrs.insert(key, value);
        }
        ScanAttributes(raw, offset, node->namespaces, node.get());
        Node *rawNode = node.get();
        if (state->stack.isEmpty()) state->document->root = std::move(node);
        else state->stack.last()->children.push_back(std::move(node));
        state->stack.append(rawNode);
    } catch (const ErrorParsingXml &error) {
        Fail(state, QString::fromStdString(error.what()));
    }
}

void XMLCALL EndElement(void *user, const XML_Char *)
{
    auto *state = static_cast<ParseState *>(user);
    if (state->stack.isEmpty() || !state->error.isEmpty()) return;
    Node *node = state->stack.takeLast();
    if (node->empty) {
        node->closeStart = node->end = node->openEnd;
    } else {
        node->closeStart = XML_GetCurrentByteIndex(state->parser);
        const int mark = state->data->indexOf('>', int(node->closeStart));
        if (mark < 0) {
            Fail(state, QStringLiteral("Unterminated XML end tag"));
            return;
        }
        node->end = mark + 1;
    }
}

void XMLCALL CharacterData(void *user, const XML_Char *value, int length)
{
    auto *state = static_cast<ParseState *>(user);
    if (!state->stack.isEmpty()) state->stack.last()->text += QString::fromUtf8(value, length);
}

void XMLCALL EntityDeclaration(void *user, const XML_Char *, int, const XML_Char *, int, const XML_Char *, const XML_Char *, const XML_Char *, const XML_Char *)
{
    Fail(static_cast<ParseState *>(user), QStringLiteral("Package source edits do not support custom DTD entities"));
}

int XMLCALL ExternalEntity(XML_Parser, const XML_Char *, const XML_Char *, const XML_Char *, const XML_Char *)
{
    return XML_STATUS_ERROR;
}

Document ParseDocument(const QString &source, bool requirePackage)
{
    Document document;
    document.data = source.toUtf8();
    ParseState state;
    state.data = &document.data;
    state.document = &document;
    state.parser = XML_ParserCreateNS("UTF-8", '|');
    if (!state.parser) throw ErrorParsingXml("Cannot index package XML");
    XML_SetUserData(state.parser, &state);
    XML_SetStartNamespaceDeclHandler(state.parser, StartNamespace);
    XML_SetStartElementHandler(state.parser, StartElement);
    XML_SetEndElementHandler(state.parser, EndElement);
    XML_SetCharacterDataHandler(state.parser, CharacterData);
    XML_SetEntityDeclHandler(state.parser, EntityDeclaration);
    XML_SetExternalEntityRefHandler(state.parser, ExternalEntity);
    const XML_Status status = XML_Parse(state.parser, document.data.constData(), int(document.data.size()), XML_TRUE);
    const QString parserError = QString::fromUtf8(XML_ErrorString(XML_GetErrorCode(state.parser)));
    XML_ParserFree(state.parser);
    if (!state.error.isEmpty()) throw ErrorParsingXml(state.error.toStdString());
    if (status != XML_STATUS_OK) throw ErrorParsingXml(QStringLiteral("Cannot index package XML: ").toStdString() + parserError.toStdString());
    if (!document.root) throw ErrorParsingXml("Cannot index package XML");
    if (requirePackage && document.root->name != OPF + QLatin1String("|package"))
        throw ErrorParsingXml("Expected an OPF package element in the OPF namespace");
    return document;
}

QString Signature(const Node *node)
{
    QStringList attributes;
    for (auto it = node->attrs.constBegin(); it != node->attrs.constEnd(); ++it)
        attributes.append(it.key() + QChar('\t') + it.value());
    attributes.sort();
    const QString text = node->children.empty() ? node->text : node->text.trimmed();
    QStringList children;
    for (const auto &child : node->children) children.append(Signature(child.get()));
    return node->name + QChar('\n') + attributes.join(QChar('\n')) + QChar('\n') + text
        + QChar('\n') + children.join(QChar('\f'));
}

QString Identity(const Node *node)
{
    if (node->attrs.contains(QStringLiteral("id")) && !node->attrs.value(QStringLiteral("id")).isEmpty())
        return node->name + QStringLiteral("\nid\n") + node->attrs.value(QStringLiteral("id"));
    for (const QString &key : {QStringLiteral("idref"), QStringLiteral("property"), QStringLiteral("name"),
                               QStringLiteral("media-type"), QStringLiteral("type")}) {
        if (node->attrs.contains(key))
            return node->name + QChar('\n') + key + QChar('\n') + node->attrs.value(key)
                + QChar('\n') + node->attrs.value(QStringLiteral("refines"));
    }
    return node->name;
}

QHash<int, int> MatchNodes(const QVector<Node *> &oldNodes, const QVector<Node *> &newNodes)
{
    QHash<int, int> matched;
    QVector<int> available;
    for (int index = 0; index < oldNodes.size(); ++index) available.append(index);
    const auto bucketOf = [](const QVector<Node *> &nodes, const QVector<int> &usable, auto key) {
        QHash<QString, QVector<int>> buckets;
        QVector<int> ordered = usable;
        std::sort(ordered.begin(), ordered.end());
        for (int index : ordered) buckets[key(nodes.at(index))].append(index);
        return buckets;
    };
    for (const auto &key : {std::function<QString(const Node *)>(Signature), std::function<QString(const Node *)>(Identity)}) {
        QHash<QString, int> cursor;
        const auto buckets = bucketOf(oldNodes, available, key);
        for (int index = 0; index < newNodes.size(); ++index) {
            if (matched.contains(index)) continue;
            const QString value = key(newNodes.at(index));
            const auto found = buckets.constFind(value);
            if (found == buckets.constEnd()) continue;
            const int position = cursor.value(value);
            if (position >= found.value().size()) continue;
            const int candidate = found.value().at(position);
            cursor.insert(value, position + 1);
            matched.insert(index, candidate);
            available.removeOne(candidate);
        }
    }
    for (int index = 0; index < newNodes.size(); ++index) {
        if (matched.contains(index)) continue;
        const QString name = newNodes.at(index)->name;
        QVector<int> candidates;
        for (int oldIndex : available)
            if (oldNodes.at(oldIndex)->name == name) candidates.append(oldIndex);
        int missing = 0;
        for (int newIndex = 0; newIndex < newNodes.size(); ++newIndex)
            if (!matched.contains(newIndex) && newNodes.at(newIndex)->name == name) ++missing;
        if (candidates.size() == 1 && missing == 1) {
            matched.insert(index, candidates.at(0));
            available.removeOne(candidates.at(0));
        }
    }
    return matched;
}

QByteArray EscapeText(QString value)
{
    value.replace(QLatin1Char('&'), QLatin1String("&amp;"));
    value.replace(QLatin1Char('>'), QLatin1String("&gt;"));
    value.replace(QLatin1Char('<'), QLatin1String("&lt;"));
    value.replace(QLatin1Char('\r'), QLatin1String("&#13;"));
    return value.toUtf8();
}

QByteArray EscapeAttribute(QString value, QChar quote)
{
    value.replace(QLatin1Char('&'), QLatin1String("&amp;"));
    value.replace(QLatin1Char('>'), QLatin1String("&gt;"));
    value.replace(QLatin1Char('<'), QLatin1String("&lt;"));
    value.replace(quote, quote == QLatin1Char('"') ? QLatin1String("&quot;") : QLatin1String("&apos;"));
    value.replace(QLatin1Char('\r'), QLatin1String("&#13;"));
    value.replace(QLatin1Char('\n'), QLatin1String("&#10;"));
    value.replace(QLatin1Char('\t'), QLatin1String("&#9;"));
    return value.toUtf8();
}

QByteArray ApplyEdits(const QByteArray &data, QVector<Edit> edits, qint64 start, qint64 end)
{
    std::stable_sort(edits.begin(), edits.end(), [](const Edit &left, const Edit &right) {
        return left.left < right.left || (left.left == right.left && left.right < right.right);
    });
    qint64 cursor = start;
    QByteArray result;
    for (const Edit &edit : edits) {
        if (!(start <= cursor && cursor <= edit.left && edit.left <= edit.right && edit.right <= end))
            throw ErrorParsingXml("Overlapping or out-of-range package source edits");
        result += data.mid(int(cursor), int(edit.left - cursor));
        result += edit.value;
        cursor = edit.right;
    }
    result += data.mid(int(cursor), int(end - cursor));
    return result;
}

bool UsesNamespace(const Node *node, const QString &prefix, const QString &uri)
{
    const QString declaration = XMLNS + QLatin1String("|") + prefix;
    QRegularExpression curie;
    if (!prefix.isEmpty()) {
        curie = QRegularExpression(QStringLiteral("(?<![\\w.-])") + QRegularExpression::escape(prefix) + QStringLiteral(":(?=[\\w.-])"));
    }
    QVector<const Node *> pending{node};
    while (!pending.isEmpty()) {
        const Node *current = pending.takeLast();
        if (current->namespaces.value(prefix) != uri) continue;
        if (current != node && current->spans.contains(declaration)) continue;
        if (!prefix.isEmpty()) {
            if (current->qualified.startsWith(prefix + QLatin1Char(':'))) return true;
            for (const Span &span : current->spans)
                if (span.rawName.startsWith(prefix + QLatin1Char(':'))) return true;
            for (const QString &value : current->attrs)
                if (curie.match(value).hasMatch()) return true;
        } else if (current->name == (uri.isEmpty() ? current->qualified : uri + QLatin1String("|") + current->qualified)) {
            return true;
        }
        for (const auto &child : current->children) pending.append(child.get());
    }
    return false;
}

QByteArray NewXml(const Document &document, const Node *node, const Node *parent)
{
    QByteArray raw = document.raw(node);
    QByteArray declarations;
    for (const auto &item : node->namespaces.items()) {
        if (item.first == QLatin1String("xml") || parent->namespaces.value(item.first) == item.second) continue;
        if (node->spans.contains(XMLNS + QLatin1String("|") + item.first)) continue;
        if (!UsesNamespace(node, item.first, item.second)) continue;
        const QByteArray name = item.first.isEmpty() ? QByteArray("xmlns") : QByteArray("xmlns:") + item.first.toUtf8();
        declarations += QByteArray(" ") + name + "=\"" + EscapeAttribute(item.second, QLatin1Char('"')) + "\"";
    }
    if (!declarations.isEmpty()) {
        const int offset = int(node->openEnd - node->start - (node->empty ? 2 : 1));
        raw = raw.left(offset) + declarations + raw.mid(offset);
    }
    return raw;
}

QByteArray MergeNode(const Document &source, const Node *actual, const Node *before, const Document &afterDocument, const Node *after);

QByteArray MergeNode(const Document &source, const Node *actual, const Node *before, const Document &afterDocument, const Node *after)
{
    if (Signature(before) == Signature(after)) return source.raw(actual);
    if (actual->name != before->name || before->name != after->name)
        throw ErrorParsingXml("Package node identity changed unexpectedly");
    QVector<Edit> edits;
    QByteArray additions;
    PrefixMap namespaces = actual->namespaces;
    QStringList names;
    for (auto it = before->attrs.constBegin(); it != before->attrs.constEnd(); ++it) names.append(it.key());
    for (auto it = after->attrs.constBegin(); it != after->attrs.constEnd(); ++it)
        if (!names.contains(it.key())) names.append(it.key());
    names.sort();
    for (const QString &name : names) {
        if (before->attrs.value(name) == after->attrs.value(name)
            && before->attrs.contains(name) == after->attrs.contains(name)) continue;
        const auto span = actual->spans.constFind(name);
        if (!after->attrs.contains(name)) {
            if (span != actual->spans.constEnd()) edits.append({span->start, span->end, {}});
        } else if (span != actual->spans.constEnd()) {
            edits.append({span->valueStart, span->valueEnd, EscapeAttribute(after->attrs.value(name), span->quote)});
        } else {
            QString qualified = name;
            if (name.contains(QLatin1Char('|'))) {
                const int split = name.lastIndexOf(QLatin1Char('|'));
                const QString uri = name.left(split);
                const QString local = name.mid(split + 1);
                const auto prefixFor = [](const PrefixMap &map, const QString &wanted) {
                    for (const auto &item : map.items())
                        if (!item.first.isEmpty() && item.second == wanted) return item.first;
                    return QString();
                };
                QString prefix = prefixFor(namespaces, uri);
                if (prefix.isEmpty()) {
                    prefix = prefixFor(after->namespaces, uri);
                    if (prefix.isEmpty()) prefix = QStringLiteral("opfattr");
                    if (namespaces.contains(prefix)) throw ErrorParsingXml("New attribute namespace conflicts with source prefix");
                    additions += QByteArray(" xmlns:") + prefix.toUtf8() + "=\"" + EscapeAttribute(uri, QLatin1Char('"')) + "\"";
                    namespaces.insertOrAssign(prefix, uri);
                }
                qualified = prefix + QLatin1Char(':') + local;
            }
            additions += QByteArray(" ") + qualified.toUtf8() + "=\"" + EscapeAttribute(after->attrs.value(name), QLatin1Char('"')) + "\"";
        }
    }
    if (!additions.isEmpty()) {
        const qint64 offset = actual->openEnd - (actual->empty ? 2 : 1);
        edits.append({offset, offset, additions});
    }

    if (before->children.empty() && after->children.empty()) {
        if (before->text != after->text) {
            if (!actual->children.empty()) throw ErrorParsingXml("Cannot replace metadata text containing unmodeled elements");
            QByteArray value = EscapeText(after->text);
            if (actual->empty) {
                edits.append({actual->openEnd - 2, actual->openEnd,
                              QByteArray(">") + value + "</" + actual->qualified.toUtf8() + ">"});
            } else {
                const QByteArray interior = source.data.mid(int(actual->openEnd), int(actual->closeStart - actual->openEnd));
                QVector<QPair<int, int>> protectedTokens;
                for (int index = 0; index < interior.size();) {
                    if (interior.mid(index, 9) == "<![CDATA[") {
                        const int close = interior.indexOf("]]>", index + 9);
                        if (close < 0) break;
                        index = close + 3;
                        continue;
                    }
                    if (interior.mid(index, 4) == "<!--") {
                        const int close = interior.indexOf("-->", index + 4);
                        if (close < 0) break;
                        protectedTokens.append({index, close + 3});
                        index = close + 3;
                        continue;
                    }
                    if (interior.mid(index, 2) == "<?") {
                        const int close = interior.indexOf("?>", index + 2);
                        if (close < 0) break;
                        protectedTokens.append({index, close + 2});
                        index = close + 2;
                        continue;
                    }
                    ++index;
                }
                if (!protectedTokens.isEmpty()) {
                    qint64 cursor = actual->openEnd;
                    for (const auto &token : protectedTokens) {
                        const qint64 left = actual->openEnd + token.first;
                        edits.append({cursor, left, value});
                        value.clear();
                        cursor = actual->openEnd + token.second;
                    }
                    edits.append({cursor, actual->closeStart, value});
                } else if (interior.startsWith("<![CDATA[") && interior.endsWith("]]>")) {
                    QString literal = after->text;
                    literal.replace(QLatin1String("]]>"), QLatin1String("]]]]><![CDATA[>"));
                    literal.replace(QLatin1Char('\r'), QLatin1String("]]>&#13;<![CDATA["));
                    edits.append({actual->openEnd + 9, actual->closeStart - 3, literal.toUtf8()});
                } else {
                    edits.append({actual->openEnd, actual->closeStart, value});
                }
            }
        }
    } else {
        if (before->text.trimmed() != after->text.trimmed()) throw ErrorParsingXml("Cannot patch mixed package content");
        const auto beforeChildren = before->childPointers();
        const auto afterChildren = after->childPointers();
        const auto actualChildren = actual->childPointers();
        const QHash<int, int> mapping = MatchNodes(beforeChildren, afterChildren);
        const QHash<int, int> actualMapping = MatchNodes(actualChildren, beforeChildren);
        if (actualMapping.size() != beforeChildren.size())
            throw ErrorParsingXml("Cannot locate every modeled package child in original source");
        QVector<QByteArray> output;
        for (int index = 0; index < afterChildren.size(); ++index) {
            if (mapping.contains(index)) {
                const int oldIndex = mapping.value(index);
                Node *original = actualChildren.at(actualMapping.value(oldIndex));
                output.append(MergeNode(source, original, beforeChildren.at(oldIndex), afterDocument, afterChildren.at(index)));
            } else {
                output.append(NewXml(afterDocument, afterChildren.at(index), actual));
            }
        }
        QVector<int> originalIndexes;
        for (int index : actualMapping) originalIndexes.append(index);
        std::sort(originalIndexes.begin(), originalIndexes.end());
        for (int index = 0; index < originalIndexes.size(); ++index) {
            Node *node = actualChildren.at(originalIndexes.at(index));
            const QByteArray value = index < output.size() ? output.at(index) : QByteArray();
            if (source.raw(node) != value) edits.append({node->start, node->end, value});
        }
        if (output.size() > originalIndexes.size()) {
            const QByteArray newline = source.data.contains("\r\n") ? QByteArray("\r\n") : QByteArray("\n");
            QByteArray indent = "  ";
            qint64 insertAt = actual->closeStart;
            if (!originalIndexes.isEmpty()) {
                Node *node = actualChildren.at(originalIndexes.last());
                int line = 0;
                if (node->start > 0) {
                    const int found = source.data.lastIndexOf('\n', int(node->start) - 1);
                    line = found < 0 ? 0 : found + 1;
                }
                const QByteArray prefix = source.data.mid(line, int(node->start - line));
                if (QString::fromUtf8(prefix).trimmed().isEmpty()) indent = prefix;
                insertAt = node->end;
            }
            QByteArray content;
            for (int index = originalIndexes.size(); index < output.size(); ++index)
                content += newline + indent + output.at(index);
            if (actual->empty) {
                edits.append({actual->openEnd - 2, actual->openEnd,
                              QByteArray(">") + content + newline + "</" + actual->qualified.toUtf8() + ">"});
            } else {
                edits.append({insertAt, insertAt, content});
            }
        }
    }
    return ApplyEdits(source.data, edits, actual->start, actual->end);
}

QString LocalName(const QString &expanded)
{
    const int split = expanded.lastIndexOf(QLatin1Char('|'));
    return split < 0 ? expanded : expanded.mid(split + 1);
}

QString UriOf(const QString &expanded)
{
    const int split = expanded.lastIndexOf(QLatin1Char('|'));
    return split < 0 ? QString() : expanded.left(split);
}

QByteArray EscapeModel(QString value)
{
    value.replace(QLatin1Char('&'), QLatin1String("&amp;"));
    value.replace(QLatin1Char('<'), QLatin1String("&lt;"));
    value.replace(QLatin1Char('>'), QLatin1String("&gt;"));
    value.replace(QLatin1Char('"'), QLatin1String("&quot;"));
    value.replace(QLatin1Char('\r'), QLatin1String("&#13;"));
    return value.toUtf8();
}

QByteArray Qualified(const Node *node, const QString &expanded, bool attribute)
{
    const QString uri = UriOf(expanded);
    const QString local = LocalName(expanded);
    if (uri.isEmpty()) return local.toUtf8();
    if (uri == XML) return QByteArray("xml:") + local.toUtf8();
    if (!attribute && uri == OPF) return local.toUtf8();
    if (uri == DC) return QByteArray("dc:") + local.toUtf8();
    for (const auto &item : node->namespaces.items()) {
        if (!item.first.isEmpty() && item.second == uri) return item.first.toUtf8() + ":" + local.toUtf8();
    }
    return local.toUtf8();
}

QByteArray ModelAttributes(const Node *node)
{
    QByteArray result;
    for (const QString &name : node->attrOrder) {
        if (name.startsWith(XMLNS + QLatin1Char('|'))) continue;
        result += QByteArray(" ") + Qualified(node, name, true) + "=\"" + EscapeModel(node->attrs.value(name)) + "\"";
    }
    return result;
}

using NamespaceList = QVector<QPair<QString, QString>>;

// The declarations lxml adds to a projected element whose parent model scope is scope.
NamespaceList ProjectedDeclarations(const Node *node, const PrefixMap &scope)
{
    NamespaceList result;
    for (const auto &item : node->declared) {
        if (item.first.isEmpty() || item.first == QLatin1String("xml") || item.first == QLatin1String("dc")
            || item.first == QLatin1String("opf")) continue;
        if (scope.contains(item.first) && scope.value(item.first) == item.second) continue;
        result.append(item);
    }
    return result;
}

QByteArray ModelDeclarations(const NamespaceList &declarations, PrefixMap *scope)
{
    QByteArray result;
    for (const auto &item : declarations) {
        result += " xmlns:" + item.first.toUtf8() + "=\"" + EscapeModel(item.second) + "\"";
        scope->insertOrAssign(item.first, item.second);
    }
    return result;
}

QByteArray ModelLeaf(const Node *node, const PrefixMap &scope)
{
    PrefixMap leafScope = scope;
    const QByteArray name = Qualified(node, node->name, false);
    const QByteArray open = QByteArray("<") + name + ModelDeclarations(ProjectedDeclarations(node, scope), &leafScope)
        + ModelAttributes(node);
    if (node->text.isEmpty()) return open + "/>";
    return open + ">" + EscapeModel(node->text) + "</" + name + ">";
}

QString SectionLocal(const Node *node)
{
    if (UriOf(node->name) != OPF) return QString();
    const QString local = LocalName(node->name);
    if (local == QLatin1String("metadata") || local == QLatin1String("manifest") || local == QLatin1String("spine")
        || local == QLatin1String("guide") || local == QLatin1String("bindings")) return local;
    return QString();
}

bool ProjectedChild(const QString &section, const Node *node)
{
    if (!node->children.empty()) return false;
    if (section == QLatin1String("metadata")) return node->name.contains(QLatin1Char('|'));
    const QString expected = section == QLatin1String("manifest") ? QStringLiteral("item")
        : section == QLatin1String("spine") ? QStringLiteral("itemref")
        : section == QLatin1String("guide") ? QStringLiteral("reference")
        : QStringLiteral("mediaType");
    return node->name == OPF + QLatin1String("|") + expected;
}

QString SerializeModel(const Document &document)
{
    const Node *root = document.root.get();
    PrefixMap rootScope;
    rootScope.insertOrAssign(QString(), OPF);
    rootScope.insertOrAssign(QStringLiteral("dc"), DC);
    rootScope.insertOrAssign(QStringLiteral("opf"), OPF);
    QByteArray namespaces = " xmlns=\"" + OPF.toUtf8() + "\" xmlns:dc=\"" + DC.toUtf8()
        + "\" xmlns:opf=\"" + OPF.toUtf8() + "\"";
    for (const auto &item : root->namespaces.items()) {
        if (item.first.isEmpty() || item.first == QLatin1String("xml") || item.first == QLatin1String("dc")
            || item.first == QLatin1String("opf")) continue;
        namespaces += " xmlns:" + item.first.toUtf8() + "=\"" + EscapeModel(item.second) + "\"";
        rootScope.insertOrAssign(item.first, item.second);
    }
    QByteArray result = QByteArray("<package") + namespaces + ModelAttributes(root) + ">";
    for (const auto &child : root->children) {
        const QString section = SectionLocal(child.get());
        if (section.isEmpty()) continue;
        PrefixMap sectionScope = rootScope;
        result += QByteArray("\n  <") + LocalName(child->name).toUtf8()
            + ModelDeclarations(ProjectedDeclarations(child.get(), rootScope), &sectionScope)
            + ModelAttributes(child.get()) + ">\n";
        for (const auto &leaf : child->children)
            if (ProjectedChild(section, leaf.get())) result += ModelLeaf(leaf.get(), sectionScope);
        result += "</" + LocalName(child->name).toUtf8() + ">";
    }
    result += "\n</package>\n";
    return QString::fromUtf8(result);
}

[[noreturn]] void Invalid(const QString &message)
{
    throw ErrorParsingXml(message.toStdString());
}

bool IsXmlChar(uint value)
{
    return value == 0x9 || value == 0xA || value == 0xD || (value >= 0x20 && value <= 0xD7FF)
        || (value >= 0xE000 && value <= 0xFFFD) || (value >= 0x10000 && value <= 0x10FFFF);
}

void RequireXmlText(const QString &value)
{
    for (int index = 0; index < value.size(); ++index) {
        const QChar current = value.at(index);
        if (current.isHighSurrogate() && index + 1 < value.size() && value.at(index + 1).isLowSurrogate()) {
            ++index;
            continue;
        }
        if (current.isSurrogate() || !IsXmlChar(current.unicode()))
            Invalid(QStringLiteral("All strings must be XML compatible"));
    }
}

bool NameStart(uint value)
{
    return (value >= 'A' && value <= 'Z') || value == '_' || (value >= 'a' && value <= 'z')
        || (value >= 0xC0 && value <= 0xD6) || (value >= 0xD8 && value <= 0xF6) || (value >= 0xF8 && value <= 0x2FF)
        || (value >= 0x370 && value <= 0x37D) || (value >= 0x37F && value <= 0x1FFF)
        || (value >= 0x200C && value <= 0x200D) || (value >= 0x2070 && value <= 0x218F)
        || (value >= 0x2C00 && value <= 0x2FEF) || (value >= 0x3001 && value <= 0xD7FF)
        || (value >= 0xF900 && value <= 0xFDCF) || (value >= 0xFDF0 && value <= 0xFFFD)
        || (value >= 0x10000 && value <= 0xEFFFF);
}

bool NameChar(uint value)
{
    return NameStart(value) || value == '-' || value == '.' || (value >= '0' && value <= '9') || value == 0xB7
        || (value >= 0x300 && value <= 0x36F) || (value >= 0x203F && value <= 0x2040);
}

void RequireNCName(const QString &name, const char *what)
{
    RequireXmlText(name);
    const QList<uint> codepoints = name.toUcs4();
    bool valid = !codepoints.isEmpty() && NameStart(codepoints.first());
    for (int index = 1; valid && index < codepoints.size(); ++index) valid = NameChar(codepoints.at(index));
    if (!valid) Invalid(QStringLiteral("Invalid %1 name: ").arg(QLatin1String(what)) + name);
}

// RFC 3986 URI-reference, as enforced by libxml2's xmlParseURI for lxml namespaces.
class UriReference {
public:
    explicit UriReference(const QString &value) : m_value(value) {}

    bool valid()
    {
        for (const QChar current : m_value)
            if (current.unicode() > 0x7E || current.unicode() <= 0x20) return false;
        m_index = 0;
        if (absolute()) return true;
        m_index = 0;
        return relative();
    }

private:
    char at(int index) const { return index < m_value.size() ? char(m_value.at(index).unicode()) : '\0'; }
    static bool alpha(char value) { return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z'); }
    static bool digit(char value) { return value >= '0' && value <= '9'; }
    static bool hex(char value) { return digit(value) || (value >= 'A' && value <= 'F') || (value >= 'a' && value <= 'f'); }
    static bool unreserved(char value) { return alpha(value) || digit(value) || value == '-' || value == '.' || value == '_' || value == '~'; }
    static bool subDelim(char value) { return value && std::strchr("!$&'()*+,;=", value); }

    bool percent()
    {
        if (at(m_index) != '%' || !hex(at(m_index + 1)) || !hex(at(m_index + 2))) return false;
        m_index += 3;
        return true;
    }

    bool pchar(bool colon)
    {
        const char value = at(m_index);
        if (unreserved(value) || subDelim(value) || value == '@' || (colon && value == ':')) {
            ++m_index;
            return true;
        }
        return percent();
    }

    int segment(bool colon)
    {
        const int start = m_index;
        while (pchar(colon)) {}
        return m_index - start;
    }

    void segments()
    {
        while (at(m_index) == '/') {
            ++m_index;
            segment(true);
        }
    }

    bool authority()
    {
        const int end = [&] {
            int index = m_index;
            while (index < m_value.size() && at(index) != '/' && at(index) != '?' && at(index) != '#') ++index;
            return index;
        }();
        const int at_sign = m_value.indexOf(QLatin1Char('@'), m_index);
        if (at_sign >= 0 && at_sign < end) {
            while (m_index < at_sign) {
                const char value = at(m_index);
                if (unreserved(value) || subDelim(value) || value == ':') ++m_index;
                else if (!percent()) return false;
            }
            ++m_index;
        }
        if (at(m_index) == '[') {
            ++m_index;
            while (m_index < end && at(m_index) != ']') {
                const char value = at(m_index);
                if (!(unreserved(value) || subDelim(value) || value == ':')) return false;
                ++m_index;
            }
            if (at(m_index) != ']') return false;
            ++m_index;
        } else {
            while (m_index < end) {
                const char value = at(m_index);
                if (unreserved(value) || subDelim(value)) ++m_index;
                else if (value == ':' || !percent()) break;
            }
        }
        if (at(m_index) == ':') {
            ++m_index;
            while (digit(at(m_index))) ++m_index;
        }
        return m_index == end;
    }

    bool tail()
    {
        for (const char marker : {'?', '#'}) {
            if (at(m_index) != marker) continue;
            ++m_index;
            while (pchar(true) || at(m_index) == '/' || at(m_index) == '?') {
                if (at(m_index) == '/' || at(m_index) == '?') ++m_index;
            }
        }
        return m_index == m_value.size();
    }

    bool hierarchy(bool colonInFirstSegment)
    {
        if (at(m_index) == '/' && at(m_index + 1) == '/') {
            m_index += 2;
            if (!authority()) return false;
            segments();
        } else if (at(m_index) == '/') {
            segments();
        } else {
            segment(colonInFirstSegment);
            if (!colonInFirstSegment && at(m_index) == ':') return false;
            segments();
        }
        return tail();
    }

    bool absolute()
    {
        if (!alpha(at(m_index))) return false;
        while (alpha(at(m_index)) || digit(at(m_index)) || at(m_index) == '+' || at(m_index) == '-' || at(m_index) == '.')
            ++m_index;
        if (at(m_index) != ':') return false;
        ++m_index;
        return hierarchy(true);
    }

    bool relative() { return hierarchy(false); }

    QString m_value;
    int m_index = 0;
};

void RequireUri(const QString &uri)
{
    RequireXmlText(uri);
    if (!UriReference(uri).valid()) Invalid(QStringLiteral("Invalid namespace URI ") + uri);
}

struct OutAttr {
    QString expanded;
    QByteArray qualified;
    QString value;
};

struct OutLeaf {
    QByteArray name;
    NamespaceList declarations;
    QVector<OutAttr> attrs;
    QString text;
    bool closeTag = false;
};

struct OutSection {
    const Node *model = nullptr;
    NamespaceList declarations;
    QVector<OutAttr> attrs;
    QVector<OutLeaf> leaves;
};

// Resolves prefixes the way libxml2 sees a projected section of the package model.
class Scope {
public:
    Scope(const Document &model, const Node *section)
    {
        for (const auto &item : model.root->declared)
            if (item.first != QLatin1String("xml")) m_root.append(item);
        m_section = section->declared;
    }

    const NamespaceList &section() const { return m_section; }

    // lxml's nsmap of the section: its own declarations first, then unshadowed root ones.
    NamespaceList nsmap() const
    {
        NamespaceList result = m_section;
        for (const auto &item : m_root)
            if (!Contains(result, item.first)) result.append(item);
        return result;
    }

    bool resolve(const QString &prefix, const NamespaceList &own, QString *uri) const
    {
        if (prefix == QLatin1String("xml")) {
            *uri = XML;
            return true;
        }
        for (const NamespaceList *list : {&own, &m_section, &m_root}) {
            for (const auto &item : *list) {
                if (item.first == prefix) {
                    *uri = item.second;
                    return true;
                }
            }
        }
        return false;
    }

    // lxml _searchNsByHref for an attribute: the nearest unshadowed prefixed declaration.
    bool attributePrefix(const QString &uri, const NamespaceList &own, bool ownIsSection, QString *prefix) const
    {
        if (uri == XML) {
            *prefix = QStringLiteral("xml");
            return true;
        }
        QVector<const NamespaceList *> levels {&own, &m_section, &m_root};
        if (ownIsSection) levels = {&own, &m_root};
        for (int level = 0; level < levels.size(); ++level) {
            for (const auto &item : *levels.at(level)) {
                if (item.second != uri || item.first.isEmpty()) continue;
                bool shadowed = false;
                for (int nearer = 0; nearer < level && !shadowed; ++nearer)
                    shadowed = Contains(*levels.at(nearer), item.first);
                if (shadowed) continue;
                *prefix = item.first;
                return true;
            }
        }
        return false;
    }

    static bool Contains(const NamespaceList &list, const QString &prefix)
    {
        for (const auto &item : list)
            if (item.first == prefix) return true;
        return false;
    }

private:
    NamespaceList m_root;
    NamespaceList m_section;
};

QByteArray QualifiedName(const QString &prefix, const QString &local)
{
    return prefix.isEmpty() ? local.toUtf8() : prefix.toUtf8() + ":" + local.toUtf8();
}

void SetAttribute(QVector<OutAttr> &attrs, const QString &expanded, const QByteArray &qualified, const QString &value)
{
    RequireXmlText(value);
    for (OutAttr &attr : attrs) {
        if (attr.expanded == expanded) {
            attr.value = value;
            return;
        }
    }
    attrs.append({expanded, qualified, value});
}

void RemoveAttribute(QVector<OutAttr> &attrs, const QString &expanded)
{
    attrs.erase(std::remove_if(attrs.begin(), attrs.end(), [&](const OutAttr &attr) { return attr.expanded == expanded; }),
                attrs.end());
}

QVector<OutAttr> ModelAttrs(const Node *node)
{
    QVector<OutAttr> attrs;
    for (const QString &name : node->attrOrder) {
        if (name.startsWith(XMLNS + QLatin1Char('|'))) continue;
        attrs.append({name, Qualified(node, name, true), node->attrs.value(name)});
    }
    return attrs;
}

OutLeaf ModelOutLeaf(const Node *node)
{
    OutLeaf leaf;
    leaf.name = Qualified(node, node->name, false);
    leaf.declarations = node->declared;
    leaf.attrs = ModelAttrs(node);
    leaf.text = node->text;
    return leaf;
}

QByteArray DeclarationsXml(const NamespaceList &declarations)
{
    QByteArray result;
    for (const auto &item : declarations) {
        result += item.first.isEmpty() ? QByteArray(" xmlns=\"") : QByteArray(" xmlns:") + item.first.toUtf8() + "=\"";
        result += EscapeAttribute(item.second, QLatin1Char('"')) + "\"";
    }
    return result;
}

QByteArray AttributesXml(const QVector<OutAttr> &attrs)
{
    QByteArray result;
    for (const OutAttr &attr : attrs)
        result += " " + attr.qualified + "=\"" + EscapeAttribute(attr.value, QLatin1Char('"')) + "\"";
    return result;
}

QByteArray LeafXml(const OutLeaf &leaf)
{
    const QByteArray open = "<" + leaf.name + DeclarationsXml(leaf.declarations) + AttributesXml(leaf.attrs);
    if (leaf.text.isEmpty() && !leaf.closeTag) return open + "/>";
    return open + ">" + EscapeText(leaf.text) + "</" + leaf.name + ">";
}

QString SerializeUpdatedModel(const Document &model, const OutSection &section)
{
    const Node *root = model.root.get();
    QByteArray namespaces = " xmlns=\"" + OPF.toUtf8() + "\" xmlns:dc=\"" + DC.toUtf8()
        + "\" xmlns:opf=\"" + OPF.toUtf8() + "\"";
    for (const auto &item : root->namespaces.items()) {
        if (item.first.isEmpty() || item.first == QLatin1String("xml") || item.first == QLatin1String("dc")
            || item.first == QLatin1String("opf")) continue;
        namespaces += " xmlns:" + item.first.toUtf8() + "=\"" + EscapeAttribute(item.second, QLatin1Char('"')) + "\"";
    }
    QByteArray result = QByteArray("<package") + namespaces + AttributesXml(ModelAttrs(root)) + ">";
    for (const auto &child : root->children) {
        const QString local = SectionLocal(child.get());
        if (local.isEmpty()) continue;
        const bool target = child.get() == section.model;
        result += "\n  <" + local.toUtf8()
            + (target ? DeclarationsXml(section.declarations) + AttributesXml(section.attrs)
                      : DeclarationsXml(child->declared) + AttributesXml(ModelAttrs(child.get())))
            + ">\n";
        if (target) {
            for (const OutLeaf &leaf : section.leaves) result += LeafXml(leaf);
        } else {
            for (const auto &leaf : child->children)
                if (ProjectedChild(local, leaf.get())) result += LeafXml(ModelOutLeaf(leaf.get()));
        }
        result += "</" + local.toUtf8() + ">";
    }
    result += "\n</package>\n";
    return QString::fromUtf8(result);
}

const Node *RequireSection(const Document &model, const QString &name)
{
    const Node *found = nullptr;
    int count = 0;
    for (const auto &child : model.root->children) {
        if (SectionLocal(child.get()) == name) {
            found = child.get();
            ++count;
        }
    }
    if (count != 1) Invalid(QStringLiteral("Package update requires exactly one ") + name);
    return found;
}

QJsonObject StringObject(const QJsonObject &owner, const QString &key)
{
    if (!owner.contains(key)) return {};
    const QJsonValue value = owner.value(key);
    if (!value.isObject()) Invalid(QStringLiteral("Package attributes must be an object of strings"));
    const QJsonObject object = value.toObject();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
        if (!it.value().isString()) Invalid(QStringLiteral("Package attributes must be an object of strings"));
    return object;
}

QString RequiredString(const QJsonValue &value, const QString &key)
{
    const QJsonValue field = value.isObject() ? value.toObject().value(key) : QJsonValue();
    if (!field.isString() || field.toString().isEmpty())
        Invalid(QStringLiteral("Package entries require a non-empty ") + key + QStringLiteral(" string"));
    return field.toString();
}

QString OptionalString(const QJsonObject &owner, const QString &key)
{
    if (!owner.contains(key)) return QString();
    const QJsonValue value = owner.value(key);
    if (!value.isString()) Invalid(QStringLiteral("Package attributes must be strings"));
    return value.toString();
}

QJsonArray ArrayField(const QJsonObject &owner, const QString &key, bool optional, const QString &message)
{
    if (optional && !owner.contains(key)) return {};
    const QJsonValue value = owner.value(key);
    if (!value.isArray()) Invalid(message);
    return value.toArray();
}

void Assign(NamespaceList &namespaces, const QString &prefix, const QString &uri)
{
    for (auto &item : namespaces) {
        if (item.first == prefix) {
            item.second = uri;
            return;
        }
    }
    namespaces.append({prefix, uri});
}

// opf_package_update._namespaces; only metadata entries turn these into declarations.
NamespaceList Namespaces(const Scope &scope, const QJsonObject &attributes, bool declaring)
{
    NamespaceList namespaces = scope.nsmap();
    Assign(namespaces, QStringLiteral("xml"), XML);
    for (auto it = attributes.constBegin(); it != attributes.constEnd(); ++it) {
        const QString name = it.key();
        if (name != QLatin1String("xmlns") && !name.startsWith(QLatin1String("xmlns:"))) continue;
        const QString uri = it.value().toString();
        const bool isDefault = name == QLatin1String("xmlns");
        const QString prefix = isDefault ? QString() : name.mid(6);
        if (uri.isEmpty() || prefix == QLatin1String("xmlns") || (prefix == QLatin1String("xml")) != (uri == XML))
            Invalid(QStringLiteral("Invalid namespace declaration: ") + name);
        if (!isDefault && prefix.isEmpty()) {
            // lxml rejects the empty prefix only when it declares it on a new element.
            if (declaring) Invalid(QStringLiteral("Invalid namespace prefix"));
            continue;
        }
        Assign(namespaces, prefix, uri);
    }
    return namespaces;
}

QString LookUp(const NamespaceList &namespaces, const QString &prefix, bool *found)
{
    for (const auto &item : namespaces) {
        if (item.first == prefix) {
            *found = true;
            return item.second;
        }
    }
    *found = false;
    return QString();
}

// opf_package_update._name, returning the expanded "uri|local" key used by the model parser.
QString ExpandedName(const QString &name, const NamespaceList &namespaces, bool attribute)
{
    if (name.isEmpty()) Invalid(QStringLiteral("XML qualified name must be a non-empty string"));
    const int colon = name.indexOf(QLatin1Char(':'));
    if (colon >= 0) {
        if (name.count(QLatin1Char(':')) != 1) Invalid(QStringLiteral("Invalid XML qualified name: ") + name);
        const QString prefix = name.left(colon);
        const QString local = name.mid(colon + 1);
        bool found = false;
        const QString uri = prefix.isEmpty() ? QString() : LookUp(namespaces, prefix, &found);
        if (!found || local.isEmpty()) Invalid(QStringLiteral("Undeclared or invalid XML prefix: ") + name);
        RequireNCName(local, attribute ? "attribute" : "tag");
        return uri + QLatin1Char('|') + local;
    }
    RequireNCName(name, attribute ? "attribute" : "tag");
    if (attribute) return name;
    bool found = false;
    const QString uri = LookUp(namespaces, QString(), &found);
    return (found ? uri : OPF) + QLatin1Char('|') + name;
}

OutLeaf MetadataLeaf(const Scope &scope, const QJsonValue &value)
{
    const QString name = RequiredString(value, QStringLiteral("name"));
    const QJsonObject entry = value.toObject();
    if (!entry.value(QStringLiteral("content")).isString())
        Invalid(QStringLiteral("Metadata entries require a content string"));
    const QJsonObject attributes = StringObject(entry, QStringLiteral("attributes"));
    const NamespaceList namespaces = Namespaces(scope, attributes, true);
    const QString expanded = ExpandedName(name, namespaces, false);
    OutLeaf leaf;
    for (const auto &item : namespaces) {
        RequireUri(item.second);
        if (!item.first.isEmpty()) RequireNCName(item.first, "prefix");
        QString inScope;
        if (!scope.resolve(item.first, leaf.declarations, &inScope) || inScope != item.second)
            leaf.declarations.append(item);
    }
    const QString uri = UriOf(expanded);
    RequireUri(uri);
    // lxml binds a new element to the first nsmap entry with its namespace.
    bool bound = false;
    for (const auto &item : namespaces) {
        if (item.second != uri) continue;
        leaf.name = QualifiedName(item.first, LocalName(expanded));
        bound = true;
        break;
    }
    if (!bound) Invalid(QStringLiteral("Cannot find a namespace prefix for ") + name);
    QString prefix;
    for (auto it = attributes.constBegin(); it != attributes.constEnd(); ++it) {
        if (it.key() == QLatin1String("xmlns") || it.key().startsWith(QLatin1String("xmlns:"))) continue;
        const QString attribute = ExpandedName(it.key(), namespaces, true);
        QByteArray qualified = LocalName(attribute).toUtf8();
        if (attribute.contains(QLatin1Char('|'))) {
            if (!scope.attributePrefix(UriOf(attribute), leaf.declarations, false, &prefix))
                Invalid(QStringLiteral("Cannot find a namespace prefix for ") + it.key());
            qualified = QualifiedName(prefix, LocalName(attribute));
        }
        SetAttribute(leaf.attrs, attribute, qualified, it.value().toString());
    }
    leaf.text = entry.value(QStringLiteral("content")).toString();
    RequireXmlText(leaf.text);
    leaf.closeTag = true;
    return leaf;
}

OutSection MetadataUpdate(const Document &model, const QJsonObject &payload)
{
    OutSection section;
    section.model = RequireSection(model, QStringLiteral("metadata"));
    section.declarations = section.model->declared;
    section.attrs = ModelAttrs(section.model);
    const Scope scope(model, section.model);
    const QJsonArray entries = ArrayField(payload, QStringLiteral("items"), false, QStringLiteral("Metadata items must be an array"));
    for (const QJsonValue &entry : entries) section.leaves.append(MetadataLeaf(scope, entry));
    return section;
}

bool CodepointLess(const QString &left, const QString &right)
{
    const QList<uint> a = left.toUcs4();
    const QList<uint> b = right.toUcs4();
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end());
}

QString AttributeValue(const OutLeaf &leaf, const QString &name)
{
    for (const OutAttr &attr : leaf.attrs)
        if (attr.expanded == name) return attr.value;
    return QString();
}

void SetOptional(OutLeaf &leaf, const QJsonObject &owner, const QString &name)
{
    const QString value = OptionalString(owner, name);
    if (value.isEmpty()) RemoveAttribute(leaf.attrs, name);
    else SetAttribute(leaf.attrs, name, name.toUtf8(), value);
}

// lxml _searchNsByHref for a new child: section declarations, the section's default, then the root.
QByteArray PackageElement(const Scope &scope, const QString &local)
{
    for (const auto &item : scope.section())
        if (item.second == OPF) return QualifiedName(item.first, local);
    return local.toUtf8();
}

OutSection ManifestUpdate(const Document &model, const QJsonObject &payload)
{
    OutSection section;
    section.model = RequireSection(model, QStringLiteral("manifest"));
    section.declarations = section.model->declared;
    section.attrs = ModelAttrs(section.model);
    const Scope scope(model, section.model);
    QVector<OutLeaf> leaves;
    QVector<bool> kept;
    QHash<QString, int> byId;
    QHash<QString, QString> byHref;
    for (const auto &child : section.model->children) {
        if (!ProjectedChild(QStringLiteral("manifest"), child.get())) continue;
        const QString identifier = child->attrs.value(QStringLiteral("id"));
        const QString href = child->attrs.value(QStringLiteral("href"));
        if (identifier.isEmpty() || href.isEmpty() || byId.contains(identifier) || byHref.contains(href))
            Invalid(QStringLiteral("Manifest has missing or ambiguous IDs/hrefs"));
        byId.insert(identifier, leaves.size());
        byHref.insert(href, identifier);
        leaves.append(ModelOutLeaf(child.get()));
        kept.append(true);
    }
    const QString message = QStringLiteral("Manifest changes must be arrays");
    const QJsonArray removals = ArrayField(payload, QStringLiteral("removals"), true, message);
    const QJsonArray relocations = ArrayField(payload, QStringLiteral("relocations"), true, message);
    const QJsonArray additions = ArrayField(payload, QStringLiteral("additions"), true, message);
    for (const QJsonValue &value : removals) {
        if (!value.isString()) Invalid(QStringLiteral("Manifest removal hrefs must be strings"));
        const QString href = value.toString();
        if (!byHref.contains(href)) continue;
        kept[byId.take(byHref.take(href))] = false;
    }
    for (const QJsonValue &value : relocations) {
        const QString original = RequiredString(value, QStringLiteral("original_href"));
        const QString target = RequiredString(value, QStringLiteral("target_href"));
        if (original == target || !byHref.contains(original)) continue;
        const QString identifier = byHref.value(original);
        if (byHref.contains(target) && byHref.value(target) != identifier)
            Invalid(QStringLiteral("A manifest relocation target is occupied"));
        byHref.remove(original);
        byHref.insert(target, identifier);
        SetAttribute(leaves[byId.value(identifier)].attrs, QStringLiteral("href"), "href", target);
    }
    QVector<QJsonObject> ordered;
    for (const QJsonValue &value : additions) {
        for (const QString &key : {QStringLiteral("id"), QStringLiteral("href"), QStringLiteral("media-type")})
            RequiredString(value, key);
        ordered.append(value.toObject());
    }
    std::stable_sort(ordered.begin(), ordered.end(), [](const QJsonObject &left, const QJsonObject &right) {
        return CodepointLess(left.value(QStringLiteral("href")).toString(), right.value(QStringLiteral("href")).toString());
    });
    for (const QJsonObject &addition : ordered) {
        const QString identifier = addition.value(QStringLiteral("id")).toString();
        const QString href = addition.value(QStringLiteral("href")).toString();
        int index = byId.value(identifier, -1);
        if (index >= 0) {
            if (AttributeValue(leaves.at(index), QStringLiteral("href")) != href)
                Invalid(QStringLiteral("A manifest ID already uses another href"));
        } else {
            if (byHref.contains(href)) Invalid(QStringLiteral("A manifest href already uses another ID"));
            OutLeaf leaf;
            leaf.name = PackageElement(scope, QStringLiteral("item"));
            SetAttribute(leaf.attrs, QStringLiteral("id"), "id", identifier);
            SetAttribute(leaf.attrs, QStringLiteral("href"), "href", href);
            index = leaves.size();
            leaves.append(leaf);
            kept.append(true);
            byId.insert(identifier, index);
            byHref.insert(href, identifier);
        }
        OutLeaf &leaf = leaves[index];
        SetAttribute(leaf.attrs, QStringLiteral("media-type"), "media-type", addition.value(QStringLiteral("media-type")).toString());
        for (const QString &key : {QStringLiteral("properties"), QStringLiteral("fallback"), QStringLiteral("media-overlay")})
            SetOptional(leaf, addition, key);
    }
    for (int index = 0; index < leaves.size(); ++index)
        if (kept.at(index)) section.leaves.append(leaves.at(index));
    return section;
}

QString DefaultPrefix(const QString &uri)
{
    static const QHash<QString, QString> prefixes {
        {QStringLiteral("http://www.w3.org/1999/xhtml"), QStringLiteral("html")},
        {QStringLiteral("http://www.w3.org/1999/XSL/Transform"), QStringLiteral("xsl")},
        {QStringLiteral("http://www.w3.org/1999/02/22-rdf-syntax-ns#"), QStringLiteral("rdf")},
        {QStringLiteral("http://schemas.xmlsoap.org/wsdl/"), QStringLiteral("wsdl")},
        {QStringLiteral("http://www.w3.org/2001/XMLSchema"), QStringLiteral("xs")},
        {QStringLiteral("http://www.w3.org/2001/XMLSchema-instance"), QStringLiteral("xsi")},
        {DC, QStringLiteral("dc")},
        {QStringLiteral("http://codespeak.net/lxml/objectify/pytype"), QStringLiteral("py")},
    };
    return prefixes.value(uri);
}

OutSection SpineUpdate(const Document &model, const QJsonObject &payload)
{
    OutSection section;
    section.model = RequireSection(model, QStringLiteral("spine"));
    section.declarations = section.model->declared;
    section.attrs = ModelAttrs(section.model);
    const Scope scope(model, section.model);
    const QJsonArray items = ArrayField(payload, QStringLiteral("items"), false, QStringLiteral("Spine items must be an array"));
    const QJsonObject attributes = StringObject(payload, QStringLiteral("attributes"));
    const NamespaceList namespaces = Namespaces(scope, attributes, false);
    int generated = 0;
    for (auto it = attributes.constBegin(); it != attributes.constEnd(); ++it) {
        if (it.key() == QLatin1String("xmlns") || it.key().startsWith(QLatin1String("xmlns:"))) continue;
        const QString attribute = ExpandedName(it.key(), namespaces, true);
        QByteArray qualified = attribute.toUtf8();
        if (attribute.contains(QLatin1Char('|'))) {
            const QString uri = UriOf(attribute);
            QString prefix;
            if (!scope.attributePrefix(uri, section.declarations, true, &prefix)) {
                // lxml invents a prefix for a namespace the spine cannot already reach.
                prefix = DefaultPrefix(uri);
                QString unused;
                while (prefix.isEmpty() || scope.resolve(prefix, section.declarations, &unused))
                    prefix = QStringLiteral("ns%1").arg(generated++);
                section.declarations.append({prefix, uri});
            }
            qualified = QualifiedName(prefix, LocalName(attribute));
        }
        SetAttribute(section.attrs, attribute, qualified, it.value().toString());
    }
    QHash<QString, QVector<const Node *>> existing;
    for (const auto &child : section.model->children) {
        if (!ProjectedChild(QStringLiteral("spine"), child.get()) || !child->attrs.contains(QStringLiteral("idref"))) continue;
        existing[child->attrs.value(QStringLiteral("idref"))].append(child.get());
    }
    for (const QJsonValue &value : items) {
        const QString identifier = RequiredString(value, QStringLiteral("idref"));
        QVector<const Node *> &matches = existing[identifier];
        OutLeaf leaf;
        if (!matches.isEmpty()) {
            leaf = ModelOutLeaf(matches.takeFirst());
        } else {
            leaf.name = PackageElement(scope, QStringLiteral("itemref"));
            SetAttribute(leaf.attrs, QStringLiteral("idref"), "idref", identifier);
        }
        const QJsonObject item = value.toObject();
        for (const QString &name : {QStringLiteral("id"), QStringLiteral("linear"), QStringLiteral("properties")})
            SetOptional(leaf, item, name);
        section.leaves.append(leaf);
    }
    return section;
}

}

namespace OPFSourcePatch {

QString ModelXml(const QString &source)
{
    return SerializeModel(ParseDocument(source, true));
}

QString ApplyModelUpdate(const QString &source, const QString &beforeModel, const QString &afterModel)
{
    const Document before = ParseDocument(beforeModel, true);
    const Document after = ParseDocument(afterModel, true);
    if (Signature(before.root.get()) == Signature(after.root.get())) return source;
    const Document original = ParseDocument(source, true);
    const QByteArray root = MergeNode(original, original.root.get(), before.root.get(), after, after.root.get());
    const QByteArray bytes = original.data.left(int(original.root->start)) + root
        + original.data.mid(int(original.root->end));
    const QString result = QString::fromUtf8(bytes);
    ParseDocument(result, true);
    return result;
}

QString AddNavigationManifest(const QString &source, const QString &href, const QString &identifier)
{
    const QString before = ModelXml(source);
    const Document model = ParseDocument(before, true);
    const Node *manifest = nullptr;
    int manifests = 0;
    for (const auto &child : model.root->children) {
        if (SectionLocal(child.get()) == QLatin1String("manifest")) {
            manifest = child.get();
            ++manifests;
        }
    }
    if (manifests != 1) throw ErrorParsingXml("Navigation repair requires exactly one manifest");
    const std::function<void(const Node *)> rejectId = [&](const Node *node) {
        if (node->attrs.value(QStringLiteral("id")) == identifier)
            throw ErrorParsingXml("Navigation manifest identifier already exists");
        for (const auto &child : node->children) rejectId(child.get());
    };
    rejectId(model.root.get());
    for (const auto &child : manifest->children) {
        const QStringList properties = child->attrs.value(QStringLiteral("properties")).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (properties.contains(QStringLiteral("nav")) || child->attrs.value(QStringLiteral("href")) == href)
            throw ErrorParsingXml("Navigation manifest path or property already exists");
    }
    const QString item = QStringLiteral("<item id=\"%1\" href=\"%2\" media-type=\"application/xhtml+xml\" properties=\"nav\"/>")
        .arg(identifier, href);
    QString after = before;
    const int close = after.lastIndexOf(QStringLiteral("</manifest>"));
    if (close < 0) throw ErrorParsingXml("Navigation repair requires exactly one manifest");
    after.insert(close, item);
    return ApplyModelUpdate(source, before, after);
}

QString MapIdentifiers(const QString &source, const QHash<QString, QString> &changedIds)
{
    if (changedIds.isEmpty()) return source;
    const Document document = ParseDocument(source, true);
    QVector<Edit> edits;
    const auto replaceValue = [&](const Node *node, const QString &attribute, const QString &value) {
        const auto span = node->spans.constFind(attribute);
        if (span == node->spans.constEnd()) return;
        edits.append({span->valueStart, span->valueEnd, EscapeAttribute(value, span->quote)});
    };
    const std::function<void(const Node *, const QString &)> visit = [&](const Node *node, const QString &parent) {
        const QString local = LocalName(node->name);
        const bool packageNamespace = UriOf(node->name).isEmpty() || UriOf(node->name) == OPF;
        if (packageNamespace && parent == QLatin1String("manifest") && local == QLatin1String("item")) {
            const QString id = node->attrs.value(QStringLiteral("id"));
            if (changedIds.contains(id)) replaceValue(node, QStringLiteral("id"), changedIds.value(id));
            for (const QString &name : {QStringLiteral("media-overlay"), QStringLiteral("fallback")}) {
                const QString value = node->attrs.value(name);
                if (changedIds.contains(value)) replaceValue(node, name, changedIds.value(value));
            }
        }
        if (packageNamespace && local == QLatin1String("spine")) {
            const QString toc = node->attrs.value(QStringLiteral("toc"));
            if (changedIds.contains(toc)) replaceValue(node, QStringLiteral("toc"), changedIds.value(toc));
        }
        if (packageNamespace && parent == QLatin1String("spine") && local == QLatin1String("itemref")) {
            const QString idref = node->attrs.value(QStringLiteral("idref"));
            if (changedIds.contains(idref)) replaceValue(node, QStringLiteral("idref"), changedIds.value(idref));
        }
        if (packageNamespace && parent == QLatin1String("bindings")
            && (local == QLatin1String("mediaType") || local == QLatin1String("mediatype"))) {
            const QString handler = node->attrs.value(QStringLiteral("handler"));
            if (changedIds.contains(handler)) replaceValue(node, QStringLiteral("handler"), changedIds.value(handler));
        }
        if (local == QLatin1String("meta")) {
            bool cover = false;
            bool property = false;
            bool refines = false;
            for (auto it = node->attrs.constBegin(); it != node->attrs.constEnd(); ++it) {
                if (it.key() == QLatin1String("name") && it.value() == QLatin1String("cover")) cover = true;
                if (it.key() == QLatin1String("property")) property = true;
                if (it.key() == QLatin1String("refines")) refines = true;
            }
            if (cover) {
                const QString content = node->attrs.value(QStringLiteral("content"));
                if (changedIds.contains(content)) replaceValue(node, QStringLiteral("content"), changedIds.value(content));
            }
            if (property && refines) {
                const QString value = node->attrs.value(QStringLiteral("refines"));
                if (value.startsWith(QLatin1Char('#')) && changedIds.contains(value.mid(1)))
                    replaceValue(node, QStringLiteral("refines"), QLatin1Char('#') + changedIds.value(value.mid(1)));
            }
        }
        for (const auto &child : node->children) visit(child.get(), local);
    };
    visit(document.root.get(), QString());
    std::stable_sort(edits.begin(), edits.end(), [](const Edit &left, const Edit &right) {
        return left.left > right.left;
    });
    QByteArray bytes = document.data;
    for (const Edit &edit : edits)
        bytes = bytes.left(int(edit.left)) + edit.value + bytes.mid(int(edit.right));
    return QString::fromUtf8(bytes);
}

QString ApplyPackageUpdate(const QString &source, const QString &operation, const QJsonObject &payload)
{
    using Update = OutSection (*)(const Document &, const QJsonObject &);
    Update update = nullptr;
    if (operation == QLatin1String("metadata")) update = MetadataUpdate;
    else if (operation == QLatin1String("manifest")) update = ManifestUpdate;
    else if (operation == QLatin1String("spine")) update = SpineUpdate;
    else Invalid(QStringLiteral("Unknown package update operation"));
    const QString before = ModelXml(source);
    const Document model = ParseDocument(before, true);
    const OutSection section = update(model, payload);
    return ApplyModelUpdate(source, before, SerializeUpdatedModel(model, section));
}

}
