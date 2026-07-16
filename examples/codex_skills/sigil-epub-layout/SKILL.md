---
name: sigil-epub-layout
description: Build, typeset, and validate a reflowable EPUB 3 in the currently open Sigil Book through Sigil MCP, using local TXT/XHTML/images and an explicit layout profile. Use only when the user explicitly invokes `$sigil-epub-layout`; never invoke it implicitly for ordinary Sigil, EPUB, writing, or CSS requests.
---

# Sigil EPUB Layout

Use Sigil MCP to turn local source material into a conservative, accessible EPUB 3. Keep the
reference books as design evidence only. Never copy their prose, images, fonts, identifiers, or
vendor-specific code into a new book.

## Required Inputs

Obtain or infer these values before writing:

- source directory and primary text/XHTML source;
- title, language, creator, volume/series data, and contributor roles;
- image roles and insertion anchors;
- desired output path;
- layout profile, defaulting to `standard-lightnovel-horizontal`.

Ask only for values that cannot be recovered safely. Never invent an ISBN, copyright holder,
translator, publisher, image alt text, or pronunciation.

## Load References

Read [references/layout-rules.md](references/layout-rules.md) before creating the page plan.
Read [references/mcp-workflow.md](references/mcp-workflow.md) before calling write tools.

Use the bundled scripts without reading their source unless they fail:

```sh
python3 scripts/inspect_source.py SOURCE_DIR --pretty
python3 scripts/inspect_epub.py REFERENCE.epub --pretty
python3 scripts/validate_epub.py OUTPUT.epub --strict-layout
```

## Workflow

1. Inventory the source. Detect text encoding, chapter candidates, image markers, Ruby syntax,
   scene breaks, footnotes, image dimensions, and unmatched assets. Do not send local content to
   a model or network service.
2. Inspect the live Book with `sigil.capabilities.list`, `sigil.book.info`,
   `sigil.book.package`, and `sigil.resource.list`. Require the binary-add tool when importing
   images. Stop if multiple Books are ambiguous or the running plugin lacks required tools.
3. Treat a Book with meaningful existing content as an edit, not a blank template. Do not replace
   it wholesale without explicit user approval. Read adjacent XHTML/CSS before editing it.
4. Build a page plan from source evidence. Parameterize optional front matter, chapter hierarchy,
   illustration placement, back matter, language, and theme. Use assets as starting points, not as
   immutable output.
5. Generate semantic XHTML. Convert source Ruby markers to `<ruby><rp><rt>`, use stable unique
   IDs, add useful image `alt`, keep URLs relative, and avoid JavaScript and vendor extensions by
   default.
6. Apply the MCP transaction sequence in `references/mcp-workflow.md`. For a new book, stage all
   resources before updating metadata/spine, then preview, validate, and commit once.
   A commit applies immediately without a native confirmation dialog.
7. After the commit, immediately batch-read every newly added text resource. Fail the run if any file is empty,
   malformed, truncated, or different from the generated text. Confirm that no transaction remains
   active.
8. Have the user save to a new `.epub` path when the public MCP catalog has no output tool. Never
   automate Save As through keystrokes. Validate the saved file with `validate_epub.py`.
9. Report the output path, SHA-256, resource/chapter/image/Ruby/footnote counts, warnings, and any
   validation not performed. Do not describe the book as complete while an error remains.

## Quality Gates

Require all of the following:

- EPUB ZIP, container, OPF, manifest, spine, nav, and XML are structurally valid;
- every manifest path and local XHTML/CSS reference resolves;
- every spine text resource is nonempty and machine-readable;
- title, identifier, creator, exact language, cover-image, and modified time are present;
- nav order and labels match the actual reading order;
- chapter, image, Ruby, and footnote counts reconcile with the source plan;
- source images are neither silently recompressed nor substituted;
- no active MCP transaction or stale write lock remains.

Use a warning, not a false success, when EPUBCheck is unavailable. The bundled validator is a
deterministic structural gate, not a replacement for the complete EPUBCheck ruleset.

## Assets

- `assets/reflowable-horizontal.css`: minimal horizontal reflowable typography.
- `assets/chapter.xhtml`: semantic chapter skeleton.
- `assets/image-page.xhtml`: cover or full-page illustration skeleton.
- `assets/nav.xhtml`: EPUB 3 navigation skeleton.

Replace every `{{PLACEHOLDER}}`, remove unused rules, and follow the open Book's canonical package
directory. Do not add embedded fonts unless the user supplies licensed fonts and requests them.
