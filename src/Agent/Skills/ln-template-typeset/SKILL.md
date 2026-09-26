---
name: ln-template-typeset
description: Fill the open 轻小说模板 EPUB from a dropped light-novel manuscript (TXT plus images). Use when the user opens the template, drags a book folder into Book Browser, or asks to 排版, 套模板, typeset, import a 文稿, or place 插图/章节 automatically.
compatibility: Native Sigil-Enhanced Agent. Requires manuscript.parse and content.typeset_from_manuscript.
metadata:
  sigil.skill-version: "1"
  sigil.category: "typesetting"
  sigil.risk: "bulk-edit"
  sigil.requires-book: "true"
allowed-tools: >
  book.summary book.resources book.spine book.toc book.metadata book.validate book.check
  font.inventory style.stylesheets resource.read_fragment resource.patch_fragment
  manuscript.parse content.typeset_from_manuscript content.fill_section
  resource.copy resource.replace_text resource.create
  transaction.begin transaction.preview transaction.commit transaction.rollback
  checkpoint.create metadata.update
---

# Light-novel template typeset

## Goal

Fill the **currently open** 轻小说模板 with the manuscript the user dropped into Book Browser. Keep the template CSS, spine roles, and page wrappers. Put novel text into `Section00N` pages.

This is a C++ typeset job. The model plans and reports; it does not paste chapters.

## When this applies

The open book has pages named like `cover`, `start`, `title`, `message`, `summary`, `illus1`…, `contents`, `Section001`… and the user has added a `.txt` (or ImportTXT HTML) plus images such as `cover.jpg`.

## Do not

- Do not call `resource.patch_fragment` with chapter bodies.
- Do not put novel text in `resource.replace_text` (it will refuse large payloads).
- Do not tell the user to paste into Book View / 书籍视图.
- Do not invent image filenames. `manuscript.parse` reports what is in the book.
- Preserve the template CSS unless the user requests a font or style change. For a font change, use `font.inventory` and verify each referenced font file exists.
- Do not ask for font or image bytes.

## Procedure

1. `book.summary` / `book.resources` if the book map is not already in context.
2. `manuscript.parse` (omit `manuscript_id` unless several candidates exist). Check its chapter list against the manuscript's actual headings and illustration markers. If prologue, interlude, finale, or afterword is missing, supply `heading_pattern` and `illustration_pattern` and parse again.
3. If `template.detected` is false, stop and tell the user to open `轻小说模板.epub` first, then drop the book folder.
4. If `chapter_count` is 0, stop and report that the TXT has no `第N話` / `后记` headings.
5. `transaction.begin` with a short label.
6. `content.typeset_from_manuscript` — one call. Pass exactly the same custom patterns used by the successful `manuscript.parse`, if any. It copies extra `Section` pages, wraps every chapter, rewrites `illus`/`cover`/`start` image hrefs, and fills title / 制作信息 / 简介 / visible contents / navigation TOC / metadata.
7. Compare `chapters_filled` with the verified `chapter_count`; if they differ, roll back and correct the patterns. Review `missing_images` and `warnings` before commit.
8. `transaction.preview`, then `transaction.commit` with the current `book_revision`.
9. `book.validate` and `book.toc`. Report filled chapters, copied sections, image map, missing images, and whether the raw import page was stubbed.

If the user asks to use the embedded font by default, inspect the existing `@font-face`, `body`, and title rules. Point them to actual bundled fonts, then check CSS URLs and chapter pages before reporting success.

## Image mapping (engine, not the model)

- `cover.jpg` → `cover.xhtml`; `start.jpg` → `start.xhtml`.
- Front-matter `［插图：…］` markers before the first chapter body fill `illus1`… in order (`color1`, `kuchie-001`, `author`, …). Template `co1.jpg` placeholders are rewritten.
- In-chapter `［插图：name］` become `<div class="illus">` pointing at `../Images/name.jpg` (or the real dropped filename).
- A TXT with no illustration markers still maps leftover images onto `illus` pages; remaining files are listed as `unplaced_images`.

## Extra chapters

Template ships seven `Section` pages. If the manuscript has more, `content.typeset_from_manuscript` copies the last section (Section007 → Section008…). Do not ask the user to duplicate files in Book Browser.

## Afterward

If `retired_source` is true, a leftover ImportTXT page in Text is a stub; `resource.delete` can remove it after commit. Mention missing images so the user can rename or drop the file.
