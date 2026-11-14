---
name: book-translation
description: Translate the current EPUB in native batches, default bilingual, with full-text LLM scan, glossary/memory aggregation, translation, correction and Chinese reading format. Use for 翻译, 雙語, 双语, translate, or terminology correction.
triggers: [翻译, 翻譯, 双语, 雙語, 译文, 譯文, translate, translation]
requires_capabilities: [translation.prepare, translation.run, translation.context, translation.status]
requires_book: true
allowed-tools: >
  translation.prepare translation.run translation.status translation.control
  translation.context translation.revise translation.format translation.save_glossary
  book.summary book.resources book.spine book.search resource.read_fragment
  session.tasks session.task_add session.task_update
---

# Translate the open book

Use the window's native translation controller. It extracts whole reading
paragraphs (ruby base + inline text), groups requests and writes bilingual pairs
directly into the live Book. Never copy chapter bodies into tool arguments,
generate XHTML, or issue a patch per paragraph.

1. Check translation.status for an existing window context. Prepare once for the
   requested scope; omit resource_ids only for a whole-book request. A selection
   uses one resource and its UTF-16 range, expanded to complete paragraphs.
2. Keep a short temporary plan. Load any explicitly selected Misc glossary with
   existing resource tools and translation.context; do not read private paths
   or invent terminology. Imported user mappings take priority.
3. Call translation.run. The default workflow scans ALL source batches, merges
   terms/short memory, translates, reviews and formats. For a short sample use
   stages:[translate], preset:preserve; do not scan/reformat the whole book.
4. The tool returns immediately and the native panel continues the work. Normal
   batches require no additional model turns or user confirmations. Do not
   poll in a loop or claim completion merely because the workflow started.
   The host resumes this same conversation with a background completion notice.
   Wait for that notice before final reporting or saving the completed glossary;
   then finish the original user's remaining requests. Retry failed work at most
   twice; never restart the already completed whole book.
5. Use translation.revise for requested name/term corrections, and control for
   pause/resume/stop/retry. Do not patch sentences individually. Use context to
   inspect only the needed glossary/memory pages.
6. Save the glossary to Misc when requested. Report actual translated/reviewed/
   failed counts and unsaved Book edits. The normal Sigil Save saves the EPUB.

Default whole-book Chinese reading format uses native horizontal/LTR and image
adaptation. Preserve is available. Closing/changing the Book discards temporary
state and stops requests; there is no cross-restart task database or local NER.
