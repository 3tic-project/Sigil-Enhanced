#!/usr/bin/env python3
"""Read-only corpus oracle for the Advanced Regex Workbench sample recipes."""

from __future__ import annotations

import argparse
import json
import re
import sys
import zipfile
from pathlib import Path


FIXTURE_DIR = Path(__file__).parent / "fixtures" / "regex_workbench_book"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def load_recipe(filename: str) -> dict:
    path = FIXTURE_DIR / filename
    recipe = json.loads(path.read_text(encoding="utf-8"))
    require(recipe["format"] == "sigil.regexWorkbench.recipe", f"bad format: {path}")
    require(recipe["version"] == 1, f"bad version: {path}")
    return recipe


def load_xhtml(epub: Path) -> dict[str, str]:
    with zipfile.ZipFile(epub, "r") as archive:
        return {
            name.rsplit("/", 1)[-1]: archive.read(name).decode("utf-8")
            for name in archive.namelist()
            if name.startswith("item/xhtml/") and name.endswith(".xhtml")
        }


def python_replacement(pcre_replacement: str) -> str:
    return re.sub(r"\\g\{([^}]+)\}", r"\\g<\1>", pcre_replacement)


def audit_secondary(resources: dict[str, str]) -> None:
    rule = load_recipe("01-secondary-dialog-inner-quotes.json")["rules"][0]
    require(rule["secondaryMode"] == "PreSearch", "secondary case must use PreSearch")
    outer = re.compile(rule["secondaryPattern"], re.DOTALL)
    primary = re.compile(rule["find"], re.DOTALL)
    replacement = python_replacement(rule["replace"])
    replacement_count = 0
    changed: set[str] = set()
    outputs: dict[str, str] = {}

    for name, source in resources.items():
        def replace_range(match: re.Match[str]) -> str:
            nonlocal replacement_count
            prefix = match.group(0)[: match.start(1) - match.start(0)]
            suffix = match.group(0)[match.end(1) - match.start(0) :]
            body, count = primary.subn(replacement, match.group(1))
            replacement_count += count
            return prefix + body + suffix

        output = outer.sub(replace_range, source)
        outputs[name] = output
        if output != source:
            changed.add(name)

    require(replacement_count == 2, f"secondary replacements: {replacement_count}, expected 2")
    require(changed == {"p-007.xhtml", "p-025.xhtml"}, f"secondary resources: {changed}")
    require("「良『聖女』乙女早」" in outputs["p-007.xhtml"], "p-007 spot check failed")
    require("『やっぱなし』" in outputs["p-025.xhtml"], "p-025 spot check failed")


def audit_recursive(resources: dict[str, str]) -> None:
    rule = load_recipe("02-recursive-fullwidth-spaces.json")["rules"][0]
    require(rule["recursive"], "recursive case must enable recursion")
    pattern = re.compile(rule["find"])
    replacement_count = 0
    changed: set[str] = set()
    colophon_count = 0

    for name, source in resources.items():
        output = source
        resource_count = 0
        for _ in range(rule["maxIterations"]):
            output, count = pattern.subn(rule["replace"], output)
            resource_count += count
            if count == 0:
                break
        else:
            raise AssertionError(f"recursive case did not converge: {name}")
        require(pattern.search(output) is None, f"adjacent spaces remain: {name}")
        replacement_count += resource_count
        if resource_count:
            changed.add(name)
        if name == "p-colophon.xhtml":
            colophon_count = resource_count

    require(replacement_count == 73, f"recursive replacements: {replacement_count}, expected 73")
    require(len(changed) == 16, f"recursive resources: {len(changed)}, expected 16")
    require(colophon_count == 9, f"colophon replacements: {colophon_count}, expected 9")


def audit_named_variable(resources: dict[str, str]) -> None:
    rules = load_recipe("03-python-named-capture-variable.json")["rules"]
    require(len(rules) == 2, "variable case needs a producer and consumer")
    source = resources["p-titlepage.xhtml"]
    capture_rule, consume_rule = rules
    capture_pattern = re.compile(capture_rule["find"])
    match = capture_pattern.search(source)
    require(match is not None, "title-page author did not match")
    author = match.group("author")
    require(author == "桜木桜", f"captured author: {author!r}")

    staged, capture_count = capture_pattern.subn(
        python_replacement(capture_rule["replace"]), source
    )
    expanded = consume_rule["replace"].replace("${var:author}", author)
    staged, consume_count = re.subn(consume_rule["find"], expanded, staged)
    require(capture_count == 1 and consume_count == 1, "variable rule trace counts changed")
    require('<hr data-test-author="桜木桜"/>' in staged, "variable consumption failed")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--epub", required=True, type=Path)
    args = parser.parse_args()
    require(args.epub.is_file(), f"EPUB not found: {args.epub}")
    resources = load_xhtml(args.epub)
    require("p-titlepage.xhtml" in resources, "expected title page is missing")
    audit_secondary(resources)
    audit_recursive(resources)
    audit_named_variable(resources)
    print("regex workbench EPUB cases passed: secondary=2, recursive=73/16, author=桜木桜")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (AssertionError, KeyError, OSError, re.error, zipfile.BadZipFile) as error:
        print(f"FAILED: {error}", file=sys.stderr)
        sys.exit(1)
