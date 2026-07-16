"""Coverage companion: EventsApi.next_event (book-session lifetime)."""


def run(plugin):
    plugin.ui.show_status("Live API Coverage Session: waiting for events (Cancel to stop)")
    plugin.events.subscribe(
        "editor.activeChanged",
        "editor.selectionChanged",
        "editor.cursorChanged",
        "editor.contentChanged",
        "book.resourceChanged",
        "book.resourceAdded",
        "book.resourceRemoved",
    )
    # Also drain any pending notification once via poll before blocking.
    plugin.events.poll()
    count = 0
    try:
        while True:
            event = plugin.events.next_event()
            count += 1
            print("coverage event", count, event.get("name"), event.get("params"))
            plugin.ui.show_status(
                "Coverage Session event #{0}: {1}".format(count, event.get("name"))
            )
    except (ConnectionError, OSError, RuntimeError):
        plugin.ui.show_status(
            "Coverage Session stopped after {0} event(s)".format(count)
        )
        return 0
