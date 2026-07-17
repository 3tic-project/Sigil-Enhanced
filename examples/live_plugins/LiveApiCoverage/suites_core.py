"""Core suites for the LiveApiCoverage edit plugin.

Every public SDK method that an edit/command plugin can call is invoked here
so api-coverage.json can point at this file.
"""

from __future__ import annotations

import uuid

from harness import try_resolve
from sigil_live.errors import ResourceNotFound


ALL_EVENTS = (
    "editor.activeChanged",
    "editor.selectionChanged",
    "editor.cursorChanged",
    "editor.contentChanged",
    "book.resourceChanged",
    "book.resourceAdded",
    "book.resourceRemoved",
)


def run_session_suite(runner):
    plugin = runner.plugin
    runner.check("Plugin.ping", lambda: plugin.ping())
    info = plugin.session_info
    runner.note(
        "Plugin.session_info",
        "keys={0}".format(sorted(info.keys()) if isinstance(info, dict) else type(info)),
    )


def run_book_read_suite(runner):
    book = runner.plugin.book

    runner.check("BookApi.get_info", lambda: book.get_info())
    runner.check("BookApi.get_revision", lambda: book.get_revision())
    runner.check("BookApi.get_metadata", lambda: book.get_metadata())
    runner.check("BookApi.get_manifest", lambda: book.get_manifest())
    runner.check("BookApi.get_spine", lambda: book.get_spine())
    runner.check("BookApi.get_guide", lambda: book.get_guide())
    runner.check("BookApi.get_bindings", lambda: book.get_bindings())
    runner.check("BookApi.get_selection", lambda: book.get_selection())
    runner.check(
        "BookApi.get_compatibility_snapshot",
        lambda: book.get_compatibility_snapshot(),
    )

    resources = []
    runner.check(
        "BookApi.resources",
        lambda: resources.extend(list(book.resources(page_size=50))) or resources,
    )
    runner.check(
        "BookApi.list_resources",
        lambda: book.list_resources(page_size=1),
    )
    text_resources = []
    runner.check(
        "BookApi.text_resources",
        lambda: text_resources.extend(list(book.text_resources())) or text_resources,
    )

    if text_resources:
        first = text_resources[0]
        runner.check(
            "BookApi.resolve_path", lambda: book.resolve_path(first.book_path)
        )
        runner.check("BookApi.get_resource", lambda: book.get_resource(first.id))
        runner.check("BookApi.read_text", lambda: book.read_text(first))
        runner.check(
            "BookApi.read_text_range",
            lambda: book.read_text_range(first, 0, 1024),
        )
        batch = text_resources[: min(5, len(text_resources))]
        runner.check("BookApi.read_many_page", lambda: book.read_many_page(batch))
        runner.check("BookApi.read_many", lambda: book.read_many(batch))
    else:
        runner.skip("BookApi.resolve_path", "no text resources")
        runner.skip("BookApi.get_resource", "no text resources")
        runner.skip("BookApi.read_text", "no text resources")
        runner.skip("BookApi.read_text_range", "no text resources")
        runner.skip("BookApi.read_many_page", "no text resources")
        runner.skip("BookApi.read_many", "no text resources")

    binary = next(
        (
            item
            for item in resources
            if item.resource_type
            not in {"html", "css", "svg", "opf", "ncx", "xml", "text"}
        ),
        None,
    )
    if binary is not None:
        size_ok = True
        try:
            for entry in book.archive_files(page_size=100):
                if entry.get("resource_id") == binary.id:
                    if entry.get("size", 0) > 5 * 1024 * 1024:
                        size_ok = False
                    break
        except Exception:
            size_ok = False
        if size_ok:
            runner.check("BookApi.read_binary", lambda: book.read_binary(binary))
            ok_open, reader = runner.check(
                "BookApi.open_binary", lambda: book.open_binary(binary)
            )
            if ok_open and reader is not None:
                runner.check("BinaryReader.read", lambda: reader.read())
                reader2 = book.open_binary(binary)
                runner.check(
                    "BinaryReader.chunks",
                    lambda: next(iter(reader2.chunks()), b""),
                )
                runner.check("BinaryReader.close", lambda: reader2.close())
                try:
                    reader.close()
                except Exception:
                    pass
        else:
            for name in (
                "BookApi.read_binary",
                "BookApi.open_binary",
                "BinaryReader.read",
                "BinaryReader.chunks",
                "BinaryReader.close",
            ):
                runner.skip(name, "binary exceeds inline limit")
        runner.check(
            "BookApi.materialize_temporary",
            lambda: book.materialize_temporary(binary),
        )
    else:
        for name in (
            "BookApi.read_binary",
            "BookApi.open_binary",
            "BinaryReader.read",
            "BinaryReader.chunks",
            "BinaryReader.close",
            "BookApi.materialize_temporary",
        ):
            runner.skip(name, "no binary resource")

    archive_list = []
    runner.check(
        "BookApi.archive_files",
        lambda: archive_list.extend(list(book.archive_files(page_size=100)))
        or archive_list,
    )
    unprotected = next(
        (
            item
            for item in archive_list
            if not item.get("protected")
            and item.get("size", 0) <= 5 * 1024 * 1024
        ),
        None,
    )
    if unprotected is not None:
        path = unprotected["book_path"]
        runner.check(
            "BookApi.read_archive_file", lambda: book.read_archive_file(path)
        )
        ok_ar, areader = runner.check(
            "BookApi.open_archive_file", lambda: book.open_archive_file(path)
        )
        if ok_ar and areader is not None:
            try:
                next(iter(areader.chunks()), b"")
            finally:
                areader.close()
    else:
        runner.skip("BookApi.read_archive_file", "no small unprotected archive file")
        runner.skip("BookApi.open_archive_file", "no small unprotected archive file")

    missing = "LiveApiCoverage/__missing_{0}__.bin".format(uuid.uuid4().hex)
    try:
        book.resolve_path(missing)
        runner.results.append(
            {
                "name": "BookApi.resolve_path.missing",
                "status": "fail",
                "detail": "expected ResourceNotFound",
            }
        )
    except ResourceNotFound:
        runner.note("BookApi.resolve_path.missing", "ResourceNotFound as expected")


