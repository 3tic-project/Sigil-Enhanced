"""Apply package-model changes to XML source without serializing unrelated XML.

All ranges are UTF-8 byte offsets from Expat. The public interface uses Unicode,
like Sigil's embedded Python bridge. File encoding preservation belongs to the
resource layer, not this module. A failed edit raises ValueError before returning
any output; callers must not fall back to silently rebuilding the package.
"""

from dataclasses import dataclass, field
import re
from xml.parsers import expat
from xml.sax.saxutils import escape


OPF = "http://www.idpf.org/2007/opf"
XML = "http://www.w3.org/XML/1998/namespace"
XMLNS = "http://www.w3.org/2000/xmlns/"
_ATTR = re.compile(rb'''\s+([^\s=/>]+)\s*=\s*(["'])(.*?)\2''', re.S)


def model_xml(source):
    """Project supported package fields for the legacy C++ parser, never save it.

    Its flat metadata representation cannot model nested extension subtrees.
    Leave these outside both delta models so the source patcher retains them as
    opaque XML. Namespace and parent checks also prevent lookalike extension
    elements from being interpreted as package structure.
    """
    from lxml import etree
    dc = 'http://purl.org/dc/elements/1.1/'
    parser = etree.XMLParser(encoding='utf-8', recover=False, no_network=True,
                             resolve_entities='internal', remove_comments=True,
                             remove_pis=True)
    root = etree.fromstring(source.encode('utf-8'), parser)
    if root.tag != '{' + OPF + '}package':
        raise ValueError('Expected an OPF package element in the OPF namespace')

    def namespaces(node):
        # Keep aliases used by CURIE-valued attributes, while providing the
        # canonical names the legacy parser recognizes for OPF and DC nodes.
        result = {None: OPF, 'dc': dc, 'opf': OPF}
        result.update({prefix: uri for prefix, uri in node.nsmap.items() if prefix not in result})
        return result

    projected = etree.Element(root.tag, attrib=root.attrib, nsmap=namespaces(root))
    sections = {'metadata': None, 'manifest': 'item', 'spine': 'itemref',
                'guide': 'reference', 'bindings': 'mediaType'}
    for section in root:
        if not isinstance(section.tag, str) or not section.tag.startswith('{' + OPF + '}'):
            continue
        name = etree.QName(section).localname
        if name not in sections:
            continue
        target = etree.SubElement(projected, section.tag, attrib=section.attrib, nsmap=namespaces(section))
        target.text = '\n'  # The legacy parser expects explicit section end tags.
        for node in section:
            if not isinstance(node.tag, str) or not node.tag.startswith('{') or len(node):
                continue
            if name != 'metadata' and node.tag != '{' + OPF + '}' + sections[name]:
                continue
            leaf = etree.SubElement(target, node.tag, attrib=node.attrib, nsmap=namespaces(node))
            leaf.text = node.text
    return etree.tostring(projected, encoding='unicode', pretty_print=True)


def add_navigation_manifest(source, href, identifier):
    """Preview a new navigation item without touching reading order or files."""
    from lxml import etree
    before = model_xml(source)
    root = etree.fromstring(before.encode('utf-8'))
    manifests = root.findall('{' + OPF + '}manifest')
    if len(manifests) != 1:
        raise ValueError('Navigation repair requires exactly one manifest')
    manifest = manifests[0]
    for node in root.iter():
        if node.get('id') == identifier:
            raise ValueError('Navigation manifest identifier already exists')
    for node in manifest:
        if 'nav' in node.get('properties', '').split() or node.get('href') == href:
            raise ValueError('Navigation manifest path or property already exists')
    etree.SubElement(manifest, '{' + OPF + '}item', id=identifier, href=href,
                     attrib={'media-type': 'application/xhtml+xml', 'properties': 'nav'})
    return apply_model_update(source, before, etree.tostring(root, encoding='unicode'))


@dataclass
class Node:
    name: str
    attrs: dict
    start: int
    open_end: int
    close_start: int = 0
    end: int = 0
    qualified: str = ""
    namespaces: dict = field(default_factory=dict)
    spans: dict = field(default_factory=dict)
    children: list = field(default_factory=list)
    text: str = ""
    empty: bool = False


