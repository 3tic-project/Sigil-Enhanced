import re


EMPTY_TITLE = re.compile(r"<title(?:\s[^>]*)?>\s*</title>", re.IGNORECASE)


def run(plugin):
    resources = list(plugin.book.resources(types=("html",)))
    results = []
    with plugin.ui.progress("Checking XHTML titles", total=len(resources)) as progress:
        for index, resource in enumerate(resources, 1):
            source = plugin.book.read_text(resource)["text"]
            if EMPTY_TITLE.search(source):
                results.append({
                    "type": "warning",
                    "book_path": resource.book_path,
                    "line": 1,
                    "character": 0,
                    "message": "The XHTML title element is empty.",
                })
            progress.update(index, resource.book_path)
    plugin.validation.publish_results(results)
    return 0