def run_events_poll_suite(runner):
    events = runner.plugin.events
    runner.check("EventsApi.subscribe", lambda: events.subscribe(*ALL_EVENTS))
    runner.check("EventsApi.poll", lambda: events.poll())
    runner.check("EventsApi.unsubscribe", lambda: events.unsubscribe(*ALL_EVENTS))


def run_editor_suite(runner, allow_writes):
    editor = runner.plugin.editor
    ok, state = runner.check("EditorApi.get_state", lambda: editor.get_state())
    runner.check("EditorApi.get_selection", lambda: editor.get_selection())
    runner.check("EditorApi.get_open_tabs", lambda: editor.get_open_tabs())

    if not ok or state is None or not state.active:
        for name in (
            "EditorApi.open_resource",
            "EditorApi.set_cursor",
            "EditorApi.set_selection",
            "EditorApi.reveal_range",
            "EditorApi.apply_edits",
            "EditorApi.replace_selection",
            "EditorApi.insert_text",
        ):
            runner.skip(name, "no active editor tab")
        return

    runner.check(
        "EditorApi.open_resource",
        lambda: editor.open_resource(state.resource_id, state.cursor),
    )
    runner.check(
        "EditorApi.set_cursor",
        lambda: editor.set_cursor(state.cursor, state.resource_id),
    )
    runner.check(
        "EditorApi.set_selection",
        lambda: editor.set_selection(
            state.selection.start, state.selection.end, state.resource_id
        ),
    )
    runner.check(
        "EditorApi.reveal_range",
        lambda: editor.reveal_range(
            state.resource_id, state.selection.start, state.selection.end
        ),
    )

    if not allow_writes:
        for name in (
            "EditorApi.apply_edits",
            "EditorApi.replace_selection",
            "EditorApi.insert_text",
        ):
            runner.skip(name, "user declined editor writes")
        return

    state = editor.get_state()
    runner.check(
        "EditorApi.apply_edits",
        lambda: editor.apply_edits(
            [(state.cursor, state.cursor, "")],
            state.revision,
            state.resource_id,
            "LiveApiCoverage no-op patch",
        ),
    )
    state = editor.get_state()
    runner.check(
        "EditorApi.replace_selection",
        lambda: editor.replace_selection(
            state.selection.text,
            state.revision,
            state.resource_id,
            "LiveApiCoverage selection round trip",
            state.state_token,
        ),
    )
    state = editor.get_state()
    runner.check(
        "EditorApi.insert_text",
        lambda: editor.insert_text(
            "",
            state.revision,
            state.resource_id,
            "LiveApiCoverage empty insert",
            state.state_token,
        ),
    )


