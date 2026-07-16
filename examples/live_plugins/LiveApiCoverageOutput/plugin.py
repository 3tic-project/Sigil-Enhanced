"""Coverage companion: OutputApi + choose_save_file."""


def run(plugin):
    if not plugin.ui.confirm(
        "Run Live API Coverage Output?\n\n"
        "• export_epub via save dialog\n"
        "• save_source if a source path exists",
        "Live API Coverage Output",
    ):
        return 0

    fails = 0

    path = plugin.ui.choose_save_file(
        "LiveApiCoverage-export.epub",
        "Coverage: export EPUB",
        "EPUB (*.epub)",
    )
    if path is None:
        plugin.ui.show_status("export_epub skipped (cancelled)")
    else:
        try:
            plugin.output.export_epub(path)
            plugin.ui.show_status("export_epub OK: {0}".format(path))
        except Exception as exc:
            fails += 1
            plugin.ui.show_message(
                "export_epub failed: {0}".format(exc),
                "Live API Coverage Output",
                "error",
            )

    try:
        plugin.output.save_source()
        plugin.ui.show_status("save_source OK")
    except Exception as exc:
        # Common when the Book was never saved to disk.
        plugin.ui.show_message(
            "save_source did not succeed (often expected if no source path):\n{0}".format(
                exc
            ),
            "Live API Coverage Output",
            "warning",
        )
        # Do not hard-fail the suite for missing source path.
        _ = plugin.output.save_source  # keep attribute reference for tooling

    plugin.ui.show_message(
        "Output coverage finished.\nexport path={0}\nfails={1}".format(path, fails),
        "Live API Coverage Output",
    )
    return 1 if fails else 0
