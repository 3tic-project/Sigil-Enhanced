"""Read-only structured plugin plans using Sigil's shared OPF source patcher.

The temporary XML model is never returned for persistence. Unknown subtrees
outside the flat package API stay opaque; only the requested model delta is
applied to the original source. Revisions and transaction commit live in C++.
"""

import json
from copy import deepcopy

from lxml import etree

from opf_source_legacy import OPF, XML, apply_model_update, model_xml


def _section(root, name):
    nodes = root.findall('{' + OPF + '}' + name)
    if len(nodes) != 1:
        raise ValueError('Package update requires exactly one ' + name)
    return nodes[0]


def _strings(value):
    if not isinstance(value, dict) or any(not isinstance(v, str) for v in value.values()):
        raise ValueError('Package attributes must be an object of strings')
    return value


def _required(value, key):
    if not isinstance(value, dict) or not isinstance(value.get(key), str) or not value[key]:
        raise ValueError('Package entries require a non-empty ' + key + ' string')
    return value[key]


def _namespaces(parent, attributes):
    namespaces = dict(parent.nsmap)
    namespaces['xml'] = XML
    for name, uri in attributes.items():
        if name == 'xmlns' or name.startswith('xmlns:'):
            prefix = None if name == 'xmlns' else name[6:]
            if not uri or prefix == 'xmlns' or (prefix == 'xml') != (uri == XML):
                raise ValueError('Invalid namespace declaration: ' + name)
            namespaces[prefix] = uri
    return namespaces


def _name(name, namespaces, attribute=False):
    if not isinstance(name, str) or not name:
        raise ValueError('XML qualified name must be a non-empty string')
    if ':' in name:
        if name.count(':') != 1:
            raise ValueError('Invalid XML qualified name: ' + name)
        prefix, local = name.split(':')
        if not prefix or prefix not in namespaces or not local:
            raise ValueError('Undeclared or invalid XML prefix: ' + name)
        return etree.QName(namespaces[prefix], local).text
    return etree.QName(name if attribute else '{' + namespaces.get(None, OPF) + '}' + name).text


def _attributes(element, values, namespaces):
    for name, value in values.items():
        if name == 'xmlns' or name.startswith('xmlns:'):
            continue
        element.set(_name(name, namespaces, attribute=True), value)


def _metadata(root, payload):
    metadata = _section(root, 'metadata')
    entries = payload.get('items')
    if not isinstance(entries, list):
        raise ValueError('Metadata items must be an array')
    for child in list(metadata):
        metadata.remove(child)
    for entry in entries:
        name = _required(entry, 'name')
        if not isinstance(entry.get('content'), str):
            raise ValueError('Metadata entries require a content string')
        attributes = _strings(entry.get('attributes', {}))
        # Declarations belong to this entry, not to siblings or other sections.
        namespaces = _namespaces(metadata, attributes)
        node = etree.SubElement(metadata, _name(name, namespaces), nsmap=namespaces)
        _attributes(node, attributes, namespaces)
        node.text = entry['content']


def _optional(node, name, value):
    if not isinstance(value, str):
        raise ValueError('Package attributes must be strings')
    if value:
        node.set(name, value)
    else:
        # The API represents an absent optional attribute as an empty string.
        node.attrib.pop(name, None)


def _manifest(root, payload):
    manifest = _section(root, 'manifest')
    by_id, by_href = {}, {}
    for node in manifest:
        identifier, href = node.get('id'), node.get('href')
        if not identifier or not href or identifier in by_id or href in by_href:
            raise ValueError('Manifest has missing or ambiguous IDs/hrefs')
        by_id[identifier], by_href[href] = node, identifier
    removals, relocations, additions = (payload.get(key, []) for key in ('removals', 'relocations', 'additions'))
    if any(not isinstance(value, list) for value in (removals, relocations, additions)):
        raise ValueError('Manifest changes must be arrays')
    for href in removals:
        if not isinstance(href, str):
            raise ValueError('Manifest removal hrefs must be strings')
        identifier = by_href.pop(href, None)
        if identifier is not None:
            manifest.remove(by_id.pop(identifier))
    for relocation in relocations:
        original, target = (_required(relocation, key) for key in ('original_href', 'target_href'))
        identifier = by_href.get(original)
        if original == target or identifier is None:
            continue  # Idempotent replay of an already represented relocation.
        if target in by_href and by_href[target] != identifier:
            raise ValueError('A manifest relocation target is occupied')
        del by_href[original]
        by_href[target] = identifier
        by_id[identifier].set('href', target)
    for addition in additions:
        for key in ('id', 'href', 'media-type'):
            _required(addition, key)
    for addition in sorted(additions, key=lambda entry: entry['href']):
        identifier, href = addition['id'], addition['href']
        node = by_id.get(identifier)
        if node is not None:
            if node.get('href') != href:
                raise ValueError('A manifest ID already uses another href')
        else:
            if href in by_href:
                raise ValueError('A manifest href already uses another ID')
            node = etree.SubElement(manifest, '{' + OPF + '}item', id=identifier, href=href)
            by_id[identifier], by_href[href] = node, identifier
        node.set('media-type', addition['media-type'])
        for key in ('properties', 'fallback', 'media-overlay'):
            _optional(node, key, addition.get(key, ''))


def _spine(root, payload):
    spine = _section(root, 'spine')
    items = payload.get('items')
    if not isinstance(items, list):
        raise ValueError('Spine items must be an array')
    attributes = _strings(payload.get('attributes', {}))
    _attributes(spine, attributes, _namespaces(spine, attributes))
    existing = {}
    for node in spine:
        existing.setdefault(node.get('idref'), []).append(node)
    for node in list(spine):
        spine.remove(node)
    for item in items:
        identifier = _required(item, 'idref')
        matches = existing.get(identifier, [])
        # Retain unexposed extension attributes when replacing a known itemref.
        node = deepcopy(matches.pop(0)) if matches else etree.Element('{' + OPF + '}itemref', idref=identifier)
        for name in ('id', 'linear', 'properties'):
            _optional(node, name, item.get(name, ''))
        spine.append(node)


def apply_update(source, operation, payload_json):
    payload = json.loads(payload_json)
    if not isinstance(payload, dict):
        raise ValueError('Package update payload must be an object')
    operations = {'metadata': _metadata, 'manifest': _manifest, 'spine': _spine}
    if operation not in operations:
        raise ValueError('Unknown package update operation')
    try:
        before = model_xml(source)
    except etree.XMLSyntaxError as error:
        raise ValueError('Package XML is not well formed: ' + str(error)) from error
    root = etree.fromstring(before.encode('utf-8'))
    operations[operation](root, payload)
    return apply_model_update(source, before, etree.tostring(root, encoding='unicode'))