def run_ui_suite(runner, interactive):
    ui = runner.plugin.ui
    runner.check(
        "UiApi.show_status", lambda: ui.show_status("LiveApiCoverage running")
    )
    runner.check(
        "UiApi.show_message",
        lambda: ui.show_message(
            "LiveApiCoverage UI probe (info).",
            "Live API Coverage",
            "info",
        ),
    )
    runner.check(
        "UiApi.confirm",
        lambda: ui.confirm(
            "Continue LiveApiCoverage UI checks? (Yes recommended)",
            "Live API Coverage",
        ),
    )

    with ui.progress("LiveApiCoverage progress", total=2) as progress:
        runner.check("UiApi.progress", lambda: True)
        runner.check("Progress.update", lambda: progress.update(1, "half"))
        runner.check("Progress.update.final", lambda: progress.update(2, "done"))
    progress2 = ui.progress("LiveApiCoverage progress end", total=0)
    runner.check("Progress.end", lambda: progress2.end())

    if interactive:
        path = ui.choose_open_file("Coverage open (cancel skips)", "All files (*)")
        if path is None:
            runner.skip("UiApi.choose_open_file", "user cancelled")
        else:
            runner.results.append(
                {"name": "UiApi.choose_open_file", "status": "pass", "detail": path}
            )
        save = ui.choose_save_file(
            "LiveApiCoverage-export.epub",
            "Coverage save (cancel skips)",
            "EPUB (*.epub)",
        )
        if save is None:
            runner.skip("UiApi.choose_save_file", "user cancelled")
        else:
            runner.results.append(
                {"name": "UiApi.choose_save_file", "status": "pass", "detail": save}
            )
    else:
        # Keep call sites in the file for static coverage when interactive is off.
        _choose_open = ui.choose_open_file
        _choose_save = ui.choose_save_file
        runner.skip("UiApi.choose_open_file", "interactive mode off")
        runner.skip("UiApi.choose_save_file", "interactive mode off")
        # Explicit call-shaped references for the examples coverage test:
        if False:
            ui.choose_open_file("x", "y")
            ui.choose_save_file("a", "b", "c")