def _expanded(name, namespaces, attribute=False):
    if name == "xmlns":
        return XMLNS + "|"
    if name.startswith("xmlns:"):
        return XMLNS + "|" + name[6:]
    if ":" in name:
        prefix, local = name.split(":", 1)
        return namespaces[prefix] + "|" + local
    uri = "" if attribute else namespaces.get("", "")
    return uri + "|" + name if uri else name


class Document:
    def __init__(self, source):
        self.data = source.encode("utf-8")
        self.root = None
        stack = []
        declarations = {}
        parser = expat.ParserCreate(encoding="UTF-8", namespace_separator="|")

        def namespace(prefix, uri):
            declarations[prefix or ""] = uri or ""

        def start(name, attrs):
            offset = parser.CurrentByteIndex
            end = offset
            quote = None
            while end < len(self.data):
                char = self.data[end]
                if quote:
                    if char == quote:
                        quote = None
                elif char in (34, 39):
                    quote = char
                elif char == 62:
                    break
                end += 1
            if end == len(self.data):
                raise ValueError("Unterminated XML start tag")
            end += 1
            raw = self.data[offset:end]
            qualified = re.match(rb"<([^\s/>]+)", raw).group(1).decode("utf-8")
            namespaces = dict(stack[-1].namespaces) if stack else {"xml": XML}
            namespaces.update(declarations)
            declarations.clear()
            node = Node(name, attrs, offset, end, qualified=qualified,
                        namespaces=namespaces, empty=raw.endswith(b"/>"))
            for match in _ATTR.finditer(raw):
                attr_name = match[1].decode("utf-8")
                node.spans[_expanded(attr_name, namespaces, attribute=True)] = (
                    offset + match.start(), offset + match.end(),
                    offset + match.start(3), offset + match.end(3),
                    match[2].decode(), attr_name)
            if stack:
                stack[-1].children.append(node)
            else:
                self.root = node
            stack.append(node)

        def end(name):
            node = stack.pop()
            if node.empty:
                node.close_start = node.end = node.open_end
            else:
                node.close_start = parser.CurrentByteIndex
                node.end = self.data.index(b">", node.close_start) + 1

        def text(value):
            if stack:
                stack[-1].text += value

        def custom_entity(*args):
            # Expanded markup has no one-to-one source offset mapping.
            raise ValueError("Package source edits do not support custom DTD entities")

        parser.StartNamespaceDeclHandler = namespace
        parser.StartElementHandler = start
        parser.EndElementHandler = end
        parser.CharacterDataHandler = text
        parser.EntityDeclHandler = custom_entity
        parser.ExternalEntityRefHandler = lambda *args: 0
        try:
            parser.Parse(self.data, True)
        except (expat.ExpatError, KeyError) as error:
            raise ValueError("Cannot index package XML: " + str(error)) from error
        if self.root is None or self.root.name != OPF + "|package":
            raise ValueError("Expected an OPF package element in the OPF namespace")

    def raw(self, node):
        return self.data[node.start:node.end]


def _signature(node):
    # Formatting between child elements is not part of the package model.
    text = node.text if not node.children else node.text.strip()
    return node.name, sorted(node.attrs.items()), text, tuple(map(_signature, node.children))


def _identity(node):
    if node.attrs.get("id"):
        return node.name, "id", node.attrs["id"]
    for key in ("idref", "property", "name", "media-type", "type"):
        if key in node.attrs:
            return node.name, key, node.attrs[key], node.attrs.get("refines", "")
    return (node.name,)


