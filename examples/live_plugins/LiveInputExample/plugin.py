import os


UPLOAD_MODE = "stream"  # Also supports "file" and "bytes" for SDK convenience APIs.


def run(plugin):
    path = plugin.ui.choose_open_file("Choose EPUB", "EPUB files (*.epub)")
    if path is None:
        return 0
    if UPLOAD_MODE == "file":
        plugin.input.submit_epub_file(path)
        return 0
    if UPLOAD_MODE == "bytes":
        with open(path, "rb") as source:
            plugin.input.submit_epub(source.read(), os.path.basename(path))
        return 0
    size = os.path.getsize(path)
    writer = plugin.input.begin_epub(os.path.basename(path), size)
    with plugin.ui.progress("Uploading EPUB", total=size) as progress:
        with open(path, "rb") as source:
            while True:
                chunk = source.read(writer.chunk_size)
                if not chunk:
                    break
                writer.write(chunk)
                progress.update(writer.received)
        writer.finish()
    return 0
