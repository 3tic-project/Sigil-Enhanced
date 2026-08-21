import collections
import datetime
import decimal
import io
from lxml import etree
from PIL import (Image, ImageDraw, ImageFont)
import posixpath
import re
import uuid
import xml.sax.saxutils
import zipfile

try:
    from calibre.utils.resources import get_path
    from calibre.utils.config_base import tweaks
except ImportError:
    get_path = None
    tweaks = {}

from .message_logging import log
from .resources import (EPUB2_ALT_MIMETYPES, MIMETYPE_OF_EXT, RESOURCE_TYPE_OF_EXT)
from .utilities import (get_url_filename, make_unique_name, urlabspath, urlrelpath)


__license__ = "GPL v3"
__copyright__ = "2016-2025, John Howell <jhowell@acm.org>"


GENERATE_EPUB2_NCX_DOCTYPE = False
CONSOLIDATE_HTML = True
BEAUTIFY_HTML = True
USE_HIDDEN_ATTRIBUTE = True
KEEP_PAGE_NUMBERS = False


STANDARD_GUIDE_TYPE = {
    "srl": "text",
}

EPUB3_VOCABULARY_OF_GUIDE_TYPE = {
    "cover": "cover",
    "text": "bodymatter",
    "toc": "toc",
    }

DEFAULT_LABEL_OF_GUIDE_TYPE = {
    "cover": "Cover",
    "text": "Beginning",
    "toc": "Table of Contents",
    }

TOC_PRIORITY_OF_GUIDE_TYPE = {
    "toc": 1,
    "text": 2,
    "cover": 3,
}

PERIODICAL_NCX_CLASSES = {
    0: "section",
    1: "article",
    }

MANIFEST_ITEM_PROPERTIES = {"cover-image", "mathml", "nav", "remote-resources", "scripted", "svg", "switch"}

SPINE_ITEMREF_PROPERTIES = {
    "page-spread-left", "page-spread-right", "rendition:align-x-center",
    "rendition:flow-auto", "rendition:flow-paginated", "rendition:flow-scrolled-continuous",
    "rendition:flow-scrolled-doc", "rendition:layout-pre-paginated", "rendition:layout-reflowable",
    "rendition:orientation-auto", "rendition:orientation-landscape", "rendition:orientation-portrait",
    "rendition:page-spread-center", "rendition:spread-auto", "rendition:spread-both",
    "rendition:spread-landscape", "rendition:spread-none", "rendition:spread-portrait",

    "facing-page-left", "facing-page-right", "layout-blank"}

OPF_PROPERTIES = MANIFEST_ITEM_PROPERTIES | SPINE_ITEMREF_PROPERTIES


XML_NS_URI = "http://www.w3.org/XML/1998/namespace"

DC_NS_URI = "http://purl.org/dc/elements/1.1/"
OPF_NS_URI = "http://www.idpf.org/2007/opf"

NCX_NS_URI = "http://www.daisy.org/z3986/2005/ncx/"

XHTML_NS_URI = "http://www.w3.org/1999/xhtml"
EPUB_NS_URI = "http://www.idpf.org/2007/ops"
IDX_NS_URI = "https://kindlegen.s3.amazonaws.com/AmazonKindlePublishingGuidelines.pdf"
MBP_NS_URI = "https://kindlegen.s3.amazonaws.com/AmazonKindlePublishingGuidelines.pdf"

XHTML_NAMESPACES = {
    None: XHTML_NS_URI,
    "epub": EPUB_NS_URI,
    "idx": IDX_NS_URI,
}

SVG_NS_URI = "http://www.w3.org/2000/svg"
XLINK_NS_URI = "http://www.w3.org/1999/xlink"

SVG_NAMESPACES = {
    None: SVG_NS_URI,
    "xlink": XLINK_NS_URI,
}

MATHML_NS_URI = "http://exslt.org/math"

MATHML_NAMESPACES = {
    None: MATHML_NS_URI,
}


RESERVED_OPF_VALUE_PREFIXES = {
    "a11y": "http://www.idpf.org/epub/vocab/package/a11y/#",
    "dcterms": "http://purl.org/dc/terms/",
    "marc": "http://id.loc.gov/vocabulary/",
    "media": "http://www.idpf.org/epub/vocab/overlays/#",
    "onix": "http://www.editeur.org/ONIX/book/codelists/current.html#",
    "rendition": "http://www.idpf.org/vocab/rendition/#",
    "schema": "http://schema.org/",
    "xsd": "http://www.w3.org/2001/XMLSchema#",
    }


class OPFProperties(object):
    def __init__(self, opf_properties):
        self.opf_properties = set(opf_properties) if opf_properties is not None else set()

    @property
    def is_fxl(self):
        return "rendition:layout-pre-paginated" in self.opf_properties

    @is_fxl.setter
    def is_fxl(self, value):
        if value:
            self.opf_properties.add("rendition:layout-pre-paginated")
        else:
            self.opf_properties.discard("rendition:layout-pre-paginated")

    @property
    def is_nav(self):
        return "nav" in self.opf_properties

    @is_nav.setter
    def is_nav(self, value):
        if value:
            self.opf_properties.add("nav")
        else:
            self.opf_properties.discard("nav")

    @property
    def is_cover_image(self):
        return "cover-image" in self.opf_properties

    @is_cover_image.setter
    def is_cover_image(self, value):
        if value:
            self.opf_properties.add("cover-image")
        else:
            self.opf_properties.discard("cover-image")


class BookPart(OPFProperties):
    def __init__(self, filename, part_index, html, opf_properties=None, linear=None, omit=False, idref=None):
        self.filename = filename
        self.part_index = part_index
        self.html = html
        OPFProperties.__init__(self, opf_properties)
        self.linear = linear
        self.omit = omit
        self.idref = idref

        self.is_cover_page = False
        self.nmdl_template_id = None
        self.writing_mode_value = None
        self.has_vertical_rl_class = False

    def head(self):
        head = self.html.find("head")
        if head is None:
            head = etree.Element("head")
            self.html.insert(0, head)

        return head

    def body(self):
        body = self.html.find("body")
        if body is None:
            body = etree.SubElement(self.html, "body")

        return body


class ManifestEntry(OPFProperties):
    def __init__(self, filename, opf_properties, linear, external, id):
        self.filename = filename
        OPFProperties.__init__(self, opf_properties)
        self.linear = linear
        self.external = external
        self.id = id
        self.reference_count = 0


class TocEntry(object):
    def __init__(self, title, target=None, children=None, description=None, icon=None, anchor=None):
        self.title = title
        self.target = target
        self.children = children if children else []
        self.description = description
        self.icon = icon
        self.anchor = anchor


class GuideEntry(object):
    def __init__(self, guide_type, title, target=None, anchor=None):
        self.guide_type = guide_type
        self.title = title
        self.target = target
        self.anchor = anchor


class PageMapEntry(object):
    def __init__(self, label, target=None, anchor=None):
        self.label = label
        self.target = target
        self.anchor = anchor

    def __repr__(self):
        return "%s=%s" % (self.label, self.anchor)


class OutputFile(object):
    def __init__(self, binary_data, mimetype, height=None, width=None):
        self.binary_data = binary_data
        self.mimetype = mimetype
        self.height = height
        self.width = width


