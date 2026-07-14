import uuid


def require_valid(transaction):
    preview = transaction.preview()
    validation = transaction.validate()
    if not preview["valid"] or not validation["valid"]:
        raise RuntimeError("transaction validation failed: {0}".format(validation))


def run(plugin):
    if not plugin.ui.confirm(
        "Run disposable text, binary, structure, package, and archive transactions?",
        "Live Transaction Lab",
    ):
        return 0

    token = uuid.uuid4().hex
    manifest_id = "live_probe_" + token
    first_path = "LiveApiProbe/{0}.bin".format(token)
    renamed_path = "LiveApiProbe/{0}-renamed.bin".format(token)
    moved_path = "LiveApiProbeMoved/{0}-renamed.bin".format(token)
    archive_path = "META-INF/live-api-{0}.txt".format(token)

    text = next(plugin.book.text_resources(), None)
    if text is not None:
        transaction = plugin.book.transaction("Text rollback example", checkpoint="none")
        current = transaction.read_text(text)
        transaction.replace_text(text, current["text"], current["revision"])
        transaction.apply_edits(text, [(0, 0, "")], current["revision"])
        require_valid(transaction)
        transaction.rollback()

    with plugin.ui.progress("Running transaction probes", total=8) as progress:
        transaction = plugin.book.transaction("Add binary probe", checkpoint="required")
        transaction.add_resource(
            first_path, b"probe-v1", "application/octet-stream",
            manifest_id=manifest_id, add_to_spine=False,
        )
        require_valid(transaction)
        transaction.commit()
        progress.update(1)

        probe = plugin.book.resolve_path(first_path)
        transaction = plugin.book.transaction("Update binary probe")
        transaction.read_binary(probe)
        transaction.write_binary(probe, b"probe-v2", probe.revision)
        require_valid(transaction)
        transaction.commit()
        progress.update(2)

        probe = plugin.book.resolve_path(first_path)
        transaction = plugin.book.transaction("Rename binary probe")
        transaction.rename_resource(probe, renamed_path.rsplit("/", 1)[1], probe.revision)
        require_valid(transaction)
        transaction.commit()
        progress.update(3)

        probe = plugin.book.resolve_path(renamed_path)
        transaction = plugin.book.transaction("Move binary probe")
        transaction.move_resource(probe, moved_path, probe.revision)
        require_valid(transaction)
        transaction.commit()
        progress.update(4)

        probe = plugin.book.resolve_path(moved_path)
        transaction = plugin.book.transaction("Remove binary probe")
        transaction.remove_resource(probe, probe.revision)
        require_valid(transaction)
        transaction.commit()
        progress.update(5)

        transaction = plugin.book.transaction("Add unmanaged archive probe")
        transaction.add_resource(
            archive_path, b"archive-v1", "text/plain",
            manifested=False, add_to_spine=False,
        )
        require_valid(transaction)
        transaction.commit()
        progress.update(6)

        archive = plugin.book.read_archive_file(archive_path)
        transaction = plugin.book.transaction("Replace unmanaged archive probe")
        transaction.replace_archive_file(
            archive_path, b"archive-v2", archive["sha256"]
        )
        require_valid(transaction)
        transaction.commit()
        progress.update(7)

        archive = plugin.book.read_archive_file(archive_path)
        transaction = plugin.book.transaction("Remove unmanaged archive probe")
        transaction.remove_archive_file(archive_path, archive["sha256"])
        require_valid(transaction)
        transaction.commit()

        snapshot = plugin.book.get_compatibility_snapshot()
        package = snapshot["package"]
        transaction = plugin.book.transaction("Package round trip", checkpoint="required")
        transaction.replace_package(
            package["text"], package["resource"]["content_revision"]
        )
        require_valid(transaction)
        transaction.commit()
        progress.update(8)

    plugin.ui.show_message("All disposable transaction probes completed.")
    return 0
