import os
import zipfile


TEXT_TYPES = {"html", "css", "svg", "opf", "ncx", "xml", "text"}
EXPORT_MODE = "native"  # Use "source" to save in place or "stream" for manual ZIP output.


def write_entry(plugin, archive, item):
    book_path = item["book_path"]
    resource_id = item.get("resource_id")
    if resource_id:
        resource = plugin.book.get_resource(resource_id)
        if resource.resource_type in TEXT_TYPES:
            archive.writestr(book_path, plugin.book.read_text(resource)["text"].encode("utf-8"))
            return
    with plugin.book.open_archive_file(book_path) as reader:
        with archive.open(book_path, "w") as target:
            for chunk in reader.chunks():
                target.write(chunk)


def run(plugin):
    info = plugin.book.get_info()
    current = info["file_path"]
    if EXPORT_MODE == "source":
        if not current:
            plugin.ui.show_message(
                "Save the Book in Sigil before using source mode.",
                "Live Output Example", "warning",
            )
            return 1
        result = plugin.output.save_source()
        return 0 if result["exported"] else 1

    stem = os.path.splitext(os.path.basename(current or "book.epub"))[0]
    destination = plugin.ui.choose_save_file(
        stem + "-live.epub", "Export current live Book", "EPUB files (*.epub)"
    )
    if destination is None:
        return 0
    if EXPORT_MODE == "native":
        result = plugin.output.export_epub(destination)
        return 0 if result["exported"] else 1

    files = list(plugin.book.archive_files())
    mimetype = next(item for item in files if item["book_path"] == "mimetype")
    remaining = [item for item in files if item is not mimetype]
    with plugin.ui.progress("Exporting EPUB", total=len(files)) as progress:
        with zipfile.ZipFile(destination, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            archive.writestr(
                "mimetype", b"application/epub+zip", compress_type=zipfile.ZIP_STORED
            )
            progress.update(1, "mimetype")
            for index, item in enumerate(remaining, 2):
                write_entry(plugin, archive, item)
                progress.update(index, item["book_path"])
    plugin.ui.show_status("Exported {0}".format(destination), 10000)
    return 0