class EPUB_Output(object):
    DEBUG = False

    GENERATE_EPUB2_COMPATIBLE = False
    PLACE_FILES_IN_SUBDIRS = True

    OEBPS_DIR = "item"

    OPF_FILEPATH = "/standard.opf"
    NCX_FILEPATH = "/toc.ncx"
    TEXT_FILEPATH = "/p-%s.xhtml"
    FRONTMATTER_TEXT_FILEPATH = "/p-fmatter-%s.xhtml"
    SECTION_TEXT_FILEPATH = "/%s.xhtml"
    COVER_FILEPATH = "/p-cover.xhtml"
    TOC_FILEPATH = "/p-toc.xhtml"
    COLOPHON_FILEPATH = "/p-colophon.xhtml"
    NAV_FILEPATH = "/navigation-documents%s.xhtml"
    FONT_FILEPATH = "/%s"
    IMAGE_FILEPATH = "/%s"
    PDF_FILEPATH = "/%s"
    STYLES_CSS_FILEPATH = "/book-style.css"
    KFX_CSS_FILEPATH = "/style-kfx.css"
    RESET_CSS_FILEPATH = "/style-reset.css"
    STANDARD_CSS_FILEPATH = "/style-standard.css"
    ADVANCE_CSS_FILEPATH = "/style-advance.css"
    CHECK_CSS_FILEPATH = "/style-check.css"
    FIXED_LAYOUT_CSS_FILEPATH = "/fixed-layout-jp.css"
    LAYOUT_CSS_FILEPATH = "/layout%04d.css"

    if PLACE_FILES_IN_SUBDIRS:
        TEXT_FILEPATH = "/xhtml" + TEXT_FILEPATH
        FRONTMATTER_TEXT_FILEPATH = "/xhtml" + FRONTMATTER_TEXT_FILEPATH
        SECTION_TEXT_FILEPATH = "/xhtml" + SECTION_TEXT_FILEPATH
        COVER_FILEPATH = "/xhtml" + COVER_FILEPATH
        TOC_FILEPATH = "/xhtml" + TOC_FILEPATH
        COLOPHON_FILEPATH = "/xhtml" + COLOPHON_FILEPATH
        NAV_FILEPATH = NAV_FILEPATH
        FONT_FILEPATH = "/fonts" + FONT_FILEPATH
        IMAGE_FILEPATH = "/image" + IMAGE_FILEPATH
        PDF_FILEPATH = "/misc" + PDF_FILEPATH
        STYLES_CSS_FILEPATH = "/style" + STYLES_CSS_FILEPATH
        KFX_CSS_FILEPATH = "/style" + KFX_CSS_FILEPATH
        RESET_CSS_FILEPATH = "/style" + RESET_CSS_FILEPATH
        STANDARD_CSS_FILEPATH = "/style" + STANDARD_CSS_FILEPATH
        ADVANCE_CSS_FILEPATH = "/style" + ADVANCE_CSS_FILEPATH
        CHECK_CSS_FILEPATH = "/style" + CHECK_CSS_FILEPATH
        FIXED_LAYOUT_CSS_FILEPATH = "/style" + FIXED_LAYOUT_CSS_FILEPATH
        LAYOUT_CSS_FILEPATH = "/style" + LAYOUT_CSS_FILEPATH

    def __init__(self, epub2_desired=False, force_cover=False, will_output=True):
        self.epub2_desired = epub2_desired
        self.generate_epub2 = epub2_desired
        self.force_cover = force_cover
        self.will_output = will_output

        self.oebps_files = {}
        self.book_parts = []
        self.ncx_toc = []
        self.manifest = []
        self.manifest_files = {}
        self.manifest_ids = set()
        self.guide = []
        self.css_files = set()
        self.pagemap = []
        self.dom_image_filenames = set()
        self.original_dom_image_roles = {}

        self.asin = ""
        self.book_id = ""
        self.title = ""
        self.title_pronunciation = ""
        self.authors = []
        self.author_pronunciations = []
        self.publisher = ""
        self.pubdate = ""
        self.description = ""
        self.subject = ""
        self.rights = ""
        self.language = ""
        self.source_language = self.target_language = ""
        self.is_sample = False
        self.is_dictionary = False
        self.override_kindle_font = False
        self.ncx_location = None
        self.toc_ncx_id = None
        self.orientation_lock = "none"
        self.min_aspect_ratio = self.max_aspect_ratio = None
        self.original_width = self.original_height = None
        self.fixed_layout = False
        self.region_magnification = False
        self.virtual_panels = self.virtual_panels_allowed = False
        self.illustrated_layout = False
        self.html_cover = False
        self.guided_view_native = False
        self.remove_html_cover = False
        self.scrolled_continuous = False
        self.book_type = None

        self.set_book_type(None)
        self.set_primary_writing_mode("horizontal-lr")

        if self.will_output:
            log.info("Converting book to EPUB %s" % ("2" if self.generate_epub2 else "3"))

    def set_book_type(self, book_type):
        if self.book_type is not None and self.book_type != book_type:
            log.error("Book type changed from %s to %s" % (self.book_type, book_type))

        self.book_type = book_type
        self.is_children = self.is_comic = self.is_magazine = self.is_notebook = False

        if self.book_type is None:
            pass
        elif self.book_type == "children":
            self.is_children = True
        elif self.book_type == "comic":
            self.is_comic = True
        elif self.book_type == "magazine":
            self.is_magazine = True
        elif self.book_type == "notebook":
            self.is_notebook = True
        else:
            log.error("Unexpected bookType: %s" % self.book_type)

    def set_primary_writing_mode(self, primary_writing_mode):
        if primary_writing_mode == "horizontal-lr":
            self.writing_mode = "horizontal-tb"
            self.page_progression_direction = "ltr"
        elif primary_writing_mode == "horizontal-rl":
            self.writing_mode = "horizontal-tb"
            self.page_progression_direction = "rtl"
        elif primary_writing_mode == "vertical-rl":
            self.writing_mode = "vertical-tb"
            self.page_progression_direction = "rtl"
        else:
            log.error("Unexpected PrimaryWritingMode: %s" % primary_writing_mode)

    def manifest_id_for_filename(self, filename):
        basename = filename.rpartition("/")[2]
        basename = posixpath.splitext(basename)[0] or basename
        if filename.lstrip("/").startswith("image/") and basename != "cover":
            basename = "img-" + basename
        return self.fix_html_id(basename[:64])

    def manifest_resource(self, filename, opf_properties=None, linear=None, external=False,
                          data=None, mimetype=None, height=None, width=None, idref=None,
                          report_dupe=True):
        if filename in self.manifest_files:
            if report_dupe:
                log.error("Duplicate file name in manifest: %s" % filename)

            return

        idref = self.fix_html_id(idref) if idref else self.manifest_id_for_filename(filename)
        idref = make_unique_name(idref, self.manifest_ids, sep="_")

        manifest_entry = ManifestEntry(filename, opf_properties or set(), linear, external, id=idref)
        self.manifest.append(manifest_entry)
        self.manifest_files[filename] = manifest_entry
        self.manifest_ids.add(idref)
        self.reference_resource(manifest_entry)

        if data is not None:
            self.add_oebps_file(filename, data, mimetype or self.mimetype_of_filename(filename), height, width)

        return manifest_entry

    def reference_resource(self, manifest_entry):
        manifest_entry.reference_count += 1
        return manifest_entry

    def unreference_resource(self, filename):
        manifest_entry = self.manifest_files.get(filename, None)
        if manifest_entry is None:
            log.error("Unreferenced file not in manifest: %s" % filename)
            return

        manifest_entry.reference_count -= 1

        if manifest_entry.reference_count == 0:
            for i, ent in enumerate(self.manifest):
                if ent is manifest_entry:
                    self.manifest.pop(i)

            self.manifest_files.pop(filename)
            self.manifest_ids.remove(manifest_entry.id)
            self.remove_oebps_file(filename)

    def add_guide_entry(self, guide_type, title=None, target=None, anchor=None):
        std_guide_type = STANDARD_GUIDE_TYPE.get(guide_type, guide_type)

        self.guide.append(GuideEntry(
            std_guide_type,
            title or DEFAULT_LABEL_OF_GUIDE_TYPE.get(std_guide_type) or guide_type,
            target=target, anchor=anchor))

    def add_pagemap_entry(self, label, target=None, anchor=None):
        self.pagemap.append(PageMapEntry(label, target=target, anchor=anchor))

    def add_oebps_file(self, filename, binary_data, mimetype, height=None, width=None):
        self.oebps_files[filename] = OutputFile(binary_data, mimetype, height, width)

    def remove_oebps_file(self, filename):
        self.oebps_files.pop(filename, None)

    def generate_epub(self):

        if self.asin:
            self.uid = self.asin
        elif self.book_id:
            self.uid = self.book_id
        else:
            self.uid = "urn:uuid:" + str(uuid.uuid4())

        if not self.authors:
            self.authors = ["Unknown"]

        if not self.title:
            self.title = "Unknown"

        if self.is_sample:
            self.title += " - Sample"

        desc = []
        if self.is_dictionary:
            desc.append("dictionary")
        if self.is_sample:
            desc.append("sample")
        if self.fixed_layout:
            desc.append("fixed layout")
        if self.illustrated_layout:
            desc.append("illustrated layout")
        if self.book_type:
            desc.append(self.book_type)

        if desc:
            log.info("Format is %s" % " ".join(desc))

        self.check_epub_version()

        self.identify_cover()
        if self.remove_html_cover:
            self.do_remove_html_cover()

        if self.force_cover:
            self.add_generic_cover_page()

        if not self.book_parts:
            raise Exception("Book does not contain any content")

        if len(self.ncx_toc) == 0:
            for g in sorted(self.guide, key=lambda g: TOC_PRIORITY_OF_GUIDE_TYPE.get(g.guide_type, 999)):
                self.ncx_toc.append(TocEntry(g.title, target=g.target))
                break
            else:
                self.ncx_toc.append(TocEntry("Content", target=self.book_parts[0].filename))

        if not KEEP_PAGE_NUMBERS:
            self.remove_page_markers()

        if not self.generate_epub2:
            for book_part in self.book_parts:
                if book_part.is_nav:
                    break
            else:
                self.create_epub3_nav()

            self.rename_landmark_book_parts()
            self.rename_colophon_book_part()
            self.rename_spine_book_parts()

        if self.fixed_layout and (self.original_height is None or self.original_width is None) and (self.is_comic or self.is_children):
            self.compare_fixed_layout_viewports()

        self.save_book_parts()

        if self.ncx_location is None and (self.generate_epub2 or self.GENERATE_EPUB2_COMPATIBLE):
            self.create_ncx()

        self.create_opf()

        if self.generate_epub2 is not self.epub2_desired:
            log.warning("Book converted to EPUB %s to accommodate content not supported in EPUB %s" % (
                "2" if self.generate_epub2 else "3", "2" if self.epub2_desired else "3"))

        return self.zip_epub()

    def remove_page_markers(self):
        self.pagemap = []

        for book_part in self.book_parts:
            for elem in list(book_part.html.iter("*")):
                if re.match(r"^page_[0-9]+$", elem.get("id", "")) is None:
                    continue

                elem.attrib.pop("id", None)

                if elem.tag == "span" and len(elem.attrib) == 0 and len(elem) == 0 and not (elem.text or ""):
                    parent = elem.getparent()
                    if parent is not None:
                        index = parent.index(elem)
                        tail = elem.tail
                        parent.remove(elem)
                        if tail:
                            if index > 0:
                                prev = parent[index - 1]
                                prev.tail = (prev.tail or "") + tail
                            else:
                                parent.text = (parent.text or "") + tail

    def fix_html_id(self, id):
        if self.illustrated_layout:
            id = id.replace(".", "_")

        id = re.sub("[\u0660-\u0669\u06f0-\u06f9]", lambda m: chr((ord(m.group()) & 0x0f) + 0x30), id)

        id = re.sub(r"[^A-Za-z0-9_.-]", "_", id)

        if len(id) == 0 or not re.match(r"^[A-Za-z]", id):
            id = "id_" + id

        return id

    def new_book_part(self, filename=None, opf_properties=set(), linear=True, omit=False, html=None, idref=None, first=False):
        part_index = len(self.book_parts)

        if filename is None:
            filename = self.TEXT_FILEPATH % self.sequence_number(part_index)

        while self.is_book_part_filename(filename):
            log.error("BookPart filename %s is not unique" % filename)
            filename = filename.replace(".", "_.")

        if html is None:
            html = new_xhtml()

        book_part = BookPart(filename, part_index, html, opf_properties, linear, omit, idref)

        if first:
            self.book_parts.insert(0, book_part)
        else:
            self.book_parts.append(book_part)

        if self.DEBUG:
            log.debug("new_book_part %s" % filename)

        return book_part

    def sequence_number(self, index, total=None, minimum_digits=3):
        width = max(minimum_digits, len(str(total if total is not None else index)))
        return ("%%0%dd" % width) % index

    def rename_landmark_book_parts(self):
        landmark_targets = []

        for nav_part in self.book_parts:
            if nav_part.is_nav:
                for nav in nav_part.body().findall("nav"):
                    if nav.get(EPUB_TYPE) != "landmarks":
                        continue

                    for a in nav.iter("a"):
                        epub_type = a.get(EPUB_TYPE, "").split()
                        if "cover" in epub_type:
                            landmark_targets.append(("cover", a.get("href"), nav_part.filename))
                        elif "toc" in epub_type:
                            landmark_targets.append(("toc", a.get("href"), nav_part.filename))

        renamed = {}
        for landmark_type, href, ref_from in landmark_targets:
            target_filename = self.landmark_target_filename(href, ref_from)
            if not target_filename:
                continue

            new_filename = self.COVER_FILEPATH if landmark_type == "cover" else self.TOC_FILEPATH
            if target_filename in renamed:
                if renamed[target_filename] != new_filename:
                    log.warning("Landmark %s points to %s already renamed to %s" % (
                        landmark_type, target_filename, renamed[target_filename]))
                continue

            if self.rename_book_part_file(target_filename, new_filename):
                renamed[target_filename] = new_filename

    def rename_colophon_book_part(self):
        for nav_part in self.book_parts:
            if not nav_part.is_nav:
                continue

            for nav in nav_part.body().findall("nav"):
                if nav.get(EPUB_TYPE) != "toc":
                    continue

                for a in nav.iter("a"):
                    if "".join(a.itertext()).strip() != "奥付":
                        continue

                    target_filename = self.landmark_target_filename(a.get("href"), nav_part.filename)
                    if target_filename and target_filename not in {self.COVER_FILEPATH, self.TOC_FILEPATH}:
                        self.rename_book_part_file(target_filename, self.COLOPHON_FILEPATH)
                    return

    def rename_spine_book_parts(self):
        spine_parts = [bp for bp in self.book_parts if bp.linear is not None and bp.filename.endswith(".xhtml")]
        if not spine_parts:
            return

        toc_in_spine = any(bp.filename == self.TOC_FILEPATH for bp in spine_parts)
        special_filenames = {self.COVER_FILEPATH, self.TOC_FILEPATH, self.COLOPHON_FILEPATH}
        frontmatter_parts = []
        content_parts = []
        before_toc = toc_in_spine

        for book_part in spine_parts:
            if book_part.filename == self.TOC_FILEPATH:
                before_toc = False
                continue

            if book_part.filename in special_filenames:
                continue

            if before_toc:
                frontmatter_parts.append(book_part)
            else:
                content_parts.append(book_part)

        rename_map = {}
        for index, book_part in enumerate(frontmatter_parts, start=1):
            new_filename = self.FRONTMATTER_TEXT_FILEPATH % self.sequence_number(index, total=len(frontmatter_parts))
            if book_part.filename != new_filename:
                rename_map[book_part.filename] = new_filename

        for index, book_part in enumerate(content_parts, start=1):
            new_filename = self.TEXT_FILEPATH % self.sequence_number(index, total=len(content_parts))
            if book_part.filename != new_filename:
                rename_map[book_part.filename] = new_filename

        self.rename_book_part_files(rename_map)

    def rename_book_part_files(self, rename_map):
        if not rename_map:
            return

        book_part_by_filename = dict((bp.filename, bp) for bp in self.book_parts)
        filtered_map = {}
        new_filenames = set()

        for old_filename, new_filename in rename_map.items():
            if old_filename not in book_part_by_filename or old_filename == new_filename:
                continue

            if new_filename in new_filenames:
                log.warning("Cannot rename %s to duplicate book part filename %s" % (old_filename, new_filename))
                continue

            if new_filename in book_part_by_filename and new_filename not in rename_map:
                log.warning("Cannot rename %s to existing book part filename %s" % (old_filename, new_filename))
                continue

            filtered_map[old_filename] = new_filename
            new_filenames.add(new_filename)

        temp_map = {}
        temp_filenames = set()
        for index, old_filename in enumerate(filtered_map):
            temp_filename = self.SECTION_TEXT_FILEPATH % ("p-rename-tmp-%04d" % index)
            while temp_filename in book_part_by_filename or temp_filename in new_filenames or temp_filename in temp_filenames:
                index += 1
                temp_filename = self.SECTION_TEXT_FILEPATH % ("p-rename-tmp-%04d" % index)

            temp_map[old_filename] = temp_filename
            temp_filenames.add(temp_filename)
            book_part_by_filename[old_filename].filename = temp_filename
            self.update_book_part_references(old_filename, temp_filename)

        for old_filename, new_filename in filtered_map.items():
            temp_filename = temp_map[old_filename]
            book_part_by_filename[old_filename].filename = new_filename
            self.update_book_part_references(temp_filename, new_filename)

    def landmark_target_filename(self, href, ref_from):
        if not href:
            return None

        filename = get_url_filename(urlabspath(href, ref_from=ref_from))
        if filename is None:
            return None

        filename = remove_url_fragment(filename)
        if not filename.endswith(".xhtml"):
            return None

        return filename

    def rename_book_part_file(self, old_filename, new_filename):
        if old_filename == new_filename:
            return True

        book_part = None
        for bp in self.book_parts:
            if bp.filename == old_filename:
                book_part = bp
                break

        if book_part is None:
            log.warning("Landmark target book part not found: %s" % old_filename)
            return False

        for bp in self.book_parts:
            if bp.filename == new_filename:
                log.warning("Cannot rename %s to existing book part filename %s" % (old_filename, new_filename))
                return False

        book_part.filename = new_filename
        self.update_book_part_references(old_filename, new_filename)
        return True

    def update_book_part_references(self, old_filename, new_filename):
        def replace_url(url, ref_from):
            abs_url = urlabspath(url, ref_from=ref_from)
            filename = get_url_filename(abs_url)
            if filename != old_filename:
                return url

            fragment = abs_url.partition("#")[2]
            new_url = new_filename + ("#" + fragment if fragment else "")
            return urlrelpath(new_url, ref_from=ref_from)

        for anchor_name, uri in list(getattr(self, "anchor_uri", {}).items()):
            if remove_url_fragment(uri) == old_filename:
                fragment = uri.partition("#")[2]
                self.anchor_uri[anchor_name] = new_filename + ("#" + fragment if fragment else "")

        for guide_entry in self.guide:
            if guide_entry.target and remove_url_fragment(guide_entry.target) == old_filename:
                fragment = guide_entry.target.partition("#")[2]
                guide_entry.target = new_filename + ("#" + fragment if fragment else "")

        for page_entry in self.pagemap:
            if page_entry.target and remove_url_fragment(page_entry.target) == old_filename:
                fragment = page_entry.target.partition("#")[2]
                page_entry.target = new_filename + ("#" + fragment if fragment else "")

        def update_toc(ncx_toc):
            for toc_entry in ncx_toc:
                if toc_entry.target and remove_url_fragment(toc_entry.target) == old_filename:
                    fragment = toc_entry.target.partition("#")[2]
                    toc_entry.target = new_filename + ("#" + fragment if fragment else "")

                if toc_entry.children:
                    update_toc(toc_entry.children)

        update_toc(self.ncx_toc)

        for book_part in self.book_parts:
            for elem in book_part.html.iter("*"):
                for attr in ["href", "src", XLINK_HREF]:
                    if attr in elem.attrib:
                        elem.set(attr, replace_url(elem.get(attr), book_part.filename))

    def prepare_dom_image_sequence_widths(self):
        seen_filenames = set()
        role_counts = collections.defaultdict(int)

        for book_part in self.book_parts:
            for elem in book_part.body().iter("*"):
                attr = self.image_reference_attr(elem)
                if attr is None:
                    continue

                filename = get_url_filename(urlabspath(elem.get(attr), ref_from=book_part.filename))
                filename = remove_url_fragment(filename or "")
                if not filename or filename in seen_filenames or self.is_cover_image_filename(filename):
                    continue

                seen_filenames.add(filename)
                role = self.dom_image_role(elem, filename)
                role_counts[role] += 1

        self.illustration_image_sequence_width = max(3, len(str(role_counts["illustration"] or 0)))
        self.page_image_sequence_width = max(3, len(str(role_counts["page"] or 0)))

    def prepare_dom_image_filenames(self, book_part):
        for elem in book_part.body().iter("*"):
            attr = self.image_reference_attr(elem)
            if attr is None:
                continue

            filename = get_url_filename(urlabspath(elem.get(attr), ref_from=book_part.filename))
            filename = remove_url_fragment(filename or "")
            if not filename or filename in self.dom_image_filenames or self.is_cover_image_filename(filename):
                continue

            new_filename = self.rename_image_file(
                filename,
                self.dom_image_role(elem, filename))
            if new_filename:
                self.dom_image_filenames.add(filename)
                self.dom_image_filenames.add(new_filename)

    def image_reference_attr(self, elem):
        if elem.tag == "img" and elem.get("src"):
            return "src"

        if elem.tag == qname(SVG_NS_URI, "image") and elem.get(XLINK_HREF):
            return XLINK_HREF

        return None

    def original_image_role(self, elem):
        parent = elem.getparent()
        while parent is not None and parent.tag != "body":
            if parent.tag == "div" and "main" not in parent.get("class", "").split():
                return "illustration"
            parent = parent.getparent()

        return "page"

    def dom_image_role(self, elem, filename):
        if elem.tag == "img" and "fit" in elem.get("class", "").split():
            return "illustration"

        return self.original_dom_image_roles.get(filename) or self.original_image_role(elem)

    def is_cover_image_filename(self, filename):
        manifest_entry = self.manifest_files.get(remove_url_fragment(filename))
        return manifest_entry is not None and manifest_entry.is_cover_image

    def is_illustration_image(self, elem):
        parent = elem.getparent()
        while parent is not None and parent.tag != "body":
            if parent.tag == "div" and "main" not in parent.get("class", "").split():
                return True
            parent = parent.getparent()

        return False

    def rename_image_file(self, old_filename, image_role, html_by_filename=None):
        old_filename = remove_url_fragment(old_filename)
        if old_filename not in self.oebps_files:
            return None

        ext = posixpath.splitext(old_filename)[1]
        if RESOURCE_TYPE_OF_EXT.get(ext) != "image":
            return None

        if image_role == "cover":
            new_filename = self.IMAGE_FILEPATH % ("cover" + ext)
        else:
            new_filename = self.numbered_image_filename(image_role, ext)

        if old_filename == new_filename:
            return new_filename

        output_file = self.oebps_files.pop(old_filename)
        self.oebps_files[new_filename] = output_file

        manifest_entry = self.manifest_files.pop(old_filename, None)
        if manifest_entry is not None:
            manifest_entry.filename = new_filename
            self.update_manifest_entry_id(manifest_entry, new_filename)
            self.manifest_files[new_filename] = manifest_entry

        if html_by_filename is None:
            html_by_filename = dict((book_part.filename, book_part.html) for book_part in self.book_parts)

        for filename, html in html_by_filename.items():
            for elem in html.iter("*"):
                for attr in ["src", XLINK_HREF]:
                    if attr in elem.attrib:
                        elem.set(attr, self.replace_resource_url(elem.get(attr), filename, old_filename, new_filename))

        return new_filename

    def update_manifest_entry_id(self, manifest_entry, filename):
        old_id = manifest_entry.id
        new_id = self.manifest_id_for_filename(filename)

        if old_id in self.manifest_ids:
            self.manifest_ids.remove(old_id)

        manifest_entry.id = make_unique_name(new_id, self.manifest_ids, sep="_")
        self.manifest_ids.add(manifest_entry.id)

    def replace_resource_url(self, url, ref_from, old_filename, new_filename):
        abs_url = urlabspath(url, ref_from=ref_from)
        filename = get_url_filename(abs_url)
        if remove_url_fragment(filename or "") != old_filename:
            return url

        fragment = abs_url.partition("#")[2]
        new_url = new_filename + ("#" + fragment if fragment else "")
        return urlrelpath(new_url, ref_from=ref_from)

    def link_css_file(self, book_part, css_file, css_type="text/css"):
        head = book_part.head()
        href = urlrelpath(css_file, ref_from=book_part.filename)

        if css_file == self.STYLES_CSS_FILEPATH:
            fixed_layout_href = urlrelpath(self.FIXED_LAYOUT_CSS_FILEPATH, ref_from=book_part.filename)
            for link in head.findall("link"):
                if link.get("rel") == "stylesheet" and link.get("href") == fixed_layout_href:
                    return

        elif css_file == self.FIXED_LAYOUT_CSS_FILEPATH:
            styles_href = urlrelpath(self.STYLES_CSS_FILEPATH, ref_from=book_part.filename)
            for link in list(head.findall("link")):
                if link.get("rel") == "stylesheet" and link.get("href") == styles_href:
                    head.remove(link)

        for link in head.findall("link"):
            if link.get("rel") == "stylesheet" and link.get("href") == href:
                return

        self.css_files.add(css_file)
        link = etree.SubElement(head, "link")
        link.set("rel", "stylesheet")
        link.set("type", css_type)
        link.set("href", href)

    def identify_cover(self):
        if self.book_parts:
            for g in sorted(self.guide, key=lambda ge: (ge.guide_type, ge.title)):
                if g.guide_type == "cover":
                    cover_page = remove_url_fragment(g.target)
                    break
            else:
                for manifest_entry in self.manifest:
                    if manifest_entry.is_cover_image:
                        cover_page = self.book_parts[0].filename
                        break
                else:
                    cover_page = None

            if cover_page:
                for book_part in self.book_parts:
                    if book_part.filename == cover_page:
                        book_part.is_cover_page = True

                        if book_part.part_index != 0:
                            log.warning("Cover page is not first in book: %s" % cover_page)

                        break
                else:
                    log.warning("Cover page %s not found in book" % cover_page)

    def do_remove_html_cover(self):
        for i, book_part in enumerate(self.book_parts):
            if book_part.is_cover_page:
                self.book_parts.pop(i)
                break

        for i, g in enumerate(self.guide):
            if g.guide_type == "cover":
                self.guide.pop(i)
                break

    def add_generic_cover_page(self):
        for i, book_part in enumerate(self.book_parts):
            if book_part.is_cover_page:
                return

        message = self.title

        img = Image.new("RGB", (600, 800), (220, 220, 220))
        draw = ImageDraw.Draw(img)

        try:
            if get_path is not None:
                font = ImageFont.truetype(get_path("fonts/liberation/LiberationSans-Bold.ttf"), 20)
            else:
                font = ImageFont.truetype("arial.ttf", 20)
        except Exception:
            font = ImageFont.load_default()

        for i, word in enumerate(message.split()):
            draw.text((300, (img.size[1] / 4) + (i * 30)), word, fill=(0, 0, 0), font=font, anchor="ms")

        outfile = io.BytesIO()
        img.save(outfile, "JPEG")
        img.close()
        image_data = outfile.getvalue()

        image_filename = self.IMAGE_FILEPATH % "cover.jpg"
        manifest_entry = self.manifest_resource(image_filename, data=image_data)
        if manifest_entry is not None:
            manifest_entry.is_cover_image = True

        book_part = self.new_book_part(filename=self.COVER_FILEPATH, first=True)
        div_elem = etree.SubElement(book_part.body(), "div")
        etree.SubElement(div_elem, "img", attrib={
            "src": urlrelpath(image_filename, ref_from=book_part.filename),
            "style": "width: 100%"})

        self.add_guide_entry("cover", target=book_part.filename)

    def is_book_part_filename(self, filename):
        for book_part in self.book_parts:
            if book_part.filename == filename:
                return True

        return False

    def compare_fixed_layout_viewports(self):
        viewport_count = collections.defaultdict(int)
        cover_width, cover_height = None, None
        for book_part in self.book_parts:
            head = book_part.html.find("head")
            if head is not None:
                for meta in head.iterfind("meta"):
                    if meta.get("name") == "viewport":
                        content = meta.get("content", "")
                        mw = re.search("width=([0-9]+)", content)
                        mh = re.search("height=([0-9]+)", content)
                        if mw and mh:
                            width = int(mw.group(1))
                            height = int(mh.group(1))
                            viewport_count[(width, height)] += 1
                            if book_part.is_cover_page:
                                cover_width, cover_height = width, height
                            if width < 100 or height < 100:
                                log.warning("Fixed-layout viewport %s is too small: %s" % (book_part.filename, content))

        if len(viewport_count) > 0:
            viewports_by_count = sorted(viewport_count.items(), key=lambda x: -x[1])
            (self.original_width, self.original_height), best_count = viewports_by_count[0]

            if len(viewports_by_count) > 1:
                best_aspect_ratio = self.original_width / (self.original_height or 1)
                conflicts = [(best_count, best_aspect_ratio, self.original_width, self.original_height)]

                for (width, height), count in viewports_by_count:
                    aspect_ratio = width / (height or 1)
                    if not (
                            aspect_ratio_match(best_aspect_ratio, aspect_ratio) or
                            aspect_ratio_match(best_aspect_ratio, aspect_ratio * 2) or
                            aspect_ratio_match(best_aspect_ratio, aspect_ratio / 2) or
                            (self.is_comic and width == cover_width and height == cover_height and count == 1)):
                        conflicts.append((count, aspect_ratio, width, height))

                if len(conflicts) > 1:
                    log.info("Conflicting viewport aspect ratios: %s" % ", ".join(["%d @ %f (%dw x %dh)" % x for x in conflicts]))

    def check_epub_version(self):
        if not self.generate_epub2:
            return

        if self.fixed_layout or self.author_pronunciations or self.title_pronunciation:
            self.generate_epub2 = False
            return

        for oebps_file in self.oebps_files.values():
            if oebps_file.mimetype in ["application/octet-stream", "application/xml", "text/javascript", "text/html"]:
                self.generate_epub2 = False
                return

        for book_part in self.book_parts:
            if book_part.is_nav or book_part.is_fxl:
                self.generate_epub2 = False
                return

            for elem in book_part.html.iter("*"):
                if elem.tag in {
                        "article", "aside", "audio", "bdi", "canvas", "details", "dialog", "embed", "figcaption", "figure", "footer",
                        "header", "main", "mark", "meter", "nav", "picture", "progress", "rt", "ruby", "section", "source",
                        "summary", "template", "time", "track", "video", "wbr"}:
                    self.generate_epub2 = False
                    return

                for attrib in elem.attrib.keys():
                    if attrib.startswith("data-") or attrib in [EPUB_PREFIX, EPUB_TYPE]:
                        self.generate_epub2 = False
                        return

    TEXT_BLOCK_TAGS = {"p", "h1", "h2", "h3", "h4", "h5", "h6", "li", "td", "th", "dt", "dd", "blockquote", "nav"}

    def is_image_only_body(self, body):
        has_text_block = any(body.find(".//%s" % tag) is not None for tag in self.TEXT_BLOCK_TAGS)
        if has_text_block:
            return False

        has_image = body.find(".//img") is not None or body.find(".//%s" % SVG) is not None
        return has_image

    def fixed_layout_viewport_size(self, head):
        for meta in head.findall("meta"):
            if meta.get("name") != "viewport":
                continue

            content = meta.get("content", "")
            mw = re.search("width=([0-9]+)", content)
            mh = re.search("height=([0-9]+)", content)
            if mw and mh:
                return int(mw.group(1)), int(mh.group(1))

        return None, None

    def fixed_layout_image_file_size(self, src, ref_from):
        filename = get_url_filename(urlabspath(src, ref_from=ref_from))
        filename = remove_url_fragment(filename or "")
        image_file = self.oebps_files.get(filename)
        if image_file is not None and image_file.width and image_file.height:
            return image_file.width, image_file.height

        return None, None

    def fixed_layout_length(self, value):
        if value is None:
            return None

        match = re.match(r"^([0-9]+)(?:px)?$", str(value).strip())
        if match:
            return int(match.group(1))

        return None

    def fixed_layout_svg_viewbox_size(self, svg):
        match = re.match(r"^\s*0\s+0\s+([0-9]+)\s+([0-9]+)\s*$", svg.get("viewBox", ""))
        if match:
            return int(match.group(1)), int(match.group(2))

        return None, None

    def extract_single_fixed_layout_image(self, book_part, body):
        image_sources = []

        for img in body.findall(".//img"):
            src = img.get("src")
            if not src:
                continue

            width = self.fixed_layout_length(img.get("width"))
            height = self.fixed_layout_length(img.get("height"))
            if width is None or height is None:
                width, height = self.fixed_layout_image_file_size(src, book_part.filename)
            image_sources.append((src, width, height))

        for svg in body.findall(".//%s" % SVG):
            viewbox_width, viewbox_height = self.fixed_layout_svg_viewbox_size(svg)
            for image in svg.findall(".//%s" % qname(SVG_NS_URI, "image")):
                src = image.get(XLINK_HREF) or image.get("href")
                if not src:
                    continue

                width = self.fixed_layout_length(image.get("width")) or viewbox_width
                height = self.fixed_layout_length(image.get("height")) or viewbox_height
                if width is None or height is None:
                    width, height = self.fixed_layout_image_file_size(src, book_part.filename)
                image_sources.append((src, width, height))

        if len(image_sources) != 1:
            log.warning("Fixed-layout page %s has %d image sources; keeping original XHTML" % (book_part.filename, len(image_sources)))
            return None, None, None

        src, width, height = image_sources[0]
        filename = get_url_filename(urlabspath(src, ref_from=book_part.filename))
        filename = remove_url_fragment(filename or "")
        if filename:
            self.original_dom_image_roles[filename] = "illustration"

        return src, width, height

    def order_fixed_layout_head(self, book_part, head):
        self.link_css_file(book_part, self.FIXED_LAYOUT_CSS_FILEPATH)
        fixed_layout_href = urlrelpath(self.FIXED_LAYOUT_CSS_FILEPATH, ref_from=book_part.filename)

        charset_meta = next((meta for meta in head.findall("meta") if meta.get("charset") is not None), None)
        title = head.find("title")
        fixed_layout_link = next((link for link in head.findall("link") if link.get("rel") == "stylesheet" and link.get("href") == fixed_layout_href), None)
        viewport_meta = next((meta for meta in head.findall("meta") if meta.get("name") == "viewport"), None)

        ordered = [e for e in [charset_meta, title, fixed_layout_link, viewport_meta] if e is not None]
        for elem in ordered:
            head.remove(elem)

        for index, elem in enumerate(ordered):
            head.insert(index, elem)

    def convert_to_fixed_layout_svg_page(self, book_part, head, body):
        src, image_width, image_height = self.extract_single_fixed_layout_image(book_part, body)
        if not src:
            return

        viewport_width, viewport_height = self.fixed_layout_viewport_size(head)
        width = viewport_width or image_width or self.original_width
        height = viewport_height or image_height or self.original_height
        if not width or not height:
            log.warning("Fixed-layout page %s has no usable viewport or image size; keeping original XHTML" % book_part.filename)
            return

        self.order_fixed_layout_head(book_part, head)

        body.clear()
        if book_part.is_cover_page:
            body.set(EPUB_TYPE, "cover")
        body.text = "\n"

        main = etree.SubElement(body, "div")
        main.set("class", "main")
        main.text = "\n\n"
        main.tail = "\n"

        svg = etree.SubElement(main, SVG, nsmap=SVG_NAMESPACES)
        svg.set("version", "1.1")
        svg.set("width", "100%")
        svg.set("height", "100%")
        svg.set("viewBox", "0 0 %d %d" % (width, height))
        svg.text = "\n"
        svg.tail = "\n\n"

        image = etree.SubElement(svg, qname(SVG_NS_URI, "image"))
        image.set("width", "%d" % width)
        image.set("height", "%d" % height)
        image.set(XLINK_HREF, src)
        image.tail = "\n"

    def ensure_main_wrapper(self, body):
        # ebpaj reflowable pages use a single body > div.main container.
        if len(body) == 1 and body[0].tag == "div" and "main" in body[0].get("class", "").split():
            return

        main = etree.Element("div")
        main.set("class", "main")
        main.text = (body.text or "") + "\n"
        body.text = None

        for child in list(body):
            body.remove(child)
            main.append(child)
            if not child.tail:
                child.tail = "\n"

        main.tail = "\n"
        body.append(main)

    def convert_to_fit_image_page(self, book_part):
        # ebpaj single-image page: <body class="p-image"><div class="main"><p><img class="fit"/></p>
        # Cover pages use <body epub:type="cover" class="p-cover">.
        body = book_part.body()
        image_sources = []

        for img in body.findall(".//img"):
            src = img.get("src")
            if src:
                filename = get_url_filename(urlabspath(src, ref_from=book_part.filename))
                filename = remove_url_fragment(filename or "")
                if filename:
                    self.original_dom_image_roles.setdefault(filename, self.original_image_role(img))
                image_sources.append((src, img.get("alt", "")))

        for svg in body.findall(".//%s" % SVG):
            for image in svg.findall(".//%s" % qname(SVG_NS_URI, "image")):
                src = image.get(XLINK_HREF) or image.get("href")
                if src:
                    filename = get_url_filename(urlabspath(src, ref_from=book_part.filename))
                    filename = remove_url_fragment(filename or "")
                    if filename:
                        self.original_dom_image_roles.setdefault(filename, self.original_image_role(svg))
                    image_sources.append((src, ""))

        if not image_sources:
            return

        body.clear()
        if book_part.is_cover_page:
            body.set(EPUB_TYPE, "cover")
            body.set("class", "p-cover")
        else:
            body.set("class", "p-image")
        body.text = "\n"
        main = etree.SubElement(body, "div")
        main.set("class", "main")
        main.text = "\n"
        main.tail = "\n"

        for src, alt in image_sources:
            p = etree.SubElement(main, "p")
            p.tail = "\n"
            img = etree.SubElement(p, "img", attrib={"class": "fit", "src": src, "alt": alt})

    def finalize_xhtml_structure(self, book_part, head, body):
        # B2: prepend <meta charset="UTF-8"/> and order head as meta -> title -> link
        for meta in head.findall("meta"):
            if meta.get("charset") is not None:
                head.remove(meta)

        charset_meta = etree.Element("meta")
        charset_meta.set("charset", "UTF-8")
        head.insert(0, charset_meta)

        title = head.find("title")
        if title is not None:
            head.remove(title)
            head.insert(1, title)

        if not book_part.html.get(XML_LANG):
            book_part.html.set(XML_LANG, self.language if self.language else "ja")

        # Direction class on <html>. Fixed-layout pages follow the ebpaj FXL template (no class, viewport governs).
        existing = book_part.html.get("class", "").split()
        existing = [c for c in existing if c not in ("vrtl", "hltr", "vltr", "hrtl")]

        if book_part.is_nav:
            body.attrib.pop("class", None)
            body.attrib.pop("style", None)
        elif book_part.is_fxl:
            self.convert_to_fixed_layout_svg_page(book_part, head, body)
        else:
            image_only = self.is_image_only_body(body)

            if image_only:
                self.convert_to_fit_image_page(book_part)
            else:
                self.ensure_main_wrapper(body)
                if book_part.filename == self.TOC_FILEPATH and not body.get("class"):
                    body.set("class", "p-toc")
                elif book_part.filename == self.COLOPHON_FILEPATH and not body.get("class"):
                    body.set("class", "p-colophon")
                elif not body.get("class"):
                    body.set("class", "p-text")
                if book_part.has_vertical_rl_class:
                    existing.insert(0, "vrtl")

        if existing:
            book_part.html.set("class", " ".join(existing))
        else:
            book_part.html.attrib.pop("class", None)

    def format_xhtml_document_header(self, html_str):
        declaration = b'<?xml version="1.0" encoding="UTF-8"?>\n'
        html_str = declaration + html_str
        html_str = html_str.replace(b"\n class=", b" class=")
        html_str = html_str.replace(b"<html ", b"<html\n ", 1)
        html_str = html_str.replace(b'" xmlns:epub=', b'"\n xmlns:epub=', 1)
        html_str = html_str.replace(b'" xml:lang=', b'"\n xml:lang=', 1)

        html_tag_end = html_str.find(b">\n<head>")
        if html_tag_end >= 0:
            html_tag = html_str[:html_tag_end]
            html_tag = html_tag.replace(b'" class=', b'"\n class=', 1)
            html_str = html_tag + html_str[html_tag_end:]

        html_str = html_str.replace(b'">\n<head>', b'"\n>\n<head>', 1)
        html_str = html_str.replace(b"</head>\n<body", b"</head>\n\n<body", 1)
        return html_str

    def save_book_parts(self):
        for book_part in self.book_parts:
            if self.DEBUG:
                log.debug("%s: %s" % (book_part.filename, etree.tostring(book_part.html)))

            book_part.html.tag = HTML

            head = book_part.head()
            body = book_part.body()

            if head.find("title") is None:
                title = etree.SubElement(head, "title")
                title.text = self.title

            self.finalize_xhtml_structure(book_part, head, body)

            if CONSOLIDATE_HTML:
                self.consolidate_html(body)

            if BEAUTIFY_HTML:
                self.beautify_html(book_part)

        self.prepare_dom_image_sequence_widths()

        for book_part in self.book_parts:
            body = book_part.body()
            self.prepare_dom_image_filenames(book_part)

            if body.find(".//%s" % SVG) is not None:
                book_part.opf_properties.add("svg")

            if body.find(".//{*}math") is not None:
                book_part.opf_properties.add("mathml")

            for e in body.iterfind(".//*[@src]"):
                src = e.get("src", "")
                if (src.startswith("http://") or src.startswith("https://")):
                    book_part.opf_properties.add("remote-resources")
                    break

            for e in body.iterfind(".//*"):
                if e.get(EPUB_TYPE, "").startswith("amzn:"):
                    book_part.html.set(
                        qname(EPUB_NS_URI, "prefix"),
                        "amzn: https://kindlegen.s3.amazonaws.com/AmazonKindlePublishingGuidelines.pdf")
                    break

            etree.cleanup_namespaces(book_part.html, keep_ns_prefixes=["epub"])

            if self.DEBUG:
                log.debug("%s: %s" % (book_part.filename, etree.tostring(book_part.html)))

            if not book_part.omit:
                document = etree.ElementTree(book_part.html)
                doctype = b"<!DOCTYPE html PUBLIC '-//W3C//DTD XHTML 1.1//EN' 'http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd'>"

                html_str = etree.tostring(document, encoding="utf-8", doctype=doctype, xml_declaration=False)

                if not self.generate_epub2:
                    html_str = html_str.replace(doctype + b"\n", b"<!DOCTYPE html>\n")

                html_str = self.format_xhtml_document_header(html_str)

                self.manifest_resource(
                    book_part.filename, book_part.opf_properties, book_part.linear, idref=book_part.idref,
                    data=html_str, mimetype="application/xhtml+xml")

        if hasattr(self, "refresh_book_style_css"):
            self.refresh_book_style_css()

    def consolidate_html(self, body):

        for toptag in body.findall("*"):
            changed = True
            while changed:
                changed = False
                for e in toptag.iterdescendants():
                    if e.tag in {"a", "b", "em", "i", "span", "strong", "sub", "sup", "u"}:
                        n = e.getnext()
                        while ((not e.tail) and (n is not None) and n.tag == e.tag and
                                tuple(sorted(dict(e.attrib).items())) == tuple(sorted(dict(n.attrib).items()))):

                            if n.text:
                                if len(e) > 0:
                                    tt = e[-1]
                                    tt.tail = (tt.tail + n.text) if tt.tail else n.text
                                else:
                                    e.text = (e.text + n.text) if e.text else n.text

                                n.text = ""

                            while len(n) > 0:
                                c = n[0]
                                n.remove(c)
                                e.append(c)

                            if n.tail:
                                e.tail = n.tail

                            n.getparent().remove(n)

                            changed = True

                            n = e.getnext()

                        if changed:
                            break

        TEMP_TAG = "temporary-tag"
        temp_tag_used = False

        for e in body.iter("span"):
            if e.tag == "span" and len(e.attrib) == 0:
                e.tag = TEMP_TAG
                temp_tag_used = True

        if temp_tag_used:
            etree.strip_tags(body, TEMP_TAG)
            temp_tag_used = False

        while True:
            for e in body.iter("div"):
                if len(e.attrib) == 0:
                    parent = e.getparent()
                    if len(parent) == 1 and not parent.text:
                        if parent.tag in {
                                "aside", "caption", "div", "figure", "h1", "h2", "h3", "h4", "h5", "h6", "li", "p", "td",
                                IDX_ENTRY}:
                            e.tag = TEMP_TAG
                        elif parent.tag == "body" and not e.text:
                            for child in e.iterfind("*"):
                                if (child.tag not in {
                                        "aside", "div", "figure", "h1", "h2", "h3", "h4", "h5", "h6", "hr", "iframe",
                                        "ol", "p", "table", "ul", IDX_ENTRY} or child.tail):
                                    break
                            else:
                                e.tag = TEMP_TAG

                        if e.tag == TEMP_TAG:
                            etree.strip_tags(parent, TEMP_TAG)
                            break
            else:
                break

    def beautify_html(self, book_part):
        html = book_part.html
        head = book_part.head()
        body = book_part.body()

        for e in [html] + html.findall("*") + head.findall("*") + body.findall("*"):
            if e.tag in {HTML, "head", "body"}:
                e.text = (e.text or "") + "\n"

            if e.tag in {
                    "aside", "body", "div", "figure", "h1", "h2", "h3", "h4", "h5", "h6", "head", "hr",
                    "link", "meta", "nav", "ol", "p", "style", "table", "title", "ul", IDX_ENTRY}:
                e.tail = (e.tail or "") + "\n"

            if e.tag == "div" and e.get("id", "").startswith("amzn_master_range_"):
                for ee in e.iterfind("*"):
                    if ee.tag in {"aside", "div", "figure", "h1", "h2", "h3", "h4", "h5", "h6",
                                  "hr", "p", "table", "ul", "ol", IDX_ENTRY} and not ee.tail:
                        ee.tail = "\n"

        for nav in body.iterfind(".//nav"):
            nav.text = (nav.text or "") + "\n"
            for e in nav.iterdescendants():
                if e.tag == "ol" and not e.text:
                    e.text = "\n"
                if e.tag in {"h1", "h2", "h3", "h4", "h5", "h6", "li", "ol"} and not e.tail:
                    e.tail = "\n"

    def opf_tag_name(self, elem):
        ns = namespace(elem.tag)
        name = localname(elem.tag)
        if ns == DC_NS_URI:
            return "dc:" + name
        return name

    def opf_attr_name(self, attr):
        ns = namespace(attr)
        name = localname(attr)
        if ns == XML_NS_URI:
            return "xml:" + name
        return name

    def opf_attrs(self, elem, order=()):
        items = dict((self.opf_attr_name(k), v) for k, v in elem.attrib.items())
        names = [n for n in order if n in items] + sorted(n for n in items if n not in order)
        return "".join(' %s="%s"' % (n, xml.sax.saxutils.escape(str(items[n]), {'"': '&quot;'})) for n in names)

    def opf_element_line(self, elem, order=()):
        tag = self.opf_tag_name(elem)
        attrs = self.opf_attrs(elem, order)
        text = elem.text or ""
        if len(elem) == 0 and text == "":
            return "<%s%s/>" % (tag, attrs)
        if len(elem) == 0:
            return "<%s%s>%s</%s>" % (tag, attrs, xml.sax.saxutils.escape(str(text)), tag)
        return etree.tostring(elem, encoding="unicode", pretty_print=True).strip()

    def serialize_opf_metadata(self, metadata):
        children = list(metadata)
        consumed = set()

        def take(pred):
            result = []
            for i, child in enumerate(children):
                if i not in consumed and pred(child):
                    consumed.add(i)
                    result.append((i, child))
            return result

        title_items = take(lambda e: e.tag == qname(DC_NS_URI, "title"))
        title_refines = take(lambda e: e.tag == "meta" and e.get("refines") == "#title")
        creator_items = []
        for i, child in enumerate(children):
            if i in consumed or child.tag != qname(DC_NS_URI, "creator"):
                continue
            consumed.add(i)
            block = [child]
            creator_id = child.get("id")
            if creator_id:
                for j, meta in enumerate(children):
                    if j not in consumed and meta.tag == "meta" and meta.get("refines") == "#" + creator_id:
                        consumed.add(j)
                        block.append(meta)
            creator_items.append(block)

        publisher_items = take(lambda e: e.tag == qname(DC_NS_URI, "publisher"))
        language_items = take(lambda e: e.tag == qname(DC_NS_URI, "language"))
        identifier_items = take(lambda e: e.tag == qname(DC_NS_URI, "identifier"))
        modified_items = take(lambda e: e.tag == "meta" and e.get("property") == "dcterms:modified")
        etc_items = [(i, e) for i, e in enumerate(children) if i not in consumed]

        lines = ['<metadata xmlns:dc="%s">' % DC_NS_URI, ""]

        if title_items:
            lines.extend(["<!-- 作品名 -->"])
            lines.extend(self.opf_element_line(e, ("id",)) for i, e in title_items)
            lines.extend(self.opf_element_line(e, ("refines", "property", "scheme")) for i, e in title_refines)
            lines.append("")

        if creator_items:
            lines.extend(["<!-- 著者名 -->"])
            for block_index, block in enumerate(creator_items):
                if block_index:
                    lines.append("")
                for elem in block:
                    lines.append(self.opf_element_line(elem, ("id", "refines", "property", "scheme")))
            lines.append("")

        if publisher_items:
            lines.extend(["<!-- 出版社名 -->"])
            lines.extend(self.opf_element_line(e, ("id",)) for i, e in publisher_items)
            lines.append("")

        if language_items:
            lines.extend(["<!-- 言語 -->"])
            lines.extend(self.opf_element_line(e) for i, e in language_items)
            lines.append("")

        if identifier_items:
            lines.extend(["<!-- ファイルid -->"])
            lines.extend(self.opf_element_line(e, ("id", "opf:scheme")) for i, e in identifier_items)
            lines.append("")

        if modified_items:
            lines.extend(["<!-- 更新日 -->"])
            lines.extend(self.opf_element_line(e, ("property",)) for i, e in modified_items)
            lines.append("")

        if etc_items:
            lines.extend(["<!-- etc. -->"])
            lines.extend(self.opf_element_line(e, ("name", "content", "property", "refines", "scheme")) for i, e in etc_items)
            lines.append("")

        lines.append("</metadata>")
        return "\n".join(lines)

    def opf_manifest_category(self, item):
        href = item.get("href", "")
        if item.get("properties") == "nav" or href.startswith("navigation-documents"):
            return "navigation"
        if href.startswith("style/"):
            return "style"
        if href.startswith("image/"):
            return "image"
        if href.startswith("xhtml/"):
            return "xhtml"
        if href.startswith("fonts/"):
            return "font"
        return "misc"

    def serialize_opf_manifest(self, manifest):
        groups = collections.OrderedDict((name, []) for name in ["navigation", "style", "image", "xhtml", "font", "misc"])
        for item in manifest:
            groups[self.opf_manifest_category(item)].append(item)

        lines = ["<manifest>", ""]
        for category, items in groups.items():
            if not items:
                continue
            lines.append("<!-- %s -->" % category)
            lines.extend(self.opf_element_line(item, ("media-type", "id", "href", "properties")) for item in items)
            lines.append("")
        lines.append("</manifest>")
        return "\n".join(lines)

    def serialize_opf_spine(self, spine):
        attrs = self.opf_attrs(spine, ("toc", "page-progression-direction"))
        lines = ["<spine%s>" % attrs, ""]
        lines.extend(self.opf_element_line(itemref, ("idref", "linear", "properties")) for itemref in spine)
        lines.extend(["", "</spine>"])
        return "\n".join(lines)

    def serialize_standard_opf(self, package):
        metadata = package.find("metadata")
        manifest = package.find("manifest")
        spine = package.find("spine")
        lines = ['<?xml version="1.0" encoding="UTF-8"?>', '<package']
        lines.append(' xmlns="%s"' % OPF_NS_URI)
        lines.append(' version="%s"' % package.get("version", "3.0"))
        xml_lang = package.get(XML_LANG)
        if xml_lang:
            lines.append(' xml:lang="%s"' % xml.sax.saxutils.escape(xml_lang, {'"': '&quot;'}))
        lines.append(' unique-identifier="%s"' % package.get("unique-identifier", "ASIN"))
        prefix_value = package.get("prefix")
        if prefix_value:
            prefix_lines = prefix_value.split("\n")
            lines.append(' prefix="%s' % xml.sax.saxutils.escape(prefix_lines[0], {'"': '&quot;'}))
            for pfx in prefix_lines[1:]:
                lines.append('         %s' % xml.sax.saxutils.escape(pfx, {'"': '&quot;'}))
            lines[-1] += '"'
        lines.extend([">", ""])
        lines.append(self.serialize_opf_metadata(metadata))
        lines.append("")
        lines.append(self.serialize_opf_manifest(manifest))
        lines.append("")
        lines.append(self.serialize_opf_spine(spine))
        lines.append("")
        lines.append("</package>")
        return ("\n".join(lines) + "\n").encode("utf-8")

    def create_opf(self):

        def add_metadata_meta_name_content(name, content):
            add_meta_name_content(metadata, name, content)

        def add_metadata_meta_property(prop, text):
            meta_property = add_attribs(etree.SubElement(metadata, "meta"), "property", prop)
            meta_property.text = text

        def add_metadata_meta_refines_property(refines, prop, text, scheme=None):
            meta_refines = add_attribs(etree.SubElement(metadata, "meta"), "refines", refines, "property", prop)
            if scheme:
                meta_refines.set("scheme", scheme)
            meta_refines.text = text

        def prefix(value):
            if ":" in value:
                used_prefixes.add(value.partition(":")[0])
            return value

        used_prefixes = set()

        ALT_OPF_NS_URI = OPF_NS_URI + "?"
        OPF_NAMESPACES = {
            None: OPF_NS_URI,
            "dc": DC_NS_URI,
            "opf": ALT_OPF_NS_URI,
        }
        package = etree.Element(qname(OPF_NS_URI, "package"), nsmap=OPF_NAMESPACES, attrib={
            "version": ("2.0" if self.generate_epub2 else "3.0"), "unique-identifier": "ASIN"})
        if not self.generate_epub2:
            package.set(XML_LANG, self.language if self.language else "ja")

        metadata = etree.SubElement(package, "metadata")

        identifier = etree.SubElement(metadata, qname(DC_NS_URI, "identifier"), attrib={"id": "ASIN"})
        identifier.text = self.uid

        if self.uid.startswith("urn:uuid:") and self.generate_epub2:
            identifier.set(qname(ALT_OPF_NS_URI, "scheme"), "uuid")

        title = etree.SubElement(metadata, qname(DC_NS_URI, "title"), attrib={"id": "title"})
        title.text = self.title
        if self.title_pronunciation and not self.generate_epub2:
            add_metadata_meta_refines_property("#title", "file-as", self.title_pronunciation)

        for i, author in enumerate(self.authors):
            creator = etree.SubElement(metadata, qname(DC_NS_URI, "creator"))
            creator.text = author

            if not self.generate_epub2:
                author_id = "creator%02d" % (i + 1)
                creator.set("id", author_id)

                add_metadata_meta_refines_property("#" + author_id, "role", "aut", prefix("marc:relators"))

                if len(self.author_pronunciations) > i:
                    add_metadata_meta_refines_property("#" + author_id, "file-as", self.author_pronunciations[i])
            else:
                creator.set(qname(ALT_OPF_NS_URI, "role"), "aut")

        language = etree.SubElement(metadata, qname(DC_NS_URI, "language"))
        language.text = self.language if self.language else "en"

        if self.publisher:
            publisher = etree.SubElement(metadata, qname(DC_NS_URI, "publisher"), attrib={"id": "publisher"})
            publisher.text = self.publisher

        if self.pubdate:
            pubdate = etree.SubElement(metadata, qname(DC_NS_URI, "date"))

            if self.generate_epub2:
                pubdate.set(qname(ALT_OPF_NS_URI, "event"), "publication")

            pubdate.text = str(self.pubdate)[0:10]

        if self.description:
            description = etree.SubElement(metadata, qname(DC_NS_URI, "description"))
            description.text = self.description

        if self.subject:
            subject = etree.SubElement(metadata, qname(DC_NS_URI, "subject"))
            subject.text = self.subject

        if self.rights:
            rights = etree.SubElement(metadata, qname(DC_NS_URI, "rights"))
            rights.text = self.rights

        if not self.generate_epub2:
            add_metadata_meta_property("dcterms:modified", datetime.datetime.utcnow().replace(microsecond=0).isoformat() + "Z")
            add_metadata_meta_property("ebpaj:guide-version", "1.1.4")
            add_metadata_meta_property("dpfj:guide-version", "1.1.4")

        if self.fixed_layout:
            if not self.generate_epub2:
                add_metadata_meta_property(prefix("rendition:layout"), "pre-paginated")

            add_metadata_meta_name_content("fixed-layout", "true")

            if self.original_width and self.original_height:
                if self.orientation_lock == "none":
                    self.orientation_lock = "landscape" if self.original_width > self.original_height else "portrait"

                add_metadata_meta_name_content("original-resolution", "%dx%d" % (self.original_width, self.original_height))

        if self.scrolled_continuous and not self.generate_epub2:
            add_metadata_meta_property(prefix("rendition:flow"), "scrolled-continuous")

        if self.book_type in {"children", "comic"}:
            add_metadata_meta_name_content("book-type", self.book_type)

        if self.orientation_lock != "none":
            if not self.generate_epub2:
                add_metadata_meta_property(
                    prefix("rendition:orientation"), self.orientation_lock if self.orientation_lock != "none" else "auto")

            add_metadata_meta_name_content("orientation-lock", self.orientation_lock)

        if self.override_kindle_font:
            add_metadata_meta_name_content("Override-Kindle-Fonts", "true")

        if self.writing_mode == "horizontal-tb":
            primary_writing_mode = "horizontal-rl" if self.page_progression_direction == "rtl" else "horizontal-lr"
        else:
            primary_writing_mode = self.writing_mode

        if primary_writing_mode != "horizontal-lr":
            add_metadata_meta_name_content("primary-writing-mode", primary_writing_mode)

        if self.region_magnification:
            add_metadata_meta_name_content("RegionMagnification", "true")

        if self.virtual_panels:
            if not self.virtual_panels_allowed:
                log.error("Virtual panels used when not allowed")

            if self.region_magnification and not self.is_children:
                log.error("Virtual panels used with region magnification")

        if self.illustrated_layout:
            add_metadata_meta_name_content("amzn:kindle-illustrated", "true")

        if self.html_cover:
            add_metadata_meta_name_content("amzn:cover-as-html", "true")

        if self.guided_view_native:
            add_metadata_meta_name_content("amzn:guided-view-native", "true")

        if self.min_aspect_ratio:
            add_metadata_meta_name_content("amzn:min-aspect-ratio", value_str(self.min_aspect_ratio))

        if self.max_aspect_ratio:
            add_metadata_meta_name_content("amzn:max-aspect-ratio", value_str(self.max_aspect_ratio))

        if self.is_dictionary:
            x_metadata = etree.SubElement(metadata, "x-metadata")

            in_language = etree.SubElement(x_metadata, "DictionaryInLanguage")
            in_language.text = self.source_language

            out_language = etree.SubElement(x_metadata, "DictionaryOutLanguage")
            out_language.text = self.target_language

        if self.is_magazine:
            x_metadata = etree.SubElement(metadata, "x-metadata")

            etree.SubElement(x_metadata, "output", attrib={
                "content-type": "application/x-mobipocket-subscription-magazine", "encoding": "utf-8"})

        ebpaj_prefixes = collections.OrderedDict([
            ("ebpaj", "http://www.ebpaj.jp/"),
            ("dpfj", "https://www.dpfj.or.jp/"),
            ])
        for pfx, uri in ebpaj_prefixes.items():
            if pfx in used_prefixes:
                used_prefixes.discard(pfx)
        prefix_items = ["%s: %s" % item for item in ebpaj_prefixes.items()]
        prefix_items.extend(["%s: %s" % (p, RESERVED_OPF_VALUE_PREFIXES[p]) for p in sorted(list(used_prefixes))])
        if prefix_items and not self.generate_epub2:
            package.set("prefix", "\n".join(prefix_items))

        man = etree.SubElement(package, "manifest")
        toc_idref = None
        has_page_spread = False

        for manifest_entry in sorted(self.manifest, key=lambda m: m.filename):
            if manifest_entry.filename == self.ncx_location or manifest_entry.filename.endswith(".ncx"):
                toc_idref = manifest_entry.id

            if manifest_entry.external:
                mimetype = self.mimetype_of_filename(manifest_entry.filename)
                href = manifest_entry.filename
            else:
                mimetype = self.oebps_files[manifest_entry.filename].mimetype
                href = urlrelpath(manifest_entry.filename, ref_from=self.OPF_FILEPATH)

            if self.generate_epub2:
                mimetype = EPUB2_ALT_MIMETYPES.get(mimetype, mimetype)

            item = etree.SubElement(man, "item", attrib={
                "href": href, "id": manifest_entry.id, "media-type": mimetype})

            if manifest_entry.is_cover_image and not (self.html_cover or self.is_comic):
                add_metadata_meta_name_content("cover", manifest_entry.id)

            if self.fixed_layout:
                if manifest_entry.is_fxl:
                    manifest_entry.opf_properties.discard("rendition:layout-pre-paginated")
                else:
                    manifest_entry.opf_properties.add("rendition:layout-reflowable")

                if manifest_entry.opf_properties & {"page-spread-left", "page-spread-right"}:
                    has_page_spread = True

            item_properties = manifest_entry.opf_properties & MANIFEST_ITEM_PROPERTIES
            if len(item_properties) and not self.generate_epub2:
                item.set("properties", " ".join(sorted(list(item_properties))))

            unknown_properties = manifest_entry.opf_properties - OPF_PROPERTIES
            if len(unknown_properties):
                log.error("Manifest file %s has %d unknown OPF properties: \"%s\"" % (
                            manifest_entry.filename, len(unknown_properties), " ".join(sorted(list(unknown_properties)))))

        spine = etree.SubElement(package, "spine")

        if (self.generate_epub2 or self.GENERATE_EPUB2_COMPATIBLE) and toc_idref:
            spine.set("toc", toc_idref)

        if self.page_progression_direction != "ltr" and not self.generate_epub2:
            spine.set("page-progression-direction", self.page_progression_direction)

        for manifest_entry in self.manifest:
            if manifest_entry.linear is not None:
                itemref = etree.SubElement(spine, "itemref", attrib={"idref": manifest_entry.id})

                itemref_properties = manifest_entry.opf_properties & SPINE_ITEMREF_PROPERTIES

                if (tweaks.get("kfx_input_add_comic_spread_center", True) and self.is_comic
                        and has_page_spread and not itemref_properties):
                    itemref_properties.add("rendition:page-spread-center")

                if len(itemref_properties) and not self.generate_epub2:
                    itemref.set("properties", " ".join([prefix(p) for p in sorted(list(itemref_properties))]))

                if manifest_entry.linear is False:
                    itemref.set("linear", "no")

        if self.guide and (self.generate_epub2 or self.GENERATE_EPUB2_COMPATIBLE):
            gd = etree.SubElement(package, "guide")

            for g in self.guide:
                etree.SubElement(gd, "reference", attrib={
                    "type": g.guide_type, "title": g.title,
                    "href": urlrelpath(g.target, ref_from=self.OPF_FILEPATH)})

        etree.cleanup_namespaces(package)

        if self.generate_epub2:
            data = etree.tostring(package, encoding="utf-8", pretty_print=True, xml_declaration=True)
            data = data.replace(ALT_OPF_NS_URI.encode("utf-8"), OPF_NS_URI.encode("utf-8"))
        else:
            data = self.serialize_standard_opf(package)

        self.add_oebps_file(self.OPF_FILEPATH, data, "application/oebps-package+xml")

    def container_xml(self):
        return ("""<?xml version="1.0"?>
<container
 version="1.0"
 xmlns="urn:oasis:names:tc:opendocument:xmlns:container"
>
<rootfiles>
<rootfile
 full-path="%s"
 media-type="application/oebps-package+xml"
/>
</rootfiles>
</container>
""" % (self.OEBPS_DIR + self.OPF_FILEPATH)).encode("utf-8")

    def create_ncx(self):
        doctype = (
            None if not (self.generate_epub2 or GENERATE_EPUB2_NCX_DOCTYPE) else
            "<!DOCTYPE ncx PUBLIC \"-//NISO//DTD ncx 2005-1//EN\" \"http://www.daisy.org/z3986/2005/ncx-2005-1.dtd\">")

        emit_playorder = doctype is not None

        NCX_NAMESPACES = {
            None: NCX_NS_URI,
            "mbp": MBP_NS_URI
        }

        ncx = etree.Element(qname(NCX_NS_URI, "ncx"), nsmap=NCX_NAMESPACES, attrib={"version": "2005-1"})
        head = etree.SubElement(ncx, "head")
        add_meta_name_content(head, "dtb:uid", self.uid)

        doc_title = etree.SubElement(ncx, ("docTitle"))
        doc_title_text = etree.SubElement(doc_title, "text")
        doc_title_text.text = self.title

        for author in self.authors:
            doc_author = etree.SubElement(ncx, "docAuthor")
            doc_author_text = etree.SubElement(doc_author, "text")
            doc_author_text.text = author

        self.nav_id_count = 0
        self.uri_playorder = {}

        nav_map = etree.SubElement(ncx, "navMap")
        self.create_navmap(nav_map, self.ncx_toc, emit_playorder, 0)

        if len(self.pagemap) > 0:
            pl = etree.SubElement(ncx, "pageList")

            nl = etree.SubElement(pl, "navLabel")
            nlt = etree.SubElement(nl, "text")
            nlt.text = "Pages"
            page_ids = set()

            for p in self.pagemap:
                pt = etree.SubElement(pl, "pageTarget")

                p_id = make_unique_name(self.fix_html_id("page_%s" % p.label), page_ids, sep="_")
                pt.set("id", p_id)
                page_ids.add(p_id)

                if re.match("^[0-9]+$", p.label):
                    pt.set("value", p.label)
                    pt.set("type", "normal")
                elif re.match("^[ivx]+$", p.label, flags=re.IGNORECASE):
                    pt.set("value", str(roman_to_int(p.label)))
                    pt.set("type", "front")
                else:
                    pt.set("type", "special")

                if emit_playorder:
                    pt.set("playOrder", self.get_next_playorder(p.target))

                nl = etree.SubElement(pt, "navLabel")
                nlt = etree.SubElement(nl, "text")
                nlt.text = p.label

                ct = etree.SubElement(pt, "content")
                ct.set("src", urlrelpath(p.target, ref_from=self.NCX_FILEPATH))

        etree.cleanup_namespaces(ncx)

        data = etree.tostring(ncx, encoding="utf-8", pretty_print=True, xml_declaration=True, doctype=doctype)
        self.manifest_resource(self.NCX_FILEPATH, idref=self.toc_ncx_id, data=data, mimetype="application/x-dtbncx+xml")
        self.ncx_location = self.NCX_FILEPATH

    def get_next_playorder(self, uri):
        if (uri not in self.uri_playorder) or (uri is None):
            self.uri_playorder[uri] = "%d" % (len(self.uri_playorder) + 1)

        return self.uri_playorder[uri]

    def create_navmap(self, root, ncx_toc, emit_playorder, depth):
        for toc_entry in ncx_toc:
            nav_point = etree.SubElement(root, "navPoint", attrib={"id": "nav%d" % self.nav_id_count})

            if self.is_magazine and depth in PERIODICAL_NCX_CLASSES:
                nav_point.set("class", PERIODICAL_NCX_CLASSES[depth])

            if emit_playorder:
                nav_point.set("playOrder", self.get_next_playorder(toc_entry.target))

            self.nav_id_count += 1

            nav_label = etree.SubElement(nav_point, "navLabel")
            nav_label_text = etree.SubElement(nav_label, "text")
            nav_label_text.text = toc_entry.title

            if toc_entry.target:
                etree.SubElement(nav_point, "content", attrib={
                    "src": urlrelpath(toc_entry.target, ref_from=self.NCX_FILEPATH)})

            if toc_entry.description:
                meta = etree.SubElement(nav_point, qname(MBP_NS_URI, "meta"), attrib={"name": "description"})
                meta.text = toc_entry.description

            if toc_entry.icon:
                meta = etree.SubElement(nav_point, qname(MBP_NS_URI, "meta-img"), attrib={
                    "name": "mastheadImage",
                    "src": urlrelpath(toc_entry.icon, ref_from=self.NCX_FILEPATH)})

            if toc_entry.children:
                self.create_navmap(nav_point, toc_entry.children, emit_playorder, depth + 1)

    def create_epub3_nav(self):
        filename = self.NAV_FILEPATH % ""

        count = 0
        while self.is_book_part_filename(filename):
            filename = self.NAV_FILEPATH % ("%d" % count)
            count += 1

        book_part = self.new_book_part(filename=filename, linear=None)
        book_part.is_nav = True
        book_part.html.set(XML_LANG, self.language if self.language else "ja")

        body = etree.SubElement(book_part.html, "body")
        body.text = "\n"

        nav = etree.SubElement(body, "nav")
        nav.set(EPUB_TYPE, "toc")
        nav.set("id", "toc")
        nav.tail = "\n"

        if not self.is_magazine:
            self.hide_element(nav)

        h1 = etree.SubElement(nav, "h1")
        h1.text = "Navigation"

        self.create_nav_list(nav, self.ncx_toc, book_part)

        if self.guide:
            nav = etree.SubElement(body, "nav")
            nav.set(EPUB_TYPE, "landmarks")
            nav.tail = "\n"

            if not self.is_magazine:
                self.hide_element(nav)

            h2 = etree.SubElement(nav, "h2")
            h2.text = "Guide"

            ol = etree.SubElement(nav, "ol")

            for g in sorted(self.guide, key=lambda ge: (ge.guide_type, ge.title)):
                li = etree.SubElement(ol, "li")
                a = etree.SubElement(li, "a")

                if g.guide_type in EPUB3_VOCABULARY_OF_GUIDE_TYPE:
                    a.set(EPUB_TYPE, EPUB3_VOCABULARY_OF_GUIDE_TYPE[g.guide_type])

                a.set("href", urlrelpath(g.target, ref_from=book_part.filename))
                a.text = g.title

        if len(self.pagemap) > 0:
            nav = etree.SubElement(body, "nav")
            nav.set(EPUB_TYPE, "page-list")
            nav.tail = "\n"

            if not self.is_magazine:
                self.hide_element(nav)

            ol = etree.SubElement(nav, "ol")

            for p in self.pagemap:
                li = etree.SubElement(ol, "li")
                a = etree.SubElement(li, "a", attrib={
                    "href": urlrelpath(p.target, ref_from=book_part.filename)})
                a.text = p.label

    def hide_element(self, elem):
        if USE_HIDDEN_ATTRIBUTE:
            elem.set("hidden", "")
        else:
            self.add_style_(elem, {"display": "none"})

    def create_nav_list(self, parent, ncx_toc, book_part):
        ol = etree.SubElement(parent, "ol")

        for toc_entry in ncx_toc:
            li = etree.SubElement(ol, "li")
            a = etree.SubElement(li, "a")
            a.text = toc_entry.title or "."

            if toc_entry.target:
                a.set("href", urlrelpath(toc_entry.target, ref_from=book_part.filename))

            if toc_entry.children:
                self.create_nav_list(li, toc_entry.children, book_part)

    def zip_epub(self):
        file = io.BytesIO()

        with zipfile.ZipFile(file, "w", compression=zipfile.ZIP_DEFLATED) as zf:
            zf.writestr("mimetype", "application/epub+zip".encode("ascii"), compress_type=zipfile.ZIP_STORED)
            zf.writestr("META-INF/container.xml", self.container_xml())

            for filename, oebps_file in sorted(self.oebps_files.items()):
                zf.writestr(self.OEBPS_DIR + filename, oebps_file.binary_data)

        data = file.getvalue()
        file.close()

        return data

    def add_style_(self, elem, style):
        elem.set("style", " ".join(["%s: %s;" % (p, v) for p, v in style.items()]))

    def mimetype_of_filename(self, filename, default="application/octet-stream"):
        ext = posixpath.splitext("x" + filename)[1].lower()
        return MIMETYPE_OF_EXT.get(ext, default)

    def fixup_ns_prefixes(self, tree):

        def fixup(name, elem):
            fixed = memo.get(name)
            if fixed is not None:
                return fixed

            if name.startswith("{"):
                if name.startswith(default_ns_prefix):
                    memo[name] = tag = name[len(default_ns_prefix):]
                    return tag

            elif ":" in name:
                namespace, sep, localname = name.rpartition(":")
                uri = elem.nsmap.get(namespace) or tree.nsmap.get(namespace)
                if uri:
                    memo[name] = tag = qname(uri, localname)
                    return tag

                log.error("namespace of element %s is undefined in %s" % (name, repr(elem.nsmap)))

            return None

        default_ns = tree.nsmap.get(None)

        if default_ns is None:
            default_ns = XHTML_NS_URI

        default_ns_prefix = "{" + default_ns + "}"
        memo = {}

        for elem in tree.getiterator():
            name = fixup(elem.tag, elem)
            if name:
                elem.tag = name

            for key, value in list(elem.items()):
                name = fixup(key, elem)
                if name:
                    elem.attrib.pop(key)
                    elem.set(name, value)

        tree.tag = qname(default_ns, tree.tag)


