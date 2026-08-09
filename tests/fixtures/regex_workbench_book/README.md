# Regex Workbench book cases

These recipes exercise the three Advanced Regex Workbench features against:

`隣の席の聖女様は俺にこっそりスカートの中を教えてくれる.epub`

The EPUB itself is not part of the repository. The expected values below were
measured read-only from the copy used on 2026-08-09.

## 1. Secondary search inside dialogue

Open `01-secondary-dialog-inner-quotes.json`, select **All XHTML**, and run
**Dry Run**.

- The secondary expression `「([^」]*)」` selects only the first captured range
  inside each paired dialogue delimiter.
- The primary expression then changes `〝text〟` to `『text』` only inside those
  ranges.
- Expected: 2 replacements in 2 resources (`p-007.xhtml` and `p-025.xhtml`).
- Spot checks: `「良〝聖女〟乙女早」` becomes `「良『聖女』乙女早」`; the inner
  `〝やっぱなし〟` occurrence also changes while its outer `「...」` stays intact.

## 2. Recursive replacement until convergence

Open `02-recursive-fullwidth-spaces.json`, select **All XHTML**, and run
**Dry Run**.

- The find field contains two U+3000 IDEOGRAPHIC SPACE characters.
- The replacement contains one U+3000 character.
- Expected: 73 replacements over multiple passes in 16 changed resources.
- The final staged XHTML has no adjacent U+3000 pair.
- With only `p-colophon.xhtml` selected, expect 9 replacements in 1 resource.

**Do not Apply this recipe to the original book.** The repeated spaces are also
used for scene separators and colophon alignment, so collapsing them changes
intentional layout. This is deliberately a Dry Run/convergence test.

## 3. Python-style named capture stored as a variable

Open `03-python-named-capture-variable.json`, open `p-titlepage.xhtml`, select
**Current file**, and run **Dry Run**.

- Rule 1 uses `(?P<author>...)`, preserves the matched markup with
  `\g{author}`, and stores capture `author`.
- Rule 2 reads `${var:author}` and places it in a synthetic `data-test-author`
  attribute.
- Expected: variable inspector shows `author = 桜木桜`; 2 replacement traces,
  1 changed resource; final staged markup contains
  `<hr data-test-author="桜木桜"/>`.

This recipe is also intended for Dry Run or a disposable copy because the
`data-test-author` attribute exists only to make variable consumption visible.

## Read-only corpus audit

The repository audit script independently checks the recipes and expected
counts against an EPUB without writing to it:

```sh
python3 tests/regex_workbench_epub_case_test.py \
  --epub "/path/to/隣の席の聖女様は俺にこっそりスカートの中を教えてくれる.epub"
```

The normal C++ test suite separately loads these JSON files through the
production `RegexRecipeStore`, and covers PCRE2 capture-name enumeration,
secondary matching, recursive convergence, variable expansion, batch staging,
XML validation, and undo/checkpoint integration.
