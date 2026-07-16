import uuid
from datetime import datetime, timezone

from sigil_live.errors import ResourceNotFound


def require_valid(transaction):
    preview = transaction.preview()
    validation = transaction.validate()
    if not preview["valid"] or not validation["valid"]:
        raise RuntimeError("transaction validation failed: {0}".format(validation))


def run(plugin):
    if not plugin.ui.confirm(
        "Run text, binary, structure, package, and archive transaction probes?\n\n"
        "Created probe files are kept so you can inspect them in Book Browser "
        "and on disk (including unmanifested META-INF files).",
        "Live Transaction Lab",
    ):
        return 0

    token = uuid.uuid4().hex
    manifest_id = "live_probe_" + token
    first_path = "LiveApiProbe/{0}.bin".format(token)
    renamed_path = "LiveApiProbe/{0}-renamed.bin".format(token)
    moved_path = "LiveApiProbeMoved/{0}-renamed.bin".format(token)
    archive_path = "META-INF/live-api-{0}.txt".format(token)
    proof_path = "Misc/LiveTransactionLab-proof.txt"
    proof_id = "live_tx_lab_proof"
    kept = []
    steps = []

    text = next(plugin.book.text_resources(), None)
    if text is not None:
        transaction = plugin.book.transaction("Text rollback example", checkpoint="none")
        current = transaction.read_text(text)
        transaction.replace_text(text, current["text"], current["revision"])
        transaction.apply_edits(text, [(0, 0, "")], current["revision"])
        require_valid(transaction)
        transaction.rollback()
        steps.append("text stage + rollback (no net change)")

    with plugin.ui.progress("Running transaction probes", total=8) as progress:
        transaction = plugin.book.transaction("Add binary probe", checkpoint="required")
        transaction.add_resource(
            first_path, b"probe-v1", "application/octet-stream",
            manifest_id=manifest_id, add_to_spine=False,
        )
        require_valid(transaction)
        transaction.commit()
        progress.update(1)
        steps.append("add binary " + first_path)

        probe = plugin.book.resolve_path(first_path)
        transaction = plugin.book.transaction("Update binary probe")
        transaction.read_binary(probe)
        transaction.write_binary(probe, b"probe-v2", probe.revision)
        require_valid(transaction)
        transaction.commit()
        progress.update(2)
        steps.append("write binary content → probe-v2")

        probe = plugin.book.resolve_path(first_path)
        materialized = plugin.book.materialize_temporary(probe)
        transaction = plugin.book.transaction("Chunked binary rollback")
        writer = transaction.begin_binary_write(probe, len(b"probe-v2"), probe.revision)
        writer.write(b"probe-v2")
        writer.finish()
        transaction.write_binary_file(probe, materialized["path"], probe.revision)
        require_valid(transaction)
        transaction.rollback()
        steps.append("chunked binary write staged then rolled back")

        probe = plugin.book.resolve_path(first_path)
        transaction = plugin.book.transaction("Rename binary probe")
        transaction.rename_resource(probe, renamed_path.rsplit("/", 1)[1], probe.revision)
        require_valid(transaction)
        transaction.commit()
        progress.update(3)
        steps.append("rename → " + renamed_path)

        probe = plugin.book.resolve_path(renamed_path)
        transaction = plugin.book.transaction("Move binary probe")
        transaction.move_resource(probe, moved_path, probe.revision)
        require_valid(transaction)
        transaction.commit()
        progress.update(4)
        steps.append("move → " + moved_path)
        kept.append(moved_path + " (managed binary, content probe-v2)")

        transaction = plugin.book.transaction("Add unmanaged archive probe")
        transaction.add_resource(
            archive_path, b"archive-v1", "text/plain",
            manifested=False, add_to_spine=False,
        )
        require_valid(transaction)
        transaction.commit()
        progress.update(5)
        steps.append("add unmanifested " + archive_path)

        archive = plugin.book.read_archive_file(archive_path)
        transaction = plugin.book.transaction("Replace unmanaged archive probe")
        transaction.replace_archive_file(
            archive_path, b"archive-v2", archive["sha256"]
        )
        require_valid(transaction)
        transaction.commit()
        progress.update(6)
        steps.append("replace archive file → archive-v2")
        kept.append(
            archive_path
            + " (unmanifested; not listed in Book Browser, open via disk/archive API)"
        )

        metadata = plugin.book.get_metadata()
        spine = plugin.book.get_spine()
        transaction = plugin.book.transaction("Structured package round trip")
        transaction.update_metadata(metadata["items"], metadata["revision"])
        transaction.update_spine(
            spine["items"], spine["attributes"], spine["revision"]
        )
        require_valid(transaction)
        transaction.commit()
        progress.update(7)
        steps.append("metadata + spine round trip")

        snapshot = plugin.book.get_compatibility_snapshot()
        package = snapshot["package"]
        transaction = plugin.book.transaction("Package round trip", checkpoint="required")
        transaction.replace_package(
            package["text"], package["resource"]["content_revision"]
        )
        require_valid(transaction)
        transaction.commit()
        steps.append("package replace round trip")

        stamp = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")
        proof_body = (
            "Live Transaction Lab completed successfully.\n"
            "Token: {0}\n"
            "Finished: {1}\n"
            "\n"
            "Kept files:\n- {2}\n"
            "\n"
            "Steps exercised:\n- {3}\n"
        ).format(token, stamp, "\n- ".join(kept), "\n- ".join(steps))

        # resolve_path raises ResourceNotFound when the path is absent.
        try:
            existing = plugin.book.resolve_path(proof_path)
        except ResourceNotFound:
            existing = None

        transaction = plugin.book.transaction(
            "Write Book Browser proof file", checkpoint="required"
        )
        if existing is not None:
            transaction.write_binary(
                existing, proof_body.encode("utf-8"), existing.revision
            )
        else:
            transaction.add_resource(
                proof_path,
                proof_body.encode("utf-8"),
                "text/plain",
                manifest_id=proof_id,
                add_to_spine=False,
            )
        require_valid(transaction)
        transaction.commit()
        progress.update(8)
        kept.append(proof_path + " (managed text summary)")
        steps.append("write proof " + proof_path)

    plugin.ui.show_message(
        "Live Transaction Lab finished. Probe files were kept.\n\n"
        "In Book Browser look for:\n"
        "  • {0}\n"
        "  • {1}\n\n"
        "Unmanifested archive (not in Book Browser):\n"
        "  • {2}\n\n"
        "Steps:\n- {3}".format(
            moved_path, proof_path, archive_path, "\n- ".join(steps)
        ),
        "Live Transaction Lab",
    )
    plugin.ui.show_status(
        "Live Transaction Lab kept {0} and {1}".format(moved_path, proof_path)
    )
    return 0