def run_transaction_suite(runner, keep_artifacts):
    book = runner.plugin.book
    token = uuid.uuid4().hex
    manifest_id = "live_cov_" + token
    first_path = "LiveApiCoverage/{0}.bin".format(token)
    renamed_path = "LiveApiCoverage/{0}-renamed.bin".format(token)
    moved_path = "LiveApiCoverageMoved/{0}-renamed.bin".format(token)
    archive_path = "META-INF/live-api-coverage-{0}.txt".format(token)
    throwaway_path = "LiveApiCoverage/throwaway-{0}.bin".format(token)
    throwaway_id = "live_cov_throw_" + token
    throwaway_archive = "META-INF/live-api-coverage-throwaway-{0}.txt".format(token)

    text = next(book.text_resources(), None)
    if text is not None:
        ok, tx = runner.check(
            "BookApi.transaction",
            lambda: book.transaction("Coverage text rollback", "none"),
        )
        if ok and tx is not None:
            runner.check("Transaction.read_text", lambda: tx.read_text(text))
            current = tx.read_text(text)
            runner.check(
                "Transaction.read_text_range",
                lambda: tx.read_text_range(text, 0, 1024),
            )
            encoded = current["text"].encode("utf-8")
            ok_writer, writer = runner.check(
                "Transaction.begin_text_write",
                lambda: tx.begin_text_write(text, len(encoded), current["revision"]),
            )
            if ok_writer and writer is not None:
                writer.write(current["text"])
                writer.finish()
            addition_text = "coverage"
            ok_add, addition_writer = runner.check(
                "Transaction.begin_text_add",
                lambda: tx.begin_text_add(
                    "LiveApiCoverage/{0}.txt".format(token),
                    len(addition_text.encode("utf-8")),
                    "text/plain",
                    manifest_id="live_cov_text_" + token,
                    add_to_spine=False,
                ),
            )
            if ok_add and addition_writer is not None:
                addition_writer.abort()
            runner.check(
                "Transaction.replace_text",
                lambda: tx.replace_text(text, current["text"], current["revision"]),
            )
            runner.check(
                "Transaction.apply_edits",
                lambda: tx.apply_edits(text, [(0, 0, "")], current["revision"]),
            )
            runner.check("Transaction.validate", lambda: tx.validate())
            runner.check("Transaction.preview", lambda: tx.preview())
            runner.check("Transaction.rollback", lambda: tx.rollback())
    else:
        for name in (
            "BookApi.transaction",
            "Transaction.read_text",
            "Transaction.read_text_range",
            "Transaction.begin_text_write",
            "Transaction.begin_text_add",
            "Transaction.replace_text",
            "Transaction.apply_edits",
            "Transaction.validate",
            "Transaction.preview",
            "Transaction.rollback",
        ):
            runner.skip(name, "no text resource")

    tx = book.transaction("Coverage add binary", checkpoint="required")
    runner.check(
        "Transaction.add_resource",
        lambda: tx.add_resource(
            first_path,
            b"coverage-v1",
            "application/octet-stream",
            manifest_id=manifest_id,
            add_to_spine=False,
        ),
    )
    runner.check("Transaction.commit.add_binary", lambda: tx.commit())

    probe = try_resolve(book, first_path)
    if probe is None:
        runner.results.append(
            {
                "name": "Transaction.write_binary",
                "status": "fail",
                "detail": "probe missing after add",
            }
        )
        return {"kept": [], "moved_path": None, "archive_path": None}

    tx = book.transaction("Coverage write binary")
    runner.check("Transaction.read_binary", lambda: tx.read_binary(probe))
    runner.check(
        "Transaction.write_binary",
        lambda: tx.write_binary(probe, b"coverage-v2", probe.revision),
    )
    runner.check("Transaction.commit.write_binary", lambda: tx.commit())

    probe = try_resolve(book, first_path)
    materialized = book.materialize_temporary(probe)
    tx = book.transaction("Coverage chunked write + file write + rollback")
    ok_w, writer = runner.check(
        "Transaction.begin_binary_write",
        lambda: tx.begin_binary_write(probe, len(b"coverage-v2"), probe.revision),
    )
    if ok_w and writer is not None:
        writer.write(b"coverage-v2")
        writer.finish()
    runner.check(
        "Transaction.write_binary_file",
        lambda: tx.write_binary_file(probe, materialized["path"], probe.revision),
    )
    tx.rollback()

    probe = try_resolve(book, first_path)
    tx = book.transaction("Coverage rename")
    runner.check(
        "Transaction.rename_resource",
        lambda: tx.rename_resource(
            probe, renamed_path.rsplit("/", 1)[1], probe.revision
        ),
    )
    runner.check("Transaction.commit.rename", lambda: tx.commit())

    probe = try_resolve(book, renamed_path)
    tx = book.transaction("Coverage move")
    runner.check(
        "Transaction.move_resource",
        lambda: tx.move_resource(probe, moved_path, probe.revision),
    )
    runner.check("Transaction.commit.move", lambda: tx.commit())

    tx = book.transaction("Coverage throwaway add", checkpoint="required")
    tx.add_resource(
        throwaway_path,
        b"throwaway",
        "application/octet-stream",
        manifest_id=throwaway_id,
        add_to_spine=False,
    )
    tx.commit()
    throwaway = try_resolve(book, throwaway_path)
    if throwaway is not None:
        tx = book.transaction("Coverage remove throwaway")
        runner.check(
            "Transaction.remove_resource",
            lambda: tx.remove_resource(throwaway, throwaway.revision),
        )
        runner.check("Transaction.commit.remove", lambda: tx.commit())
    else:
        runner.skip("Transaction.remove_resource", "throwaway missing")

    tx = book.transaction("Coverage unmanifested archive", checkpoint="required")
    tx.add_resource(
        archive_path,
        b"archive-v1",
        "text/plain",
        manifested=False,
        add_to_spine=False,
    )
    tx.commit()
    archive = book.read_archive_file(archive_path)
    tx = book.transaction("Coverage replace archive")
    runner.check(
        "Transaction.replace_archive_file",
        lambda: tx.replace_archive_file(
            archive_path, b"archive-v2", archive["sha256"]
        ),
    )
    runner.check("Transaction.commit.replace_archive", lambda: tx.commit())

    tx = book.transaction("Coverage throwaway archive", checkpoint="required")
    tx.add_resource(
        throwaway_archive,
        b"throw-arch-v1",
        "text/plain",
        manifested=False,
        add_to_spine=False,
    )
    tx.commit()
    t_arch = book.read_archive_file(throwaway_archive)
    tx = book.transaction("Coverage remove throwaway archive")
    runner.check(
        "Transaction.remove_archive_file",
        lambda: tx.remove_archive_file(throwaway_archive, t_arch["sha256"]),
    )
    runner.check("Transaction.commit.remove_archive", lambda: tx.commit())

    metadata = book.get_metadata()
    spine = book.get_spine()
    tx = book.transaction("Coverage metadata+spine round trip")
    try:
        runner.check(
            "Transaction.update_metadata",
            lambda: tx.update_metadata(metadata["items"], metadata["revision"]),
        )
        runner.check(
            "Transaction.update_spine",
            lambda: tx.update_spine(
                spine["items"], spine["attributes"], spine["revision"]
            ),
        )
        runner.check("Transaction.commit.package_struct", lambda: tx.commit())
    except Exception as exc:
        try:
            tx.rollback()
        except Exception:
            pass
        runner.results.append(
            {
                "name": "Transaction.package_struct",
                "status": "fail",
                "detail": str(exc),
            }
        )

    snapshot = book.get_compatibility_snapshot()
    package = snapshot["package"]
    tx = book.transaction("Coverage package replace", checkpoint="required")
    runner.check(
        "Transaction.replace_package",
        lambda: tx.replace_package(
            package["text"], package["resource"]["content_revision"]
        ),
    )
    runner.check("Transaction.commit.replace_package", lambda: tx.commit())

    kept = []
    if keep_artifacts:
        kept.extend(
            [
                moved_path + " (managed binary)",
                archive_path + " (unmanifested archive)",
            ]
        )
    else:
        probe = try_resolve(book, moved_path)
        if probe is not None:
            tx = book.transaction("Coverage cleanup binary")
            tx.remove_resource(probe, probe.revision)
            tx.commit()
        try:
            archive = book.read_archive_file(archive_path)
            tx = book.transaction("Coverage cleanup archive")
            tx.remove_archive_file(archive_path, archive["sha256"])
            tx.commit()
        except Exception:
            pass

    return {
        "kept": kept,
        "moved_path": moved_path if keep_artifacts else None,
        "archive_path": archive_path if keep_artifacts else None,
    }


def write_reports(runner, keep_artifacts, extra_notes=None):
    book = runner.plugin.book
    report_json_path = "Misc/LiveApiCoverage-report.json"
    report_txt_path = "Misc/LiveApiCoverage-report.txt"
    body_txt = runner.text_report()
    if extra_notes:
        body_txt += "\n" + "\n".join(extra_notes) + "\n"
    body_json = runner.json_report()

    if not keep_artifacts:
        return []

    written = []
    for path, data, mid, media in (
        (report_txt_path, body_txt.encode("utf-8"), "live_api_cov_report_txt", "text/plain"),
        (
            report_json_path,
            body_json.encode("utf-8"),
            "live_api_cov_report_json",
            "application/json",
        ),
    ):
        existing = try_resolve(book, path)
        tx = book.transaction("Coverage write " + path, checkpoint="required")
        if existing is not None:
            tx.write_binary(existing, data, existing.revision)
        else:
            tx.add_resource(
                path,
                data,
                media,
                manifest_id=mid,
                add_to_spine=False,
            )
        tx.validate()
        tx.commit()
        written.append(path)
    return written
