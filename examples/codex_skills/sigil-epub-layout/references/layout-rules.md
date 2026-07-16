# Reflowable Light-Novel Layout Rules

## Contents

- Evidence baseline
- Package and reading order
- XHTML and typography
- Images, Ruby, and notes
- Parameters
- Excluded reference patterns

## Evidence Baseline

These rules were distilled from the seven EPUB files in `todo/test_file/ref` without copying their
text or assets. Six are EPUB 3 and one is EPUB 2. They contain 20-48 XHTML files, 16-23 images, and
one to five stylesheets. All are horizontal, reflowable Chinese light novels.

Use EPUB 3 as the default. Treat the EPUB 2 sample as compatibility evidence only.

## Package And Reading Order

Use the standard container layout when creating a blank book:

```text
mimetype
META-INF/container.xml
OEBPS/content.opf
OEBPS/Text/
OEBPS/Styles/
OEBPS/Images/
OEBPS/Fonts/       # only when licensed fonts are explicitly embedded
```

When editing a book, retain its existing package root and directory conventions.

Require a real `dc:identifier`; generate a UUID when no real identifier exists. Set `dc:title`,
`dc:creator`, exact BCP 47 `dc:language`, and UTC `dcterms:modified`. Add contributor roles only
from source evidence. For a series, use EPUB 3 collection refinements and the real volume position.
Never manufacture an ISBN.

Default page order, omitting absent pages:

1. cover;
2. title page;
3. production/copyright information;
4. synopsis or character introduction;
5. color illustrations;
6. visible contents page;
7. prologue, chapters, interludes, finale;
8. afterword and bonus stories;
9. back cover;
10. EPUB Navigation Document, `linear="no"` when it is in the spine.

Keep a visible contents page and the machine navigation document separate. The nav document must
use `nav epub:type="toc" role="doc-toc"`; use nested `ol` for real hierarchy. Add hidden landmarks
for cover, body matter, and contents. Keep all links synchronized with the spine.

## XHTML And Typography

Use one XHTML resource per chapter or independent short story. A spine item is already a page
boundary; do not add redundant blank pages. Use semantic `section`, headings, paragraphs, figures,
and asides. Set `lang`, `xml:lang`, a meaningful `<title>`, and a stylesheet link on every document.

Default horizontal body values are deliberately conservative:

- serif system-font fallback;
- line height around `1.5-1.8`;
- justified paragraphs with `2em` first-line indent;
- no indent on headings, notes, dialogue labels, and scene separators;
- centered chapter titles with restrained sizing;
- link color inherited from surrounding text;
- `letter-spacing: 0` unless the source explicitly requires typographic spacing.

Do not copy the large utility stylesheets from the references. Generate only the rules used by the
book. Keep title-page customization local or in a small named class instead of inflating global CSS.

## Images, Ruby, And Notes

Give the cover, each front-matter illustration, and each full-page illustration its own XHTML when
the source plan calls for a standalone page. Center images and constrain them to the viewport with
`max-width`, `max-height`, and `object-fit: contain`. Use `figure` in body text. Preserve original
bytes unless the user requests conversion or optimization.

Write useful alt text from source evidence. Decorative images use empty alt text; covers and
illustrations do not. Record orientation explicitly rather than using user-agent detection scripts.

Convert Ruby to standard markup:

```html
<ruby>正文<rp>（</rp><rt>读音</rt><rp>）</rp></ruby>
```

Never leave `[ruby=...]...[/ruby]` or similar source markers in the EPUB. Use `rt` around `.55-.6em`.

For EPUB 3 footnotes, use a unique noteref and aside pair:

```html
<a epub:type="noteref" href="#note-1" id="note-ref-1">1</a>
<aside epub:type="footnote" id="note-1"><p>...</p></aside>
```

Do not reuse the same ID on a containing `aside` and an inner element.

## Parameters

Parameterize all of these instead of guessing:

- title, volume, series, author, illustrator, translator, producer, publisher, language, rights;
- optional page set and reading order;
- chapter detection and one-level or nested contents hierarchy;
- image role, anchor, orientation, alt text, and standalone/body placement;
- Ruby and footnote source syntax;
- accent color and title treatment;
- font role mapping, embedding, and subsetting;
- compatibility targets such as NCX or vendor-specific extensions.

Default `embed_fonts=false`, `javascript=false`, `vendor_extensions=false`. If fonts are embedded,
use only licensed files, subset them with Sigil's built-in tool, and retain system fallbacks.

## Excluded Reference Patterns

Do not inherit these patterns from the sample books:

- placeholder or fabricated ISBN values;
- broken `unique-identifier` references;
- blanket `scripted` properties on documents without scripts;
- user-agent detection and image-rotation JavaScript;
- `duokan-*`, `zy-*`, custom note elements, and proprietary enlargement attributes by default;
- duplicate footnote IDs;
- print rules that hide the entire book;
- unused font families, transform utilities, decorative classes, or hidden nav content;
- filename-only image alt text.
