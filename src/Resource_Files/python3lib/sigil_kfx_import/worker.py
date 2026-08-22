#!/usr/bin/env python3
"""Machine-readable, single-book KFX to EPUB conversion worker.

stdout is reserved for versioned JSON Lines protocol events. Diagnostic
tracebacks are written to stderr so the Qt controller can keep user-facing
errors stable and localized.
"""

from __future__ import annotations

import argparse
import errno
import json
import logging
import os
import posixpath
from pathlib import Path, PurePosixPath
import sys
import tempfile
import time
import traceback
import uuid
import zipfile
from xml.etree import ElementTree


PROTOCOL_VERSION = 1
MAX_INPUT_BYTES = 2 * 1024 * 1024 * 1024
MAX_ARCHIVE_ENTRIES = 100_000
MAX_ARCHIVE_UNCOMPRESSED_BYTES = 4 * 1024 * 1024 * 1024
MAX_COMPRESSION_RATIO = 1_000

# JSON Lines protocol must never share a stream with converter prints/logs.
PROTOCOL_STDOUT = sys.__stdout__


def _safe_text(value) -> str:
    return str(value).encode("utf-8", "replace").decode("utf-8")


def _json_safe(value):
    if isinstance(value, str):
        return _safe_text(value)
    if isinstance(value, dict):
        return {_safe_text(key): _json_safe(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [_json_safe(item) for item in value]
    return value


def emit(event: str, **payload) -> None:
    record = {"protocol": PROTOCOL_VERSION, "event": event}
    record.update(_json_safe(payload))
    PROTOCOL_STDOUT.write(json.dumps(record, ensure_ascii=True, separators=(",", ":")) + "\n")
    PROTOCOL_STDOUT.flush()


def configure_worker_logging() -> None:
    handler = logging.StreamHandler(sys.stderr)
    handler.setFormatter(logging.Formatter("%(levelname)s %(message)s"))
    root = logging.getLogger()
    root.handlers.clear()
    root.addHandler(handler)
    root.setLevel(logging.WARNING)
    kfx_logger = logging.getLogger("sigil-kfx")
    kfx_logger.handlers.clear()
    kfx_logger.addHandler(handler)
    kfx_logger.setLevel(logging.INFO)
    kfx_logger.propagate = False


def _safe_archive_name(name: str) -> bool:
    if not name or "\\" in name or name.startswith("/"):
        return False
    path = PurePosixPath(name)
    return not path.is_absolute() and ".." not in path.parts


def preflight_input(path: Path) -> str:
    if not path.is_file():
        raise WorkerError("KFX-E-INPUT", "Input is not a readable local file.")
    try:
        size = path.stat().st_size
    except OSError as exc:
        raise WorkerError("KFX-E-INPUT", str(exc)) from exc
    if size <= 0:
        raise WorkerError("KFX-E-INPUT", "Input file is empty.")
    if size > MAX_INPUT_BYTES:
        raise WorkerError("KFX-E-INPUT", "Input exceeds the safe size limit.")

    lowered = path.name.lower()
    if not (lowered.endswith(".kfx") or lowered.endswith(".kfx-zip")):
        raise WorkerError("KFX-E-INPUT", "Input must use .kfx or .kfx-zip.")

    with path.open("rb") as stream:
        signature = stream.read(8)
    if signature.startswith(b"CONT"):
        return "kfx"
    if not signature.startswith(b"PK"):
        raise WorkerError("KFX-E-INPUT", "Input does not contain a KFX container or ZIP archive.")

    try:
        with zipfile.ZipFile(path) as archive:
            infos = archive.infolist()
            if not infos or len(infos) > MAX_ARCHIVE_ENTRIES:
                raise WorkerError("KFX-E-INPUT", "KFX-ZIP has an unsafe number of entries.")
            total = 0
            for info in infos:
                if not _safe_archive_name(info.filename):
                    raise WorkerError("KFX-E-INPUT", "KFX-ZIP contains an unsafe path.")
                if info.flag_bits & 0x1:
                    raise WorkerError("KFX-E-UNSUPPORTED", "Encrypted ZIP members are not supported.")
                total += info.file_size
                if total > MAX_ARCHIVE_UNCOMPRESSED_BYTES:
                    raise WorkerError("KFX-E-INPUT", "KFX-ZIP exceeds the safe expanded-size limit.")
                if info.compress_size > 0 and info.file_size > 0:
                    if info.file_size / info.compress_size > MAX_COMPRESSION_RATIO:
                        raise WorkerError("KFX-E-INPUT", "KFX-ZIP exceeds the safe compression-ratio limit.")
    except WorkerError:
        raise
    except (OSError, zipfile.BadZipFile, zipfile.LargeZipFile) as exc:
        raise WorkerError("KFX-E-MALFORMED", f"Invalid KFX-ZIP archive: {exc}") from exc
    return "kfx-zip"


def _local_name(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def _resolve_member(base: str, href: str) -> str:
    href_path = href.split("#", 1)[0].split("?", 1)[0]
    joined = posixpath.normpath(posixpath.join(base, href_path))
    if joined == ".." or joined.startswith("../") or not _safe_archive_name(joined):
        raise WorkerError("KFX-E-VALIDATE", "Generated EPUB contains an unsafe resource path.")
    return joined


def validate_epub(path: Path) -> dict:
    try:
        with zipfile.ZipFile(path) as archive:
            infos = archive.infolist()
            names = {info.filename for info in infos}
            if not infos or infos[0].filename != "mimetype":
                raise WorkerError("KFX-E-VALIDATE", "EPUB mimetype is not the first ZIP entry.")
            if infos[0].compress_type != zipfile.ZIP_STORED:
                raise WorkerError("KFX-E-VALIDATE", "EPUB mimetype must be uncompressed.")
            if archive.read("mimetype") != b"application/epub+zip":
                raise WorkerError("KFX-E-VALIDATE", "EPUB mimetype content is invalid.")
            if "META-INF/container.xml" not in names:
                raise WorkerError("KFX-E-VALIDATE", "EPUB container.xml is missing.")

            container = ElementTree.fromstring(archive.read("META-INF/container.xml"))
            rootfiles = [node for node in container.iter() if _local_name(node.tag) == "rootfile"]
            if not rootfiles:
                raise WorkerError("KFX-E-VALIDATE", "EPUB container has no rootfile.")
            opf_path = rootfiles[0].attrib.get("full-path", "")
            if not _safe_archive_name(opf_path) or opf_path not in names:
                raise WorkerError("KFX-E-VALIDATE", "EPUB package document is missing or unsafe.")

            opf = ElementTree.fromstring(archive.read(opf_path))
            opf_base = posixpath.dirname(opf_path)
            manifest = {}
            media_types = {}
            nav_count = 0
            title = ""
            authors = []
            language = ""
            for node in opf.iter():
                name = _local_name(node.tag)
                if name == "item":
                    item_id = node.attrib.get("id", "")
                    href = node.attrib.get("href", "")
                    if not item_id or not href:
                        raise WorkerError("KFX-E-VALIDATE", "EPUB manifest contains an incomplete item.")
                    member = _resolve_member(opf_base, href)
                    if member not in names:
                        raise WorkerError("KFX-E-VALIDATE", f"EPUB manifest resource is missing: {member}")
                    manifest[item_id] = member
                    media_types[item_id] = node.attrib.get("media-type", "")
                    if "nav" in node.attrib.get("properties", "").split():
                        nav_count += 1
                elif name == "title" and not title:
                    title = (node.text or "").strip()
                elif name == "creator":
                    author = (node.text or "").strip()
                    if author:
                        authors.append(author)
                elif name == "language" and not language:
                    language = (node.text or "").strip()

            spine_ids = []
            for node in opf.iter():
                if _local_name(node.tag) == "itemref":
                    item_id = node.attrib.get("idref", "")
                    if item_id not in manifest:
                        raise WorkerError("KFX-E-VALIDATE", "EPUB spine references a missing manifest item.")
                    spine_ids.append(item_id)
            if not spine_ids:
                raise WorkerError("KFX-E-VALIDATE", "Generated EPUB has an empty spine.")
            if opf.attrib.get("version", "").startswith("3") and nav_count != 1:
                raise WorkerError("KFX-E-VALIDATE", "EPUB 3 output must contain exactly one navigation document.")

            image_count = sum(1 for value in media_types.values() if value.startswith("image/"))
            font_count = sum(
                1
                for value in media_types.values()
                if value.startswith("font/") or value in {"application/vnd.ms-opentype", "application/font-sfnt"}
            )
            return {
                "epubVersion": opf.attrib.get("version", "3.0"),
                "title": title,
                "authors": authors,
                "language": language,
                "spineItems": len(spine_ids),
                "images": image_count,
                "fonts": font_count,
                "archiveEntries": len(infos),
            }
    except WorkerError:
        raise
    except (OSError, KeyError, zipfile.BadZipFile, ElementTree.ParseError) as exc:
        raise WorkerError("KFX-E-VALIDATE", f"Generated EPUB is invalid: {exc}") from exc


class ProtocolLogger:
    def __init__(self) -> None:
        self._seen = set()
        self.warning_count = 0

    def debug(self, message, *args, **kwargs) -> None:
        logging.getLogger("sigil-kfx").debug(message, *args, **kwargs)

    def info(self, message, *args, **kwargs) -> None:
        logging.getLogger("sigil-kfx").info(message, *args, **kwargs)

    def _warning(self, message) -> None:
        rendered = _safe_text(message)
        if rendered in self._seen:
            return
        self._seen.add(rendered)
        self.warning_count += 1
        emit("warning", code="KFX-W-CONVERTER", message=rendered)
        logging.getLogger("sigil-kfx").warning(rendered)

    def warning(self, message, *args, **kwargs) -> None:
        self._warning(message % args if args else message)

    warn = warning

    def error(self, message, *args, **kwargs) -> None:
        self._warning(message % args if args else message)

    def exception(self, message, *args, **kwargs) -> None:
        self._warning(message % args if args else message)
        traceback.print_exc(file=sys.stderr)


class WorkerError(RuntimeError):
    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code


def _error_from_exception(exc: Exception) -> WorkerError:
    try:
        from .kfxlib import KFXDRMError
    except Exception:
        KFXDRMError = ()
    if KFXDRMError and isinstance(exc, KFXDRMError):
        return WorkerError("KFX-E-DRM", "The KFX file is DRM-protected and cannot be converted.")
    if isinstance(exc, OSError) and exc.errno == errno.ENOSPC:
        return WorkerError("KFX-E-NOSPACE", "There is not enough disk space to create the EPUB.")
    if isinstance(exc, (zipfile.BadZipFile, ElementTree.ParseError, ValueError)):
        return WorkerError("KFX-E-MALFORMED", str(exc))
    return WorkerError("KFX-E-CONVERT", str(exc) or exc.__class__.__name__)


def atomic_write_bytes(data: bytes, dest: Path, attempts: int = 12) -> None:
    dest = Path(dest)
    dest.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary_name = tempfile.mkstemp(prefix=dest.name + ".", suffix=".part", dir=str(dest.parent))
    temporary_path = Path(temporary_name)
    last_error = None
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        for attempt in range(attempts):
            try:
                try:
                    dest.unlink()
                except FileNotFoundError:
                    pass
                os.replace(str(temporary_path), str(dest))
                return
            except PermissionError as exc:
                last_error = exc
                time.sleep(0.05 * (attempt + 1))
        if last_error is not None:
            raise last_error
    except Exception:
        try:
            temporary_path.unlink(missing_ok=True)
        except OSError:
            pass
        raise


def convert(source: Path, output: Path, job_id: str) -> dict:
    configure_worker_logging()
    emit("started", jobId=job_id)
    emit("phase", name="preflight")
    input_kind = preflight_input(source)
    source_before = source.stat()

    emit("phase", name="parse")
    package_root = Path(__file__).resolve().parent
    bundled_modules = package_root / "kfxlib" / "calibre-plugin-modules"
    if bundled_modules.is_dir():
        sys.path.insert(0, str(bundled_modules))

    logger = ProtocolLogger()
    last_progress = [-1, 0.0]

    def progress(value) -> None:
        now = time.monotonic()
        percent = max(0, min(100, int(round(float(value)))))
        if percent != last_progress[0] and (percent == 100 or now - last_progress[1] >= 0.1):
            emit("progress", phase="convert", current=percent, total=100)
            last_progress[:] = [percent, now]

    try:
        from . import kfxlib

        kfxlib.set_logger(logger)
        book = kfxlib.YJ_Book(str(source))
        emit("phase", name="convert")
        saved_stdout = sys.stdout
        sys.stdout = sys.stderr
        try:
            epub_data = book.convert_to_epub(progress_fn=progress)
        finally:
            sys.stdout = saved_stdout
        if not epub_data:
            raise WorkerError("KFX-E-CONVERT", "The converter returned an empty EPUB.")

        source_after = source.stat()
        if source_before.st_size != source_after.st_size or source_before.st_mtime_ns != source_after.st_mtime_ns:
            raise WorkerError("KFX-E-INPUT", "The source KFX changed during conversion.")

        emit("phase", name="write")
        atomic_write_bytes(epub_data, output)

        emit("phase", name="validate")
        summary = validate_epub(output)
        summary["inputKind"] = input_kind
        summary["warnings"] = logger.warning_count
        emit("result", status="success", output=str(output), summary=summary)
        return summary
    except WorkerError:
        raise
    except Exception as exc:
        raise _error_from_exception(exc) from exc


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(add_help=True)
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--job-id", default="")
    args = parser.parse_args(argv)

    job_id = args.job_id or str(uuid.uuid4())
    output = Path(args.output).resolve()
    try:
        convert(Path(args.input).resolve(), output, job_id)
        return 0
    except WorkerError as exc:
        try:
            output.unlink(missing_ok=True)
        except OSError:
            pass
        emit("result", status="error", code=exc.code, message=str(exc))
        return 2
    except Exception as exc:
        traceback.print_exc(file=sys.stderr)
        try:
            output.unlink(missing_ok=True)
        except OSError:
            pass
        emit("result", status="error", code="KFX-E-CONVERT", message=str(exc))
        return 3


if __name__ == "__main__":
    raise SystemExit(main())