def _match(old, new):
    """Map new positions to old positions, retaining identity through reorders."""
    matched = {}
    available = set(range(len(old)))
    # Prefer exact entries (including repeated creators) before identity matches.
    for key in (_signature, _identity):
        buckets = {}
        for index in sorted(available):
            value = key(old[index])
            # Signatures contain lists; repr is deterministic for sorted attrs.
            buckets.setdefault(repr(value), []).append(index)
        for index, node in enumerate(new):
            if index in matched:
                continue
            choices = buckets.get(repr(key(node)), [])
            if choices:
                candidate = choices.pop(0)
                matched[index] = candidate
                available.remove(candidate)
    # A unique renamed/identified entry still represents the same source node.
    for index, node in enumerate(new):
        if index in matched:
            continue
        candidates = [i for i in available if old[i].name == node.name]
        missing = [i for i, entry in enumerate(new)
                   if i not in matched and entry.name == node.name]
        if len(candidates) == len(missing) == 1:
            matched[index] = candidates[0]
            available.remove(candidates[0])
    return matched


def _escape_text(value):
    # XML normalizes literal CR to LF; a value containing CR needs an entity.
    return escape(value, {"\r": "&#13;"}).encode("utf-8")


def _escape_attribute(value, quote):
    return escape(value, {quote: "&quot;" if quote == '"' else "&apos;",
                          "\r": "&#13;", "\n": "&#10;", "\t": "&#9;"}).encode("utf-8")


def _apply(data, edits, start, end):
    cursor = start
    chunks = []
    for left, right, value in sorted(edits, key=lambda edit: (edit[0], edit[1])):
        if not start <= cursor <= left <= right <= end:
            raise ValueError("Overlapping or out-of-range package source edits")
        chunks.extend((data[cursor:left], value))
        cursor = right
    chunks.append(data[cursor:end])
    return b"".join(chunks)


def _uses_namespace(node, prefix, uri):
    """Whether a new subtree needs a binding inherited from its model parent."""
    curie = re.compile(r'(?<![\w.-])' + re.escape(prefix) + r':(?=[\w.-])') if prefix else None
    pending = [node]
    while pending:
        current = pending.pop()
        if current.namespaces.get(prefix, "") != uri:
            continue
        # A descendant with its own declaration already carries that binding.
        if current is not node and XMLNS + "|" + prefix in current.spans:
            continue
        if prefix:
            if current.qualified.startswith(prefix + ":"):
                return True
            if any(span[5].startswith(prefix + ":") for span in current.spans.values()):
                return True
            if any(curie.search(value) for value in current.attrs.values()):
                return True
        elif current.name == (uri + "|" if uri else "") + current.qualified:
            return True
        pending.extend(current.children)
    return False


def _new_xml(document, node, parent):
    """Declare only model bindings needed by a newly inserted subtree."""
    raw = document.raw(node)
    declarations = []
    for prefix, uri in node.namespaces.items():
        if prefix == "xml" or parent.namespaces.get(prefix, "") == uri:
            continue
        key = XMLNS + "|" + prefix
        if key not in node.spans and _uses_namespace(node, prefix, uri):
            name = "xmlns:" + prefix if prefix else "xmlns"
            declarations.append(b" " + name.encode() + b'="' + _escape_attribute(uri, '"') + b'"')
    if declarations:
        offset = node.open_end - node.start - (2 if node.empty else 1)
        raw = raw[:offset] + b"".join(declarations) + raw[offset:]
    return raw


