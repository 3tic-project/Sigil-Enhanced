---
name: book-structure
description: Restructure the open EPUB with general tools — split/merge chapters, wrap tags, regex replace, rename/move files, spine order, stylesheet linking, TOC generation, resource delete, image insert, Live Python. Use when the user asks to 拆分, 合并, 重命名, reorder, link CSS, generate 目录, wrap paragraphs, regex replace, delete files, or run a Python script on the book. Do not assume a fixed novel template.
compatibility: Native Sigil-Enhanced Agent.
metadata:
  sigil.skill-version: "1"
  sigil.category: "structure"
  sigil.risk: "bulk-edit"
  sigil.requires-book: "true"
allowed-tools: >
  book.summary book.resources book.spine book.search_regex book.check book.validate
  content.wrap_plain content.wrap content.replace_regex content.replace_body
  content.insert content.split content.merge image.insert
  resource.copy resource.create resource.delete resource.rename resource.replace_text
  spine.set spine.sort style.link toc.generate metadata.update python.run
  transaction.begin transaction.preview transaction.commit
  session.task_add session.task_update session.remember
---

# Book structure editing

## Goal

Change the open book through typed tools. Patterns (heading regex, class names, illustration markers) come from the user or from inspecting this book — never from a hardcoded novel format.

## Long text

Text the user dropped is already a resource. Do not copy it into tool arguments.

1. `content.wrap_plain` with `source_resource_id` and `rules.heading_pattern` / `illustration_pattern` the user gave (or that you inferred from `resource.read_fragment` samples).
2. `content.split` on heading tags or a heading regex to make one file per chapter.
3. `content.replace_body` with `source_resource_id` to move a large body into a template page.

`resource.replace_text` and `content.insert` reject large payloads on purpose.

## Batch markup

- `content.wrap` — wrap matches with tags/classes
- `content.replace_regex` — global or per-file regex (`$1` captures)
- `book.search_regex` — inspect first

## Images

Images must already be in the book (Book Browser drop). `image.insert` writes an `<img>` at a unique `anchor`. `book.check` lists broken and unused images.

## Structure

- `resource.delete` — not OPF/NCX/Nav, not the last XHTML
- `resource.rename` — filename in the same folder or a full EPUB path (move)
- `spine.set` — full new reading order
- `spine.sort` — alphanumeric book_path order
- `style.link` — replace `<link rel="stylesheet">` tags
- `content.merge` — concatenate files into the first
- `toc.generate` — headings in spine order
- `metadata.update` — any DC field; `_remove` deletes
- `python.run` — Live Python v2 on the in-memory Book (`plugin.book`). Not a ZIP snapshot. Commit/rollback the Agent transaction first. Prefer typed tools.

## Session

For multi-step jobs, `session.task_add` a checklist and `session.remember` the chosen regex/class names so later turns do not reinvent them.