def add_meta_name_content(elem, name, content):
    add_attribs(etree.SubElement(elem, "meta"), "name", name, "content", content)


def add_attribs(elem, *args):

    for i in range(0, len(args), 2):
        elem.set(args[i], args[i+1])

    return elem


def aspect_ratio_match(aspect_ratio1, aspect_ratio2):
    return abs(aspect_ratio1 - aspect_ratio2) / aspect_ratio1 <= 0.015


def remove_url_fragment(filename):
    return filename.partition("#")[0]


def value_str(quantity, unit="", emit_zero_unit=False):
    if quantity is None:
        return unit

    if abs(quantity) < 1e-6:
        q_str = "0"
    elif type(quantity) is float:
        q_str = "%g" % quantity
    else:
        q_str = str(quantity)

    if "e" in q_str.lower():
        q_str = "%.4f" % quantity

    if "." in q_str:
        q_str = q_str.rstrip("0").rstrip(".")

    if q_str == "0" and not emit_zero_unit:
        return q_str

    return q_str + unit


def split_value(val):
    num_match = re.match(r"^([+-]?[0-9]+\.?[0-9]*)", val)
    if not num_match:
        return (None, val)

    num = num_match.group(1)
    unit = val[len(num):]

    return (decimal.Decimal(num), unit)


