def run(plugin):
    plugin.ui.show_status("Live Book Watcher is running; use Cancel to stop it.")
    plugin.events.subscribe(
        "editor.activeChanged",
        "editor.selectionChanged",
        "editor.cursorChanged",
        "editor.contentChanged",
        "book.resourceChanged",
        "book.resourceAdded",
        "book.resourceRemoved",
    )
    try:
        while True:
            event = plugin.events.next_event()
            print(event["name"], event["params"])
    except (ConnectionError, OSError):
        return 0
