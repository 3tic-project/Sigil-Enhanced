#include "Misc/MetadataProcessor.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Misc/ReplaceFunctions.h"

namespace {

using U = std::u32string;

// Stands for any exception the legacy Python code would raise.
struct LegacyError {};

U ToU(const QString &text)
{
    const QList<uint> points = text.toUcs4();
    return U(points.begin(), points.end());
}

QString FromU(const U &text)
{
    // QString::fromUcs4 would drop a leading U+FEFF.
    QString result;
    result.reserve(int(text.size()));
    for (char32_t c : text) {
        if (c > 0xFFFF) {
            result += QChar(QChar::highSurrogate(c));
            result += QChar(QChar::lowSurrogate(c));
        } else {
            result += QChar(char16_t(c));
        }
    }
    return result;
}

U L(const char *text)
{
    U result;
    while (*text) result += char32_t(uchar(*text++));
    return result;
}

bool StartsWith(const U &text, const U &prefix)
{
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

bool EndsWith(const U &text, const U &suffix)
{
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool Contains(const U &text, const U &part)
{
    return text.find(part) != U::npos;
}

// Python slicing text[start:end] with negative indices.
U Slice(const U &text, long long start, long long end)
{
    const long long size = static_cast<long long>(text.size());
    if (start < 0) start = std::max(0LL, start + size);
    if (end < 0) end = std::max(0LL, end + size);
    start = std::min(start, size);
    end = std::min(end, size);
    return start < end ? text.substr(size_t(start), size_t(end - start)) : U();
}

U Strip(const U &text)
{
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && ReplaceFunctions::IsPythonSpace(text[begin])) ++begin;
    while (end > begin && ReplaceFunctions::IsPythonSpace(text[end - 1])) --end;
    return text.substr(begin, end - begin);
}

U RStrip(const U &text, const U &characters)
{
    size_t end = text.size();
    while (end > 0 && characters.find(text[end - 1]) != U::npos) --end;
    return text.substr(0, end);
}

U Replace(U text, const U &from, const U &to)
{
    for (size_t position = 0; (position = text.find(from, position)) != U::npos; position += to.size())
        text.replace(position, from.size(), to);
    return text;
}

U XmlDecode(const U &data)
{
    U result = Replace(data, L("&quot;"), L("\""));
    result = Replace(result, L("&gt;"), L(">"));
    result = Replace(result, L("&lt;"), L("<"));
    return Replace(result, L("&amp;"), L("&"));
}

U XmlEncode(const U &data)
{
    U result = Replace(XmlDecode(data), L("&"), L("&amp;"));
    result = Replace(result, L("<"), L("&lt;"));
    result = Replace(result, L(">"), L("&gt;"));
    return Replace(result, L("\""), L("&quot;"));
}

// OrderedDict with str keys; a missing key where Python indexes raises.
class Attrs {
public:
    bool Has(const U &key) const { return Find(key) != m_items.end(); }
    std::optional<U> Get(const U &key) const
    {
        const auto found = Find(key);
        return found == m_items.end() ? std::nullopt : std::optional<U>(found->second);
    }
    U Get(const U &key, const U &fallback) const { return Get(key).value_or(fallback); }
    U At(const U &key) const
    {
        const auto value = Get(key);
        if (!value) throw LegacyError();
        return *value;
    }
    void Set(const U &key, const U &value)
    {
        const auto found = std::find_if(m_items.begin(), m_items.end(), [&](const auto &item) { return item.first == key; });
        if (found == m_items.end()) m_items.emplace_back(key, value);
        else found->second = value;
    }
    U Pop(const U &key, const U &fallback)
    {
        const auto found = std::find_if(m_items.begin(), m_items.end(), [&](const auto &item) { return item.first == key; });
        if (found == m_items.end()) return fallback;
        U value = found->second;
        m_items.erase(found);
        return value;
    }
    void Delete(const U &key)
    {
        const auto found = std::find_if(m_items.begin(), m_items.end(), [&](const auto &item) { return item.first == key; });
        if (found == m_items.end()) throw LegacyError();
        m_items.erase(found);
    }
    bool Empty() const { return m_items.empty(); }
    const std::vector<std::pair<U, U>> &Items() const { return m_items; }

private:
    std::vector<std::pair<U, U>>::const_iterator Find(const U &key) const
    {
        return std::find_if(m_items.begin(), m_items.end(), [&](const auto &item) { return item.first == key; });
    }
    std::vector<std::pair<U, U>> m_items;
};

struct Entry {
    U name;
    std::optional<U> content;
    Attrs attrs;
};

U BuildXml(const Entry &entry)
{
    U tag = L("<") + entry.name;
    for (const auto &[key, value] : entry.attrs.Items()) tag += L(" ") + key + L("=\"") + XmlEncode(value) + L("\"");
    if (entry.content) tag += L(">") + XmlEncode(*entry.content) + L("</") + entry.name + L(">\n");
    else tag += L(" />\n");
    return tag;
}

U ValidId(const U &id, const std::vector<U> &ids)
{
    U candidate = id;
    for (int position = 1; std::find(ids.begin(), ids.end(), candidate) != ids.end(); ++position) {
        std::string suffix = std::to_string(position);
        if (suffix.size() < 3) suffix.insert(0, 3 - suffix.size(), '0');
        candidate = id + L(suffix.c_str());
    }
    return candidate;
}

void RemoveFirst(std::vector<U> &ids, const U &id)
{
    const auto found = std::find(ids.begin(), ids.end(), id);
    if (found != ids.end()) ids.erase(found);
}

const std::vector<U> &ParentTags()
{
    static const std::vector<U> tags {L("xml"), L("package"), L("metadata"), L("dc-metadata"), L("x-metadata"),
                                      L("manifest"), L("spine"), L("tours"), L("guide"), L("bindings")};
    return tags;
}

bool In(const U &value, const std::vector<U> &list)
{
    return std::find(list.begin(), list.end(), value) != list.end();
}

// metadata_utils.OPFMetadataParser, limited to what the editor reads.
class OpfMetadataParser {
public:
    explicit OpfMetadataParser(const U &opf) : m_opf(opf) { Parse(); }

    std::optional<std::pair<U, U>> package;  // (version, unique-identifier)
    std::optional<Attrs> metadataAttrs;
    std::vector<Entry> metadata;
    std::vector<U> ids;

private:
    struct Tag {
        U type;
        U name;
        Attrs attrs;
    };

    // Returns false at the end; sets exactly one of *text or *tag.
    bool Next(std::optional<U> *text, std::optional<U> *tag)
    {
        const size_t p = m_position;
        if (p >= m_opf.size()) return false;
        if (m_opf[p] != U'<') {
            size_t end = m_opf.find(U'<', p);
            if (end == U::npos) end = m_opf.size();
            m_position = end;
            *text = m_opf.substr(p, end - p);
            return true;
        }
        size_t end;
        if (m_opf.compare(p, 4, L("<!--")) == 0) {
            end = m_opf.find(L("-->"), p + 1);
            if (end != U::npos) end += 2;
        } else {
            end = m_opf.find(U'>', p + 1);
            const size_t next = m_opf.find(U'<', p + 1);
            if (next != U::npos && end != U::npos && next < end) {
                m_position = next;
                *text = m_opf.substr(p, next - p);
                return true;
            }
        }
        // The legacy scanner restarts from offset 0 here and never ends.
        if (end == U::npos) throw LegacyError();
        m_position = end + 1;
        *tag = m_opf.substr(p, end + 1 - p);
        return true;
    }

    static Tag ParseTag(const U &s)
    {
        const size_t n = s.size();
        size_t p = 1;
        Tag tag;
        const auto at = [&](size_t index) { return index < n ? s[index] : char32_t(0); };
        while (p < n && s[p] == U' ') ++p;
        if (at(p) == U'/') {
            tag.type = L("end");
            ++p;
            while (p < n && s[p] == U' ') ++p;
        }
        size_t b = p;
        const U nameStops = L(">/ \"'\r\n");
        while (p < n && nameStops.find(s[p]) == U::npos) ++p;
        tag.name = ReplaceFunctions::Lower(s.substr(b, p - b));
        if (StartsWith(tag.name, L("opf:"))) tag.name = tag.name.substr(4);
        if (StartsWith(tag.name, L("!--"))) {
            tag.name = L("!--");
            tag.type = L("single");
            tag.attrs.Set(L("comment"), Strip(Slice(s, 4, -3)));
        }
        if (tag.name == L("?xml")) tag.name = L("xml");
        if (tag.type.empty()) {
            while (s.find(U'=', p) != U::npos) {
                while (p < n && s[p] == U' ') ++p;
                b = p;
                while (p < n && s[p] != U'=') ++p;
                const U name = RStrip(ReplaceFunctions::Lower(s.substr(b, p - b)), L(" "));
                ++p;
                while (p < n && s[p] == U' ') ++p;
                U value;
                if (at(p) == U'"' || at(p) == U'\'') {
                    const char32_t quote = s[p];
                    ++p;
                    b = p;
                    while (p < n && s[p] != quote) ++p;
                    value = s.substr(b, p - b);
                    ++p;
                } else {
                    b = p;
                    const U valueStops = L(">/ ");
                    while (p < n && valueStops.find(s[p]) == U::npos) ++p;
                    value = s.substr(std::min(b, n), p > b ? p - b : 0);
                }
                tag.attrs.Set(name, value);
            }
            tag.type = L("begin");
            if (s.find(U'/', p) != U::npos) tag.type = L("single");
        }
        return tag;
    }

    static U Join(const std::vector<U> &prefix)
    {
        U result;
        for (size_t index = 0; index < prefix.size(); ++index) {
            if (index) result += U'.';
            result += prefix[index];
        }
        return result;
    }

    void Parse()
    {
        std::optional<U> content;
        std::optional<Attrs> lastAttrs;
        std::vector<U> prefix;
        int count = 0;
        while (true) {
            std::optional<U> text;
            std::optional<U> raw;
            if (!Next(&text, &raw)) break;
            if (text) {
                content = RStrip(*text, L(" \r\n"));
                continue;
            }
            Tag tag = ParseTag(*raw);
            if (tag.type == L("begin")) {
                content.reset();
                prefix.push_back(tag.name);
                if (In(tag.name, ParentTags())) Handle(Join(prefix), tag.name, tag.attrs, content, &count);
                else lastAttrs = tag.attrs;
                continue;
            }
            if (tag.type == L("end")) {
                if (prefix.empty()) throw LegacyError();
                prefix.pop_back();
                tag.attrs = lastAttrs.value_or(Attrs());
                lastAttrs.reset();
            } else if (tag.type == L("single")) {
                content.reset();
            }
            if (tag.type == L("single") || (tag.type == L("end") && !In(tag.name, ParentTags())))
                Handle(Join(prefix), tag.name, tag.attrs, content, &count);
            content.reset();
        }
    }

    void Handle(const U &prefix, const U &name, Attrs attrs, const std::optional<U> &content, int *count)
    {
        const auto keepId = [this](const Attrs &values) {
            if (const auto id = values.Get(L("id"))) ids.push_back(*id);
        };
        if (name == L("package")) {
            const U version = attrs.Pop(L("version"), L("2.0"));
            const U uid = attrs.Pop(L("unique-identifier"), L("bookid"));
            package = std::make_pair(version, uid);
            keepId(attrs);
            return;
        }
        if (name == L("metadata")) {
            metadataAttrs = attrs;
            keepId(attrs);
            return;
        }
        if (name == L("meta") || name == L("link") || (StartsWith(name, L("dc:")) && Contains(prefix, L("metadata")))) {
            metadata.push_back({name, content, attrs});
            keepId(attrs);
            return;
        }
        if (name == L("item") && EndsWith(prefix, L("manifest"))) {
            std::string fallback = std::to_string(*count);
            if (fallback.size() < 3) fallback.insert(0, 3 - fallback.size(), '0');
            ++*count;
            ids.push_back(attrs.Pop(L("id"), L("xid") + L(fallback.c_str())));
            return;
        }
        if (name == L("spine") || (name == L("itemref") && EndsWith(prefix, L("spine"))) ||
            (name == L("reference") && EndsWith(prefix, L("guide"))) ||
            (name == L("mediatypes") && EndsWith(prefix, L("bindings")))) {
            // The popped idref, type, title, href, media-type and handler are never ids.
            keepId(attrs);
        }
    }

    U m_opf;
    size_t m_position = 0;
};

const std::vector<U> &RecognizedDc()
{
    static const std::vector<U> names {L("dc:identifier"), L("dc:title"), L("dc:creator"), L("dc:contributor"),
                                       L("dc:source"), L("dc:date"), L("dc:language"), L("dc:coverage"),
                                       L("dc:description"), L("dc:format"), L("dc:publisher"), L("dc:relation"),
                                       L("dc:rights"), L("dc:subject"), L("dc:type")};
    return names;
}

const std::vector<U> &RecognizedMeta()
{
    static const std::vector<U> names {L("belongs-to-collection"), L("dcterms:issued"), L("dcterms:created"),
                                       L("dcterms:modified"), L("schema:accessibilitySummary"), L("schema:accessMode"),
                                       L("schema:accessModeSufficient"), L("schema:accessibilityFeature"),
                                       L("schema:accessibilityHazard")};
    return names;
}

U RecordRoot(const U &name)
{
    static const std::vector<std::pair<U, U>> roots {
        {L("dc:identifier"), L("uid")}, {L("dc:title"), L("tle")}, {L("dc:creator"), L("cre")},
        {L("dc:contributor"), L("con")}, {L("dc:source"), L("src")}, {L("dc:date"), L("dat")},
        {L("dc:language"), L("lng")}, {L("dc:coverage"), L("cov")}, {L("dc:description"), L("des")},
        {L("dc:format"), L("fmt")}, {L("dc:publisher"), L("pub")}, {L("dc:relation"), L("rln")},
        {L("dc:rights"), L("rgt")}, {L("dc:subject"), L("sub")}, {L("dc:type"), L("typ")},
    };
    for (const auto &[key, root] : roots)
        if (key == name) return root;
    return L("num");
}

// The dc:language clean-up shared by both processors.
U CleanLanguage(const std::optional<U> &content)
{
    const U value = content.value_or(U());
    const size_t dash = value.find(U'-');
    if (dash == U::npos) return ReplaceFunctions::Lower(value);
    return ReplaceFunctions::Lower(value.substr(0, dash)) + L("-") + ReplaceFunctions::Upper(value.substr(dash + 1));
}

struct Extracted {
    std::vector<Entry> records;
    std::vector<Entry> other;
    std::vector<U> ids;
    std::optional<Attrs> metadataAttrs;
};

void AddRecord(Extracted &result, std::vector<std::pair<U, size_t>> &idToRecord, Entry entry)
{
    if (const auto id = entry.attrs.Get(L("id"))) {
        idToRecord.erase(std::remove_if(idToRecord.begin(), idToRecord.end(),
                                        [&](const auto &item) { return item.first == *id; }), idToRecord.end());
        idToRecord.emplace_back(*id, result.records.size());
        RemoveFirst(result.ids, *id);
    }
    result.records.push_back(std::move(entry));
}

Extracted ExtractEpub2(const U &opf)
{
    OpfMetadataParser parser(opf);
    Extracted result;
    result.ids = parser.ids;
    if (!parser.metadataAttrs) throw LegacyError();  // None.copy()
    result.metadataAttrs = parser.metadataAttrs;
    // The legacy code tests misspelled keys, so these are always (re)set.
    if (!result.metadataAttrs->Has(L("xmlsns:opf"))) result.metadataAttrs->Set(L("xmlns:opf"), L("http://www.idpf.org/2007/opf"));
    if (!result.metadataAttrs->Has(L("xmlsns:dc"))) result.metadataAttrs->Set(L("xmlns:dc"), L("http://purl.org/dc/elements/1.1/"));
    if (!parser.package) throw LegacyError();
    const U uid = parser.package->second;
    std::vector<std::pair<U, size_t>> idToRecord;
    for (Entry entry : parser.metadata) {
        if (entry.name == L("dc:identifier") && entry.attrs.Get(L("id"), U()) == uid) {
            result.other.push_back(entry);
            continue;
        }
        if (In(entry.name, RecognizedDc())) {
            if (entry.attrs.Has(L("xmlns:dc"))) entry.attrs.Delete(L("xmlns:dc"));
            if (entry.name == L("dc:language")) entry.content = CleanLanguage(entry.content);
            AddRecord(result, idToRecord, entry);
        } else if (entry.name == L("meta") && entry.attrs.Has(L("name")) && entry.attrs.At(L("name")) != L("cover")) {
            Entry record;
            record.name = entry.attrs.At(L("name"));
            entry.attrs.Delete(L("name"));
            record.content = entry.attrs.Get(L("content"), U());
            entry.attrs.Delete(L("content"));
            record.attrs = entry.attrs;
            AddRecord(result, idToRecord, record);
        } else {
            result.other.push_back(entry);
        }
    }
    return result;
}

Extracted ExtractEpub3(const U &opf)
{
    OpfMetadataParser parser(opf);
    Extracted result;
    result.ids = parser.ids;
    result.metadataAttrs = parser.metadataAttrs;
    if (!parser.package) throw LegacyError();
    const U uid = parser.package->second;
    std::vector<std::pair<U, size_t>> idToRecord;
    std::vector<Entry> refines;
    for (Entry entry : parser.metadata) {
        if (entry.name == L("dc:identifier") && entry.attrs.Get(L("id"), U()) == uid) {
            result.other.push_back(entry);
            continue;
        }
        if (In(entry.name, RecognizedDc())) {
            if (entry.attrs.Has(L("xmlns:dc"))) entry.attrs.Delete(L("xmlns:dc"));
            if (entry.name == L("dc:language")) entry.content = CleanLanguage(entry.content);
            AddRecord(result, idToRecord, entry);
        } else if (entry.name == L("meta") && entry.attrs.Has(L("refines"))) {
            refines.push_back(entry);
        } else if (entry.name == L("meta") && entry.attrs.Has(L("property"))) {
            const U property = entry.attrs.At(L("property"));
            if (In(property, RecognizedMeta())) {
                entry.attrs.Delete(L("property"));
                entry.name = property;
            }
            AddRecord(result, idToRecord, entry);
        } else {
            result.other.push_back(entry);
        }
    }
    for (const Entry &entry : refines) {
        const auto refinedId = entry.attrs.Get(L("id"));
        U target = entry.attrs.Get(L("refines"), U());
        const U property = entry.attrs.Get(L("property"), U());
        const auto scheme = entry.attrs.Get(L("scheme"));
        if (target.empty() || property.empty()) {
            result.other.push_back(entry);
            continue;
        }
        if (!StartsWith(target, L("#"))) {
            result.other.push_back(entry);
            continue;
        }
        target = target.substr(1);
        const auto found = std::find_if(idToRecord.begin(), idToRecord.end(), [&](const auto &item) { return item.first == target; });
        if (found == idToRecord.end()) {
            result.other.push_back(entry);
            continue;
        }
        Attrs &attrs = result.records[found->second].attrs;
        attrs.Set(property, entry.content.value_or(U()));
        if (scheme) attrs.Set(L("scheme"), *scheme);
        if (property == L("alternate-script") && entry.attrs.Has(L("xml:lang")))
            attrs.Set(L("altlang"), entry.attrs.At(L("xml:lang")));
        if (refinedId) RemoveFirst(result.ids, *refinedId);
    }
    return result;
}

const char32_t RS = 30;
const char32_t US = 31;

std::vector<U> Split(const U &text, char32_t separator)
{
    std::vector<U> parts;
    size_t start = 0;
    while (true) {
        const size_t found = text.find(separator, start);
        if (found == U::npos) {
            parts.push_back(text.substr(start));
            return parts;
        }
        parts.push_back(text.substr(start, found - start));
        start = found + 1;
    }
}

std::pair<U, U> Pair(const U &line)
{
    const std::vector<U> parts = Split(line, US);
    if (parts.size() != 2) throw LegacyError();
    return {Strip(parts[0]), Strip(parts[1])};
}

// The first <metadata ...> through the last </metadata> and trailing space,
// as the legacy case-insensitive DOTALL regex with a greedy body matches.
bool FindMetadataSection(const U &text, size_t *start, size_t *end)
{
    const auto spaceEnd = [&](size_t position) {
        while (position < text.size() && ReplaceFunctions::IsPythonSpace(text[position])) ++position;
        return position;
    };
    const auto word = [&](size_t position) {
        static const U metadata = L("metadata");
        if (position + metadata.size() > text.size()) return false;
        for (size_t index = 0; index < metadata.size(); ++index) {
            char32_t c = text[position + index];
            if (c >= U'A' && c <= U'Z') c += 32;
            if (c != metadata[index]) return false;
        }
        return true;
    };
    size_t openEnd = U::npos;
    for (size_t position = text.find(U'<'); position != U::npos; position = text.find(U'<', position + 1)) {
        const size_t name = spaceEnd(position + 1);
        if (!word(name)) continue;
        const size_t close = text.find(U'>', name + 8);
        if (close == U::npos) return false;
        *start = position;
        openEnd = close + 1;
        break;
    }
    if (openEnd == U::npos) return false;
    for (size_t position = text.rfind(U'<'); position != U::npos && position >= openEnd; position = position ? text.rfind(U'<', position - 1) : U::npos) {
        size_t cursor = spaceEnd(position + 1);
        if (cursor >= text.size() || text[cursor] != U'/') continue;
        cursor = spaceEnd(cursor + 1);
        if (!word(cursor)) continue;
        cursor = spaceEnd(cursor + 8);
        if (cursor >= text.size() || text[cursor] != U'>') continue;
        *end = spaceEnd(cursor + 1);
        return true;
    }
    return false;
}

U ApplyEpub2(const U &data, const U &other, std::vector<U> ids, const U &metatag, const U &opf)
{
    std::vector<Entry> records;
    std::vector<U> lines = Split(data, RS);
    if (lines.back().empty()) lines.pop_back();
    const std::vector<U> known {L("id"), L("xml:lang"), L("dir"), L("opf:scheme"), L("opf:role"), L("opf:file-as"), L("xmlns")};
    for (size_t position = 0; position < lines.size();) {
        Entry entry;
        auto [name, value] = Pair(lines[position]);
        entry.name = name;
        entry.content = value;
        if (!In(entry.name, RecognizedDc())) {
            entry.name = L("meta");
            entry.attrs.Set(L("name"), name);
            entry.attrs.Set(L("content"), value);
            entry.content.reset();
        }
        ++position;
        while (position < lines.size() && StartsWith(lines[position], L("  "))) {
            auto [child, childValue] = Pair(lines[position]);
            if ((In(child, known) || StartsWith(child, L("xmlns:"))) && child == L("id"))
                entry.attrs.Set(L("id"), ValidId(childValue, ids));
            else
                entry.attrs.Set(child, childValue);
            ++position;
        }
        records.push_back(entry);
    }
    U section = metatag;
    for (const Entry &entry : records) section += L("  ") + BuildXml(entry);
    section += other + L("</metadata>\n");
    size_t start = 0;
    size_t end = 0;
    if (!FindMetadataSection(opf, &start, &end)) return opf;
    return opf.substr(0, start) + section + opf.substr(end);
}

U ApplyEpub3(const U &data, const U &other, std::vector<U> ids, const U &metatag, const U &opf)
{
    std::vector<Entry> records;
    std::vector<U> lines = Split(data, RS);
    if (lines.back().empty()) lines.pop_back();
    for (size_t position = 0; position < lines.size();) {
        Entry entry;
        Attrs refines;
        std::optional<U> id;
        auto [name, value] = Pair(lines[position]);
        entry.name = name;
        entry.content = value;
        if (In(entry.name, RecognizedMeta())) {
            entry.name = L("meta");
            entry.attrs.Set(L("property"), name);
        }
        ++position;
        std::vector<U> known {L("id"), L("xml:lang"), L("dir"), L("xmlns")};
        if (entry.name == L("meta")) known.push_back(L("property"));
        while (position < lines.size() && StartsWith(lines[position], L("  "))) {
            auto [child, childValue] = Pair(lines[position]);
            if (In(child, known) || StartsWith(child, L("xmlns:"))) {
                if (child == L("id")) {
                    id = ValidId(childValue, ids);
                    entry.attrs.Set(L("id"), *id);
                    ids.push_back(*id);
                } else {
                    entry.attrs.Set(child, childValue);
                }
            } else {
                refines.Set(child, childValue);
            }
            ++position;
        }
        if (!refines.Empty() && !entry.attrs.Has(L("id"))) {
            id = ValidId(RecordRoot(entry.name), ids);
            entry.attrs.Set(L("id"), *id);
            ids.push_back(*id);
        }
        records.push_back(entry);
        for (const auto &[property, content] : refines.Items()) {
            if (property == L("scheme") || property == L("altlang")) continue;
            if (!id) throw LegacyError();  // "#" + None
            Entry refinement;
            refinement.name = L("meta");
            refinement.content = content;
            refinement.attrs.Set(L("refines"), L("#") + *id);
            refinement.attrs.Set(L("property"), property);
            if (property == L("alternate-script") && refines.Has(L("altlang")))
                refinement.attrs.Set(L("xml:lang"), refines.At(L("altlang")));
            if ((property == L("role") || property == L("identifier-type") || property == L("title-type") ||
                 property == L("collection-type")) && refines.Has(L("scheme")))
                refinement.attrs.Set(L("scheme"), refines.At(L("scheme")));
            records.push_back(refinement);
        }
    }
    U section = metatag;
    for (const Entry &entry : records) section += L("  ") + BuildXml(entry);
    section += other + L("</metadata>\n");
    size_t start = 0;
    size_t end = 0;
    if (!FindMetadataSection(opf, &start, &end)) return opf;
    return opf.substr(0, start) + section + opf.substr(end);
}

}

bool MetadataProcessor::Extract(const QString &opfdata, const QString &version, Pieces *out)
{
    *out = Pieces();
    Extracted extracted;
    try {
        const U opf = ToU(opfdata);
        extracted = version.startsWith('3') ? ExtractEpub3(opf) : ExtractEpub2(opf);
    } catch (const LegacyError &) {
        return false;
    }
    U data;
    for (const Entry &record : extracted.records) {
        data += record.name + US + XmlDecode(record.content.value_or(U())) + RS;
        std::vector<std::pair<U, U>> attrs = record.attrs.Items();
        std::sort(attrs.begin(), attrs.end(), [](const auto &left, const auto &right) { return left.first < right.first; });
        for (const auto &[key, value] : attrs) data += L("  ") + key + US + XmlDecode(value) + RS;
    }
    U other;
    for (const Entry &entry : extracted.other) other += L("  ") + BuildXml(entry);
    U tag = L("<metadata");
    if (extracted.metadataAttrs)
        for (const auto &[key, value] : extracted.metadataAttrs->Items()) tag += L(" ") + key + L("=\"") + value + L("\"");
    tag += L(">\n");
    out->data = FromU(data);
    out->otherxml = FromU(other);
    for (const U &id : extracted.ids) out->idlist.append(FromU(id));
    out->metatag = FromU(tag);
    return true;
}

bool MetadataProcessor::Apply(const Pieces &pieces, const QString &opfdata, const QString &version, QString *out)
{
    std::vector<U> ids;
    for (const QString &id : pieces.idlist) ids.push_back(ToU(id));
    try {
        const U data = ToU(pieces.data);
        const U other = ToU(pieces.otherxml);
        const U metatag = ToU(pieces.metatag);
        const U opf = ToU(opfdata);
        *out = FromU(version.startsWith('3') ? ApplyEpub3(data, other, ids, metatag, opf)
                                             : ApplyEpub2(data, other, ids, metatag, opf));
    } catch (const LegacyError &) {
        return false;
    }
    return true;
}