def roman_to_int(input):
    input = input.upper()
    nums = ["M", "D", "C", "L", "X", "V", "I"]
    ints = [1000, 500, 100, 50, 10, 5, 1]
    places = []
    for c in input:

        if c not in nums:
            return 0

    for i in range(len(input)):
        c = input[i]
        value = ints[nums.index(c)]

        try:
            nextvalue = ints[nums.index(input[i + 1])]
            if nextvalue > value:
                value *= -1
        except IndexError:
            pass

        places.append(value)

    sum = 0
    for n in places:
        sum += n

    return sum


def nsprefix(s, namespaces=XHTML_NAMESPACES):
    prefix, sep, local = s.partition(":")
    ns = namespaces.get(prefix)
    if ns:
        return "{%s}%s" % (ns, local)

    return s


def set_nsmap(root, nsmap):

    if root.getparent() is not None:
        raise Exception("set_nsmap: root element has parent")

    new_nsmap = dict(root.nsmap)
    new_nsmap.update(nsmap)

    new_root = etree.Element(qname(new_nsmap.get(None), localname(root.tag)), nsmap=new_nsmap)
    new_root.text = root.text
    new_root.tail = root.tail

    for k, v in root.items():
        new_root.set(k, v)

    for child in root:
        root.remove(child)
        new_root.append(child)

    return new_root


def xhtmlns(name):
    return qname(XHTML_NS_URI, name)


def new_xhtml():
    return etree.Element(HTML, nsmap=XHTML_NAMESPACES)


def namespace(tag):
    if tag.startswith("{"):
        return tag[1:].partition("}")[0]

    return ""


def localname(tag):
    return tag.rpartition("}")[2]


def qname(ns, name):
    if name.startswith("{"):
        raise Exception("qname - name is already prefixed: %s" % name)

    return "".join(["{", ns, "}", name])


EPUB_PREFIX = qname(EPUB_NS_URI, "prefix")
EPUB_TYPE = qname(EPUB_NS_URI, "type")
HTML = xhtmlns("html")
IDX_ENTRY = qname(IDX_NS_URI, "entry")
IDX_ORTH = qname(IDX_NS_URI, "orth")
IDX_INFL = qname(IDX_NS_URI, "infl")
IDX_IFORM = qname(IDX_NS_URI, "iform")
MATH = qname(MATHML_NS_URI, "math")
SVG = qname(SVG_NS_URI, "svg")
XLINK_HREF = qname(XLINK_NS_URI, "href")
XML_LANG = qname(XML_NS_URI, "lang")