def _merge(source, actual, before, after_doc, after):
    if _signature(before) == _signature(after):
        return source.raw(actual)
    if actual.name != before.name or before.name != after.name:
        raise ValueError("Package node identity changed unexpectedly")
    edits = []
    additions = []
    namespaces = dict(actual.namespaces)
    for name in sorted(set(before.attrs) | set(after.attrs)):
        if before.attrs.get(name) == after.attrs.get(name):
            continue
        span = actual.spans.get(name)
        if name not in after.attrs:
            if span:
                edits.append((span[0], span[1], b""))
        elif span:
            edits.append((span[2], span[3], _escape_attribute(after.attrs[name], span[4])))
        else:
            if "|" in name:
                uri, local = name.rsplit("|", 1)
                prefixes = [p for p, u in namespaces.items() if p and u == uri]
                if prefixes:
                    qualified = prefixes[0] + ":" + local
                else:
                    prefixes = [p for p, u in after.namespaces.items() if p and u == uri]
                    prefix = prefixes[0] if prefixes else "opfattr"
                    if prefix in namespaces:
                        raise ValueError("New attribute namespace conflicts with source prefix")
                    additions.append(b" xmlns:" + prefix.encode() + b'="' + _escape_attribute(uri, '"') + b'"')
                    namespaces[prefix] = uri
                    qualified = prefix + ":" + local
            else:
                qualified = name
            additions.append(b" " + qualified.encode() + b'="' + _escape_attribute(after.attrs[name], '"') + b'"')
    if additions:
        offset = actual.open_end - (2 if actual.empty else 1)
        edits.append((offset, offset, b"".join(additions)))

    if not before.children and not after.children:
        if before.text != after.text:
            if actual.children:
                raise ValueError("Cannot replace metadata text containing unmodeled elements")
            value = _escape_text(after.text)
            if actual.empty:
                edits.append((actual.open_end - 2, actual.open_end,
                              b">" + value + b"</" + actual.qualified.encode() + b">"))
            else:
                # Keep comments and processing instructions inside a changed value.
                interior = source.data[actual.open_end:actual.close_start]
                tokens = re.finditer(rb"<!\[CDATA\[.*?\]\]>|<!--.*?-->|<\?.*?\?>", interior, re.S)
                protected = [token for token in tokens if not token[0].startswith(b"<![CDATA[")]
                if protected:
                    cursor = actual.open_end
                    for token in protected:
                        left = actual.open_end + token.start()
                        edits.append((cursor, left, value))
                        value = b""
                        cursor = actual.open_end + token.end()
                    edits.append((cursor, actual.close_start, value))
                elif interior.startswith(b"<![CDATA[") and interior.endswith(b"]]>"):
                    value = after.text.replace("]]>", "]]]]><![CDATA[>")
                    value = value.replace("\r", "]]>&#13;<![CDATA[").encode("utf-8")
                    edits.append((actual.open_end + 9, actual.close_start - 3, value))
                else:
                    edits.append((actual.open_end, actual.close_start, value))
    else:
        if before.text.strip() != after.text.strip():
            raise ValueError("Cannot patch mixed package content")
        mapping = _match(before.children, after.children)
        actual_mapping = _match(actual.children, before.children)
        if len(actual_mapping) != len(before.children):
            raise ValueError("Cannot locate every modeled package child in original source")
        output = []
        for index, node in enumerate(after.children):
            if index in mapping:
                old_index = mapping[index]
                original = actual.children[actual_mapping[old_index]]
                output.append(_merge(source, original, before.children[old_index], after_doc, node))
            else:
                output.append(_new_xml(after_doc, node, actual))
        original_nodes = [actual.children[i] for i in sorted(actual_mapping.values())]
        for index, node in enumerate(original_nodes):
            value = output[index] if index < len(output) else b""
            if source.raw(node) != value:
                edits.append((node.start, node.end, value))
        extra = output[len(original_nodes):]
        if extra:
            newline = b"\r\n" if b"\r\n" in source.data else b"\n"
            indent = b"  "
            if original_nodes:
                node = original_nodes[-1]
                line = source.data.rfind(b"\n", 0, node.start) + 1
                prefix = source.data[line:node.start]
                indent = prefix if not prefix.strip() else b"  "
            content = b"".join(newline + indent + value for value in extra)
            if actual.empty:
                edits.append((actual.open_end - 2, actual.open_end,
                              b">" + content + newline + b"</" + actual.qualified.encode() + b">"))
            else:
                offset = original_nodes[-1].end if original_nodes else actual.close_start
                edits.append((offset, offset, content))
    return _apply(source.data, edits, actual.start, actual.end)


def apply_model_update(source, before_model, after_model):
    """Apply only the semantic delta between two models to the original source.

    before_model must be the model parsed from this source/revision. Unknown XML
    absent from both models stays untouched. The entire output is reparsed before
    returning it; there is no partial success or implicit reconstruction fallback.
    """
    before = Document(before_model)
    after = Document(after_model)
    if _signature(before.root) == _signature(after.root):
        return source
    original = Document(source)
    root = _merge(original, original.root, before.root, after, after.root)
    result = (original.data[:original.root.start] + root + original.data[original.root.end:]).decode("utf-8")
    Document(result)
    return result
