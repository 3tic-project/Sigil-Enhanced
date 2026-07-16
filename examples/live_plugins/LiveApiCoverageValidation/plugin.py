"""Coverage companion: ValidationApi.publish_results."""


def run(plugin):
    resources = list(plugin.book.resources(types=("html",)))
    results = []
    with plugin.ui.progress("Coverage validation scan", total=max(len(resources), 1)) as progress:
        for index, resource in enumerate(resources, 1):
            text = plugin.book.read_text(resource)["text"]
            if "<title" not in text.lower():
                results.append(
                    {
                        "type": "warning",
                        "book_path": resource.book_path,
                        "line": 1,
                        "character": 0,
                        "message": "LiveApiCoverage: no <title> substring found (demo finding).",
                    }
                )
            progress.update(index, resource.book_path)
        if not resources:
            progress.update(1, "no html")
            results.append(
                {
                    "type": "info",
                    "book_path": "",
                    "line": -1,
                    "character": -1,
                    "message": "LiveApiCoverage: Book has no HTML resources.",
                }
            )

    plugin.validation.publish_results(results)
    plugin.ui.show_status(
        "Validation coverage published {0} result(s)".format(len(results))
    )
    return 0
