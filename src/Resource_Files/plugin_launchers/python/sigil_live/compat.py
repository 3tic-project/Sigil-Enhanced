"""API-compatible containers and RPC-backed wrapper for live v1 plugins.

These classes deliberately inherit the authoritative v1 containers. The
RPC-backed wrapper is supplied separately, so the established method bodies,
signatures, iterator shapes, preferences, and local path helpers are reused
instead of being copied into a divergent second implementation.
"""

from collections import OrderedDict
import os

from bookcontainer import BookContainer
from hrefutils import buildRelativePath, ext_mime_map, mime_group_map, urldecodepart
from inputcontainer import InputContainer
from opf_parser import Opf_Parser
from outputcontainer import OutputContainer
from validationcontainer import ValidationContainer
from wrapper import PROTECTED_FILES, TEXT_MIMETYPES, Wrapper, WrapperException

from .client import Resource


_TEXT_RESOURCE_TYPES = {"html", "css", "svg", "opf", "ncx", "xml", "text"}


def _text_data(data):
    if isinstance(data, str):
        return data
    return bytes(data).decode("utf-8", errors="replace")


def _binary_data(data):
    if isinstance(data, str):
        return data.encode("utf-8")
    return bytes(data)


def _unicode_data(data):
    if data is None or isinstance(data, str):
        return data
    return bytes(data).decode("utf-8", errors="replace")


def _validate_book_path(book_path):
    if not isinstance(book_path, str) or not book_path:
        raise WrapperException("Book path is empty")
    if "\\" in book_path or ":" in book_path or book_path.startswith("/"):
        raise WrapperException("Book path is not canonical")
    segments = book_path.split("/")
    if any(segment in ("", ".", "..") for segment in segments):
        raise WrapperException("Book path is not canonical")
    return book_path


