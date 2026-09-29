---
name: release-preflight
version: 1
description: >
  Check the open EPUB before release or hand-off and report located findings with
  honest validator status. Use for 发行前检查, 发布前检查, 出版预检, 验收, 检查这本书,
  release preflight, or asking whether the book is ready.
compatibility: Native Sigil-Enhanced Agent.
triggers: [发行前检查, 發行前檢查, 发布前检查, 發佈前檢查, 出版预检, 出版預檢, 预检, 預檢, 验收, 驗收, preflight, release check, ready to publish, 能发布吗, 可以发布]
requires_capabilities: [preflight.run, preflight.issues]
allowed-tools: [preflight.profiles, preflight.run, preflight.issues, structure.inspect, book.check, book.summary, resource.read_fragment]
requires_book: true
metadata:
  sigil.category: "qa"
  sigil.risk: "read-only"
---

# Release preflight

## Goal

Give the editor a located, prioritized list of release blockers and review items for
the book as it is now (including unsaved and staged edits). Do not change the book in
this skill; fixes are separate, reviewed edits.

## Procedure

1. `preflight.run` with the default profile unless the user asked only for
   structure (`epub-core`) or accessibility (`a11y-basic`).
2. Follow `next_offset` with `preflight.issues` and the same `run_id` until
   `has_more=false`. Filter by `severity=error` first when there are many findings.
3. For each error, read the cited lines with `resource.read_fragment` only when the
   evidence is not enough to explain it.
4. Call `structure.inspect` and include its `differences` (body pages missing from
   the TOC or without a heading, TOC labels that differ from headings, TOC order
   against the spine) as navigation review items.
5. Report:
   - counts by severity and rule;
   - blockers (errors) as a table: rule, `book_path` L<line>, evidence, suggested fix;
   - review items (`manual_required`) grouped by rule, e.g. images that need a human
     decision about alt text;
   - `validation_status` exactly: local result, EPUBCheck and Ace `not_run` with the
     reason, human review pending, and that the saved EPUB file was not checked.
6. Offer concrete next steps. Case-only path mismatches and missing CSS references
   are mechanical; alt text, language exceptions and heading structure need the
   editor's decision.

## Rules

- Never say the book is valid, conformant, EPUBCheck-clean or accessible from local
  rules. Say "local preflight found N errors" or "no local errors; EPUBCheck not run".
- `RESOURCE_UNREFERENCED` and `IMG_ALT_FILENAME` are information, not blockers.
- Do not invent alt text from file names or surrounding prose; ask what each
  informative image shows.
- After any fix, run `preflight.run` again. `PREFLIGHT_STALE` means the old run no
  longer describes the book.
