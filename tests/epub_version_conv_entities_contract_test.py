#!/usr/bin/env python3

import re
import sys
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


repo = Path(sys.argv[1]).resolve()
source = (repo / "src/BookManipulation/EpubVersionConv.cpp").read_text(encoding="utf-8")
header = (repo / "src/BookManipulation/HtmlNamedEntity.h").read_text(encoding="utf-8")

require(
    '#include "BookManipulation/HtmlNamedEntity.h"' in source,
    "EpubVersionConv must use the shared named-entity helpers",
)
require(
    "QString::fromUcs4(&u, 1)" in header,
    "named-entity codepoints must use fromUcs4 so supplementary-plane values do not hit QChar",
)
require(
    "rep = HtmlNamedEntityToNumericReferences(sval);" in source,
    "named-entity replacement must emit numeric references from Unicode scalars",
)
require(
    "chars.toUcs4()" in header,
    "numeric references must iterate Unicode scalars, not UTF-16 code units",
)

over_bmp = re.findall(r"QChar\((0x[0-9a-fA-F]{5,})\)", source)
require(
    not over_bmp,
    "QChar cannot hold supplementary-plane entities: " + ", ".join(over_bmp[:8]),
)
require(
    source.count("HtmlNamedEntityFromCodepoint(0x0001") >= 100,
    "mathematical alphanumeric entities must use HtmlNamedEntityFromCodepoint",
)
require(
    "HtmlNamedEntityFromCodepoint(0x0001d504)" in source,
    "Afr; (the former crash site) must use HtmlNamedEntityFromCodepoint",
)

print("epub_version_conv_entities_contract_test: ok")
