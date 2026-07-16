"""Live API Coverage — edit/command plugin covering the bulk of sigil_live."""

from harness import CoverageRunner
from suites_core import (
    run_book_read_suite,
    run_editor_suite,
    run_events_poll_suite,
    run_session_suite,
    run_transaction_suite,
    run_ui_suite,
    write_reports,
)


def run(plugin):
    if not plugin.ui.confirm(
        "Run Live API Coverage (edit suites)?\n\n"
        "• Exercises Book / Editor / UI / Events(poll) / Transaction\n"
        "• Keeps probe files and writes Misc/LiveApiCoverage-report.*\n"
        "• Companion plugins cover input / output / validation / next_event\n\n"
        "Continue?",
        "Live API Coverage",
    ):
        return 0

    keep_artifacts = plugin.ui.confirm(
        "Keep probe files and write coverage reports into the Book?",
        "Live API Coverage",
    )
    interactive = plugin.ui.confirm(
        "Also run interactive file dialogs (choose open/save)?\n"
        "Cancel inside a dialog marks that check as skip.",
        "Live API Coverage",
    )
    editor_writes = plugin.ui.confirm(
        "Allow no-op editor writes (apply_edits / replace_selection / insert_text)?",
        "Live API Coverage",
    )

    runner = CoverageRunner(plugin, "edit-command")
    notes = []

    # Host allows only one progress operation at a time. Keep this outer bar for
    # non-UI suites; run_ui_suite exercises UiApi.progress / Progress.* alone.
    with plugin.ui.progress("Live API Coverage", total=5) as progress:
        run_session_suite(runner)
        progress.update(1, "session")

        run_book_read_suite(runner)
        progress.update(2, "book read")

        run_events_poll_suite(runner)
        progress.update(3, "events")

        run_editor_suite(runner, allow_writes=editor_writes)
        progress.update(4, "editor")

        tx_info = run_transaction_suite(runner, keep_artifacts=keep_artifacts)
        progress.update(5, "transaction")

    run_ui_suite(runner, interactive=interactive)

    if tx_info.get("kept"):
        notes.append("Kept:")
        notes.extend("  - " + item for item in tx_info["kept"])

    written = write_reports(runner, keep_artifacts, notes)
    if written:
        notes.append("Reports:")
        notes.extend("  - " + path for path in written)

    counts = runner.counts()
    summary = (
        "Live API Coverage (edit) finished.\n\n"
        "pass={pass}  fail={fail}  skip={skip}\n\n"
        "Also install and run the companion plugins for full SDK coverage:\n"
        "  • Live API Coverage Input\n"
        "  • Live API Coverage Output\n"
        "  • Live API Coverage Validation\n"
        "  • Live API Coverage Session (book-session / next_event)\n\n"
        "{0}"
    ).format("\n".join(notes) if notes else "(no artifacts kept)")
    summary = summary.format(**counts)

    plugin.ui.show_message(summary, "Live API Coverage")
    plugin.ui.show_status(
        "Coverage pass={pass} fail={fail} skip={skip}".format(**counts)
    )
    return 1 if runner.failed() else 0
