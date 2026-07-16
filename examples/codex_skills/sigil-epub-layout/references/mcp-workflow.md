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

`sigil.transaction.add_binary_resource` accepts strict Base64 and at most 5 MiB decoded data per
resource. Do not downsample or omit a larger image silently.

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
5. Prepare deterministic manifest IDs and reject duplicate paths before beginning a transaction.

## Two-Phase Creation

The current host validates package updates against the live manifest, not additions staged in the
same transaction. Therefore create a new book in two transactions.

### Transaction A: Content And Resources

1. Begin with a descriptive label and `checkpoint=auto`.
2. Import images with `add_binary_resource`, `add_to_spine=false`.
3. Add generated XHTML/CSS with `add_text_resource`, `add_to_spine=false`.
4. Replace blank-template XHTML, nav, or CSS only with their current resource IDs and revisions.
5. Preview and validate. Roll back on any conflict, warning that changes meaning, invalid result, or
   count mismatch.
6. Commit after successful preview and validation.
7. Refresh resources and batch-read every added text resource. Verify exact generated text and
   nonzero length before continuing.

### Transaction B: Package Metadata And Spine

1. Refresh `sigil.book.package`; do not reuse the earlier OPF revision.
2. Begin a new transaction.
3. Replace ordered metadata with the original identifier plus verified title, roles, language,
   series, description, and current UTC modified time.
4. Replace the spine with manifest IDs that now exist. Keep nav non-linear when included.
5. Preview and validate, then commit.

For edits that add no resources, related XHTML/CSS/package changes may use one transaction when the
package validator accepts the staged result.

## Failure Recovery

- Tool error before commit: call rollback with the printed transaction ID.
- Validation or preview invalid: rollback; do not call commit.
- Connection closes: reconnect, inspect `sigil.session.info`, and roll back the known transaction.
- Revision conflict: reread live content/package and rebuild the plan; never retry with stale values.
- Plugin or Sigil exits: treat the transaction as uncommitted until live Book inspection proves
  otherwise.

Always print the transaction ID in orchestration logs so recovery is possible.

## Live Acceptance Before Save

After both commits:

1. assert `transaction_active=false`;
2. verify expected resource, manifest, spine, chapter, image, Ruby, and footnote counts;
3. parse every XHTML and nav document;
4. resolve every local `href`, `src`, and CSS `url()`;
5. compare newly added text with generated content;
6. compare imported image hashes with source files when names are preserved.

The current public MCP catalog has no Save As/export tool. Ask the user to save to a new absolute
`.epub` path. Then run the bundled validator with `--strict-layout`. Do not claim EPUBCheck coverage
unless EPUBCheck was actually run.
