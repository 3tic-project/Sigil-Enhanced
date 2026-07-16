# Live API Coverage Suite — Design

## Goal

Provide an installable, human-runnable suite that **exercises every public
method** of the `sigil_live` Python SDK (Live Plugin API v2), with clear pass /
skip / fail reporting and durable artifacts left in the Book for inspection.

## Constraints

| Constraint | Implication |
| --- | --- |
| Plugin `type` gates host APIs | `input.*`, `output.*`, `validation.*` require matching plugin types. One `edit` plugin cannot cover them at runtime. |
| Lifetime | `events.next_event()` needs a long-lived `book-session` plugin; `command` plugins use `poll()`. |
| Interactive dialogs | `choose_open_file` / `choose_save_file` (and real input/export) need a user. Coverage marks **skip** if cancelled. |
| Safety | Mutating suites keep most probes (user-visible proof). Dedicated throwaways cover `remove_*` without erasing the main keep-set. |
| Installability | Each sibling directory is a full plugin (`plugin.xml` + `plugin.py` + local modules). |

## Architecture

```
examples/live_plugins/
  LiveApiCoverage/              # type=edit, lifetime=command  (bulk of SDK)
    plugin.xml
    plugin.py                   # entry: confirm → run suites → report
    harness.py                  # CheckResult, CoverageRunner
    suites_core.py              # Plugin/Book/Editor/UI/Events(poll)/Transaction
  LiveApiCoverageInput/         # type=input
  LiveApiCoverageOutput/        # type=output
  LiveApiCoverageValidation/    # type=validation
  LiveApiCoverageSession/       # type=edit, lifetime=book-session (next_event)
  api-coverage.json             # maps every public SDK method → example path
```

### Coverage mapping

`tests/live_plugin_examples_test.py` requires every public SDK method to appear
as a call (`.method(`) in the file listed by `api-coverage.json`. The suite is
the **canonical** mapping; older demos may still call the same APIs.

Lifecycle methods excluded by the existing test harness:

- `Plugin.connect` / `finish` / `close` (launcher-owned)

### Suite groups (edit plugin)

| Suite | SDK surface |
| --- | --- |
| `session` | `Plugin.ping`, `session_info` read |
| `book_read` | All `BookApi` getters, iterators, text/binary/archive reads, materialize |
| `events_poll` | `subscribe`, `poll`, `unsubscribe` |
| `editor` | Full `EditorApi` (no-op or reversible when active tab is text) |
| `ui` | status/message/confirm/progress; optional file choosers |
| `transaction` | Full `Transaction` including keep-set + throwaway remove |

### Companion plugins

| Plugin | Why separate |
| --- | --- |
| Input | Host rejects input RPC for non-input plugins |
| Output | Host rejects output RPC for non-output plugins |
| Validation | `publish_results` is validation-type only |
| Session | Blocks on `next_event` until Cancel / Book close |

## Runtime modes

Configured by the opening confirm dialog (edit plugin):

1. **Keep artifacts** (default): leave managed probes + unmanifested META-INF sample + JSON report under `Misc/`.
2. **Interactive UI**: also open open/save file dialogs (cancel → skip, not fail).

Companions always ask once before acting.

## Artifacts (edit plugin keep-set)

| Path | Role |
| --- | --- |
| `LiveApiCoverage/probe-*.bin` | Managed binary after add → write → rename → move |
| `META-INF/live-api-coverage-*.txt` | Unmanifested archive after add → replace (kept) |
| `Misc/LiveApiCoverage-report.json` | Machine-readable results |
| `Misc/LiveApiCoverage-report.txt` | Human-readable summary |

Throwaways used only to cover removals:

- `LiveApiCoverage/throwaway-*.bin` → `remove_resource`
- `META-INF/live-api-coverage-throwaway-*.txt` → `remove_archive_file`

## Pass / skip / fail rules

- **pass**: call completed without exception; optional soft assertions (keys present).
- **skip**: precondition missing (no active editor, user cancelled dialog, no binary resource).
- **fail**: unexpected exception or invariant broken.

Overall `run()` returns `0` if there are zero **fail**s (skips allowed). A modal
lists counts and the report path.

## Non-goals

- Automated headless CI driving the full GUI suite (unit/integration tests remain
  in `tests/`).
- Exhaustive negative testing of every RPC error code.
- Compat-v1 / `CompatBookContainer` coverage (separate legacy path).

## Extension

When a new public SDK method is added:

1. Call it from the appropriate suite module.
2. Map it in `api-coverage.json`.
3. Run `tests/live_plugin_examples_test.py`.