class LiveWrapper(Wrapper):
    """V1 Wrapper implementation backed by one staged live transaction."""

    def __init__(self, plugin, plugin_dir, plugin_name, writable=True, debug=False):
        self._plugin = plugin
        self._debug = debug
        self.plugin_dir = os.fsdecode(plugin_dir)
        self.plugin_name = plugin_name
        self.ebook_root = None
        self.outdir = None
        self._writable = writable
        self._transaction = None
        self._pending_writes = OrderedDict()
        self._added_data = OrderedDict()
        self._removed_resources = OrderedDict()
        self._removed_archive_files = OrderedDict()
        self._archive_reads = {}
        self._load_snapshot(plugin.book.get_compatibility_snapshot())
        if writable:
            self._begin_transaction()
        os.environ["SigilGumboLibPath"] = self.get_gumbo_path()

    def _begin_transaction(self, label="Legacy plugin changes"):
        self._transaction = self._plugin.book.transaction(
            label, checkpoint="auto"
        )

    def _load_snapshot(self, snapshot):
        package = snapshot["package"]
        config = snapshot["configuration"]
        info = self._plugin.book.get_info()
        self.opfbookpath = package["book_path"]
        self._package_original = package["text"]
        self._package_revision = package["resource"]["content_revision"]
        self.appdir = config["application_dir"]
        self.usrsupdir = config["preferences_dir"]
        self.linux_hunspell_dict_dirs = config["linux_hunspell_dictionary_dirs"]
        self.sigil_ui_lang = config["ui_language"]
        self.sigil_spellcheck_lang = config["spellcheck_language"]
        self.epub_isDirty = info["modified"]
        self.epub_filepath = info["file_path"]
        self.colormode = config["color_mode"]
        roles = ("Window", "Base", "Text", "Highlight", "HighlightedText")
        self.colors = ",".join(config["colors"][role] for role in roles)
        self.highdpi = "detect"
        self.uifont = config["ui_font"]
        self.using_automate = config["using_automate"]
        self.automate_parameter = config["automate_parameter"]
        self.font_mangling = dict(snapshot["font_mangling"])
        self.selected = list(snapshot["selected"])

        op = Opf_Parser(
            None, self.opfbookpath, debug=self._debug, opf_data=self._package_original
        )
        self.op = op
        self.opf_dir = op.opf_dir
        self.id_to_href = op.get_manifest_id_to_href_dict().copy()
        self.id_to_mime = op.get_manifest_id_to_mime_dict().copy()
        self.id_to_props = op.get_manifest_id_to_properties_dict().copy()
        self.id_to_fall = op.get_manifest_id_to_fallback_dict().copy()
        self.id_to_over = op.get_manifest_id_to_overlay_dict().copy()
        self.id_to_bookpath = op.get_manifest_id_to_bookpath_dict().copy()
        self.group_paths = op.get_group_paths().copy()
        self.spine_ppd = op.get_spine_ppd()
        self.spine = op.get_spine()
        self.guide = op.get_guide()
        self.package_tag = op.get_package_tag()
        self.epub_version = op.get_epub_version()
        self.bindings = op.get_bindings()
        self.metadataxml = op.get_metadataxml()
        self.href_to_id = OrderedDict((value, key) for key, value in self.id_to_href.items())
        self.bookpath_to_id = OrderedDict(
            (value, key) for key, value in self.id_to_bookpath.items()
        )

        resources = [Resource.from_result(value) for value in snapshot["resources"]]
        self._resource_by_path = OrderedDict(
            (resource.book_path, resource) for resource in resources
        )
        self._archive_by_path = OrderedDict(
            (value["book_path"], value)
            for value in self._plugin.book.archive_files()
            if value.get("resource_id") is None
        )
        self.other = []
        self.id_to_filepath = OrderedDict()
        self.book_href_to_filepath = OrderedDict()
        for resource in resources:
            manifest_id = self.bookpath_to_id.get(resource.book_path)
            if manifest_id is None:
                self.other.append(resource.book_path)
                self.book_href_to_filepath[resource.book_path] = resource.book_path
            else:
                self.id_to_filepath[manifest_id] = resource.book_path
        for book_path in self._archive_by_path:
            if book_path not in self.bookpath_to_id and book_path not in self.other:
                self.other.append(book_path)
                self.book_href_to_filepath[book_path] = book_path
        self.modified = OrderedDict()
        self.added = []
        self.deleted = []
        self._pending_writes.clear()
        self._added_data.clear()
        self._removed_resources.clear()
        self._removed_archive_files.clear()
        self._archive_reads.clear()

    def _require_writable(self):
        if not self._writable:
            raise WrapperException("This live compatibility container is read-only")

    def _read_resource(self, resource):
        if resource.book_path in self._pending_writes:
            return self._pending_writes[resource.book_path]
        if resource.resource_type in _TEXT_RESOURCE_TYPES:
            return self._plugin.book.read_text(resource)["text"]
        return self._plugin.book.read_binary(resource)["data"]

    def _read_archive(self, book_path):
        if book_path not in self._archive_reads:
            self._archive_reads[book_path] = self._plugin.book.read_archive_file(book_path)
        data = self._archive_reads[book_path]["data"]
        return _text_data(data) if self.getmime(book_path) in TEXT_MIMETYPES else data

    def readfile(self, id):
        id = _unicode_data(id)
        if id not in self.id_to_bookpath:
            raise WrapperException("Id does not exist in manifest")
        book_path = self.id_to_bookpath[id]
        if book_path in self._added_data:
            return self._added_data[book_path]
        resource = self._resource_by_path.get(book_path)
        if resource is None:
            raise WrapperException("File Does Not Exist")
        return self._read_resource(resource)

    def writefile(self, id, data):
        self._require_writable()
        id = _unicode_data(id)
        if id not in self.id_to_bookpath:
            raise WrapperException("Id does not exist in manifest")
        book_path = self.id_to_bookpath[id]
        mime = self.id_to_mime.get(id, "")
        resource = self._resource_by_path.get(book_path)
        value = _text_data(data) if mime in TEXT_MIMETYPES or (
            resource and resource.resource_type in _TEXT_RESOURCE_TYPES
        ) else _binary_data(data)
        if book_path in self._added_data:
            self._added_data[book_path] = value
        elif resource is None:
            raise WrapperException("File Does Not Exist")
        else:
            self._pending_writes[book_path] = value
        self.modified[id] = "file"

    def _add_manifest_file(self, uniqueid, bookpath, data, mime, properties=None,
                           fallback=None, overlay=None):
        self._require_writable()
        uniqueid = _unicode_data(uniqueid)
        bookpath = _unicode_data(bookpath)
        mime = _unicode_data(mime)
        bookpath = _validate_book_path(bookpath)
        if uniqueid in self.id_to_href:
            raise WrapperException("Manifest Id is not unique")
        if (bookpath in self._resource_by_path or bookpath in self._archive_by_path
                or bookpath in self._added_data):
            raise WrapperException("bookpath already exists")
        href = buildRelativePath(self.opfbookpath, bookpath)
        if href in self.href_to_id:
            raise WrapperException("bookpath already exists")
        value = _text_data(data) if mime in TEXT_MIMETYPES else _binary_data(data)
        self.id_to_href[uniqueid] = href
        self.id_to_mime[uniqueid] = mime
        self.id_to_props[uniqueid] = properties
        self.id_to_fall[uniqueid] = fallback
        self.id_to_over[uniqueid] = overlay
        self.id_to_bookpath[uniqueid] = bookpath
        self.href_to_id[href] = uniqueid
        self.bookpath_to_id[bookpath] = uniqueid
        self.id_to_filepath[uniqueid] = bookpath
        self._added_data[bookpath] = value
        self.added.append(uniqueid)
        self.modified[self.opfbookpath] = "file"
        return uniqueid

    def addfile(self, uniqueid, basename, data, mime=None, properties=None,
                fallback=None, overlay=None):
        basename = _unicode_data(basename)
        mime = _unicode_data(mime)
        if mime is None:
            mime = ext_mime_map.get(os.path.splitext(basename)[1].lower())
        if mime is None:
            raise WrapperException("Mime Type Missing")
        if mime == "application/x-dtbncx+xml" and self.epub_version.startswith("2"):
            raise WrapperException("Can not add or remove an ncx under epub2")
        group = mime_group_map.get(mime, "Misc")
        folder = self.group_paths[group][0]
        bookpath = basename if not folder else folder + "/" + basename
        return self._add_manifest_file(
            uniqueid, bookpath, data, mime, properties, fallback, overlay
        )

    def addbookpath(self, uniqueid, bookpath, data, mime=None):
        bookpath = _unicode_data(bookpath)
        mime = _unicode_data(mime)
        if mime is None:
            mime = ext_mime_map.get(os.path.splitext(bookpath)[1].lower())
        if mime is None:
            raise WrapperException("Mime Type Missing")
        if mime == "application/x-dtbncx+xml" and self.epub_version.startswith("2"):
            raise WrapperException("Can not add or remove an ncx under epub2")
        return self._add_manifest_file(uniqueid, bookpath, data, mime)

    def deletefile(self, id):
        self._require_writable()
        id = _unicode_data(id)
        if id not in self.id_to_bookpath:
            raise WrapperException("Id does not exist in manifest")
        if self.epub_version.startswith("2") and id == self.gettocid():
            raise WrapperException("Can not add or remove an ncx under epub2")
        bookpath = self.id_to_bookpath[id]
        if bookpath in self._added_data:
            del self._added_data[bookpath]
            self.added.remove(id)
        else:
            resource = self._resource_by_path.get(bookpath)
            if resource is None:
                raise WrapperException("File Does Not Exist")
            self._removed_resources[bookpath] = resource
            self.deleted.append(("manifest", id, bookpath))
        self._pending_writes.pop(bookpath, None)
        href = self.id_to_href.pop(id)
        self.id_to_mime.pop(id)
        self.id_to_props.pop(id)
        self.id_to_fall.pop(id)
        self.id_to_over.pop(id)
        self.id_to_bookpath.pop(id)
        self.href_to_id.pop(href)
        self.bookpath_to_id.pop(bookpath)
        self.id_to_filepath.pop(id, None)
        self.spine = [entry for entry in self.spine if entry[0] != id]
        self.modified.pop(id, None)
        self.modified[self.opfbookpath] = "file"

    def readotherfile(self, book_href):
        book_href = urldecodepart(_unicode_data(book_href))
        if book_href == self.opfbookpath and book_href in self.modified:
            return self.build_opf()
        if book_href in self._added_data:
            return self._added_data[book_href]
        resource = self._resource_by_path.get(book_href)
        if book_href not in self.other:
            raise WrapperException("Book href does not exist")
        if resource is not None:
            return self._read_resource(resource)
        if book_href in self._archive_by_path:
            return self._read_archive(book_href)
        raise WrapperException("Book href does not exist")

    def writeotherfile(self, book_href, data):
        self._require_writable()
        book_href = urldecodepart(_unicode_data(book_href))
        if book_href in PROTECTED_FILES or book_href == self.opfbookpath:
            raise WrapperException("Attempt to modify protected file")
        resource = self._resource_by_path.get(book_href)
        if book_href not in self.other:
            raise WrapperException("Book href does not exist")
        if resource is not None:
            self._pending_writes[book_href] = (
                _text_data(data) if resource.resource_type in _TEXT_RESOURCE_TYPES
                else _binary_data(data)
            )
        elif book_href in self._archive_by_path:
            self._read_archive(book_href)
            self._pending_writes[book_href] = _binary_data(data)
        else:
            raise WrapperException("Book href does not exist")
        self.modified[book_href] = "file"

    def addotherfile(self, book_href, data):
        self._require_writable()
        book_href = _validate_book_path(urldecodepart(_unicode_data(book_href)))
        if (book_href in self._resource_by_path or book_href in self._archive_by_path
                or book_href in self._added_data):
            raise WrapperException("Book href must be unique")
        self.other.append(book_href)
        self.book_href_to_filepath[book_href] = book_href
        self._added_data[book_href] = data if isinstance(data, str) else bytes(data)
        self.added.append(book_href)

    def deleteotherfile(self, book_href):
        self._require_writable()
        book_href = urldecodepart(_unicode_data(book_href))
        if book_href in PROTECTED_FILES or book_href == self.opfbookpath:
            raise WrapperException("attempt to delete protected file")
        if book_href not in self.other:
            raise WrapperException("Book href does not exist")
        if book_href in self._added_data:
            del self._added_data[book_href]
            self.added.remove(book_href)
        else:
            resource = self._resource_by_path.get(book_href)
            if resource is not None:
                self._removed_resources[book_href] = resource
            elif book_href in self._archive_by_path:
                self._read_archive(book_href)
                self._removed_archive_files[book_href] = self._archive_reads[book_href]
            else:
                raise WrapperException("Book href does not exist")
            self.deleted.append(("other", book_href, book_href))
        self.other.remove(book_href)
        self.book_href_to_filepath.pop(book_href, None)
        self._pending_writes.pop(book_href, None)
        self.modified.pop(book_href, None)

    def copy_book_contents_to(self, destdir):
        if not os.path.isdir(destdir):
            raise WrapperException("destination directory does not exist")
        paths = list(self.id_to_bookpath.values()) + list(self.other)
        for bookpath in paths:
            manifest_id = self.bookpath_to_id.get(bookpath)
            data = self.readfile(manifest_id) if manifest_id else self.readotherfile(bookpath)
            filepath = os.path.join(destdir, bookpath.replace("/", os.sep))
            os.makedirs(os.path.dirname(filepath), exist_ok=True)
            with open(filepath, "wb") as stream:
                stream.write(_binary_data(data))

    def _stage_all(self):
        for bookpath, data in self._pending_writes.items():
            resource = self._resource_by_path.get(bookpath)
            if resource is None:
                self._transaction.replace_archive_file(
                    bookpath, data, self._archive_reads[bookpath]["sha256"]
                )
            elif isinstance(data, str):
                self._transaction.replace_text(resource, data, expected_revision=resource.revision)
            else:
                self._transaction.write_binary(resource, data, expected_revision=resource.revision)
        for bookpath, resource in self._removed_resources.items():
            self._transaction.remove_resource(
                resource, expected_revision=resource.revision
            )
        for bookpath, archive in self._removed_archive_files.items():
            self._transaction.remove_archive_file(bookpath, archive["sha256"])
        for entry in self.added:
            manifested = entry in self.id_to_bookpath
            bookpath = self.id_to_bookpath[entry] if manifested else entry
            data = self._added_data[bookpath]
            mime = self.id_to_mime[entry] if manifested else self.getmime(bookpath)
            if not mime:
                mime = "application/octet-stream"
            self._transaction.add_resource(
                bookpath,
                data,
                mime,
                manifest_id=entry if manifested else None,
                properties=self.id_to_props.get(entry) if manifested else None,
                fallback=self.id_to_fall.get(entry) if manifested else None,
                overlay=self.id_to_over.get(entry) if manifested else None,
                add_to_spine=False,
                manifested=manifested,
            )
        if self.opfbookpath in self.modified:
            self._transaction.replace_package(
                self.build_opf(), expected_revision=self._package_revision
            )

    def commit(self):
        self._require_writable()
        self._stage_all()
        return self._transaction.commit()

    def rollback(self):
        if self._transaction is not None and self._transaction.active:
            return self._transaction.rollback()
        return None

    def flush(self, label="Apply staged plugin changes"):
        self._require_writable()
        self._plugin.ping()
        result = self.commit()
        self._load_snapshot(self._plugin.book.get_compatibility_snapshot())
        self._begin_transaction(label)
        return result

    def write_opf(self):
        """The live runtime commits OPF state through ``commit`` instead of disk."""


class CompatBookContainer(BookContainer):
    """V1 edit container backed by a live wrapper and implicit transaction."""

    def flush(self, label="Apply staged plugin changes"):
        """Commit the current implicit transaction and begin another one."""
        return self._w.flush(label)


class CompatOutputContainer(OutputContainer):
    """V1 output container API surface for the live compatibility runtime."""


class LiveInputWrapper(LiveWrapper):
    """Read-only v1 wrapper that submits the input plugin's resulting EPUB."""

    def __init__(self, plugin, plugin_dir, plugin_name, debug=False):
        super().__init__(plugin, plugin_dir, plugin_name, writable=False, debug=debug)

    def addotherfile(self, book_href, data):
        filename = os.path.basename(_unicode_data(book_href))
        if not filename.lower().endswith(".epub"):
            raise WrapperException("Input plugins must return an .epub file")
        self._plugin.input.submit_epub(_binary_data(data), filename)


class CompatInputContainer(InputContainer):
    """V1 input container API surface backed by chunked EPUB submission."""


class CompatValidationContainer(ValidationContainer):
    """V1 validation container API surface for the live compatibility runtime."""
