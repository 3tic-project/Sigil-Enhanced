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

Use this Agent conversation's native translation controller. It extracts whole reading
paragraphs (ruby base + inline text), groups requests and writes bilingual pairs
directly into the live Book. Never copy chapter bodies into tool arguments,
generate XHTML, or issue a patch per paragraph.

1. Check translation.status for this conversation's existing context. Prepare once for the
   requested scope; omit resource_ids only for a whole-book request. A selection
   uses one resource and its UTF-16 range, expanded to complete paragraphs. State
   this expansion when a request selects only part of a paragraph; never claim
   exact sentence selection. paragraph_offset/paragraph_limit can prepare a few
   reading paragraphs. status(include_paragraphs:true,offset,limit) returns indexes;
   run(only_paragraphs:[...]) narrows a prepared scope without another prepare.
2. Keep a short temporary plan. Load any explicitly selected Misc glossary with
   existing resource tools and translation.context; do not read private paths
   or invent terminology. Imported user mappings take priority.
   Apply user-specified names before starting: context(kind:glossary,
   action:update,terms:[{source:"ミナ",target:"米娜",locked:true}]). Use the
   typed plan object and memory string fields for the other context kinds.
   Do not repeatedly send a JSON string if a typed parameter is available.
3. Call translation.run. The default workflow scans ALL source batches, merges
   terms/short memory, translates, reviews and formats. For a short sample use
   stages:[translate], preset:preserve; do not scan/reformat the whole book.
   For a complete whole-book request, omitting stages is the recommended default.
   User-directed workflows are flexible: scan only, scan+aggregate, translate only,
   review only, format only, or another explicit ordered combination. Do not add
   unrequested whole-book work to a sample. Empty stages is invalid. preserve
   changes formatting only, not stage selection. A short sample can be one paragraph.
4. The tool returns immediately and the Agent's native controller continues the work. Normal
   batches require no additional model turns or user confirmations. Do not
   poll in a loop or claim completion merely because the workflow started.
   The host resumes this same conversation with a background completion notice.
   Wait for that notice before final reporting or saving the completed glossary;
   then finish the original user's remaining requests. Retry failed work at most
   twice; never restart the already completed whole book.
5. Use translation.revise for requested name/term corrections, and control for
   pause/resume/stop/retry. Do not patch sentences individually. Use context to
   inspect only the needed glossary/memory pages.
   A name change in an existing bilingual book must use revise(terms:[...],
   instruction:"only change the requested name; preserve other wording").
   Keep unchanged names out of the terms array. Do not use regex/source edits
   for translated names or glossary JSON. To update a previously saved glossary,
   pass its existing resource_id to save_glossary instead of creating a second file.
   revise and control(retry_failed) also return before work finishes. After
   starting either, give a short progress reply and END THE TURN. The host will
   resume with a new completion notice; do not keep calling status to wait.
6. Save the glossary to Misc when requested. Report actual translated/reviewed/
   failed counts and unsaved Book edits. The normal Sigil Save saves the EPUB.
   Request counts are cumulative across stages, not failed-attempt counts.
   translated/written means translations exist in the actual Book; reviewed means
   checked, not newly written. Report incomplete scan separately from incomplete
   review. retry_failed retries incomplete stages, including scan/aggregate.
   A compact task card in the Agent dock shows progress and direct controls.
   Keep the final report brief; omit session IDs and per-paragraph diagnostics
   unless asked.

Default whole-book Chinese reading format uses native horizontal/LTR and image
adaptation. Preserve is available. Closing/changing the Book discards temporary
state and stops requests; there is no cross-restart task database or local NER.
Starting a new Agent conversation cancels old requests and discards its temporary
plan, memory, glossary and caches. These never transfer to another conversation.
Book edits and explicitly saved Misc glossaries remain ordinary book content;
load a saved glossary explicitly when the new conversation needs it. Translation
model/thinking/batch preferences live in application Preferences under Native Agent.
