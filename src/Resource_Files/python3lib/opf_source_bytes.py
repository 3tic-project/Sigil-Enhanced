"""Lossless OPF bytes and the projection used by Qt's plain-text editor."""
import codecs
import difflib
import re


_DECLARATION = re.compile(r'''\A<\?xml\s+[^?]*?\bencoding\s*=\s*(["'])([^"']+)\1''')
_LINES = re.compile(r'[^\r\n\u2028\u2029]*(?:\r\n|[\r\n\u2028\u2029])|[^\r\n\u2028\u2029]+$')
_END = re.compile(r'(\r\n|[\r\n\u2028\u2029])$')


def editor_text(source):
    return source.replace('\r\n', '\n').replace('\r', '\n').replace('\u2028', '\n').replace('\u2029', '\n')


def _encoding(data):
    # Test UTF-32 before UTF-16 because their little-endian BOMs share a prefix.
    for bom, encoding in ((codecs.BOM_UTF32_LE, 'utf-32-le'), (codecs.BOM_UTF32_BE, 'utf-32-be'),
                          (codecs.BOM_UTF8, 'utf-8'), (codecs.BOM_UTF16_LE, 'utf-16-le'),
                          (codecs.BOM_UTF16_BE, 'utf-16-be')):
        if data.startswith(bom):
            return encoding, bom
    for signature, encoding in ((b'\x00\x00\x00<', 'utf-32-be'), (b'<\x00\x00\x00', 'utf-32-le'),
                                (b'\x00<\x00?', 'utf-16-be'), (b'<\x00?\x00', 'utf-16-le')):
        if data.startswith(signature):
            return encoding, b''
    declaration = _DECLARATION.match(data[:1024].decode('ascii', errors='replace'))
    encoding = declaration[2] if declaration else 'utf-8'
    return codecs.lookup(encoding).name, b''


def decode_source(data):
    encoding, bom = _encoding(data)
    source = data[len(bom):].decode(encoding, errors='strict')
    declaration = _DECLARATION.match(source)
    if declaration and bom:
        declared = codecs.lookup(declaration[2]).name
        compatible = {encoding, encoding.removesuffix('-le').removesuffix('-be')}
        if declared not in compatible:
            raise ValueError('OPF byte order mark conflicts with its encoding declaration')
    return source


def encode_source(original, source):
    """Retain original encoding/BOM unless the user changes the declaration.

    Unrepresentable text is an error; changing the XML encoding to UTF-8 is an
    explicit conversion. No replacement characters are silently introduced.
    """
    encoding, bom = _encoding(original)
    old_declaration = _DECLARATION.match(decode_source(original))
    new_declaration = _DECLARATION.match(source)
    old_name = codecs.lookup(old_declaration[2]).name if old_declaration else None
    new_name = codecs.lookup(new_declaration[2]).name if new_declaration else None
    if new_name is not None and new_name != old_name:
        encoding, bom = new_name, b''
    elif old_name is not None and new_name is None and not bom:
        # Without a declaration or BOM, XML readers must use UTF-8. Retaining
        # a legacy encoding here would make a deliberate declaration edit
        # produce an unreadable document.
        encoding = 'utf-8'
    try:
        return bom + source.encode(encoding, errors='strict')
    except UnicodeEncodeError as error:
        raise ValueError('OPF text cannot be represented in ' + encoding +
                         '. Change the XML encoding declaration to UTF-8 before saving.') from error


def restore_source_text(original, edited):
    """Map editor lines back to their original line terminators, without NFC.

    Equal lines are copied literally. Replaced lines retain their terminators
    when their count is unchanged; inserted lines use the nearest source style.
    Positions stay in Python code points here, separate from Qt UTF-16 offsets.
    """
    if editor_text(original) == edited:
        return original
    old = _LINES.findall(original)
    old_editor = [editor_text(line) for line in old]
    new = re.findall(r'[^\n]*\n|[^\n]+$', edited)
    # Anchor the unchanged edges before using difflib's repetitive-line
    # heuristic. Otherwise an insertion into a long run of equal lines may
    # classify the entire unchanged suffix as replaced and lose its mixed
    # terminators. Keep autojunk for bounded work on large repeated inputs.
    prefix = 0
    while prefix < min(len(old), len(new)) and old_editor[prefix] == new[prefix]:
        prefix += 1
    old_end, new_end = len(old), len(new)
    while old_end > prefix and new_end > prefix and old_editor[old_end - 1] == new[new_end - 1]:
        old_end -= 1
        new_end -= 1
    result = old[:prefix]
    endings = [match[1] if (match := _END.search(line)) else None for line in old]
    next_style = [None] * (len(old) + 1)
    previous_style = [None] * (len(old) + 1)
    for index in range(len(old) - 1, -1, -1):
        next_style[index] = endings[index] or next_style[index + 1]
    for index in range(len(old)):
        previous_style[index + 1] = endings[index] or previous_style[index]
    matcher = difflib.SequenceMatcher(None, old_editor[prefix:old_end], new[prefix:new_end], autojunk=True)
    for operation, left, right, start, end in matcher.get_opcodes():
        left, right, start, end = (value + prefix for value in (left, right, start, end))
        if operation == 'equal':
            result.extend(old[left:right])
            continue
        nearby = next_style[left] or previous_style[left] or '\n'
        for index, line in enumerate(new[start:end]):
            ending = nearby
            if right - left == end - start:
                match = _END.search(old[left + index])
                if match:
                    ending = match[1]
            result.append(line[:-1] + ending if line.endswith('\n') else line)
    result.extend(old[old_end:])
    return ''.join(result)
