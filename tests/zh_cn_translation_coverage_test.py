import argparse
import collections
import html
import pathlib
import re
import subprocess
import tempfile
import xml.etree.ElementTree as ET


PLACEHOLDER_PATTERN = re.compile(r"%(?:L?\d+|n)")
HTML_TAG_PATTERN = re.compile(r"</?([A-Za-z][\w:-]*)\b")
UI_LITERAL_PATTERNS = (
    re.compile(
        r'(?:->|\.)set(?:Text|ToolTip|StatusTip|WhatsThis|WindowTitle|PlaceholderText|AccessibleName|AccessibleDescription)'
        r'\(\s*"(?P<text>[A-Za-z][^"\n]*)"'
    ),
    re.compile(
        r'new\s+Q(?:Label|PushButton|CheckBox|RadioButton|GroupBox|Action|StandardItem)'
        r'\(\s*"(?P<text>[A-Za-z][^"\n]*)"'
    ),
)

# Product names and stable shortcut identifiers are intentionally not translated.
UI_LITERAL_ALLOWLIST = {
    "Sigil",
    "Clip",
    "h",
}


def element_text(element):
    return "" if element is None else "".join(element.itertext())


def message_key(context_name, message):
    return (
        context_name,
        element_text(message.find("source")),
        message.get("numerus") == "yes",
    )


def messages_by_key(path, include_inactive=False):
    root = ET.parse(path).getroot()
    result = {}
    for context in root.findall("context"):
        context_name = context.findtext("name") or ""
        for message in context.findall("message"):
            translation = message.find("translation")
            state = translation.get("type") if translation is not None else None
            if not include_inactive and state in {"obsolete", "vanished"}:
                continue
            result[message_key(context_name, message)] = message
    return root, result


def extract_current_messages(source_root, lupdate):
    with tempfile.TemporaryDirectory(prefix="sigil-lupdate-") as directory:
        output = pathlib.Path(directory) / "current.ts"
        command = [
            str(lupdate),
            str(source_root / "src"),
            "-recursive",
            "-extensions",
            "cpp,h,ui",
            "-no-obsolete",
            "-locations",
            "none",
            "-ts",
            str(output),
        ]
        completed = subprocess.run(command, text=True, capture_output=True)
        if completed.returncode:
            raise AssertionError(
                "lupdate failed:\n{0}\n{1}".format(completed.stdout, completed.stderr)
            )
        _, messages = messages_by_key(output)
        return messages


def validate_catalog(source_root, lupdate, locale):
    catalog = source_root / "src" / "Resource_Files" / "ts" / ("sigil_{0}.ts".format(locale))
    root, translated = messages_by_key(catalog)
    extracted = extract_current_messages(source_root, lupdate)
    failures = []

    if root.get("language") != locale:
        failures.append("catalog language must be {0}".format(locale))

    missing = sorted(set(extracted) - set(translated))
    stale = sorted(set(translated) - set(extracted))
    for context, source, _ in missing:
        failures.append("missing source: {0}: {1!r}".format(context, source))
    for context, source, _ in stale:
        failures.append("stale active source: {0}: {1!r}".format(context, source))

    for key in sorted(set(extracted) & set(translated)):
        context, source, _ = key
        translation = translated[key].find("translation")
        if translation is None or translation.get("type") == "unfinished":
            failures.append("unfinished: {0}: {1!r}".format(context, source))
            continue
        forms = translation.findall("numerusform")
        values = [element_text(form) for form in forms] or [element_text(translation)]
        if any(not value.strip() for value in values):
            failures.append("empty translation: {0}: {1!r}".format(context, source))
            continue

        source_placeholders = collections.Counter(PLACEHOLDER_PATTERN.findall(source))
        source_tags = collections.Counter(HTML_TAG_PATTERN.findall(source))
        for value in values:
            if collections.Counter(PLACEHOLDER_PATTERN.findall(value)) != source_placeholders:
                failures.append("placeholder mismatch: {0}: {1!r}".format(context, source))
            if "</" in source and collections.Counter(HTML_TAG_PATTERN.findall(value)) != source_tags:
                failures.append("rich-text tag mismatch: {0}: {1!r}".format(context, source))

    return failures, len(extracted)


def validate_ui_literals(source_root):
    failures = []
    for suffix in ("*.cpp", "*.h"):
        for path in (source_root / "src").rglob(suffix):
            source = path.read_text(encoding="utf-8")
            for pattern in UI_LITERAL_PATTERNS:
                for match in pattern.finditer(source):
                    text = html.unescape(match.group("text"))
                    if text in UI_LITERAL_ALLOWLIST:
                        continue
                    line = source.count("\n", 0, match.start()) + 1
                    failures.append(
                        "untranslated UI literal: {0}:{1}: {2!r}".format(
                            path.relative_to(source_root), line, text
                        )
                    )
    return failures


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", type=pathlib.Path, required=True)
    parser.add_argument("--lupdate", type=pathlib.Path, required=True)
    parser.add_argument("--locale", default="zh_CN")
    args = parser.parse_args()

    catalog_failures, source_count = validate_catalog(
        args.source_root, args.lupdate, args.locale
    )
    failures = catalog_failures + validate_ui_literals(args.source_root)
    if failures:
        raise SystemExit("\n".join(failures))
    print(
        "{0} coverage: {1} active messages, all translated".format(
            args.locale, source_count
        )
    )


if __name__ == "__main__":
    main()
