---
name: paragraph-normalization
description: Analyze and normalize pseudo-paragraph DIV elements with Sigil's native conservative XHTML/CSS engine. Use for 伪段落, 偽段落, DIV 转 P, DIV 轉 P, paragraph normalization, or paragraph structure cleanup.
compatibility: Native Sigil-Enhanced Agent.
metadata:
  sigil.skill-version: "1"
  sigil.category: "structure"
  sigil.risk: "bulk-edit"
  sigil.requires-book: "true"
allowed-tools: >
  paragraphs.analyze paragraphs.plan paragraphs.apply
  transaction.preview transaction.commit transaction.rollback book.validate
---

# Native paragraph normalization

## Goal

Replace only auto-safe pseudo-paragraph `div` elements with `p` through Sigil's
native deterministic analyzer. Do not rewrite chapter prose or imitate the rules
with regex.

## Procedure

1. Call `paragraphs.analyze`. Pass `resource_ids` for an explicit scope; omit it
   only when the user requested the whole book. The four optional categories
   (blank lines, scene breaks, image wrappers, nested blocks) default to false.
2. Summarize `apply`, `review`, `skip`, and `error` files. Treat CSS dependencies
   and protected ranges as constraints. Do not promote a review-only file.
3. Call `paragraphs.plan` with the exact `analysis_id` and, when needed, an
   independently selectable subset of auto-safe resource IDs. Present the
   bounded source diffs and the fact that CSS, OPF, and resources are unchanged.
4. Call `paragraphs.apply` with the exact `plan_id`, `plan_digest`, and
   `expected_book_revision` returned by the plan. Do not call
   `transaction.begin` first: this tool revalidates the plan and opens an
   exclusive staged transaction itself.
5. Call `transaction.preview`. In Plan mode, stop here. In Edit or Auto mode,
   call `transaction.commit` only after the normal policy/approval flow.

## Reporting

`paragraphs.apply` only stages XHTML. The live Book is unchanged until commit,
and the EPUB is still unsaved afterward. Report full EPUBCheck as not run unless
a separate tool actually ran it. On any stale-plan or revision error, analyze
again; never retry the old digest.
