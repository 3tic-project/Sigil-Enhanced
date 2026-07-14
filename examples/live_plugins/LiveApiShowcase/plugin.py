def run(plugin):
    plugin.ping()
    info = plugin.book.get_info()
    metadata = plugin.book.get_metadata()
    manifest = plugin.book.get_manifest()
    spine = plugin.book.get_spine()
    guide = plugin.book.get_guide()
    bindings = plugin.book.get_bindings()
    selected = plugin.book.get_selection()
    revision = plugin.book.get_revision()

    resources = list(plugin.book.resources())
    text_resources = list(plugin.book.text_resources())
    if text_resources:
        first = text_resources[0]
        plugin.book.resolve_path(first.book_path)
        plugin.book.get_resource(first.id)
        plugin.book.read_text(first)
        plugin.book.read_many(text_resources[:10])

    binary = next((item for item in resources if item.resource_type not in {
        "html", "css", "svg", "opf", "ncx", "xml", "text"
    }), None)
    if binary is not None:
        binary_size = next(item["size"] for item in plugin.book.archive_files()
                           if item.get("resource_id") == binary.id)
        if binary_size <= 5 * 1024 * 1024:
            plugin.book.read_binary(binary)
            reader = plugin.book.open_binary(binary)
            try:
                reader.read()
            finally:
                reader.close()
        else:
            with plugin.book.open_binary(binary) as reader:
                next(iter(reader.chunks()), b"")

    plugin.events.subscribe("editor.activeChanged", "book.resourceChanged")
    plugin.events.poll()
    plugin.events.unsubscribe("editor.activeChanged", "book.resourceChanged")

    state = plugin.editor.get_state()
    plugin.editor.get_selection()
    plugin.editor.get_open_tabs()
    if state.active:
        plugin.editor.open_resource(state.resource_id, state.cursor)
        plugin.editor.set_cursor(state.cursor, state.resource_id)
        plugin.editor.set_selection(
            state.selection.start, state.selection.end, state.resource_id
        )
        plugin.editor.reveal_range(
            state.resource_id, state.selection.start, state.selection.end
        )
        if plugin.ui.confirm("Apply three no-op editor edit forms?", "Live API Showcase"):
            state = plugin.editor.get_state()
            plugin.editor.apply_edits(
                [(state.cursor, state.cursor, "")],
                state.revision, state.resource_id, "Live API no-op patch",
            )
            state = plugin.editor.get_state()
            plugin.editor.replace_selection(
                state.selection.text, state.revision, state.resource_id,
                "Live API selection round trip",
            )
            state = plugin.editor.get_state()
            plugin.editor.insert_text(
                "", state.revision, state.resource_id, "Live API empty insert"
            )

    progress = plugin.ui.progress("Reading live Book state", total=1)
    try:
        progress.update(1, "Book state read")
    finally:
        progress.end()
    plugin.ui.show_status("Live API showcase completed")
    plugin.ui.show_message(
        "EPUB {0}; revision {1}; {2} resources; {3} metadata entries; "
        "{4} manifest entries; {5} spine entries; {6} guide entries; "
        "{7} bindings; {8} selected.".format(
            info["epub_version"], revision, len(resources), len(metadata["items"]),
            len(manifest), len(spine["items"]), len(guide), len(bindings), len(selected),
        ),
        "Live API Showcase",
    )
    return 0
