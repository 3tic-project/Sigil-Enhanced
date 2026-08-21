#!/usr/bin/env python3

import sys
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


repo = Path(sys.argv[1]).resolve()
source = (repo / "src/BookManipulation/EpubVersionConv.cpp").read_text(encoding="utf-8")
parser = (repo / "src/BookManipulation/NcxNavigation.cpp").read_text(encoding="utf-8")

require(
    '#include "BookManipulation/NcxNavigation.h"' in source,
    "EPUB2 to EPUB3 conversion must use the dedicated NCX parser",
)
require(
    "NcxNavigation::parse(" in source,
    "parse_ncx must call NcxNavigation::parse",
)
require(
    "NcxNavigation::bookPathToNavHref(" in source,
    "nav hrefs must be computed from book paths, not a hardcoded prefix",
)
require(
    'endsWith(".navpoint.navlabel")' not in source,
    "TagLister QWebPath tpath must not be treated as a dotted NCX path",
)
require(
    "QXmlStreamReader" in parser,
    "NCX parser must use XML streaming rather than TagLister tpath",
)
require(
    ".mid(11)" not in source.split("void EpubVersionConv::build_nav()")[1].split(
        "void EpubVersionConv::parse_ncx"
    )[0],
    "build_nav must not strip OEBPS/Text/ with a magic mid(11) offset",
)

print("epub_version_conv_nav_contract_test: ok")
