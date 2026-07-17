# Sigil MCP Layout Workflow

## Read First

Operate only on the currently open Book. Resource IDs are session-scoped. Read current revisions
before every write. A commit applies immediately after host revalidation, and one endpoint permits
only one active transaction.

Required tools for a new illustrated book:

- `sigil.capabilities.list`
- `sigil.book.info`, `sigil.book.package`
- `sigil.resource.list`, `sigil.resource.read_text`, `sigil.resource.read_many`
- `sigil.resource.read_text_range`, `sigil.transaction.read_text_range`
- transaction begin, add/replace text, add binary, metadata, spine, preview, validate, commit, rollback
- chunked text begin/write/finish/abort for large XHTML or CSS

When `sigil.capabilities.list` reports `external_import.available=true`, local images, generated
XHTML/CSS, fonts, and other files must use `sigil_mcp_upload.py`, preferably through one JSON
manifest. Do not read Base64 into model context, split it across tool calls, or delegate that token
transfer to another agent. `sigil.transaction.add_binary_resource` is only the fallback for a small
binary value the model already holds; it accepts at most 5 MiB decoded data.

Use the exact `external_import.batch_uploader_path` returned by capabilities. Do not assume the
uploader is on `PATH`, and do not search the whole filesystem for it. Verify that the path is a
readable file before beginning a transaction.

Do not send a large document as one JSON string. Page reads from `start=0` through each returned
`next_start`; those positions are UTF-16 code units. For large writes, declare the UTF-8 byte size,
begin a text write or text resource, send sequential chunks using cumulative UTF-8 byte offsets, and
finish before preview. Abort an upload when generation is abandoned. The host limit is 64 MiB per
text document and 1 MiB per host chunk.

## Preflight

1. Confirm the runtime tool catalog and limits.
2. Read Book info, package, all resources, active editor state, and session state.
3. Verify EPUB version, package root, canonical paths, current metadata, manifest IDs, spine, nav,
   stylesheet paths, and resource revisions.
4. Require explicit approval before replacing meaningful existing content.
5. Generate every local XHTML/CSS output and prepare the complete import manifest before beginning
   a transaction. Reject missing sources, duplicate Book paths/manifest IDs, files above the
   reported limits, and accidental additions over an existing Book path.

## Single-Transaction Creation

The host merges manifested additions staged before `update_spine` into the staged package document.
Create a new book in one transaction so resources, metadata, manifest, and spine share one validation
and one checkpoint.

1. Confirm all local outputs and the import manifest are complete, then begin with a descriptive
   label and `checkpoint=auto`. Do not generate files, inspect references, or encode images while
   the transaction idle timer is running.
2. Run `python3 UPLOADER_PATH --transaction TRANSACTION_ID --manifest imports.json`, using the exact
   path reported by capabilities. Keep images and CSS out of the spine and use deterministic unique
   manifest IDs.
3. Use MCP add/chunk tools only for content that exists solely in model output. Finish every
   chunked text upload before package updates.
4. Replace blank-template XHTML, nav, or CSS only with current resource IDs and revisions.
5. Replace ordered metadata with the original identifier plus verified title, roles, language,
   series, description, and current UTC modified time.
6. Call `transaction.status` and require `pending_external_imports=0`, then call `update_spine` only
   after all manifested additions are staged. Its final idref list may use those additions'
   manifest IDs; keep nav non-linear when included.
7. Preview and validate. Roll back on any conflict, warning that changes meaning, invalid result, or
   count mismatch.
8. Commit once after successful preview and validation.
9. Refresh resources and batch/range-read every added text resource. Verify exact generated text and
   nonzero length.

## Failure Recovery

- Tool error before commit: call rollback with the printed transaction ID.
- Validation or preview invalid: rollback; do not call commit.
- Connection closes: reconnect, call `sigil.transaction.status`, and roll back the active transaction.
- External uploader fails: preserve its short error and successful item count, inspect transaction
  status/preview, then use the returned `next_index` with `--start-at` or roll back. Never restart
  the manifest from zero in the same transaction, and never regenerate Base64.
- If status reports no active transaction after an uploader failure, the staged prefix is gone.
  Start a new transaction and upload the complete manifest from index zero; never use `--start-at`
  across different transaction IDs.
- Revision conflict: reread live content/package and rebuild the plan; never retry with stale values.
- Plugin or Sigil exits: treat the transaction as uncommitted until live Book inspection proves
  otherwise.

Always print the transaction ID in orchestration logs so recovery is possible.

## Import Manifest Shape

Omit the root `transaction_id` and pass the fresh ID with `--transaction`. A manifest may mix new
resources and revision-checked replacements of blank-template resources:

```json
{
  "resources": [
    {
      "source": "generated/chapter.xhtml",
      "operation": "add",
      "book_path": "Text/chapter.xhtml",
      "media_type": "application/xhtml+xml",
      "manifest_id": "chapter",
      "add_to_spine": true
    },
    {
      "source": "generated/nav.xhtml",
      "operation": "replace",
      "resource_id": "CURRENT_NAV_RESOURCE_ID",
      "expected_revision": 3,
      "media_type": "application/xhtml+xml",
      "kind": "text"
    },
    {
      "source": "images/cover.jpg",
      "operation": "add",
      "book_path": "Images/cover.jpg",
      "media_type": "image/jpeg",
      "manifest_id": "cover-image",
      "properties": "cover-image",
      "add_to_spine": false
    }
  ]
}
```

Use current resource IDs and revisions only. Never represent replacement of an existing Book path
as an add. Keep the manifest outside the source tree when source inventory scripts would otherwise
treat generated files as input.

## Live Acceptance Before Save

After the commit:

1. assert `transaction_active=false`;
2. verify expected resource, manifest, spine, chapter, image, Ruby, and footnote counts;
3. parse every XHTML and nav document;
4. resolve every local `href`, `src`, and CSS `url()`;
5. compare newly added text with generated content;
6. compare imported image hashes with source files when names are preserved.

The current public MCP catalog has no Save As/export tool. Ask the user to save to a new absolute
`.epub` path. Then run the bundled validator with `--strict-layout`. Do not claim EPUBCheck coverage
unless EPUBCheck was actually run.
