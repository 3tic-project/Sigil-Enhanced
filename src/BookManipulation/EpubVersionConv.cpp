
#include "BookManipulation/EpubVersionConv.h"
#include "ResourceObjects/OPFResource.h"
#include "ResourceObjects/HTMLResource.h"
#include "ResourceObjects/NCXResource.h"
#include "BookManipulation/FolderKeeper.h"
#include "ResourceObjects/NavProcessor.h"
#include "Parsers/TagLister.h"

// constructor
EpubVersionConv::EpubVersionConv(QSharedPointer<Book> book) :
	m_Book(book)
{
	init_members();
	OPFResource *opf = m_Book->GetOPF();
	foreach(ManifestEntry entry, opf->p.m_manifest) {
		QString opf_href = entry.m_href;
		opfhref_to_id[opf_href] = entry.m_id;
	}
}
// destructor
EpubVersionConv::~EpubVersionConv() {
}

void EpubVersionConv::convert_to_epub2() {
	build_toc();
	convert_xhtml_to2();
	convert_opf_to2();
	m_Book->SetModified();
}

void EpubVersionConv::convert_to_epub3() {
	convert_xhtml_to3();
	convert_opf_to3();
	build_nav();
	m_Book->SetModified();
}

void EpubVersionConv::convert_xhtml_to3() {
	QList<HTMLResource *> html_resources = m_Book->GetHTMLResources(); 
	foreach(HTMLResource * res, html_resources) {
		QString text = convert_named_entities(res->GetText());
		QString new_text = "";
		TagLister taglist(text);
		
		int offset = 0;
		for (int i=0;i<taglist.size();i++){
			TagLister::TagInfo ti = taglist.at(i);
			if (ti.tname == "!DOCTYPE") {
				new_text += text.mid(offset, ti.pos - offset);
				new_text += "<!DOCTYPE html>";
				offset = ti.pos + ti.len;
				continue;
			}
			if (ti.tname == "html" && ti.ttype == "begin") {
				QString tagstring = text.mid(ti.pos, ti.len);
				OpenTagInfo opentaginfo = parseAttribute(tagstring);
				opentaginfo.atts["xmlns:epub"] = "http://www.idpf.org/2007/opf";
				new_text += text.mid(offset, ti.pos - offset);
				new_text += opentaginfo_to_string(opentaginfo);
				offset = ti.pos + ti.len;
				continue;
			}
			if (ti.tname == "link" && ti.ttype != "end") {
				QString tagstring = text.mid(ti.pos, ti.len);
				OpenTagInfo opentaginfo = parseAttribute(tagstring);
				if (opentaginfo.atts.contains("charset")) {
					opentaginfo.atts.remove("charset");
				}
				new_text += text.mid(offset, ti.pos - offset);
				new_text += opentaginfo_to_string(opentaginfo, ti.ttype);
				offset = ti.pos + ti.len;
				continue;
			}
			if (ti.tname == "big") {
				if (ti.ttype == "begin") {
					QString tagstring = text.mid(ti.pos, ti.len);
					OpenTagInfo opentaginfo = parseAttribute(tagstring);
					opentaginfo.tagname = "span";
					QString style_text = opentaginfo.atts.value("style", "");
					if (style_text == "") {
						opentaginfo.atts["style"] = "font-size: larger";
					}
					else if (style_text.trimmed().endsWith(";")){
						opentaginfo.atts["style"] = style_text.trimmed() + "font-size: larger";
					}
					else {
						opentaginfo.atts["style"] = style_text.trimmed() + ";font-size: larger";
					}
					new_text += text.mid(offset, ti.pos - offset);
					new_text += opentaginfo_to_string(opentaginfo);
				}
				else if (ti.ttype == "end") {
					new_text += text.mid(offset, ti.pos - offset);
					new_text += "</span>";
				}
				offset = ti.pos + ti.len;
				continue;
			}
			if (ti.tname == "meta" && ti.ttype != "end") {
				QString tagstring = text.mid(ti.pos, ti.len);
				OpenTagInfo opentaginfo = parseAttribute(tagstring);
				QString mname = opentaginfo.atts.value("name", ""),
						mcontent = opentaginfo.atts.value("content", "");
				if (QStringList({ "layout", "orientation", "page-spread", "viewport" }).contains(mname) && mcontent != "") {
					QString opf_href = res->GetRelativePathFromResource(m_Book->GetConstOPF());
					QString mid = "";
					if (opfhref_to_id.contains(opf_href)) mid = opfhref_to_id[opf_href];
					if (mid != "") {
						extra_spine_props[mid][mname] = mcontent;
					}
				}
				else if (mcontent.contains("charset")) {
					new_text += text.mid(offset, ti.pos - offset);
					new_text += "<meta charset=\"utf-8\"/>";
					offset = ti.pos + ti.len;
				}
				continue;
			}
			if (QStringList({ "svg", "svg:svg", "script", "math", "m:math","epub:switch" }).contains(ti.tname) && ti.ttype=="begin") {
				QString opf_href = res->GetRelativePathFromResource(m_Book->GetConstOPF());
				QString mid = "";
				if (opfhref_to_id.contains(opf_href)) mid = opfhref_to_id[opf_href];
				if (mid == "") continue;
				if (ti.tname == "svg" || ti.tname == "svg:svg") {
					if (!extra_manifest_props[mid].contains("svg")) extra_manifest_props[mid].append("svg");
				}
				else if (ti.tname == "script") {
					if (!extra_manifest_props[mid].contains("scripted")) extra_manifest_props[mid].append("scripted");
				}
				else if (ti.tname == "math" || ti.tname == "m:math") {
					if (!extra_manifest_props[mid].contains("mathml")) extra_manifest_props[mid].append("mathml");
				}
				else if (ti.tname == "epub:switch") {
					if (!extra_manifest_props[mid].contains("switch")) extra_manifest_props[mid].append("switch");
				}
			}
		}
		new_text += text.mid(offset);
		res->SetText(new_text);
	}
}

void EpubVersionConv::convert_opf_to3() {

	OPFParser p;
	p.parse(m_Book->GetOPF()->GetText());
	//-----------package---------------
	QString bookid = m_Book->GetOPF()->p.m_package.m_uniqueid;
	// clear m_atts
	p.m_package.m_atts = TagAtts();
	// fill the package node's attributes
	p.m_package.m_version = "3.0";
	p.m_package.m_uniqueid = bookid;
	p.m_package.m_atts["xmlns"] = "http://www.idpf.org/2007/opf";
	p.m_package.m_atts["prefix"] = "rendition: http://www.idpf.org/vocab/rendition/#";
	//-----------metadata--------------
	// meta node's namespace
	p.m_metans.m_atts = TagAtts();
	p.m_metans.m_atts["xmlns:dc"] = "http://purl.org/dc/elements/1.1/";
	p.m_metans.m_atts["xmlns:opf"] = "http://www.idpf.org/2007/opf";
	p.m_metans.m_atts["xmlns:dcterms"] = "http://purl.org/dc/terms/";

	// edit meta elements
	int i = -1;
	TagAtts attr;
	foreach(MetaEntry entry, p.m_metadata) {
		++i;
		// meta
		if (entry.m_name == "meta") {
			attr = entry.m_atts;
			if (attr.contains("name")) {
				// pre handle
				if (attr["name"] == "calibre:series") {
					series = attr.value("content", "");
					p.m_metadata.removeAt(i);	--i;
					continue;
				}
				if (attr["name"] == "calibre:series_index") {
					series_index = entry.m_atts.value("content", "");
					p.m_metadata.removeAt(i);	--i;
					continue;
				}
				if (attr["name"] == "calibre:title_sort") {
					title_sort = attr.value("content", "");
					p.m_metadata.removeAt(i);	--i;
					continue;
				}
				p.m_metadata.replace(i, epub3_map_meta(entry));
			}
		}
		// dc:
		if (entry.m_name.startsWith("dc:")) {
			if (entry.m_name == "dc:language") {
				lang = entry.m_content;
			}
			MetaEntry new_entry = epub3_map_dc(entry);
			if (new_entry.m_name == "dc:creator" || new_entry.m_name == "dc:contributor") {
				QString role = "", fileas = "";
				if (new_entry.m_atts.contains("opf:role")) {
					role = new_entry.m_atts.value("opf:role", "");
					new_entry.m_atts.remove("opf:role");
				}
				if (new_entry.m_atts.contains("opf:file-as")) {
					fileas = new_entry.m_atts.value("opf:file-as", "");
					new_entry.m_atts.remove("opf:file-as");
				}
				p.m_metadata.replace(i, new_entry);
				if (role != "") { 
					++i;
					attr = TagAtts();
					attr["refines"] = "#" + new_entry.m_atts["id"];
					attr["property"] = "role";
					attr["scheme"] = "marc:relators";
					p.m_metadata.append(MetaEntry("meta", role, attr));
					p.m_metadata.move(p.m_metadata.size()-1, i);
				}
				if (fileas != "") {
					++i;
					attr = TagAtts();
					attr["refines"] = "#" + new_entry.m_atts["id"];
					attr["property"] = "file-as";
					p.m_metadata.append(MetaEntry("meta", fileas, attr));
					p.m_metadata.move(p.m_metadata.size() - 1, i);
				}
				continue;
			}
			if (new_entry.m_name == "dc:title") {
				if (title_count == 1) {
					attr = TagAtts();
					title_id = new_entry.m_atts["id"];
					attr["refines"] = "#" % title_id;
					attr["property"] = "title-type";
					p.m_metadata.append(MetaEntry("meta", "main", attr));
					p.m_metadata.move(p.m_metadata.size() - 1, i+1);
					p.m_metadata.replace(i, new_entry); ++i;
					continue;
				}
			}

			if (new_entry.m_name == "") {
				p.m_metadata.removeAt(i);	--i;
			}
			else {
				p.m_metadata.replace(i, new_entry);
			}
		} 
	}
	// metadata egodic end

	if (series != "") {
		QString series_id = valid_id("series");
		attr = TagAtts();
		attr["id"] = series_id;
		attr["property"] = "belongs-to-collection";
		p.m_metadata.append(MetaEntry("meta", series, attr));
		attr = TagAtts();
		attr["refines"] = "#" + series_id;
		attr["property"] = "collection-type";
		p.m_metadata.append(MetaEntry("meta", "series", attr));

		if (series_index != "") {
			attr = TagAtts();
			attr["refines"] = "#" + series_id;
			attr["property"] = "group-position";
			p.m_metadata.append(MetaEntry("meta", series_index, attr));
		}
	}
	attr = TagAtts();
	attr["property"] = "dcterms:modified";
	p.m_metadata.append(MetaEntry("meta",getDatetime(), attr));

	// metadata end

	/* Here omitted the handling of Media Overlays properties. */

	// manifest start
	i = -1;
	foreach(ManifestEntry entry, p.m_manifest) {
		++i;
		QString id = entry.m_id;
		QString mtype = entry.m_mtype;
		ManifestEntry new_entry = entry;
		if (mtype == "application/x-font-ttf" || mtype == "application/x-font-opentype") {
			new_entry.m_mtype = "application/vnd.ms-opentype";
		}
		else if (mtype == "application/x-dtbncx+xml") {
			has_ncx = id;
		}
		else if (mtype == "application/oebs-page-map+xml") {
			has_pmap = id;
		}
		if (id == cover_id) {
			QString cp = entry.m_atts.value("properties", "");
			if (cp == "") {
				new_entry.m_atts["properties"] = "cover-image";
			}
			else {
				new_entry.m_atts["properties"] = cp + " cover-image";
			}
		}
		if (extra_manifest_props.keys().contains(id)) {
			QString interval = " ";
			if (new_entry.m_atts["properties"] == "") {
				new_entry.m_atts["properties"] = "";
				interval = "";
			}
			QString ex_props = extra_manifest_props[id].join(" ");
			new_entry.m_atts["properties"] += interval + ex_props;
		}
		p.m_manifest.replace(i, new_entry);
	}

	// handle spine
	attr = TagAtts();
	if (has_ncx != "") attr["toc"] = has_ncx;
	if (ppd == "") ppd = p.m_spineattr.m_atts.value("page-progression-direction", "");
	if (ppd != "") attr["page-progression-direction"] = ppd;
	if (has_pmap != "") attr["page-map"] = has_pmap;
	p.m_spineattr.m_atts = attr;
	// handle spine itemref
	i = -1;
	foreach(SpineEntry entry, p.m_spine) {
		++i;
		QString idref = entry.m_idref;
		QString props = entry.m_atts.value("properties", "");
		SpineEntry new_entry = entry;
		if (extra_spine_props.keys().contains(idref)) {
			QString interval = " ";
			if (props == "") interval = "";
			foreach(QString key, extra_spine_props[idref].keys()) {
				props += interval + key + "-" + extra_spine_props[idref][key];
				interval = " ";
			}
			new_entry.m_atts["properties"] = props;
			p.m_spine.replace(i, new_entry);
		}
	}

	// handle guide
	foreach(GuideEntry entry, p.m_guide) {
		guide_res.append(entry);
		if (entry.m_type == "toc") {
			has_html_toc = true;
		}
	}
	p.m_guide.clear();

	OPFResource * opf = m_Book->GetOPF();
	opf->SetEpubVersion("3.0");
	QString new_text = p.convert_to_xml();
	opf->p.parse(new_text);
	opf->SetText(new_text);
}

void EpubVersionConv::build_nav() {
	NCXResource * ncx = m_Book->GetNCX();
	QString ncx_text = ncx->GetText();
	parse_ncx(ncx_text);
	QString indent(2, ' '), indent2(4, ' '), indent3(6, ' ');
	QString lang = m_Book->GetOPF()->GetPrimaryBookLanguage();
	QString new_text = "";
	new_text += "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
	new_text += "<!DOCTYPE html>\n";
	new_text += "<html xmlns=\"http://www.w3.org/1999/xhtml\" xmlns:epub=\"http://www.idpf.org/2007/opf\"";
	new_text += QString(" lang=\"%1\" xml:lang=\"%2\">\n\n").arg(lang).arg(lang);
	new_text += "<head>\n";
	new_text += indent % "<meta charset=\"utf-8\"/>\n";
	new_text += indent % "<title>Navigation</title>\n";
	new_text += indent % "<link href=\"../Styles/sgc-nav.css\" type=\"text/css\" rel=\"stylesheet\"/>\n";
	new_text += "</head>\n\n";
	new_text += "<body epub:type=\"frontmatter\">\n";
	// start with the toc
	new_text += indent % "<nav epub:type=\"toc\" id=\"toc\">\n";
	new_text += indent2 % QString("<h1>%1</h1>\n").arg(doctitle);
	new_text += indent2 % "<ol>\n";

	int curlvl = 1;
	bool initial = true;
	foreach(NavPointInfo ni, toclist) {
		QString href = ni.bookhref.mid(11);
		if (ni.lvl > curlvl) {
			while (ni.lvl > curlvl) {
				QString indent_ = indent2 + QString(4 * curlvl, ' ');
				new_text += indent_ % "<ol>\n";
				new_text += indent_ % indent % "<li>\n";
				new_text += indent_ % indent2 % QString("<a href=\"%1\">%2</a>\n").arg(href, ni.navlabel);
				curlvl += 1;
			}
		}
		else if (ni.lvl < curlvl) {
			while (ni.lvl < curlvl) {
				QString indent_ = indent2 + QString(4 * (curlvl - 1), ' ');
				new_text += indent_ + indent + "</li>\n";
				new_text += indent_ + "</ol>\n";
				curlvl -= 1;
			}
			QString indent_ = indent2 + QString(4 * (curlvl - 1), ' ');
			new_text += indent_ + indent + "</li>\n";
			new_text += indent_ + indent + "<li>\n";
			new_text += indent_ + indent2 + QString("<a href=\"%1\">%2</a>\n").arg(href, ni.navlabel);
		}
		else {
			QString indent_ = indent2 + QString(4 * (ni.lvl - 1), ' ');
			if (!initial) {
				new_text += indent_ + indent + "</li>\n";
			}
			new_text += indent_ + indent + "<li>\n";
			new_text += indent_ + indent2 + QString("<a href=\"%1\">%2</a>\n").arg(href, ni.navlabel);
		}
		initial = false;
		curlvl = ni.lvl;
	}
	while (curlvl > 0) {
		QString indent_ = indent2 + QString(4 * (curlvl - 1), ' ');
		new_text += indent_ + indent + "</li>\n";
		new_text += indent_ + "</ol>\n";
		curlvl -= 1;
	}
	new_text += indent % "</nav>\n";

	// add any existing page-list if need be
	if (pagelist.size() > 0) {
		new_text += indent % "<nav epub:type=\"page-list\" id=\"page-list\" hidden=\"\">\n";
		new_text += indent2 % "<ol>\n";
		foreach (PageInfo pi, pagelist) {
			QString href = pi.bookhref.mid(6);
			new_text += QString(8, ' ') + QString("<li><a href=\"%1\">%2</a></li>\n").arg(href).arg(pi.pagenum);
		}
		new_text += indent2 + "</ol>\n";
		new_text += indent + "</nav>\n";
	}
	// create landmark section
	new_text += indent + "<nav epub:type=\"landmarks\" id=\"landmarks\" hidden=\"\">\n";
	new_text += indent2 + "<h2>Guide</h2>\n";
	new_text += indent2 + "<ol>\n";
	foreach (GuideEntry ge, guide_res) {
		QString href = ge.m_href;
		QString etype = "";
		if (_guide_epubtype_map.contains(ge.m_type))
			etype = _guide_epubtype_map.value(ge.m_type);
		if (etype != "") {
			new_text += QString(6, ' ') + "<li>\n";
			new_text += QString(8, ' ') + QString("<a epub:type=\"%1\" href=\"%2\">%3</a>\n").arg(etype, href, ge.m_title);
			new_text += QString(6, ' ') + "</li>\n";
		}
	}
	new_text += indent2 + "</ol>\n";
	new_text += indent + "</nav>\n";
	// now close it off
	new_text += "</body>\n";
	new_text += "</html>\n";
	new_text = format_nav_text(new_text);

	// add new nav file;
	QString navdir = "OEBPS/Text",
		first_textdir = "OEBPS/Text",
		navfile = valid_id("nav.xhtml");

	bool is_update_opf = true;
	HTMLResource * nav_resource = m_Book->CreateEmptyNavFile(is_update_opf, navdir, navfile, first_textdir);
	nav_resource->SetText(new_text);
	m_Book->GetOPF()->SetNavResource(nav_resource);
	m_Book->GetOPF()->SetItemRefLinear(nav_resource, false);
	m_Book->GetOPF()->UpdateNCXOnSpine(navfile);
	FolderKeeper *folder = m_Book->GetFolderKeeper();

}

void EpubVersionConv::parse_ncx(QString ncx_text) {

	TagLister taglist(ncx_text);

	QString navlabel = "",
		pagenum = "";
	int lvl = 0;

	int offset = 0;
	for (int i = 0; i < taglist.size(); i++) {
		TagLister::TagInfo ti = taglist.at(i);
		if (ti.tname.toLower() == "text" && ti.ttype == "end") {
			QString txt = ncx_text.mid(ti.open_pos + ti.open_len, ti.pos - ti.open_pos - ti.open_len);
			if (txt != "") {
				if (ti.tpath.toLower().endsWith(".doctitle")) {
					doctitle = txt;
				}
				else if (ti.tpath.toLower().endsWith(".navpoint.navlabel")) {
					navlabel = txt;
				}
			}
			continue;
		}
		if (ti.tname.toLower() == "navpoint") {
			if (ti.ttype == "begin") {
				lvl += 1;
			}
			else if (ti.ttype == "end") {
				lvl -= 1;
			}
			continue;
		}
		if (ti.tname == "content") {
			QString tagstring = ncx_text.mid(ti.pos, ti.len);
			OpenTagInfo opentaginfo = parseAttribute(tagstring);
			if (opentaginfo.atts.contains("src")) {
				if (ti.tpath.toLower().endsWith("navpoint")) {
					QString href = opentaginfo.atts["src"];
					QString bookhref = "OEBPS/" + href;
					toclist.append(NavPointInfo({ lvl, navlabel, bookhref }));
					navlabel = "";
				}
				else if (ti.tpath.toLower().endsWith("pagetarget")) {
					QString pagehref = opentaginfo.atts["src"];
					QString bookhref = "OEBPS/" + pagehref;
					pagelist.append(PageInfo({ pagenum, bookhref }));
					pagenum = "";
				}
			}
			continue;
		}
		if (ti.tname.toLower() == "pagetarget" && ti.ttype == "begin") {
			QString tagstring = ncx_text.mid(ti.pos, ti.len);
			OpenTagInfo opentaginfo = parseAttribute(tagstring);
			if (opentaginfo.atts.contains("value")) pagenum = opentaginfo.atts["value"];
			continue;
		}
	}
}

MetaEntry EpubVersionConv::epub3_map_meta(MetaEntry entry) {
	// map meta
	TagAtts attr = entry.m_atts;
	if (attr.contains("id")) attr.remove("id");

	QString aname = attr.value("name", "");
	QString acont = attr.value("content", "");
	MetaEntry new_entry;

	if (aname == "cover") {
		cover_id = attr.value("content", "");
	}
	if (aname == "page-progression-direction") {
		ppd = attr.value("content", "");
	}

	new_entry.m_name = entry.m_name;
	if (aname.startsWith("rendition:")) {
		attr["name"] = aname.right(attr["name"].size() - 10);
	}
	if (aname == "orientation" || aname == "layout" || aname == "spread") {
		new_entry.m_atts["property"] = "rendition:" + aname;
		new_entry.m_content = acont;
		return new_entry;
	}
	if (aname == "fixed-layout") {
		acont = acont.toLower();
		acont = acont == "true" ? "pre-paginated" : "reflowable";
		new_entry.m_atts["property"] = "rendition:layout";
		new_entry.m_content = acont;
		return new_entry;
	}
	if (aname == "orientation-lock") {
		acont = acont.toLower();
		if (acont != "portrait" || acont != "landscape") acont = "auto";
		new_entry.m_atts["property"] = "rendition:orientation";
		new_entry.m_content = acont;
		return new_entry;
	}
	entry.m_content = "";
	return entry;
}


MetaEntry EpubVersionConv::epub3_map_dc(MetaEntry entry) {

	if (entry.m_content.isEmpty()) {
		return MetaEntry();
	}

	MetaEntry new_entry;
	new_entry.m_name = entry.m_name;
	if (entry.m_name == "dc:title") {
		title_count += 1;
		QString id = valid_id("title" % QString::number(title_count));
		all_ids.append(id);
		new_entry.m_atts["id"] = id;
		new_entry.m_content = entry.m_content;
		return new_entry;
	}
	if (entry.m_name == "dc:type") {
		if (_epub3_allowed_dctypes.contains(entry.m_content)) {
			new_entry.m_content = entry.m_content;
			return new_entry;
		}
		else {
			return MetaEntry();
		}
	}
	if (entry.m_name == "dc:date") {
		if (entry.m_atts.size() > 0 && (entry.m_atts.contains("opf:event") || entry.m_atts.contains("event"))) {
			QString event_type = entry.m_atts.value("opf:event", "");
			if (event_type == "") {
				event_type = entry.m_atts.value("event", "");
			}
			if (event_type == "modification") {
				return MetaEntry();
			}
			else if (event_type == "creation") {
				new_entry.m_name = "meta";
				new_entry.m_atts["property"] = "dcterms:created";
				new_entry.m_content = entry.m_content;
				return new_entry;
			}
			else if (event_type == "publication" || event_type == "issued") {
				new_entry.m_content = entry.m_content;
				return new_entry;
			}
		}
		else {
			return MetaEntry();
		}
	}
	if (entry.m_name == "dc:creator" || entry.m_name == "dc:contributor") {
		QString id;
		if (entry.m_name == "dc:creator") {
			creator_count += 1;
			id = "create" % QString::number(creator_count);
		}
		else {
			contributor_count += 1;
			id = "contrib" % QString::number(contributor_count);
		}
		id = valid_id(id);
		all_ids.append(id);
		new_entry.m_atts = entry.m_atts;
		new_entry.m_atts["id"] = id;
		new_entry.m_content = entry.m_content;
		return new_entry;
	}
	if (entry.m_name == "dc:identifier") {
		new_entry = entry;
		if (entry.m_atts.contains("id")) {
			all_ids.append(new_entry.m_atts["id"]);
		}
		if (entry.m_atts.contains("opf:scheme")) {
			QString scheme = entry.m_atts["opf:scheme"].toLower();
			new_entry.m_atts.remove("opf:scheme");
			if (!entry.m_content.startsWith("urn:")) {
				new_entry.m_content = "urn:" + scheme + ":" + entry.m_content;
			}
		}
		return new_entry;
	}

	new_entry = entry;
	if (entry.m_atts.contains("id")) {
		new_entry.m_atts.remove("id");
	}
	foreach(QString key, entry.m_atts.keys()) {
		if (key.startsWith("opf:")) {
			new_entry.m_atts.remove(key);
		}
	}
	return new_entry;
}

QString EpubVersionConv::format_nav_text(QString text) {
	QString new_text = "";
	new_text = RegExpSub("(<li[^>]*>)[ \\n\\t]*<a", "\\1<a", text);
	new_text = RegExpSub("</a>[ \\n\\t]*</li>", "</a></li>", new_text);
	return new_text;
}


/* following are the functions of converting epub2 */

void EpubVersionConv::convert_xhtml_to2() {
	QList<HTMLResource *> html_resources = m_Book->GetHTMLResources();
	foreach(HTMLResource * res, html_resources) {
		QString text = convert_named_entities(res->GetText());
		QString new_text = "";
		TagLister taglist(text);

		int offset = 0;
		for (int i = 0; i<taglist.size(); i++) {
			TagLister::TagInfo ti = taglist.at(i);
			if (ti.tname == "!DOCTYPE") {
				new_text += text.mid(offset, ti.pos - offset);
				new_text += "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.1//EN\"\n  \"http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd\">";
				offset = ti.pos + ti.len;
				continue;
			}
			if (ti.tname == "html" && ti.ttype == "begin") {
				QString tagstring = text.mid(ti.pos, ti.len);
				OpenTagInfo opentaginfo = parseAttribute(tagstring);
				opentaginfo.atts.remove("epub:prefix");
				opentaginfo.atts.remove("xmlns:ibooks");
				new_text += text.mid(offset, ti.pos - offset);
				new_text += opentaginfo_to_string(opentaginfo);
				offset = ti.pos + ti.len;
			}
		}
		new_text += text.mid(offset);
		res->SetText(new_text);
	}
}
void EpubVersionConv::convert_opf_to2() {
	
	OPFParser p;
	p.parse(m_Book->GetOPF()->GetText());
	//-----------package---------------
	QString bookid = m_Book->GetOPF()->p.m_package.m_uniqueid;
	// clear m_atts
	p.m_package.m_atts = TagAtts();
	// fill the package node's attributes
	p.m_package.m_version = "2.0";
	p.m_package.m_uniqueid = bookid;
	p.m_package.m_atts["xmlns"] = "http://www.idpf.org/2007/opf";

	//-----------metadata--------------
	// meta node's namespace
	p.m_metans.m_atts = TagAtts();
	p.m_metans.m_atts["xmlns:dc"] = "http://purl.org/dc/elements/1.1/";
	p.m_metans.m_atts["xmlns:opf"] = "http://www.idpf.org/2007/opf";

	// edit meta elements
	int i = -1;
	TagAtts attr;
	MetaEntry belongs_to_collection;
	MetaEntry cover_meta;
	foreach(MetaEntry entry, p.m_metadata) {
		++i;
		// meta
		if (entry.m_name == "meta") {
			if (entry.m_atts.value("name", "") == "cover") {
				cover_meta = entry;
				continue;
			}
			if (entry.m_atts.contains("refines")) {
				if (!entry.m_atts.value("refines", "").startsWith("#")) {
					p.m_metadata.removeAt(i); --i;
					continue;
				}
				QString dc_id = entry.m_atts["refines"].mid(1);
				QString dc_property = entry.m_atts.value("property", "");
				QString dc_text = entry.m_content;
				
				if (dc_property == "role") { meta_props_map[dc_id].role = dc_text; }
				else if (dc_property == "file-as") { meta_props_map[dc_id].file_as = dc_text; }
				else if (dc_property == "group-position") { 
					meta_props_map[dc_id].group_position = dc_text; 
				}
				
				p.m_metadata.removeAt(i); --i;
				continue;
			}
			if (entry.m_atts.contains("property")) {
				if (entry.m_atts["property"] == "dcterms:modified") {
					TagAtts attr;
					attr["opf:event"] = "modification";
					p.m_metadata.replace(i, MetaEntry("dc:date", getDatetime(false), attr));
					continue;
				}
				if (entry.m_atts["property"] == "belongs-to-collection") {
					belongs_to_collection = entry;
				}
				p.m_metadata.removeAt(i); --i;
				continue;
			}
		}
		if (entry.m_name == "dc:identifier") {
			QRegExp re("urn:([a-zA-Z]+):(.*)", Qt::CaseInsensitive);
			int si = re.indexIn(entry.m_content);
			if (si > -1) {
				QString scheme = re.cap(1);
				QString cont = re.cap(2);
				entry.m_atts["opf:scheme"] = scheme.toUpper();
				entry.m_content = cont;
				p.m_metadata.replace(i, entry);
			}
		}
		if (entry.m_name == "link") {
			p.m_metadata.removeAt(i); --i;
			continue;
		}
	}
	// metadata egodic end
	i = -1;
	foreach(MetaEntry entry, p.m_metadata) {
		++i;
		if (entry.m_name.startsWith("dc:") && meta_props_map.keys().contains(entry.m_atts["id"])) {
			QString dc_id = entry.m_atts["id"];
			if (meta_props_map[dc_id].role != "") {
				entry.m_atts["opf:role"] = meta_props_map[dc_id].role;
				p.m_metadata.replace(i, entry);
			}
			if (meta_props_map[dc_id].file_as != ""){
				if (entry.m_name == "dc:creator" || entry.m_name == "dc:contributor") {
					entry.m_atts["opf:file-as"] = meta_props_map[dc_id].file_as;
					p.m_metadata.replace(i, entry);
				}
				else if (entry.m_name == "dc:title") {
					TagAtts attr;
					attr["name"] = "calibre:title_sort";
					attr["content"] = meta_props_map[dc_id].file_as;
					p.m_metadata.append(MetaEntry("meta", "", attr));
					p.m_metans.m_atts["xmlns:calibre"] = "http://calibre.kovidgoyal.net/2009/metadata";
				}
				
			}
		}
	}
	if (belongs_to_collection.m_name != "") {
		QString id = belongs_to_collection.m_atts["id"];
		if (meta_props_map[id].group_position != "") {
			TagAtts attr;
			attr["name"] = "calibre:series_index";
			attr["content"] = meta_props_map[id].group_position;
			p.m_metadata.append(MetaEntry("meta", "", attr));
			attr["name"] = "calibre:series";
			attr["content"] = belongs_to_collection.m_content;
			p.m_metadata.append(MetaEntry("meta", "", attr));
			p.m_metans.m_atts["xmlns:calibre"] = "http://calibre.kovidgoyal.net/2009/metadata";
		}
	}
	// metadata end


	// manifest start
	i = -1;
	foreach(ManifestEntry entry, p.m_manifest) {
		++i;
		all_ids.append(entry.m_id);
		if (entry.m_atts.contains("properties")) {
			QString prop = entry.m_atts["properties"];
			if (prop == "cover-image" && cover_meta.m_name.isEmpty()) {
				TagAtts attr;
				attr["name"] = "cover";
				attr["content"] = entry.m_id;
				p.m_metadata.append(MetaEntry("meta", "", attr));
			}
			entry.m_atts.remove("properties");
			p.m_manifest.replace(i, entry);
		}
		if (entry.m_mtype == "application/x-dtbncx+xml") {
			has_ncx = entry.m_id;
		}
	}

	// handle spine
	// The ncx_id had been handled by MainWindow::GenerateNCXGuideFromNav(),so we don't need to handle it again.
	if (p.m_spineattr.m_atts.contains("page-progression-direction")) {
		p.m_spineattr.m_atts.remove("page-progression-direction");
	}
	// handle spine itemref
	i = -1;
	foreach(SpineEntry entry, p.m_spine) {
		++i;
		if (entry.m_atts.contains("properties")) {
			entry.m_atts.remove("properties");
			p.m_spine.replace(i, entry);
		}
	}
	// handle guide
	// The guide element had been handled by MainWindow::GenerateNCXGuideFromNav(),so we don't need to handle it again.

	OPFResource * opf = m_Book->GetOPF();
	opf->SetEpubVersion("2.0");
	QString new_text = p.convert_to_xml();
	opf->p.parse(new_text);
	opf->SetText(new_text);
}
void EpubVersionConv::build_toc() {
	// The toc.ncx file had been generated by MainWindow::GenerateNCXGuideFromNav(), so we don't need to create it again.

	// Here we just clean the infomation of the nav resource.
	m_Book->GetOPF()->SetNavResource(nullptr);
}
/* following are some common functions */

QString EpubVersionConv::RegExpSub(const QString &regexp, const QString &alt_pattern, const QString &text, int max_count) {
	
	QRegExp re(regexp);
	QString new_text = "";
	int index = re.indexIn(text);
	int count = 0;
	int offset = 0;
	while (index > -1) {
		if (max_count > 0 && count == max_count) {
			break;
		}
		++count;
		new_text += text.mid(offset, index - offset);
		offset = index + re.cap(0).size();

		QString alt_text = "";
		bool backslash = false;
		foreach(QChar ch, alt_pattern) {
			if (ch == '\\') {
				backslash = true;
				continue;
			}
			if (backslash) {
				backslash = false;
				if (48 <= ch.unicode() && 57 >= ch.unicode()) {
					int group_num = ch.unicode() - 48;
					if (group_num <= re.captureCount()) {
						alt_text += re.cap(group_num);
						continue;
					}
				}
				alt_text.append("\\");
			}
			alt_text.append(ch);
		}
		new_text += alt_text;
		index = re.indexIn(text, offset);
	}
	new_text += text.mid(offset);
	return new_text;
}

QString EpubVersionConv::opentaginfo_to_string(OpenTagInfo &opentag,QString type) {
	if (opentag.tagname == "") return "";
	QString tagstring = "<"%opentag.tagname;
	foreach(QString key, opentag.atts.keys()) {
		tagstring += " " % key % "=" % "\""%opentag.atts[key] % "\"";
	}
	if (type == "begin") {
		tagstring += ">";
	}
	else if (type == "single") {
		tagstring += "/>";
	}
	
	return tagstring;
}
EpubVersionConv::OpenTagInfo EpubVersionConv::parseAttribute(QString tagstring) {
	OpenTagInfo taginfo;
	taginfo.tagname = "";
	taginfo.atts = TagAtts();
	if (!(tagstring.startsWith("<") && tagstring.endsWith(">") || tagstring.at(tagstring.size() - 2) == "/")) {
		return taginfo;
	}
	QRegExp tagname_re("<([a-zA-Z0-9:]+)");
	int offset = 0;
	int index = tagname_re.indexIn(tagstring);
	if (index > -1) {
		taginfo.tagname = tagname_re.cap(1);
		offset = index + tagname_re.cap(0).size();
	}
	else {
		return taginfo;
	}

	QRegExp tagattr_re("[ \\t\\n\\r]+([a-zA-Z:_-]+)[ \\t\\n\\r]*=[ \\t\\n\\r]*([\"\'])([^\"\']*)\\2");
	QString ttt = tagstring.mid(offset);
	index = tagattr_re.indexIn(tagstring.mid(offset));
	while (index > -1) {
		QString key = tagattr_re.cap(1);
		QString val = tagattr_re.cap(3);
		taginfo.atts[key] = val;
		offset += index + tagattr_re.cap(0).size();
		if (offset >= tagstring.size()) break;
		index = tagattr_re.indexIn(tagstring.mid(offset));
	}
	return taginfo;
}
QString EpubVersionConv::getDatetime(bool is_epub3) {

	QString datetime;
	QDateTime local(QDateTime::currentDateTime());
	local.setTimeSpec(Qt::UTC);
	if (is_epub3) {
		datetime = local.toString(Qt::ISODate);
	}
	else {
		datetime = local.toString("yyyy-MM-dd");
	}
	
	return datetime;
}

QString EpubVersionConv::valid_id(QString candidate) {
	QString new_id = candidate;
	while (all_ids.contains(new_id)) {
		new_id.prepend("_");
	}
	return new_id;
}

QString EpubVersionConv::convert_named_entities(QString text) {
	QRegExp re = QRegExp("&(\\w+;)");
	int index,offset = 0;
	QString new_text = "";
	
	index  = re.indexIn(text, offset);
	while (index > -1) {
		new_text += text.mid(offset, index - offset);
		offset = index + re.cap(0).size();

		QString sval = "", rep = "";
		QString entity = re.cap(1);
		if (named_entities.contains(entity)) {
			sval = named_entities.value(entity);
		}
		if (sval.size() > 0) {
			for (int i = 0; i < sval.size(); i++) {
				QChar ch = sval.at(i);
				rep += "&#x"+QString::number(ch.unicode(),16)+";";
			}
		}
		if (rep.size() > 0) {
			new_text += rep;
		}
		index = re.indexIn(text, offset);
	}
	new_text += text.mid(offset, text.size() - offset);
	return new_text;
}




/* following are some initial datas */
void EpubVersionConv::init_members() {

	_epub3_allowed_dctypes << "dictionary" << "index" << "distributable-object" << "edupub"
		<< "preview" << "teacher-edition" << "teacher-guide" << "widget";

	_guide_epubtype_map = QHash<QString, QString>{
		{ "acknowledgements", "acknowledgments" },
	{ "other.afterword", "afterword" },
	{ "other.appendix", "appendix" },
	{ "other.backmatter", "backmatter" },
	{ "bibliography", "bibliography" },
	{ "text", "bodymatter" },
	{ "other.chapter", "chapter" },
	{ "colophon", "colophon" },
	{ "other.conclusion", "conclusion" },
	{ "other.contributors", "contributors" },
	{ "copyright-page", "copyright-page" },
	{ "cover", "cover" },
	{ "dedication", "dedication" },
	{ "other.division", "division" },
	{ "epigraph", "epigraph" },
	{ "other.epilogue", "epilogue" },
	{ "other.errata", "errata" },
	{ "other.footnotes", "footnotes" },
	{ "foreword", "foreword" },
	{ "other.frontmatter", "frontmatter" },
	{ "glossary", "glossary" },
	{ "other.halftitlepage", "halftitlepage" },
	{ "other.imprint", "imprint" },
	{ "other.imprimatur", "imprimatur" },
	{ "index", "index" },
	{ "other.introduction", "introduction" },
	{ "other.landmarks", "landmarks" },
	{ "other.loa", "loa" },
	{ "loi", "loi" },
	{ "lot", "lot" },
	{ "other.lov", "lov" },
	{ "notes", "" },
	{ "other.notice", "notice" },
	{ "other.other-credits", "other-credits" },
	{ "other.part", "part" },
	{ "other.preamble", "preamble" },
	{ "preface", "preface" },
	{ "other.prologue", "prologue" },
	{ "other.rearnotes", "rearnotes" },
	{ "other.subchapter", "subchapter" },
	{ "title-page", "titlepage" },
	{ "toc", "toc" },
	{ "other.volume", "volume" },
	{ "other.warning", "warning" }
	}.unite(_guide_epubtype_map);

	_ncx_tagname_map = QHash<QString, QString>{
		{ "doctitle", "docTitle" },
	{ "docauthor", "docAuthor" },
	{ "navmap", "navMap" },
	{ "navpoint", "navPoint" },
	{ "playorder", "playOrder" },
	{ "navlabel", "navLabel" },
	{ "pagelist", "pageList" },
	{ "pagetarget", "pageTarget" }
	}.unite(_ncx_tagname_map);

	_namespace_map = QHash<QString, QString>{
		{ "smil", "http://www.w3.org/ns/SMIL" },
	{ "epub", "http://www.idpf.org/2007/opf" }
	}.unite(_namespace_map);

	named_entities = QHash<QString, QString>{
		{ "AElig", QChar(0xc6) },
	{ "AElig;", QChar(0xc6) },
	{ "AMP", "&" },
	{ "AMP;", "&" },
	{ "Aacute", QChar(0xc1) },
	{ "Aacute;", QChar(0xc1) },
	{ "Abreve;", QChar(0x0102) },
	{ "Acirc", QChar(0xc2) },
	{ "Acirc;", QChar(0xc2) },
	{ "Acy;", QChar(0x0410) },
	{ "Afr;", QChar(0x0001d504) },
	{ "Agrave", QChar(0xc0) },
	{ "Agrave;", QChar(0xc0) },
	{ "Alpha;", QChar(0x0391) },
	{ "Amacr;", QChar(0x0100) },
	{ "And;", QChar(0x2a53) },
	{ "Aogon;", QChar(0x0104) },
	{ "Aopf;", QChar(0x0001d538) },
	{ "ApplyFunction;", QChar(0x2061) },
	{ "Aring", QChar(0xc5) },
	{ "Aring;", QChar(0xc5) },
	{ "Ascr;", QChar(0x0001d49c) },
	{ "Assign;", QChar(0x2254) },
	{ "Atilde", QChar(0xc3) },
	{ "Atilde;", QChar(0xc3) },
	{ "Auml", QChar(0xc4) },
	{ "Auml;", QChar(0xc4) },
	{ "Backslash;", QChar(0x2216) },
	{ "Barv;", QChar(0x2ae7) },
	{ "Barwed;", QChar(0x2306) },
	{ "Bcy;", QChar(0x0411) },
	{ "Because;", QChar(0x2235) },
	{ "Bernoullis;", QChar(0x212c) },
	{ "Beta;", QChar(0x0392) },
	{ "Bfr;", QChar(0x0001d505) },
	{ "Bopf;", QChar(0x0001d539) },
	{ "Breve;", QChar(0x02d8) },
	{ "Bscr;", QChar(0x212c) },
	{ "Bumpeq;", QChar(0x224e) },
	{ "CHcy;", QChar(0x0427) },
	{ "COPY", QChar(0xa9) },
	{ "COPY;", QChar(0xa9) },
	{ "Cacute;", QChar(0x0106) },
	{ "Cap;", QChar(0x22d2) },
	{ "CapitalDifferentialD;", QChar(0x2145) },
	{ "Cayleys;", QChar(0x212d) },
	{ "Ccaron;", QChar(0x010c) },
	{ "Ccedil", QChar(0xc7) },
	{ "Ccedil;", QChar(0xc7) },
	{ "Ccirc;", QChar(0x0108) },
	{ "Cconint;", QChar(0x2230) },
	{ "Cdot;", QChar(0x010a) },
	{ "Cedilla;", QChar(0xb8) },
	{ "CenterDot;", QChar(0xb7) },
	{ "Cfr;", QChar(0x212d) },
	{ "Chi;", QChar(0x03a7) },
	{ "CircleDot;", QChar(0x2299) },
	{ "CircleMinus;", QChar(0x2296) },
	{ "CirclePlus;", QChar(0x2295) },
	{ "CircleTimes;", QChar(0x2297) },
	{ "ClockwiseContourIntegral;", QChar(0x2232) },
	{ "CloseCurlyDoubleQuote;", QChar(0x201d) },
	{ "CloseCurlyQuote;", QChar(0x2019) },
	{ "Colon;", QChar(0x2237) },
	{ "Colone;", QChar(0x2a74) },
	{ "Congruent;", QChar(0x2261) },
	{ "Conint;", QChar(0x222f) },
	{ "ContourIntegral;", QChar(0x222e) },
	{ "Copf;", QChar(0x2102) },
	{ "Coproduct;", QChar(0x2210) },
	{ "CounterClockwiseContourIntegral;", QChar(0x2233) },
	{ "Cross;", QChar(0x2a2f) },
	{ "Cscr;", QChar(0x0001d49e) },
	{ "Cup;", QChar(0x22d3) },
	{ "CupCap;", QChar(0x224d) },
	{ "DD;", QChar(0x2145) },
	{ "DDotrahd;", QChar(0x2911) },
	{ "DJcy;", QChar(0x0402) },
	{ "DScy;", QChar(0x0405) },
	{ "DZcy;", QChar(0x040f) },
	{ "Dagger;", QChar(0x2021) },
	{ "Darr;", QChar(0x21a1) },
	{ "Dashv;", QChar(0x2ae4) },
	{ "Dcaron;", QChar(0x010e) },
	{ "Dcy;", QChar(0x0414) },
	{ "Del;", QChar(0x2207) },
	{ "Delta;", QChar(0x0394) },
	{ "Dfr;", QChar(0x0001d507) },
	{ "DiacriticalAcute;", QChar(0xb4) },
	{ "DiacriticalDot;", QChar(0x02d9) },
	{ "DiacriticalDoubleAcute;", QChar(0x02dd) },
	{ "DiacriticalGrave;", "`" },
	{ "DiacriticalTilde;", QChar(0x02dc) },
	{ "Diamond;", QChar(0x22c4) },
	{ "DifferentialD;", QChar(0x2146) },
	{ "Dopf;", QChar(0x0001d53b) },
	{ "Dot;", QChar(0xa8) },
	{ "DotDot;", QChar(0x20dc) },
	{ "DotEqual;", QChar(0x2250) },
	{ "DoubleContourIntegral;", QChar(0x222f) },
	{ "DoubleDot;", QChar(0xa8) },
	{ "DoubleDownArrow;", QChar(0x21d3) },
	{ "DoubleLeftArrow;", QChar(0x21d0) },
	{ "DoubleLeftRightArrow;", QChar(0x21d4) },
	{ "DoubleLeftTee;", QChar(0x2ae4) },
	{ "DoubleLongLeftArrow;", QChar(0x27f8) },
	{ "DoubleLongLeftRightArrow;", QChar(0x27fa) },
	{ "DoubleLongRightArrow;", QChar(0x27f9) },
	{ "DoubleRightArrow;", QChar(0x21d2) },
	{ "DoubleRightTee;", QChar(0x22a8) },
	{ "DoubleUpArrow;", QChar(0x21d1) },
	{ "DoubleUpDownArrow;", QChar(0x21d5) },
	{ "DoubleVerticalBar;", QChar(0x2225) },
	{ "DownArrow;", QChar(0x2193) },
	{ "DownArrowBar;", QChar(0x2913) },
	{ "DownArrowUpArrow;", QChar(0x21f5) },
	{ "DownBreve;", QChar(0x0311) },
	{ "DownLeftRightVector;", QChar(0x2950) },
	{ "DownLeftTeeVector;", QChar(0x295e) },
	{ "DownLeftVector;", QChar(0x21bd) },
	{ "DownLeftVectorBar;", QChar(0x2956) },
	{ "DownRightTeeVector;", QChar(0x295f) },
	{ "DownRightVector;", QChar(0x21c1) },
	{ "DownRightVectorBar;", QChar(0x2957) },
	{ "DownTee;", QChar(0x22a4) },
	{ "DownTeeArrow;", QChar(0x21a7) },
	{ "Downarrow;", QChar(0x21d3) },
	{ "Dscr;", QChar(0x0001d49f) },
	{ "Dstrok;", QChar(0x0110) },
	{ "ENG;", QChar(0x014a) },
	{ "ETH", QChar(0xd0) },
	{ "ETH;", QChar(0xd0) },
	{ "Eacute", QChar(0xc9) },
	{ "Eacute;", QChar(0xc9) },
	{ "Ecaron;", QChar(0x011a) },
	{ "Ecirc", QChar(0xca) },
	{ "Ecirc;", QChar(0xca) },
	{ "Ecy;", QChar(0x042d) },
	{ "Edot;", QChar(0x0116) },
	{ "Efr;", QChar(0x0001d508) },
	{ "Egrave", QChar(0xc8) },
	{ "Egrave;", QChar(0xc8) },
	{ "Element;", QChar(0x2208) },
	{ "Emacr;", QChar(0x0112) },
	{ "EmptySmallSquare;", QChar(0x25fb) },
	{ "EmptyVerySmallSquare;", QChar(0x25ab) },
	{ "Eogon;", QChar(0x0118) },
	{ "Eopf;", QChar(0x0001d53c) },
	{ "Epsilon;", QChar(0x0395) },
	{ "Equal;", QChar(0x2a75) },
	{ "EqualTilde;", QChar(0x2242) },
	{ "Equilibrium;", QChar(0x21cc) },
	{ "Escr;", QChar(0x2130) },
	{ "Esim;", QChar(0x2a73) },
	{ "Eta;", QChar(0x0397) },
	{ "Euml", QChar(0xcb) },
	{ "Euml;", QChar(0xcb) },
	{ "Exists;", QChar(0x2203) },
	{ "ExponentialE;", QChar(0x2147) },
	{ "Fcy;", QChar(0x0424) },
	{ "Ffr;", QChar(0x0001d509) },
	{ "FilledSmallSquare;", QChar(0x25fc) },
	{ "FilledVerySmallSquare;", QChar(0x25aa) },
	{ "Fopf;", QChar(0x0001d53d) },
	{ "ForAll;", QChar(0x2200) },
	{ "Fouriertrf;", QChar(0x2131) },
	{ "Fscr;", QChar(0x2131) },
	{ "GJcy;", QChar(0x0403) },
	{ "GT", ">" },
	{ "GT;", ">" },
	{ "Gamma;", QChar(0x0393) },
	{ "Gammad;", QChar(0x03dc) },
	{ "Gbreve;", QChar(0x011e) },
	{ "Gcedil;", QChar(0x0122) },
	{ "Gcirc;", QChar(0x011c) },
	{ "Gcy;", QChar(0x0413) },
	{ "Gdot;", QChar(0x0120) },
	{ "Gfr;", QChar(0x0001d50a) },
	{ "Gg;", QChar(0x22d9) },
	{ "Gopf;", QChar(0x0001d53e) },
	{ "GreaterEqual;", QChar(0x2265) },
	{ "GreaterEqualLess;", QChar(0x22db) },
	{ "GreaterFullEqual;", QChar(0x2267) },
	{ "GreaterGreater;", QChar(0x2aa2) },
	{ "GreaterLess;", QChar(0x2277) },
	{ "GreaterSlantEqual;", QChar(0x2a7e) },
	{ "GreaterTilde;", QChar(0x2273) },
	{ "Gscr;", QChar(0x0001d4a2) },
	{ "Gt;", QChar(0x226b) },
	{ "HARDcy;", QChar(0x042a) },
	{ "Hacek;", QChar(0x02c7) },
	{ "Hat;", "^" },
	{ "Hcirc;", QChar(0x0124) },
	{ "Hfr;", QChar(0x210c) },
	{ "HilbertSpace;", QChar(0x210b) },
	{ "Hopf;", QChar(0x210d) },
	{ "HorizontalLine;", QChar(0x2500) },
	{ "Hscr;", QChar(0x210b) },
	{ "Hstrok;", QChar(0x0126) },
	{ "HumpDownHump;", QChar(0x224e) },
	{ "HumpEqual;", QChar(0x224f) },
	{ "IEcy;", QChar(0x0415) },
	{ "IJlig;", QChar(0x0132) },
	{ "IOcy;", QChar(0x0401) },
	{ "Iacute", QChar(0xcd) },
	{ "Iacute;", QChar(0xcd) },
	{ "Icirc", QChar(0xce) },
	{ "Icirc;", QChar(0xce) },
	{ "Icy;", QChar(0x0418) },
	{ "Idot;", QChar(0x0130) },
	{ "Ifr;", QChar(0x2111) },
	{ "Igrave", QChar(0xcc) },
	{ "Igrave;", QChar(0xcc) },
	{ "Im;", QChar(0x2111) },
	{ "Imacr;", QChar(0x012a) },
	{ "ImaginaryI;", QChar(0x2148) },
	{ "Implies;", QChar(0x21d2) },
	{ "Int;", QChar(0x222c) },
	{ "Integral;", QChar(0x222b) },
	{ "Intersection;", QChar(0x22c2) },
	{ "InvisibleComma;", QChar(0x2063) },
	{ "InvisibleTimes;", QChar(0x2062) },
	{ "Iogon;", QChar(0x012e) },
	{ "Iopf;", QChar(0x0001d540) },
	{ "Iota;", QChar(0x0399) },
	{ "Iscr;", QChar(0x2110) },
	{ "Itilde;", QChar(0x0128) },
	{ "Iukcy;", QChar(0x0406) },
	{ "Iuml", QChar(0xcf) },
	{ "Iuml;", QChar(0xcf) },
	{ "Jcirc;", QChar(0x0134) },
	{ "Jcy;", QChar(0x0419) },
	{ "Jfr;", QChar(0x0001d50d) },
	{ "Jopf;", QChar(0x0001d541) },
	{ "Jscr;", QChar(0x0001d4a5) },
	{ "Jsercy;", QChar(0x0408) },
	{ "Jukcy;", QChar(0x0404) },
	{ "KHcy;", QChar(0x0425) },
	{ "KJcy;", QChar(0x040c) },
	{ "Kappa;", QChar(0x039a) },
	{ "Kcedil;", QChar(0x0136) },
	{ "Kcy;", QChar(0x041a) },
	{ "Kfr;", QChar(0x0001d50e) },
	{ "Kopf;", QChar(0x0001d542) },
	{ "Kscr;", QChar(0x0001d4a6) },
	{ "LJcy;", QChar(0x0409) },
	{ "LT", "<" },
	{ "LT;", "<" },
	{ "Lacute;", QChar(0x0139) },
	{ "Lambda;", QChar(0x039b) },
	{ "Lang;", QChar(0x27ea) },
	{ "Laplacetrf;", QChar(0x2112) },
	{ "Larr;", QChar(0x219e) },
	{ "Lcaron;", QChar(0x013d) },
	{ "Lcedil;", QChar(0x013b) },
	{ "Lcy;", QChar(0x041b) },
	{ "LeftAngleBracket;", QChar(0x27e8) },
	{ "LeftArrow;", QChar(0x2190) },
	{ "LeftArrowBar;", QChar(0x21e4) },
	{ "LeftArrowRightArrow;", QChar(0x21c6) },
	{ "LeftCeiling;", QChar(0x2308) },
	{ "LeftDoubleBracket;", QChar(0x27e6) },
	{ "LeftDownTeeVector;", QChar(0x2961) },
	{ "LeftDownVector;", QChar(0x21c3) },
	{ "LeftDownVectorBar;", QChar(0x2959) },
	{ "LeftFloor;", QChar(0x230a) },
	{ "LeftRightArrow;", QChar(0x2194) },
	{ "LeftRightVector;", QChar(0x294e) },
	{ "LeftTee;", QChar(0x22a3) },
	{ "LeftTeeArrow;", QChar(0x21a4) },
	{ "LeftTeeVector;", QChar(0x295a) },
	{ "LeftTriangle;", QChar(0x22b2) },
	{ "LeftTriangleBar;", QChar(0x29cf) },
	{ "LeftTriangleEqual;", QChar(0x22b4) },
	{ "LeftUpDownVector;", QChar(0x2951) },
	{ "LeftUpTeeVector;", QChar(0x2960) },
	{ "LeftUpVector;", QChar(0x21bf) },
	{ "LeftUpVectorBar;", QChar(0x2958) },
	{ "LeftVector;", QChar(0x21bc) },
	{ "LeftVectorBar;", QChar(0x2952) },
	{ "Leftarrow;", QChar(0x21d0) },
	{ "Leftrightarrow;", QChar(0x21d4) },
	{ "LessEqualGreater;", QChar(0x22da) },
	{ "LessFullEqual;", QChar(0x2266) },
	{ "LessGreater;", QChar(0x2276) },
	{ "LessLess;", QChar(0x2aa1) },
	{ "LessSlantEqual;", QChar(0x2a7d) },
	{ "LessTilde;", QChar(0x2272) },
	{ "Lfr;", QChar(0x0001d50f) },
	{ "Ll;", QChar(0x22d8) },
	{ "Lleftarrow;", QChar(0x21da) },
	{ "Lmidot;", QChar(0x013f) },
	{ "LongLeftArrow;", QChar(0x27f5) },
	{ "LongLeftRightArrow;", QChar(0x27f7) },
	{ "LongRightArrow;", QChar(0x27f6) },
	{ "Longleftarrow;", QChar(0x27f8) },
	{ "Longleftrightarrow;", QChar(0x27fa) },
	{ "Longrightarrow;", QChar(0x27f9) },
	{ "Lopf;", QChar(0x0001d543) },
	{ "LowerLeftArrow;", QChar(0x2199) },
	{ "LowerRightArrow;", QChar(0x2198) },
	{ "Lscr;", QChar(0x2112) },
	{ "Lsh;", QChar(0x21b0) },
	{ "Lstrok;", QChar(0x0141) },
	{ "Lt;", QChar(0x226a) },
	{ "Map;", QChar(0x2905) },
	{ "Mcy;", QChar(0x041c) },
	{ "MediumSpace;", QChar(0x205f) },
	{ "Mellintrf;", QChar(0x2133) },
	{ "Mfr;", QChar(0x0001d510) },
	{ "MinusPlus;", QChar(0x2213) },
	{ "Mopf;", QChar(0x0001d544) },
	{ "Mscr;", QChar(0x2133) },
	{ "Mu;", QChar(0x039c) },
	{ "NJcy;", QChar(0x040a) },
	{ "Nacute;", QChar(0x0143) },
	{ "Ncaron;", QChar(0x0147) },
	{ "Ncedil;", QChar(0x0145) },
	{ "Ncy;", QChar(0x041d) },
	{ "NegativeMediumSpace;", QChar(0x200b) },
	{ "NegativeThickSpace;", QChar(0x200b) },
	{ "NegativeThinSpace;", QChar(0x200b) },
	{ "NegativeVeryThinSpace;", QChar(0x200b) },
	{ "NestedGreaterGreater;", QChar(0x226b) },
	{ "NestedLessLess;", QChar(0x226a) },
	{ "NewLine;", "\n" },
	{ "Nfr;", QChar(0x0001d511) },
	{ "NoBreak;", QChar(0x2060) },
	{ "NonBreakingSpace;", QChar(0xa0) },
	{ "Nopf;", QChar(0x2115) },
	{ "Not;", QChar(0x2aec) },
	{ "NotCongruent;", QChar(0x2262) },
	{ "NotCupCap;", QChar(0x226d) },
	{ "NotDoubleVerticalBar;", QChar(0x2226) },
	{ "NotElement;", QChar(0x2209) },
	{ "NotEqual;", QChar(0x2260) },
	{ "NotEqualTilde;", QChar(0x2242) % QChar(0x0338) },
	{ "NotExists;", QChar(0x2204) },
	{ "NotGreater;", QChar(0x226f) },
	{ "NotGreaterEqual;", QChar(0x2271) },
	{ "NotGreaterFullEqual;", QChar(0x2267) % QChar(0x0338) },
	{ "NotGreaterGreater;", QChar(0x226b) % QChar(0x0338) },
	{ "NotGreaterLess;", QChar(0x2279) },
	{ "NotGreaterSlantEqual;", QChar(0x2a7e) % QChar(0x0338) },
	{ "NotGreaterTilde;", QChar(0x2275) },
	{ "NotHumpDownHump;", QChar(0x224e) % QChar(0x0338) },
	{ "NotHumpEqual;", QChar(0x224f) % QChar(0x0338) },
	{ "NotLeftTriangle;", QChar(0x22ea) },
	{ "NotLeftTriangleBar;", QChar(0x29cf) % QChar(0x0338) },
	{ "NotLeftTriangleEqual;", QChar(0x22ec) },
	{ "NotLess;", QChar(0x226e) },
	{ "NotLessEqual;", QChar(0x2270) },
	{ "NotLessGreater;", QChar(0x2278) },
	{ "NotLessLess;", QChar(0x226a) % QChar(0x0338) },
	{ "NotLessSlantEqual;", QChar(0x2a7d) % QChar(0x0338) },
	{ "NotLessTilde;", QChar(0x2274) },
	{ "NotNestedGreaterGreater;", QChar(0x2aa2) % QChar(0x0338) },
	{ "NotNestedLessLess;", QChar(0x2aa1) % QChar(0x0338) },
	{ "NotPrecedes;", QChar(0x2280) },
	{ "NotPrecedesEqual;", QChar(0x2aaf) % QChar(0x0338) },
	{ "NotPrecedesSlantEqual;", QChar(0x22e0) },
	{ "NotReverseElement;", QChar(0x220c) },
	{ "NotRightTriangle;", QChar(0x22eb) },
	{ "NotRightTriangleBar;", QChar(0x29d0) % QChar(0x0338) },
	{ "NotRightTriangleEqual;", QChar(0x22ed) },
	{ "NotSquareSubset;", QChar(0x228f) % QChar(0x0338) },
	{ "NotSquareSubsetEqual;", QChar(0x22e2) },
	{ "NotSquareSuperset;", QChar(0x2290) % QChar(0x0338) },
	{ "NotSquareSupersetEqual;", QChar(0x22e3) },
	{ "NotSubset;", QChar(0x2282) % QChar(0x20d2) },
	{ "NotSubsetEqual;", QChar(0x2288) },
	{ "NotSucceeds;", QChar(0x2281) },
	{ "NotSucceedsEqual;", QChar(0x2ab0) % QChar(0x0338) },
	{ "NotSucceedsSlantEqual;", QChar(0x22e1) },
	{ "NotSucceedsTilde;", QChar(0x227f) % QChar(0x0338) },
	{ "NotSuperset;", QChar(0x2283) % QChar(0x20d2) },
	{ "NotSupersetEqual;", QChar(0x2289) },
	{ "NotTilde;", QChar(0x2241) },
	{ "NotTildeEqual;", QChar(0x2244) },
	{ "NotTildeFullEqual;", QChar(0x2247) },
	{ "NotTildeTilde;", QChar(0x2249) },
	{ "NotVerticalBar;", QChar(0x2224) },
	{ "Nscr;", QChar(0x0001d4a9) },
	{ "Ntilde", QChar(0xd1) },
	{ "Ntilde;", QChar(0xd1) },
	{ "Nu;", QChar(0x039d) },
	{ "OElig;", QChar(0x0152) },
	{ "Oacute", QChar(0xd3) },
	{ "Oacute;", QChar(0xd3) },
	{ "Ocirc", QChar(0xd4) },
	{ "Ocirc;", QChar(0xd4) },
	{ "Ocy;", QChar(0x041e) },
	{ "Odblac;", QChar(0x0150) },
	{ "Ofr;", QChar(0x0001d512) },
	{ "Ograve", QChar(0xd2) },
	{ "Ograve;", QChar(0xd2) },
	{ "Omacr;", QChar(0x014c) },
	{ "Omega;", QChar(0x03a9) },
	{ "Omicron;", QChar(0x039f) },
	{ "Oopf;", QChar(0x0001d546) },
	{ "OpenCurlyDoubleQuote;", QChar(0x201c) },
	{ "OpenCurlyQuote;", QChar(0x2018) },
	{ "Or;", QChar(0x2a54) },
	{ "Oscr;", QChar(0x0001d4aa) },
	{ "Oslash", QChar(0xd8) },
	{ "Oslash;", QChar(0xd8) },
	{ "Otilde", QChar(0xd5) },
	{ "Otilde;", QChar(0xd5) },
	{ "Otimes;", QChar(0x2a37) },
	{ "Ouml", QChar(0xd6) },
	{ "Ouml;", QChar(0xd6) },
	{ "OverBar;", QChar(0x203e) },
	{ "OverBrace;", QChar(0x23de) },
	{ "OverBracket;", QChar(0x23b4) },
	{ "OverParenthesis;", QChar(0x23dc) },
	{ "PartialD;", QChar(0x2202) },
	{ "Pcy;", QChar(0x041f) },
	{ "Pfr;", QChar(0x0001d513) },
	{ "Phi;", QChar(0x03a6) },
	{ "Pi;", QChar(0x03a0) },
	{ "PlusMinus;", QChar(0xb1) },
	{ "Poincareplane;", QChar(0x210c) },
	{ "Popf;", QChar(0x2119) },
	{ "Pr;", QChar(0x2abb) },
	{ "Precedes;", QChar(0x227a) },
	{ "PrecedesEqual;", QChar(0x2aaf) },
	{ "PrecedesSlantEqual;", QChar(0x227c) },
	{ "PrecedesTilde;", QChar(0x227e) },
	{ "Prime;", QChar(0x2033) },
	{ "Product;", QChar(0x220f) },
	{ "Proportion;", QChar(0x2237) },
	{ "Proportional;", QChar(0x221d) },
	{ "Pscr;", QChar(0x0001d4ab) },
	{ "Psi;", QChar(0x03a8) },
	{ "QUOT", "\"" },
	{ "QUOT;", "\"" },
	{ "Qfr;", QChar(0x0001d514) },
	{ "Qopf;", QChar(0x211a) },
	{ "Qscr;", QChar(0x0001d4ac) },
	{ "RBarr;", QChar(0x2910) },
	{ "REG", QChar(0xae) },
	{ "REG;", QChar(0xae) },
	{ "Racute;", QChar(0x0154) },
	{ "Rang;", QChar(0x27eb) },
	{ "Rarr;", QChar(0x21a0) },
	{ "Rarrtl;", QChar(0x2916) },
	{ "Rcaron;", QChar(0x0158) },
	{ "Rcedil;", QChar(0x0156) },
	{ "Rcy;", QChar(0x0420) },
	{ "Re;", QChar(0x211c) },
	{ "ReverseElement;", QChar(0x220b) },
	{ "ReverseEquilibrium;", QChar(0x21cb) },
	{ "ReverseUpEquilibrium;", QChar(0x296f) },
	{ "Rfr;", QChar(0x211c) },
	{ "Rho;", QChar(0x03a1) },
	{ "RightAngleBracket;", QChar(0x27e9) },
	{ "RightArrow;", QChar(0x2192) },
	{ "RightArrowBar;", QChar(0x21e5) },
	{ "RightArrowLeftArrow;", QChar(0x21c4) },
	{ "RightCeiling;", QChar(0x2309) },
	{ "RightDoubleBracket;", QChar(0x27e7) },
	{ "RightDownTeeVector;", QChar(0x295d) },
	{ "RightDownVector;", QChar(0x21c2) },
	{ "RightDownVectorBar;", QChar(0x2955) },
	{ "RightFloor;", QChar(0x230b) },
	{ "RightTee;", QChar(0x22a2) },
	{ "RightTeeArrow;", QChar(0x21a6) },
	{ "RightTeeVector;", QChar(0x295b) },
	{ "RightTriangle;", QChar(0x22b3) },
	{ "RightTriangleBar;", QChar(0x29d0) },
	{ "RightTriangleEqual;", QChar(0x22b5) },
	{ "RightUpDownVector;", QChar(0x294f) },
	{ "RightUpTeeVector;", QChar(0x295c) },
	{ "RightUpVector;", QChar(0x21be) },
	{ "RightUpVectorBar;", QChar(0x2954) },
	{ "RightVector;", QChar(0x21c0) },
	{ "RightVectorBar;", QChar(0x2953) },
	{ "Rightarrow;", QChar(0x21d2) },
	{ "Ropf;", QChar(0x211d) },
	{ "RoundImplies;", QChar(0x2970) },
	{ "Rrightarrow;", QChar(0x21db) },
	{ "Rscr;", QChar(0x211b) },
	{ "Rsh;", QChar(0x21b1) },
	{ "RuleDelayed;", QChar(0x29f4) },
	{ "SHCHcy;", QChar(0x0429) },
	{ "SHcy;", QChar(0x0428) },
	{ "SOFTcy;", QChar(0x042c) },
	{ "Sacute;", QChar(0x015a) },
	{ "Sc;", QChar(0x2abc) },
	{ "Scaron;", QChar(0x0160) },
	{ "Scedil;", QChar(0x015e) },
	{ "Scirc;", QChar(0x015c) },
	{ "Scy;", QChar(0x0421) },
	{ "Sfr;", QChar(0x0001d516) },
	{ "ShortDownArrow;", QChar(0x2193) },
	{ "ShortLeftArrow;", QChar(0x2190) },
	{ "ShortRightArrow;", QChar(0x2192) },
	{ "ShortUpArrow;", QChar(0x2191) },
	{ "Sigma;", QChar(0x03a3) },
	{ "SmallCircle;", QChar(0x2218) },
	{ "Sopf;", QChar(0x0001d54a) },
	{ "Sqrt;", QChar(0x221a) },
	{ "Square;", QChar(0x25a1) },
	{ "SquareIntersection;", QChar(0x2293) },
	{ "SquareSubset;", QChar(0x228f) },
	{ "SquareSubsetEqual;", QChar(0x2291) },
	{ "SquareSuperset;", QChar(0x2290) },
	{ "SquareSupersetEqual;", QChar(0x2292) },
	{ "SquareUnion;", QChar(0x2294) },
	{ "Sscr;", QChar(0x0001d4ae) },
	{ "Star;", QChar(0x22c6) },
	{ "Sub;", QChar(0x22d0) },
	{ "Subset;", QChar(0x22d0) },
	{ "SubsetEqual;", QChar(0x2286) },
	{ "Succeeds;", QChar(0x227b) },
	{ "SucceedsEqual;", QChar(0x2ab0) },
	{ "SucceedsSlantEqual;", QChar(0x227d) },
	{ "SucceedsTilde;", QChar(0x227f) },
	{ "SuchThat;", QChar(0x220b) },
	{ "Sum;", QChar(0x2211) },
	{ "Sup;", QChar(0x22d1) },
	{ "Superset;", QChar(0x2283) },
	{ "SupersetEqual;", QChar(0x2287) },
	{ "Supset;", QChar(0x22d1) },
	{ "THORN", QChar(0xde) },
	{ "THORN;", QChar(0xde) },
	{ "TRADE;", QChar(0x2122) },
	{ "TSHcy;", QChar(0x040b) },
	{ "TScy;", QChar(0x0426) },
	{ "Tab;", "\t" },
	{ "Tau;", QChar(0x03a4) },
	{ "Tcaron;", QChar(0x0164) },
	{ "Tcedil;", QChar(0x0162) },
	{ "Tcy;", QChar(0x0422) },
	{ "Tfr;", QChar(0x0001d517) },
	{ "Therefore;", QChar(0x2234) },
	{ "Theta;", QChar(0x0398) },
	{ "ThickSpace;", QChar(0x205f) % QChar(0x200a) },
	{ "ThinSpace;", QChar(0x2009) },
	{ "Tilde;", QChar(0x223c) },
	{ "TildeEqual;", QChar(0x2243) },
	{ "TildeFullEqual;", QChar(0x2245) },
	{ "TildeTilde;", QChar(0x2248) },
	{ "Topf;", QChar(0x0001d54b) },
	{ "TripleDot;", QChar(0x20db) },
	{ "Tscr;", QChar(0x0001d4af) },
	{ "Tstrok;", QChar(0x0166) },
	{ "Uacute", QChar(0xda) },
	{ "Uacute;", QChar(0xda) },
	{ "Uarr;", QChar(0x219f) },
	{ "Uarrocir;", QChar(0x2949) },
	{ "Ubrcy;", QChar(0x040e) },
	{ "Ubreve;", QChar(0x016c) },
	{ "Ucirc", QChar(0xdb) },
	{ "Ucirc;", QChar(0xdb) },
	{ "Ucy;", QChar(0x0423) },
	{ "Udblac;", QChar(0x0170) },
	{ "Ufr;", QChar(0x0001d518) },
	{ "Ugrave", QChar(0xd9) },
	{ "Ugrave;", QChar(0xd9) },
	{ "Umacr;", QChar(0x016a) },
	{ "UnderBar;", "_" },
	{ "UnderBrace;", QChar(0x23df) },
	{ "UnderBracket;", QChar(0x23b5) },
	{ "UnderParenthesis;", QChar(0x23dd) },
	{ "Union;", QChar(0x22c3) },
	{ "UnionPlus;", QChar(0x228e) },
	{ "Uogon;", QChar(0x0172) },
	{ "Uopf;", QChar(0x0001d54c) },
	{ "UpArrow;", QChar(0x2191) },
	{ "UpArrowBar;", QChar(0x2912) },
	{ "UpArrowDownArrow;", QChar(0x21c5) },
	{ "UpDownArrow;", QChar(0x2195) },
	{ "UpEquilibrium;", QChar(0x296e) },
	{ "UpTee;", QChar(0x22a5) },
	{ "UpTeeArrow;", QChar(0x21a5) },
	{ "Uparrow;", QChar(0x21d1) },
	{ "Updownarrow;", QChar(0x21d5) },
	{ "UpperLeftArrow;", QChar(0x2196) },
	{ "UpperRightArrow;", QChar(0x2197) },
	{ "Upsi;", QChar(0x03d2) },
	{ "Upsilon;", QChar(0x03a5) },
	{ "Uring;", QChar(0x016e) },
	{ "Uscr;", QChar(0x0001d4b0) },
	{ "Utilde;", QChar(0x0168) },
	{ "Uuml", QChar(0xdc) },
	{ "Uuml;", QChar(0xdc) },
	{ "VDash;", QChar(0x22ab) },
	{ "Vbar;", QChar(0x2aeb) },
	{ "Vcy;", QChar(0x0412) },
	{ "Vdash;", QChar(0x22a9) },
	{ "Vdashl;", QChar(0x2ae6) },
	{ "Vee;", QChar(0x22c1) },
	{ "Verbar;", QChar(0x2016) },
	{ "Vert;", QChar(0x2016) },
	{ "VerticalBar;", QChar(0x2223) },
	{ "VerticalLine;", "|" },
	{ "VerticalSeparator;", QChar(0x2758) },
	{ "VerticalTilde;", QChar(0x2240) },
	{ "VeryThinSpace;", QChar(0x200a) },
	{ "Vfr;", QChar(0x0001d519) },
	{ "Vopf;", QChar(0x0001d54d) },
	{ "Vscr;", QChar(0x0001d4b1) },
	{ "Vvdash;", QChar(0x22aa) },
	{ "Wcirc;", QChar(0x0174) },
	{ "Wedge;", QChar(0x22c0) },
	{ "Wfr;", QChar(0x0001d51a) },
	{ "Wopf;", QChar(0x0001d54e) },
	{ "Wscr;", QChar(0x0001d4b2) },
	{ "Xfr;", QChar(0x0001d51b) },
	{ "Xi;", QChar(0x039e) },
	{ "Xopf;", QChar(0x0001d54f) },
	{ "Xscr;", QChar(0x0001d4b3) },
	{ "YAcy;", QChar(0x042f) },
	{ "YIcy;", QChar(0x0407) },
	{ "YUcy;", QChar(0x042e) },
	{ "Yacute", QChar(0xdd) },
	{ "Yacute;", QChar(0xdd) },
	{ "Ycirc;", QChar(0x0176) },
	{ "Ycy;", QChar(0x042b) },
	{ "Yfr;", QChar(0x0001d51c) },
	{ "Yopf;", QChar(0x0001d550) },
	{ "Yscr;", QChar(0x0001d4b4) },
	{ "Yuml;", QChar(0x0178) },
	{ "ZHcy;", QChar(0x0416) },
	{ "Zacute;", QChar(0x0179) },
	{ "Zcaron;", QChar(0x017d) },
	{ "Zcy;", QChar(0x0417) },
	{ "Zdot;", QChar(0x017b) },
	{ "ZeroWidthSpace;", QChar(0x200b) },
	{ "Zeta;", QChar(0x0396) },
	{ "Zfr;", QChar(0x2128) },
	{ "Zopf;", QChar(0x2124) },
	{ "Zscr;", QChar(0x0001d4b5) },
	{ "aacute", QChar(0xe1) },
	{ "aacute;", QChar(0xe1) },
	{ "abreve;", QChar(0x0103) },
	{ "ac;", QChar(0x223e) },
	{ "acE;", QChar(0x223e) % QChar(0x0333) },
	{ "acd;", QChar(0x223f) },
	{ "acirc", QChar(0xe2) },
	{ "acirc;", QChar(0xe2) },
	{ "acute", QChar(0xb4) },
	{ "acute;", QChar(0xb4) },
	{ "acy;", QChar(0x0430) },
	{ "aelig", QChar(0xe6) },
	{ "aelig;", QChar(0xe6) },
	{ "af;", QChar(0x2061) },
	{ "afr;", QChar(0x0001d51e) },
	{ "agrave", QChar(0xe0) },
	{ "agrave;", QChar(0xe0) },
	{ "alefsym;", QChar(0x2135) },
	{ "aleph;", QChar(0x2135) },
	{ "alpha;", QChar(0x03b1) },
	{ "amacr;", QChar(0x0101) },
	{ "amalg;", QChar(0x2a3f) },
	{ "amp", "&" },
	{ "amp;", "&" },
	{ "and;", QChar(0x2227) },
	{ "andand;", QChar(0x2a55) },
	{ "andd;", QChar(0x2a5c) },
	{ "andslope;", QChar(0x2a58) },
	{ "andv;", QChar(0x2a5a) },
	{ "ang;", QChar(0x2220) },
	{ "ange;", QChar(0x29a4) },
	{ "angle;", QChar(0x2220) },
	{ "angmsd;", QChar(0x2221) },
	{ "angmsdaa;", QChar(0x29a8) },
	{ "angmsdab;", QChar(0x29a9) },
	{ "angmsdac;", QChar(0x29aa) },
	{ "angmsdad;", QChar(0x29ab) },
	{ "angmsdae;", QChar(0x29ac) },
	{ "angmsdaf;", QChar(0x29ad) },
	{ "angmsdag;", QChar(0x29ae) },
	{ "angmsdah;", QChar(0x29af) },
	{ "angrt;", QChar(0x221f) },
	{ "angrtvb;", QChar(0x22be) },
	{ "angrtvbd;", QChar(0x299d) },
	{ "angsph;", QChar(0x2222) },
	{ "angst;", QChar(0xc5) },
	{ "angzarr;", QChar(0x237c) },
	{ "aogon;", QChar(0x0105) },
	{ "aopf;", QChar(0x0001d552) },
	{ "ap;", QChar(0x2248) },
	{ "apE;", QChar(0x2a70) },
	{ "apacir;", QChar(0x2a6f) },
	{ "ape;", QChar(0x224a) },
	{ "apid;", QChar(0x224b) },
	{ "apos;", "'" },
	{ "approx;", QChar(0x2248) },
	{ "approxeq;", QChar(0x224a) },
	{ "aring", QChar(0xe5) },
	{ "aring;", QChar(0xe5) },
	{ "ascr;", QChar(0x0001d4b6) },
	{ "ast;", "*" },
	{ "asymp;", QChar(0x2248) },
	{ "asympeq;", QChar(0x224d) },
	{ "atilde", QChar(0xe3) },
	{ "atilde;", QChar(0xe3) },
	{ "auml", QChar(0xe4) },
	{ "auml;", QChar(0xe4) },
	{ "awconint;", QChar(0x2233) },
	{ "awint;", QChar(0x2a11) },
	{ "bNot;", QChar(0x2aed) },
	{ "backcong;", QChar(0x224c) },
	{ "backepsilon;", QChar(0x03f6) },
	{ "backprime;", QChar(0x2035) },
	{ "backsim;", QChar(0x223d) },
	{ "backsimeq;", QChar(0x22cd) },
	{ "barvee;", QChar(0x22bd) },
	{ "barwed;", QChar(0x2305) },
	{ "barwedge;", QChar(0x2305) },
	{ "bbrk;", QChar(0x23b5) },
	{ "bbrktbrk;", QChar(0x23b6) },
	{ "bcong;", QChar(0x224c) },
	{ "bcy;", QChar(0x0431) },
	{ "bdquo;", QChar(0x201e) },
	{ "becaus;", QChar(0x2235) },
	{ "because;", QChar(0x2235) },
	{ "bemptyv;", QChar(0x29b0) },
	{ "bepsi;", QChar(0x03f6) },
	{ "bernou;", QChar(0x212c) },
	{ "beta;", QChar(0x03b2) },
	{ "beth;", QChar(0x2136) },
	{ "between;", QChar(0x226c) },
	{ "bfr;", QChar(0x0001d51f) },
	{ "bigcap;", QChar(0x22c2) },
	{ "bigcirc;", QChar(0x25ef) },
	{ "bigcup;", QChar(0x22c3) },
	{ "bigodot;", QChar(0x2a00) },
	{ "bigoplus;", QChar(0x2a01) },
	{ "bigotimes;", QChar(0x2a02) },
	{ "bigsqcup;", QChar(0x2a06) },
	{ "bigstar;", QChar(0x2605) },
	{ "bigtriangledown;", QChar(0x25bd) },
	{ "bigtriangleup;", QChar(0x25b3) },
	{ "biguplus;", QChar(0x2a04) },
	{ "bigvee;", QChar(0x22c1) },
	{ "bigwedge;", QChar(0x22c0) },
	{ "bkarow;", QChar(0x290d) },
	{ "blacklozenge;", QChar(0x29eb) },
	{ "blacksquare;", QChar(0x25aa) },
	{ "blacktriangle;", QChar(0x25b4) },
	{ "blacktriangledown;", QChar(0x25be) },
	{ "blacktriangleleft;", QChar(0x25c2) },
	{ "blacktriangleright;", QChar(0x25b8) },
	{ "blank;", QChar(0x2423) },
	{ "blk12;", QChar(0x2592) },
	{ "blk14;", QChar(0x2591) },
	{ "blk34;", QChar(0x2593) },
	{ "block;", QChar(0x2588) },
	{ "bne;", "=QChar(0x20e5)" },
	{ "bnequiv;", QChar(0x2261) % QChar(0x20e5) },
	{ "bnot;", QChar(0x2310) },
	{ "bopf;", QChar(0x0001d553) },
	{ "bot;", QChar(0x22a5) },
	{ "bottom;", QChar(0x22a5) },
	{ "bowtie;", QChar(0x22c8) },
	{ "boxDL;", QChar(0x2557) },
	{ "boxDR;", QChar(0x2554) },
	{ "boxDl;", QChar(0x2556) },
	{ "boxDr;", QChar(0x2553) },
	{ "boxH;", QChar(0x2550) },
	{ "boxHD;", QChar(0x2566) },
	{ "boxHU;", QChar(0x2569) },
	{ "boxHd;", QChar(0x2564) },
	{ "boxHu;", QChar(0x2567) },
	{ "boxUL;", QChar(0x255d) },
	{ "boxUR;", QChar(0x255a) },
	{ "boxUl;", QChar(0x255c) },
	{ "boxUr;", QChar(0x2559) },
	{ "boxV;", QChar(0x2551) },
	{ "boxVH;", QChar(0x256c) },
	{ "boxVL;", QChar(0x2563) },
	{ "boxVR;", QChar(0x2560) },
	{ "boxVh;", QChar(0x256b) },
	{ "boxVl;", QChar(0x2562) },
	{ "boxVr;", QChar(0x255f) },
	{ "boxbox;", QChar(0x29c9) },
	{ "boxdL;", QChar(0x2555) },
	{ "boxdR;", QChar(0x2552) },
	{ "boxdl;", QChar(0x2510) },
	{ "boxdr;", QChar(0x250c) },
	{ "boxh;", QChar(0x2500) },
	{ "boxhD;", QChar(0x2565) },
	{ "boxhU;", QChar(0x2568) },
	{ "boxhd;", QChar(0x252c) },
	{ "boxhu;", QChar(0x2534) },
	{ "boxminus;", QChar(0x229f) },
	{ "boxplus;", QChar(0x229e) },
	{ "boxtimes;", QChar(0x22a0) },
	{ "boxuL;", QChar(0x255b) },
	{ "boxuR;", QChar(0x2558) },
	{ "boxul;", QChar(0x2518) },
	{ "boxur;", QChar(0x2514) },
	{ "boxv;", QChar(0x2502) },
	{ "boxvH;", QChar(0x256a) },
	{ "boxvL;", QChar(0x2561) },
	{ "boxvR;", QChar(0x255e) },
	{ "boxvh;", QChar(0x253c) },
	{ "boxvl;", QChar(0x2524) },
	{ "boxvr;", QChar(0x251c) },
	{ "bprime;", QChar(0x2035) },
	{ "breve;", QChar(0x02d8) },
	{ "brvbar", QChar(0xa6) },
	{ "brvbar;", QChar(0xa6) },
	{ "bscr;", QChar(0x0001d4b7) },
	{ "bsemi;", QChar(0x204f) },
	{ "bsim;", QChar(0x223d) },
	{ "bsime;", QChar(0x22cd) },
	{ "bsol;", "\\" },
	{ "bsolb;", QChar(0x29c5) },
	{ "bsolhsub;", QChar(0x27c8) },
	{ "bull;", QChar(0x2022) },
	{ "bullet;", QChar(0x2022) },
	{ "bump;", QChar(0x224e) },
	{ "bumpE;", QChar(0x2aae) },
	{ "bumpe;", QChar(0x224f) },
	{ "bumpeq;", QChar(0x224f) },
	{ "cacute;", QChar(0x0107) },
	{ "cap;", QChar(0x2229) },
	{ "capand;", QChar(0x2a44) },
	{ "capbrcup;", QChar(0x2a49) },
	{ "capcap;", QChar(0x2a4b) },
	{ "capcup;", QChar(0x2a47) },
	{ "capdot;", QChar(0x2a40) },
	{ "caps;", QChar(0x2229) % QChar(0xfe00) },
	{ "caret;", QChar(0x2041) },
	{ "caron;", QChar(0x02c7) },
	{ "ccaps;", QChar(0x2a4d) },
	{ "ccaron;", QChar(0x010d) },
	{ "ccedil", QChar(0xe7) },
	{ "ccedil;", QChar(0xe7) },
	{ "ccirc;", QChar(0x0109) },
	{ "ccups;", QChar(0x2a4c) },
	{ "ccupssm;", QChar(0x2a50) },
	{ "cdot;", QChar(0x010b) },
	{ "cedil", QChar(0xb8) },
	{ "cedil;", QChar(0xb8) },
	{ "cemptyv;", QChar(0x29b2) },
	{ "cent", QChar(0xa2) },
	{ "cent;", QChar(0xa2) },
	{ "centerdot;", QChar(0xb7) },
	{ "cfr;", QChar(0x0001d520) },
	{ "chcy;", QChar(0x0447) },
	{ "check;", QChar(0x2713) },
	{ "checkmark;", QChar(0x2713) },
	{ "chi;", QChar(0x03c7) },
	{ "cir;", QChar(0x25cb) },
	{ "cirE;", QChar(0x29c3) },
	{ "circ;", QChar(0x02c6) },
	{ "circeq;", QChar(0x2257) },
	{ "circlearrowleft;", QChar(0x21ba) },
	{ "circlearrowright;", QChar(0x21bb) },
	{ "circledR;", QChar(0xae) },
	{ "circledS;", QChar(0x24c8) },
	{ "circledast;", QChar(0x229b) },
	{ "circledcirc;", QChar(0x229a) },
	{ "circleddash;", QChar(0x229d) },
	{ "cire;", QChar(0x2257) },
	{ "cirfnint;", QChar(0x2a10) },
	{ "cirmid;", QChar(0x2aef) },
	{ "cirscir;", QChar(0x29c2) },
	{ "clubs;", QChar(0x2663) },
	{ "clubsuit;", QChar(0x2663) },
	{ "colon;", "," },
	{ "colone;", QChar(0x2254) },
	{ "coloneq;", QChar(0x2254) },
	{ "comma;", "," },
	{ "commat;", "@" },
	{ "comp;", QChar(0x2201) },
	{ "compfn;", QChar(0x2218) },
	{ "complement;", QChar(0x2201) },
	{ "complexes;", QChar(0x2102) },
	{ "cong;", QChar(0x2245) },
	{ "congdot;", QChar(0x2a6d) },
	{ "conint;", QChar(0x222e) },
	{ "copf;", QChar(0x0001d554) },
	{ "coprod;", QChar(0x2210) },
	{ "copy", QChar(0xa9) },
	{ "copy;", QChar(0xa9) },
	{ "copysr;", QChar(0x2117) },
	{ "crarr;", QChar(0x21b5) },
	{ "cross;", QChar(0x2717) },
	{ "cscr;", QChar(0x0001d4b8) },
	{ "csub;", QChar(0x2acf) },
	{ "csube;", QChar(0x2ad1) },
	{ "csup;", QChar(0x2ad0) },
	{ "csupe;", QChar(0x2ad2) },
	{ "ctdot;", QChar(0x22ef) },
	{ "cudarrl;", QChar(0x2938) },
	{ "cudarrr;", QChar(0x2935) },
	{ "cuepr;", QChar(0x22de) },
	{ "cuesc;", QChar(0x22df) },
	{ "cularr;", QChar(0x21b6) },
	{ "cularrp;", QChar(0x293d) },
	{ "cup;", QChar(0x222a) },
	{ "cupbrcap;", QChar(0x2a48) },
	{ "cupcap;", QChar(0x2a46) },
	{ "cupcup;", QChar(0x2a4a) },
	{ "cupdot;", QChar(0x228d) },
	{ "cupor;", QChar(0x2a45) },
	{ "cups;", QChar(0x222a) % QChar(0xfe00) },
	{ "curarr;", QChar(0x21b7) },
	{ "curarrm;", QChar(0x293c) },
	{ "curlyeqprec;", QChar(0x22de) },
	{ "curlyeqsucc;", QChar(0x22df) },
	{ "curlyvee;", QChar(0x22ce) },
	{ "curlywedge;", QChar(0x22cf) },
	{ "curren", QChar(0xa4) },
	{ "curren;", QChar(0xa4) },
	{ "curvearrowleft;", QChar(0x21b6) },
	{ "curvearrowright;", QChar(0x21b7) },
	{ "cuvee;", QChar(0x22ce) },
	{ "cuwed;", QChar(0x22cf) },
	{ "cwconint;", QChar(0x2232) },
	{ "cwint;", QChar(0x2231) },
	{ "cylcty;", QChar(0x232d) },
	{ "dArr;", QChar(0x21d3) },
	{ "dHar;", QChar(0x2965) },
	{ "dagger;", QChar(0x2020) },
	{ "daleth;", QChar(0x2138) },
	{ "darr;", QChar(0x2193) },
	{ "dash;", QChar(0x2010) },
	{ "dashv;", QChar(0x22a3) },
	{ "dbkarow;", QChar(0x290f) },
	{ "dblac;", QChar(0x02dd) },
	{ "dcaron;", QChar(0x010f) },
	{ "dcy;", QChar(0x0434) },
	{ "dd;", QChar(0x2146) },
	{ "ddagger;", QChar(0x2021) },
	{ "ddarr;", QChar(0x21ca) },
	{ "ddotseq;", QChar(0x2a77) },
	{ "deg", QChar(0xb0) },
	{ "deg;", QChar(0xb0) },
	{ "delta;", QChar(0x03b4) },
	{ "demptyv;", QChar(0x29b1) },
	{ "dfisht;", QChar(0x297f) },
	{ "dfr;", QChar(0x0001d521) },
	{ "dharl;", QChar(0x21c3) },
	{ "dharr;", QChar(0x21c2) },
	{ "diam;", QChar(0x22c4) },
	{ "diamond;", QChar(0x22c4) },
	{ "diamondsuit;", QChar(0x2666) },
	{ "diams;", QChar(0x2666) },
	{ "die;", QChar(0xa8) },
	{ "digamma;", QChar(0x03dd) },
	{ "disin;", QChar(0x22f2) },
	{ "div;", QChar(0xf7) },
	{ "divide", QChar(0xf7) },
	{ "divide;", QChar(0xf7) },
	{ "divideontimes;", QChar(0x22c7) },
	{ "divonx;", QChar(0x22c7) },
	{ "djcy;", QChar(0x0452) },
	{ "dlcorn;", QChar(0x231e) },
	{ "dlcrop;", QChar(0x230d) },
	{ "dollar;", "$" },
	{ "dopf;", QChar(0x0001d555) },
	{ "dot;", QChar(0x02d9) },
	{ "doteq;", QChar(0x2250) },
	{ "doteqdot;", QChar(0x2251) },
	{ "dotminus;", QChar(0x2238) },
	{ "dotplus;", QChar(0x2214) },
	{ "dotsquare;", QChar(0x22a1) },
	{ "doublebarwedge;", QChar(0x2306) },
	{ "downarrow;", QChar(0x2193) },
	{ "downdownarrows;", QChar(0x21ca) },
	{ "downharpoonleft;", QChar(0x21c3) },
	{ "downharpoonright;", QChar(0x21c2) },
	{ "drbkarow;", QChar(0x2910) },
	{ "drcorn;", QChar(0x231f) },
	{ "drcrop;", QChar(0x230c) },
	{ "dscr;", QChar(0x0001d4b9) },
	{ "dscy;", QChar(0x0455) },
	{ "dsol;", QChar(0x29f6) },
	{ "dstrok;", QChar(0x0111) },
	{ "dtdot;", QChar(0x22f1) },
	{ "dtri;", QChar(0x25bf) },
	{ "dtrif;", QChar(0x25be) },
	{ "duarr;", QChar(0x21f5) },
	{ "duhar;", QChar(0x296f) },
	{ "dwangle;", QChar(0x29a6) },
	{ "dzcy;", QChar(0x045f) },
	{ "dzigrarr;", QChar(0x27ff) },
	{ "eDDot;", QChar(0x2a77) },
	{ "eDot;", QChar(0x2251) },
	{ "eacute", QChar(0xe9) },
	{ "eacute;", QChar(0xe9) },
	{ "easter;", QChar(0x2a6e) },
	{ "ecaron;", QChar(0x011b) },
	{ "ecir;", QChar(0x2256) },
	{ "ecirc", QChar(0xea) },
	{ "ecirc;", QChar(0xea) },
	{ "ecolon;", QChar(0x2255) },
	{ "ecy;", QChar(0x044d) },
	{ "edot;", QChar(0x0117) },
	{ "ee;", QChar(0x2147) },
	{ "efDot;", QChar(0x2252) },
	{ "efr;", QChar(0x0001d522) },
	{ "eg;", QChar(0x2a9a) },
	{ "egrave", QChar(0xe8) },
	{ "egrave;", QChar(0xe8) },
	{ "egs;", QChar(0x2a96) },
	{ "egsdot;", QChar(0x2a98) },
	{ "el;", QChar(0x2a99) },
	{ "elinters;", QChar(0x23e7) },
	{ "ell;", QChar(0x2113) },
	{ "els;", QChar(0x2a95) },
	{ "elsdot;", QChar(0x2a97) },
	{ "emacr;", QChar(0x0113) },
	{ "empty;", QChar(0x2205) },
	{ "emptyset;", QChar(0x2205) },
	{ "emptyv;", QChar(0x2205) },
	{ "emsp13;", QChar(0x2004) },
	{ "emsp14;", QChar(0x2005) },
	{ "emsp;", QChar(0x2003) },
	{ "eng;", QChar(0x014b) },
	{ "ensp;", QChar(0x2002) },
	{ "eogon;", QChar(0x0119) },
	{ "eopf;", QChar(0x0001d556) },
	{ "epar;", QChar(0x22d5) },
	{ "eparsl;", QChar(0x29e3) },
	{ "eplus;", QChar(0x2a71) },
	{ "epsi;", QChar(0x03b5) },
	{ "epsilon;", QChar(0x03b5) },
	{ "epsiv;", QChar(0x03f5) },
	{ "eqcirc;", QChar(0x2256) },
	{ "eqcolon;", QChar(0x2255) },
	{ "eqsim;", QChar(0x2242) },
	{ "eqslantgtr;", QChar(0x2a96) },
	{ "eqslantless;", QChar(0x2a95) },
	{ "equals;", "=" },
	{ "equest;", QChar(0x225f) },
	{ "equiv;", QChar(0x2261) },
	{ "equivDD;", QChar(0x2a78) },
	{ "eqvparsl;", QChar(0x29e5) },
	{ "erDot;", QChar(0x2253) },
	{ "erarr;", QChar(0x2971) },
	{ "escr;", QChar(0x212f) },
	{ "esdot;", QChar(0x2250) },
	{ "esim;", QChar(0x2242) },
	{ "eta;", QChar(0x03b7) },
	{ "eth", QChar(0xf0) },
	{ "eth;", QChar(0xf0) },
	{ "euml", QChar(0xeb) },
	{ "euml;", QChar(0xeb) },
	{ "euro;", QChar(0x20ac) },
	{ "excl;", "!" },
	{ "exist;", QChar(0x2203) },
	{ "expectation;", QChar(0x2130) },
	{ "exponentiale;", QChar(0x2147) },
	{ "fallingdotseq;", QChar(0x2252) },
	{ "fcy;", QChar(0x0444) },
	{ "female;", QChar(0x2640) },
	{ "ffilig;", QChar(0xfb03) },
	{ "fflig;", QChar(0xfb00) },
	{ "ffllig;", QChar(0xfb04) },
	{ "ffr;", QChar(0x0001d523) },
	{ "filig;", QChar(0xfb01) },
	{ "fjlig;", "fj" },
	{ "flat;", QChar(0x266d) },
	{ "fllig;", QChar(0xfb02) },
	{ "fltns;", QChar(0x25b1) },
	{ "fnof;", QChar(0x0192) },
	{ "fopf;", QChar(0x0001d557) },
	{ "forall;", QChar(0x2200) },
	{ "fork;", QChar(0x22d4) },
	{ "forkv;", QChar(0x2ad9) },
	{ "fpartint;", QChar(0x2a0d) },
	{ "frac12", QChar(0xbd) },
	{ "frac12;", QChar(0xbd) },
	{ "frac13;", QChar(0x2153) },
	{ "frac14", QChar(0xbc) },
	{ "frac14;", QChar(0xbc) },
	{ "frac15;", QChar(0x2155) },
	{ "frac16;", QChar(0x2159) },
	{ "frac18;", QChar(0x215b) },
	{ "frac23;", QChar(0x2154) },
	{ "frac25;", QChar(0x2156) },
	{ "frac34", QChar(0xbe) },
	{ "frac34;", QChar(0xbe) },
	{ "frac35;", QChar(0x2157) },
	{ "frac38;", QChar(0x215c) },
	{ "frac45;", QChar(0x2158) },
	{ "frac56;", QChar(0x215a) },
	{ "frac58;", QChar(0x215d) },
	{ "frac78;", QChar(0x215e) },
	{ "frasl;", QChar(0x2044) },
	{ "frown;", QChar(0x2322) },
	{ "fscr;", QChar(0x0001d4bb) },
	{ "gE;", QChar(0x2267) },
	{ "gEl;", QChar(0x2a8c) },
	{ "gacute;", QChar(0x01f5) },
	{ "gamma;", QChar(0x03b3) },
	{ "gammad;", QChar(0x03dd) },
	{ "gap;", QChar(0x2a86) },
	{ "gbreve;", QChar(0x011f) },
	{ "gcirc;", QChar(0x011d) },
	{ "gcy;", QChar(0x0433) },
	{ "gdot;", QChar(0x0121) },
	{ "ge;", QChar(0x2265) },
	{ "gel;", QChar(0x22db) },
	{ "geq;", QChar(0x2265) },
	{ "geqq;", QChar(0x2267) },
	{ "geqslant;", QChar(0x2a7e) },
	{ "ges;", QChar(0x2a7e) },
	{ "gescc;", QChar(0x2aa9) },
	{ "gesdot;", QChar(0x2a80) },
	{ "gesdoto;", QChar(0x2a82) },
	{ "gesdotol;", QChar(0x2a84) },
	{ "gesl;", QChar(0x22db) % QChar(0xfe00) },
	{ "gesles;", QChar(0x2a94) },
	{ "gfr;", QChar(0x0001d524) },
	{ "gg;", QChar(0x226b) },
	{ "ggg;", QChar(0x22d9) },
	{ "gimel;", QChar(0x2137) },
	{ "gjcy;", QChar(0x0453) },
	{ "gl;", QChar(0x2277) },
	{ "glE;", QChar(0x2a92) },
	{ "gla;", QChar(0x2aa5) },
	{ "glj;", QChar(0x2aa4) },
	{ "gnE;", QChar(0x2269) },
	{ "gnap;", QChar(0x2a8a) },
	{ "gnapprox;", QChar(0x2a8a) },
	{ "gne;", QChar(0x2a88) },
	{ "gneq;", QChar(0x2a88) },
	{ "gneqq;", QChar(0x2269) },
	{ "gnsim;", QChar(0x22e7) },
	{ "gopf;", QChar(0x0001d558) },
	{ "grave;", "`" },
	{ "gscr;", QChar(0x210a) },
	{ "gsim;", QChar(0x2273) },
	{ "gsime;", QChar(0x2a8e) },
	{ "gsiml;", QChar(0x2a90) },
	{ "gt", ">" },
	{ "gt;", ">" },
	{ "gtcc;", QChar(0x2aa7) },
	{ "gtcir;", QChar(0x2a7a) },
	{ "gtdot;", QChar(0x22d7) },
	{ "gtlPar;", QChar(0x2995) },
	{ "gtquest;", QChar(0x2a7c) },
	{ "gtrapprox;", QChar(0x2a86) },
	{ "gtrarr;", QChar(0x2978) },
	{ "gtrdot;", QChar(0x22d7) },
	{ "gtreqless;", QChar(0x22db) },
	{ "gtreqqless;", QChar(0x2a8c) },
	{ "gtrless;", QChar(0x2277) },
	{ "gtrsim;", QChar(0x2273) },
	{ "gvertneqq;", QChar(0x2269) % QChar(0xfe00) },
	{ "gvnE;", QChar(0x2269) % QChar(0xfe00) },
	{ "hArr;", QChar(0x21d4) },
	{ "hairsp;", QChar(0x200a) },
	{ "half;", QChar(0xbd) },
	{ "hamilt;", QChar(0x210b) },
	{ "hardcy;", QChar(0x044a) },
	{ "harr;", QChar(0x2194) },
	{ "harrcir;", QChar(0x2948) },
	{ "harrw;", QChar(0x21ad) },
	{ "hbar;", QChar(0x210f) },
	{ "hcirc;", QChar(0x0125) },
	{ "hearts;", QChar(0x2665) },
	{ "heartsuit;", QChar(0x2665) },
	{ "hellip;", QChar(0x2026) },
	{ "hercon;", QChar(0x22b9) },
	{ "hfr;", QChar(0x0001d525) },
	{ "hksearow;", QChar(0x2925) },
	{ "hkswarow;", QChar(0x2926) },
	{ "hoarr;", QChar(0x21ff) },
	{ "homtht;", QChar(0x223b) },
	{ "hookleftarrow;", QChar(0x21a9) },
	{ "hookrightarrow;", QChar(0x21aa) },
	{ "hopf;", QChar(0x0001d559) },
	{ "horbar;", QChar(0x2015) },
	{ "hscr;", QChar(0x0001d4bd) },
	{ "hslash;", QChar(0x210f) },
	{ "hstrok;", QChar(0x0127) },
	{ "hybull;", QChar(0x2043) },
	{ "hyphen;", QChar(0x2010) },
	{ "iacute", QChar(0xed) },
	{ "iacute;", QChar(0xed) },
	{ "ic;", QChar(0x2063) },
	{ "icirc", QChar(0xee) },
	{ "icirc;", QChar(0xee) },
	{ "icy;", QChar(0x0438) },
	{ "iecy;", QChar(0x0435) },
	{ "iexcl", QChar(0xa1) },
	{ "iexcl;", QChar(0xa1) },
	{ "iff;", QChar(0x21d4) },
	{ "ifr;", QChar(0x0001d526) },
	{ "igrave", QChar(0xec) },
	{ "igrave;", QChar(0xec) },
	{ "ii;", QChar(0x2148) },
	{ "iiiint;", QChar(0x2a0c) },
	{ "iiint;", QChar(0x222d) },
	{ "iinfin;", QChar(0x29dc) },
	{ "iiota;", QChar(0x2129) },
	{ "ijlig;", QChar(0x0133) },
	{ "imacr;", QChar(0x012b) },
	{ "image;", QChar(0x2111) },
	{ "imagline;", QChar(0x2110) },
	{ "imagpart;", QChar(0x2111) },
	{ "imath;", QChar(0x0131) },
	{ "imof;", QChar(0x22b7) },
	{ "imped;", QChar(0x01b5) },
	{ "in;", QChar(0x2208) },
	{ "incare;", QChar(0x2105) },
	{ "infin;", QChar(0x221e) },
	{ "infintie;", QChar(0x29dd) },
	{ "inodot;", QChar(0x0131) },
	{ "int;", QChar(0x222b) },
	{ "intcal;", QChar(0x22ba) },
	{ "integers;", QChar(0x2124) },
	{ "intercal;", QChar(0x22ba) },
	{ "intlarhk;", QChar(0x2a17) },
	{ "intprod;", QChar(0x2a3c) },
	{ "iocy;", QChar(0x0451) },
	{ "iogon;", QChar(0x012f) },
	{ "iopf;", QChar(0x0001d55a) },
	{ "iota;", QChar(0x03b9) },
	{ "iprod;", QChar(0x2a3c) },
	{ "iquest", QChar(0xbf) },
	{ "iquest;", QChar(0xbf) },
	{ "iscr;", QChar(0x0001d4be) },
	{ "isin;", QChar(0x2208) },
	{ "isinE;", QChar(0x22f9) },
	{ "isindot;", QChar(0x22f5) },
	{ "isins;", QChar(0x22f4) },
	{ "isinsv;", QChar(0x22f3) },
	{ "isinv;", QChar(0x2208) },
	{ "it;", QChar(0x2062) },
	{ "itilde;", QChar(0x0129) },
	{ "iukcy;", QChar(0x0456) },
	{ "iuml", QChar(0xef) },
	{ "iuml;", QChar(0xef) },
	{ "jcirc;", QChar(0x0135) },
	{ "jcy;", QChar(0x0439) },
	{ "jfr;", QChar(0x0001d527) },
	{ "jmath;", QChar(0x0237) },
	{ "jopf;", QChar(0x0001d55b) },
	{ "jscr;", QChar(0x0001d4bf) },
	{ "jsercy;", QChar(0x0458) },
	{ "jukcy;", QChar(0x0454) },
	{ "kappa;", QChar(0x03ba) },
	{ "kappav;", QChar(0x03f0) },
	{ "kcedil;", QChar(0x0137) },
	{ "kcy;", QChar(0x043a) },
	{ "kfr;", QChar(0x0001d528) },
	{ "kgreen;", QChar(0x0138) },
	{ "khcy;", QChar(0x0445) },
	{ "kjcy;", QChar(0x045c) },
	{ "kopf;", QChar(0x0001d55c) },
	{ "kscr;", QChar(0x0001d4c0) },
	{ "lAarr;", QChar(0x21da) },
	{ "lArr;", QChar(0x21d0) },
	{ "lAtail;", QChar(0x291b) },
	{ "lBarr;", QChar(0x290e) },
	{ "lE;", QChar(0x2266) },
	{ "lEg;", QChar(0x2a8b) },
	{ "lHar;", QChar(0x2962) },
	{ "lacute;", QChar(0x013a) },
	{ "laemptyv;", QChar(0x29b4) },
	{ "lagran;", QChar(0x2112) },
	{ "lambda;", QChar(0x03bb) },
	{ "lang;", QChar(0x27e8) },
	{ "langd;", QChar(0x2991) },
	{ "langle;", QChar(0x27e8) },
	{ "lap;", QChar(0x2a85) },
	{ "laquo", QChar(0xab) },
	{ "laquo;", QChar(0xab) },
	{ "larr;", QChar(0x2190) },
	{ "larrb;", QChar(0x21e4) },
	{ "larrbfs;", QChar(0x291f) },
	{ "larrfs;", QChar(0x291d) },
	{ "larrhk;", QChar(0x21a9) },
	{ "larrlp;", QChar(0x21ab) },
	{ "larrpl;", QChar(0x2939) },
	{ "larrsim;", QChar(0x2973) },
	{ "larrtl;", QChar(0x21a2) },
	{ "lat;", QChar(0x2aab) },
	{ "latail;", QChar(0x2919) },
	{ "late;", QChar(0x2aad) },
	{ "lates;", QChar(0x2aad) % QChar(0xfe00) },
	{ "lbarr;", QChar(0x290c) },
	{ "lbbrk;", QChar(0x2772) },
	{ "lbrace;", "{" },
	{ "lbrack;", "[" },
	{ "lbrke;", QChar(0x298b) },
	{ "lbrksld;", QChar(0x298f) },
	{ "lbrkslu;", QChar(0x298d) },
	{ "lcaron;", QChar(0x013e) },
	{ "lcedil;", QChar(0x013c) },
	{ "lceil;", QChar(0x2308) },
	{ "lcub;", "{" },
	{ "lcy;", QChar(0x043b) },
	{ "ldca;", QChar(0x2936) },
	{ "ldquo;", QChar(0x201c) },
	{ "ldquor;", QChar(0x201e) },
	{ "ldrdhar;", QChar(0x2967) },
	{ "ldrushar;", QChar(0x294b) },
	{ "ldsh;", QChar(0x21b2) },
	{ "le;", QChar(0x2264) },
	{ "leftarrow;", QChar(0x2190) },
	{ "leftarrowtail;", QChar(0x21a2) },
	{ "leftharpoondown;", QChar(0x21bd) },
	{ "leftharpoonup;", QChar(0x21bc) },
	{ "leftleftarrows;", QChar(0x21c7) },
	{ "leftrightarrow;", QChar(0x2194) },
	{ "leftrightarrows;", QChar(0x21c6) },
	{ "leftrightharpoons;", QChar(0x21cb) },
	{ "leftrightsquigarrow;", QChar(0x21ad) },
	{ "leftthreetimes;", QChar(0x22cb) },
	{ "leg;", QChar(0x22da) },
	{ "leq;", QChar(0x2264) },
	{ "leqq;", QChar(0x2266) },
	{ "leqslant;", QChar(0x2a7d) },
	{ "les;", QChar(0x2a7d) },
	{ "lescc;", QChar(0x2aa8) },
	{ "lesdot;", QChar(0x2a7f) },
	{ "lesdoto;", QChar(0x2a81) },
	{ "lesdotor;", QChar(0x2a83) },
	{ "lesg;", QChar(0x22da) % QChar(0xfe00) },
	{ "lesges;", QChar(0x2a93) },
	{ "lessapprox;", QChar(0x2a85) },
	{ "lessdot;", QChar(0x22d6) },
	{ "lesseqgtr;", QChar(0x22da) },
	{ "lesseqqgtr;", QChar(0x2a8b) },
	{ "lessgtr;", QChar(0x2276) },
	{ "lesssim;", QChar(0x2272) },
	{ "lfisht;", QChar(0x297c) },
	{ "lfloor;", QChar(0x230a) },
	{ "lfr;", QChar(0x0001d529) },
	{ "lg;", QChar(0x2276) },
	{ "lgE;", QChar(0x2a91) },
	{ "lhard;", QChar(0x21bd) },
	{ "lharu;", QChar(0x21bc) },
	{ "lharul;", QChar(0x296a) },
	{ "lhblk;", QChar(0x2584) },
	{ "ljcy;", QChar(0x0459) },
	{ "ll;", QChar(0x226a) },
	{ "llarr;", QChar(0x21c7) },
	{ "llcorner;", QChar(0x231e) },
	{ "llhard;", QChar(0x296b) },
	{ "lltri;", QChar(0x25fa) },
	{ "lmidot;", QChar(0x0140) },
	{ "lmoust;", QChar(0x23b0) },
	{ "lmoustache;", QChar(0x23b0) },
	{ "lnE;", QChar(0x2268) },
	{ "lnap;", QChar(0x2a89) },
	{ "lnapprox;", QChar(0x2a89) },
	{ "lne;", QChar(0x2a87) },
	{ "lneq;", QChar(0x2a87) },
	{ "lneqq;", QChar(0x2268) },
	{ "lnsim;", QChar(0x22e6) },
	{ "loang;", QChar(0x27ec) },
	{ "loarr;", QChar(0x21fd) },
	{ "lobrk;", QChar(0x27e6) },
	{ "longleftarrow;", QChar(0x27f5) },
	{ "longleftrightarrow;", QChar(0x27f7) },
	{ "longmapsto;", QChar(0x27fc) },
	{ "longrightarrow;", QChar(0x27f6) },
	{ "looparrowleft;", QChar(0x21ab) },
	{ "looparrowright;", QChar(0x21ac) },
	{ "lopar;", QChar(0x2985) },
	{ "lopf;", QChar(0x0001d55d) },
	{ "loplus;", QChar(0x2a2d) },
	{ "lotimes;", QChar(0x2a34) },
	{ "lowast;", QChar(0x2217) },
	{ "lowbar;", "_" },
	{ "loz;", QChar(0x25ca) },
	{ "lozenge;", QChar(0x25ca) },
	{ "lozf;", QChar(0x29eb) },
	{ "lpar;", "(" },
	{ "lparlt;", QChar(0x2993) },
	{ "lrarr;", QChar(0x21c6) },
	{ "lrcorner;", QChar(0x231f) },
	{ "lrhar;", QChar(0x21cb) },
	{ "lrhard;", QChar(0x296d) },
	{ "lrm;", QChar(0x200e) },
	{ "lrtri;", QChar(0x22bf) },
	{ "lsaquo;", QChar(0x2039) },
	{ "lscr;", QChar(0x0001d4c1) },
	{ "lsh;", QChar(0x21b0) },
	{ "lsim;", QChar(0x2272) },
	{ "lsime;", QChar(0x2a8d) },
	{ "lsimg;", QChar(0x2a8f) },
	{ "lsqb;", "[" },
	{ "lsquo;", QChar(0x2018) },
	{ "lsquor;", QChar(0x201a) },
	{ "lstrok;", QChar(0x0142) },
	{ "lt", "<" },
	{ "lt;", "<" },
	{ "ltcc;", QChar(0x2aa6) },
	{ "ltcir;", QChar(0x2a79) },
	{ "ltdot;", QChar(0x22d6) },
	{ "lthree;", QChar(0x22cb) },
	{ "ltimes;", QChar(0x22c9) },
	{ "ltlarr;", QChar(0x2976) },
	{ "ltquest;", QChar(0x2a7b) },
	{ "ltrPar;", QChar(0x2996) },
	{ "ltri;", QChar(0x25c3) },
	{ "ltrie;", QChar(0x22b4) },
	{ "ltrif;", QChar(0x25c2) },
	{ "lurdshar;", QChar(0x294a) },
	{ "luruhar;", QChar(0x2966) },
	{ "lvertneqq;", QChar(0x2268) % QChar(0xfe00) },
	{ "lvnE;", QChar(0x2268) % QChar(0xfe00) },
	{ "mDDot;", QChar(0x223a) },
	{ "macr", QChar(0xaf) },
	{ "macr;", QChar(0xaf) },
	{ "male;", QChar(0x2642) },
	{ "malt;", QChar(0x2720) },
	{ "maltese;", QChar(0x2720) },
	{ "map;", QChar(0x21a6) },
	{ "mapsto;", QChar(0x21a6) },
	{ "mapstodown;", QChar(0x21a7) },
	{ "mapstoleft;", QChar(0x21a4) },
	{ "mapstoup;", QChar(0x21a5) },
	{ "marker;", QChar(0x25ae) },
	{ "mcomma;", QChar(0x2a29) },
	{ "mcy;", QChar(0x043c) },
	{ "mdash;", QChar(0x2014) },
	{ "measuredangle;", QChar(0x2221) },
	{ "mfr;", QChar(0x0001d52a) },
	{ "mho;", QChar(0x2127) },
	{ "micro", QChar(0xb5) },
	{ "micro;", QChar(0xb5) },
	{ "mid;", QChar(0x2223) },
	{ "midast;", "*" },
	{ "midcir;", QChar(0x2af0) },
	{ "middot", QChar(0xb7) },
	{ "middot;", QChar(0xb7) },
	{ "minus;", QChar(0x2212) },
	{ "minusb;", QChar(0x229f) },
	{ "minusd;", QChar(0x2238) },
	{ "minusdu;", QChar(0x2a2a) },
	{ "mlcp;", QChar(0x2adb) },
	{ "mldr;", QChar(0x2026) },
	{ "mnplus;", QChar(0x2213) },
	{ "models;", QChar(0x22a7) },
	{ "mopf;", QChar(0x0001d55e) },
	{ "mp;", QChar(0x2213) },
	{ "mscr;", QChar(0x0001d4c2) },
	{ "mstpos;", QChar(0x223e) },
	{ "mu;", QChar(0x03bc) },
	{ "multimap;", QChar(0x22b8) },
	{ "mumap;", QChar(0x22b8) },
	{ "nGg;", QChar(0x22d9) % QChar(0x0338) },
	{ "nGt;", QChar(0x226b) % QChar(0x20d2) },
	{ "nGtv;", QChar(0x226b) % QChar(0x0338) },
	{ "nLeftarrow;", QChar(0x21cd) },
	{ "nLeftrightarrow;", QChar(0x21ce) },
	{ "nLl;", QChar(0x22d8) % QChar(0x0338) },
	{ "nLt;", QChar(0x226a) % QChar(0x20d2) },
	{ "nLtv;", QChar(0x226a) % QChar(0x0338) },
	{ "nRightarrow;", QChar(0x21cf) },
	{ "nVDash;", QChar(0x22af) },
	{ "nVdash;", QChar(0x22ae) },
	{ "nabla;", QChar(0x2207) },
	{ "nacute;", QChar(0x0144) },
	{ "nang;", QChar(0x2220) % QChar(0x20d2) },
	{ "nap;", QChar(0x2249) },
	{ "napE;", QChar(0x2a70) % QChar(0x0338) },
	{ "napid;", QChar(0x224b) % QChar(0x0338) },
	{ "napos;", QChar(0x0149) },
	{ "napprox;", QChar(0x2249) },
	{ "natur;", QChar(0x266e) },
	{ "natural;", QChar(0x266e) },
	{ "naturals;", QChar(0x2115) },
	{ "nbsp", QChar(0xa0) },
	{ "nbsp;", QChar(0xa0) },
	{ "nbump;", QChar(0x224e) % QChar(0x0338) },
	{ "nbumpe;", QChar(0x224f) % QChar(0x0338) },
	{ "ncap;", QChar(0x2a43) },
	{ "ncaron;", QChar(0x0148) },
	{ "ncedil;", QChar(0x0146) },
	{ "ncong;", QChar(0x2247) },
	{ "ncongdot;", QChar(0x2a6d) % QChar(0x0338) },
	{ "ncup;", QChar(0x2a42) },
	{ "ncy;", QChar(0x043d) },
	{ "ndash;", QChar(0x2013) },
	{ "ne;", QChar(0x2260) },
	{ "neArr;", QChar(0x21d7) },
	{ "nearhk;", QChar(0x2924) },
	{ "nearr;", QChar(0x2197) },
	{ "nearrow;", QChar(0x2197) },
	{ "nedot;", QChar(0x2250) % QChar(0x0338) },
	{ "nequiv;", QChar(0x2262) },
	{ "nesear;", QChar(0x2928) },
	{ "nesim;", QChar(0x2242) % QChar(0x0338) },
	{ "nexist;", QChar(0x2204) },
	{ "nexists;", QChar(0x2204) },
	{ "nfr;", QChar(0x0001d52b) },
	{ "ngE;", QChar(0x2267) % QChar(0x0338) },
	{ "nge;", QChar(0x2271) },
	{ "ngeq;", QChar(0x2271) },
	{ "ngeqq;", QChar(0x2267) % QChar(0x0338) },
	{ "ngeqslant;", QChar(0x2a7e) % QChar(0x0338) },
	{ "nges;", QChar(0x2a7e) % QChar(0x0338) },
	{ "ngsim;", QChar(0x2275) },
	{ "ngt;", QChar(0x226f) },
	{ "ngtr;", QChar(0x226f) },
	{ "nhArr;", QChar(0x21ce) },
	{ "nharr;", QChar(0x21ae) },
	{ "nhpar;", QChar(0x2af2) },
	{ "ni;", QChar(0x220b) },
	{ "nis;", QChar(0x22fc) },
	{ "nisd;", QChar(0x22fa) },
	{ "niv;", QChar(0x220b) },
	{ "njcy;", QChar(0x045a) },
	{ "nlArr;", QChar(0x21cd) },
	{ "nlE;", QChar(0x2266) % QChar(0x0338) },
	{ "nlarr;", QChar(0x219a) },
	{ "nldr;", QChar(0x2025) },
	{ "nle;", QChar(0x2270) },
	{ "nleftarrow;", QChar(0x219a) },
	{ "nleftrightarrow;", QChar(0x21ae) },
	{ "nleq;", QChar(0x2270) },
	{ "nleqq;", QChar(0x2266) % QChar(0x0338) },
	{ "nleqslant;", QChar(0x2a7d) % QChar(0x0338) },
	{ "nles;", QChar(0x2a7d) % QChar(0x0338) },
	{ "nless;", QChar(0x226e) },
	{ "nlsim;", QChar(0x2274) },
	{ "nlt;", QChar(0x226e) },
	{ "nltri;", QChar(0x22ea) },
	{ "nltrie;", QChar(0x22ec) },
	{ "nmid;", QChar(0x2224) },
	{ "nopf;", QChar(0x0001d55f) },
	{ "not", QChar(0xac) },
	{ "not;", QChar(0xac) },
	{ "notin;", QChar(0x2209) },
	{ "notinE;", QChar(0x22f9) % QChar(0x0338) },
	{ "notindot;", QChar(0x22f5) % QChar(0x0338) },
	{ "notinva;", QChar(0x2209) },
	{ "notinvb;", QChar(0x22f7) },
	{ "notinvc;", QChar(0x22f6) },
	{ "notni;", QChar(0x220c) },
	{ "notniva;", QChar(0x220c) },
	{ "notnivb;", QChar(0x22fe) },
	{ "notnivc;", QChar(0x22fd) },
	{ "npar;", QChar(0x2226) },
	{ "nparallel;", QChar(0x2226) },
	{ "nparsl;", QChar(0x2afd) % QChar(0x20e5) },
	{ "npart;", QChar(0x2202) % QChar(0x0338) },
	{ "npolint;", QChar(0x2a14) },
	{ "npr;", QChar(0x2280) },
	{ "nprcue;", QChar(0x22e0) },
	{ "npre;", QChar(0x2aaf) % QChar(0x0338) },
	{ "nprec;", QChar(0x2280) },
	{ "npreceq;", QChar(0x2aaf) % QChar(0x0338) },
	{ "nrArr;", QChar(0x21cf) },
	{ "nrarr;", QChar(0x219b) },
	{ "nrarrc;", QChar(0x2933) % QChar(0x0338) },
	{ "nrarrw;", QChar(0x219d) % QChar(0x0338) },
	{ "nrightarrow;", QChar(0x219b) },
	{ "nrtri;", QChar(0x22eb) },
	{ "nrtrie;", QChar(0x22ed) },
	{ "nsc;", QChar(0x2281) },
	{ "nsccue;", QChar(0x22e1) },
	{ "nsce;", QChar(0x2ab0) % QChar(0x0338) },
	{ "nscr;", QChar(0x0001d4c3) },
	{ "nshortmid;", QChar(0x2224) },
	{ "nshortparallel;", QChar(0x2226) },
	{ "nsim;", QChar(0x2241) },
	{ "nsime;", QChar(0x2244) },
	{ "nsimeq;", QChar(0x2244) },
	{ "nsmid;", QChar(0x2224) },
	{ "nspar;", QChar(0x2226) },
	{ "nsqsube;", QChar(0x22e2) },
	{ "nsqsupe;", QChar(0x22e3) },
	{ "nsub;", QChar(0x2284) },
	{ "nsubE;", QChar(0x2ac5) % QChar(0x0338) },
	{ "nsube;", QChar(0x2288) },
	{ "nsubset;", QChar(0x2282) % QChar(0x20d2) },
	{ "nsubseteq;", QChar(0x2288) },
	{ "nsubseteqq;", QChar(0x2ac5) % QChar(0x0338) },
	{ "nsucc;", QChar(0x2281) },
	{ "nsucceq;", QChar(0x2ab0) % QChar(0x0338) },
	{ "nsup;", QChar(0x2285) },
	{ "nsupE;", QChar(0x2ac6) % QChar(0x0338) },
	{ "nsupe;", QChar(0x2289) },
	{ "nsupset;", QChar(0x2283) % QChar(0x20d2) },
	{ "nsupseteq;", QChar(0x2289) },
	{ "nsupseteqq;", QChar(0x2ac6) % QChar(0x0338) },
	{ "ntgl;", QChar(0x2279) },
	{ "ntilde", QChar(0xf1) },
	{ "ntilde;", QChar(0xf1) },
	{ "ntlg;", QChar(0x2278) },
	{ "ntriangleleft;", QChar(0x22ea) },
	{ "ntrianglelefteq;", QChar(0x22ec) },
	{ "ntriangleright;", QChar(0x22eb) },
	{ "ntrianglerighteq;", QChar(0x22ed) },
	{ "nu;", QChar(0x03bd) },
	{ "num;", "#" },
	{ "numero;", QChar(0x2116) },
	{ "numsp;", QChar(0x2007) },
	{ "nvDash;", QChar(0x22ad) },
	{ "nvHarr;", QChar(0x2904) },
	{ "nvap;", QChar(0x224d) % QChar(0x20d2) },
	{ "nvdash;", QChar(0x22ac) },
	{ "nvge;", QChar(0x2265) % QChar(0x20d2) },
	{ "nvgt;", ">QChar(0x20d2)" },
	{ "nvinfin;", QChar(0x29de) },
	{ "nvlArr;", QChar(0x2902) },
	{ "nvle;", QChar(0x2264) % QChar(0x20d2) },
	{ "nvlt;", "<QChar(0x20d2)" },
	{ "nvltrie;", QChar(0x22b4) % QChar(0x20d2) },
	{ "nvrArr;", QChar(0x2903) },
	{ "nvrtrie;", QChar(0x22b5) % QChar(0x20d2) },
	{ "nvsim;", QChar(0x223c) % QChar(0x20d2) },
	{ "nwArr;", QChar(0x21d6) },
	{ "nwarhk;", QChar(0x2923) },
	{ "nwarr;", QChar(0x2196) },
	{ "nwarrow;", QChar(0x2196) },
	{ "nwnear;", QChar(0x2927) },
	{ "oS;", QChar(0x24c8) },
	{ "oacute", QChar(0xf3) },
	{ "oacute;", QChar(0xf3) },
	{ "oast;", QChar(0x229b) },
	{ "ocir;", QChar(0x229a) },
	{ "ocirc", QChar(0xf4) },
	{ "ocirc;", QChar(0xf4) },
	{ "ocy;", QChar(0x043e) },
	{ "odash;", QChar(0x229d) },
	{ "odblac;", QChar(0x0151) },
	{ "odiv;", QChar(0x2a38) },
	{ "odot;", QChar(0x2299) },
	{ "odsold;", QChar(0x29bc) },
	{ "oelig;", QChar(0x0153) },
	{ "ofcir;", QChar(0x29bf) },
	{ "ofr;", QChar(0x0001d52c) },
	{ "ogon;", QChar(0x02db) },
	{ "ograve", QChar(0xf2) },
	{ "ograve;", QChar(0xf2) },
	{ "ogt;", QChar(0x29c1) },
	{ "ohbar;", QChar(0x29b5) },
	{ "ohm;", QChar(0x03a9) },
	{ "oint;", QChar(0x222e) },
	{ "olarr;", QChar(0x21ba) },
	{ "olcir;", QChar(0x29be) },
	{ "olcross;", QChar(0x29bb) },
	{ "oline;", QChar(0x203e) },
	{ "olt;", QChar(0x29c0) },
	{ "omacr;", QChar(0x014d) },
	{ "omega;", QChar(0x03c9) },
	{ "omicron;", QChar(0x03bf) },
	{ "omid;", QChar(0x29b6) },
	{ "ominus;", QChar(0x2296) },
	{ "oopf;", QChar(0x0001d560) },
	{ "opar;", QChar(0x29b7) },
	{ "operp;", QChar(0x29b9) },
	{ "oplus;", QChar(0x2295) },
	{ "or;", QChar(0x2228) },
	{ "orarr;", QChar(0x21bb) },
	{ "ord;", QChar(0x2a5d) },
	{ "order;", QChar(0x2134) },
	{ "orderof;", QChar(0x2134) },
	{ "ordf", QChar(0xaa) },
	{ "ordf;", QChar(0xaa) },
	{ "ordm", QChar(0xba) },
	{ "ordm;", QChar(0xba) },
	{ "origof;", QChar(0x22b6) },
	{ "oror;", QChar(0x2a56) },
	{ "orslope;", QChar(0x2a57) },
	{ "orv;", QChar(0x2a5b) },
	{ "oscr;", QChar(0x2134) },
	{ "oslash", QChar(0xf8) },
	{ "oslash;", QChar(0xf8) },
	{ "osol;", QChar(0x2298) },
	{ "otilde", QChar(0xf5) },
	{ "otilde;", QChar(0xf5) },
	{ "otimes;", QChar(0x2297) },
	{ "otimesas;", QChar(0x2a36) },
	{ "ouml", QChar(0xf6) },
	{ "ouml;", QChar(0xf6) },
	{ "ovbar;", QChar(0x233d) },
	{ "par;", QChar(0x2225) },
	{ "para", QChar(0xb6) },
	{ "para;", QChar(0xb6) },
	{ "parallel;", QChar(0x2225) },
	{ "parsim;", QChar(0x2af3) },
	{ "parsl;", QChar(0x2afd) },
	{ "part;", QChar(0x2202) },
	{ "pcy;", QChar(0x043f) },
	{ "percnt;", "%" },
	{ "period;", "." },
	{ "permil;", QChar(0x2030) },
	{ "perp;", QChar(0x22a5) },
	{ "pertenk;", QChar(0x2031) },
	{ "pfr;", QChar(0x0001d52d) },
	{ "phi;", QChar(0x03c6) },
	{ "phiv;", QChar(0x03d5) },
	{ "phmmat;", QChar(0x2133) },
	{ "phone;", QChar(0x260e) },
	{ "pi;", QChar(0x03c0) },
	{ "pitchfork;", QChar(0x22d4) },
	{ "piv;", QChar(0x03d6) },
	{ "planck;", QChar(0x210f) },
	{ "planckh;", QChar(0x210e) },
	{ "plankv;", QChar(0x210f) },
	{ "plus;", "+" },
	{ "plusacir;", QChar(0x2a23) },
	{ "plusb;", QChar(0x229e) },
	{ "pluscir;", QChar(0x2a22) },
	{ "plusdo;", QChar(0x2214) },
	{ "plusdu;", QChar(0x2a25) },
	{ "pluse;", QChar(0x2a72) },
	{ "plusmn", QChar(0xb1) },
	{ "plusmn;", QChar(0xb1) },
	{ "plussim;", QChar(0x2a26) },
	{ "plustwo;", QChar(0x2a27) },
	{ "pm;", QChar(0xb1) },
	{ "pointint;", QChar(0x2a15) },
	{ "popf;", QChar(0x0001d561) },
	{ "pound", QChar(0xa3) },
	{ "pound;", QChar(0xa3) },
	{ "pr;", QChar(0x227a) },
	{ "prE;", QChar(0x2ab3) },
	{ "prap;", QChar(0x2ab7) },
	{ "prcue;", QChar(0x227c) },
	{ "pre;", QChar(0x2aaf) },
	{ "prec;", QChar(0x227a) },
	{ "precapprox;", QChar(0x2ab7) },
	{ "preccurlyeq;", QChar(0x227c) },
	{ "preceq;", QChar(0x2aaf) },
	{ "precnapprox;", QChar(0x2ab9) },
	{ "precneqq;", QChar(0x2ab5) },
	{ "precnsim;", QChar(0x22e8) },
	{ "precsim;", QChar(0x227e) },
	{ "prime;", QChar(0x2032) },
	{ "primes;", QChar(0x2119) },
	{ "prnE;", QChar(0x2ab5) },
	{ "prnap;", QChar(0x2ab9) },
	{ "prnsim;", QChar(0x22e8) },
	{ "prod;", QChar(0x220f) },
	{ "profalar;", QChar(0x232e) },
	{ "profline;", QChar(0x2312) },
	{ "profsurf;", QChar(0x2313) },
	{ "prop;", QChar(0x221d) },
	{ "propto;", QChar(0x221d) },
	{ "prsim;", QChar(0x227e) },
	{ "prurel;", QChar(0x22b0) },
	{ "pscr;", QChar(0x0001d4c5) },
	{ "psi;", QChar(0x03c8) },
	{ "puncsp;", QChar(0x2008) },
	{ "qfr;", QChar(0x0001d52e) },
	{ "qint;", QChar(0x2a0c) },
	{ "qopf;", QChar(0x0001d562) },
	{ "qprime;", QChar(0x2057) },
	{ "qscr;", QChar(0x0001d4c6) },
	{ "quaternions;", QChar(0x210d) },
	{ "quatint;", QChar(0x2a16) },
	{ "quest;", "?" },
	{ "questeq;", QChar(0x225f) },
	{ "quot", "\"" },
	{ "quot;", "\"" },
	{ "rAarr;", QChar(0x21db) },
	{ "rArr;", QChar(0x21d2) },
	{ "rAtail;", QChar(0x291c) },
	{ "rBarr;", QChar(0x290f) },
	{ "rHar;", QChar(0x2964) },
	{ "race;", QChar(0x223d) % QChar(0x0331) },
	{ "racute;", QChar(0x0155) },
	{ "radic;", QChar(0x221a) },
	{ "raemptyv;", QChar(0x29b3) },
	{ "rang;", QChar(0x27e9) },
	{ "rangd;", QChar(0x2992) },
	{ "range;", QChar(0x29a5) },
	{ "rangle;", QChar(0x27e9) },
	{ "raquo", QChar(0xbb) },
	{ "raquo;", QChar(0xbb) },
	{ "rarr;", QChar(0x2192) },
	{ "rarrap;", QChar(0x2975) },
	{ "rarrb;", QChar(0x21e5) },
	{ "rarrbfs;", QChar(0x2920) },
	{ "rarrc;", QChar(0x2933) },
	{ "rarrfs;", QChar(0x291e) },
	{ "rarrhk;", QChar(0x21aa) },
	{ "rarrlp;", QChar(0x21ac) },
	{ "rarrpl;", QChar(0x2945) },
	{ "rarrsim;", QChar(0x2974) },
	{ "rarrtl;", QChar(0x21a3) },
	{ "rarrw;", QChar(0x219d) },
	{ "ratail;", QChar(0x291a) },
	{ "ratio;", QChar(0x2236) },
	{ "rationals;", QChar(0x211a) },
	{ "rbarr;", QChar(0x290d) },
	{ "rbbrk;", QChar(0x2773) },
	{ "rbrace;", "}" },
	{ "rbrack;", "]" },
	{ "rbrke;", QChar(0x298c) },
	{ "rbrksld;", QChar(0x298e) },
	{ "rbrkslu;", QChar(0x2990) },
	{ "rcaron;", QChar(0x0159) },
	{ "rcedil;", QChar(0x0157) },
	{ "rceil;", QChar(0x2309) },
	{ "rcub;", "}" },
	{ "rcy;", QChar(0x0440) },
	{ "rdca;", QChar(0x2937) },
	{ "rdldhar;", QChar(0x2969) },
	{ "rdquo;", QChar(0x201d) },
	{ "rdquor;", QChar(0x201d) },
	{ "rdsh;", QChar(0x21b3) },
	{ "real;", QChar(0x211c) },
	{ "realine;", QChar(0x211b) },
	{ "realpart;", QChar(0x211c) },
	{ "reals;", QChar(0x211d) },
	{ "rect;", QChar(0x25ad) },
	{ "reg", QChar(0xae) },
	{ "reg;", QChar(0xae) },
	{ "rfisht;", QChar(0x297d) },
	{ "rfloor;", QChar(0x230b) },
	{ "rfr;", QChar(0x0001d52f) },
	{ "rhard;", QChar(0x21c1) },
	{ "rharu;", QChar(0x21c0) },
	{ "rharul;", QChar(0x296c) },
	{ "rho;", QChar(0x03c1) },
	{ "rhov;", QChar(0x03f1) },
	{ "rightarrow;", QChar(0x2192) },
	{ "rightarrowtail;", QChar(0x21a3) },
	{ "rightharpoondown;", QChar(0x21c1) },
	{ "rightharpoonup;", QChar(0x21c0) },
	{ "rightleftarrows;", QChar(0x21c4) },
	{ "rightleftharpoons;", QChar(0x21cc) },
	{ "rightrightarrows;", QChar(0x21c9) },
	{ "rightsquigarrow;", QChar(0x219d) },
	{ "rightthreetimes;", QChar(0x22cc) },
	{ "ring;", QChar(0x02da) },
	{ "risingdotseq;", QChar(0x2253) },
	{ "rlarr;", QChar(0x21c4) },
	{ "rlhar;", QChar(0x21cc) },
	{ "rlm;", QChar(0x200f) },
	{ "rmoust;", QChar(0x23b1) },
	{ "rmoustache;", QChar(0x23b1) },
	{ "rnmid;", QChar(0x2aee) },
	{ "roang;", QChar(0x27ed) },
	{ "roarr;", QChar(0x21fe) },
	{ "robrk;", QChar(0x27e7) },
	{ "ropar;", QChar(0x2986) },
	{ "ropf;", QChar(0x0001d563) },
	{ "roplus;", QChar(0x2a2e) },
	{ "rotimes;", QChar(0x2a35) },
	{ "rpar;", ")" },
	{ "rpargt;", QChar(0x2994) },
	{ "rppolint;", QChar(0x2a12) },
	{ "rrarr;", QChar(0x21c9) },
	{ "rsaquo;", QChar(0x203a) },
	{ "rscr;", QChar(0x0001d4c7) },
	{ "rsh;", QChar(0x21b1) },
	{ "rsqb;", "]" },
	{ "rsquo;", QChar(0x2019) },
	{ "rsquor;", QChar(0x2019) },
	{ "rthree;", QChar(0x22cc) },
	{ "rtimes;", QChar(0x22ca) },
	{ "rtri;", QChar(0x25b9) },
	{ "rtrie;", QChar(0x22b5) },
	{ "rtrif;", QChar(0x25b8) },
	{ "rtriltri;", QChar(0x29ce) },
	{ "ruluhar;", QChar(0x2968) },
	{ "rx;", QChar(0x211e) },
	{ "sacute;", QChar(0x015b) },
	{ "sbquo;", QChar(0x201a) },
	{ "sc;", QChar(0x227b) },
	{ "scE;", QChar(0x2ab4) },
	{ "scap;", QChar(0x2ab8) },
	{ "scaron;", QChar(0x0161) },
	{ "sccue;", QChar(0x227d) },
	{ "sce;", QChar(0x2ab0) },
	{ "scedil;", QChar(0x015f) },
	{ "scirc;", QChar(0x015d) },
	{ "scnE;", QChar(0x2ab6) },
	{ "scnap;", QChar(0x2aba) },
	{ "scnsim;", QChar(0x22e9) },
	{ "scpolint;", QChar(0x2a13) },
	{ "scsim;", QChar(0x227f) },
	{ "scy;", QChar(0x0441) },
	{ "sdot;", QChar(0x22c5) },
	{ "sdotb;", QChar(0x22a1) },
	{ "sdote;", QChar(0x2a66) },
	{ "seArr;", QChar(0x21d8) },
	{ "searhk;", QChar(0x2925) },
	{ "searr;", QChar(0x2198) },
	{ "searrow;", QChar(0x2198) },
	{ "sect", QChar(0xa7) },
	{ "sect;", QChar(0xa7) },
	{ "semi;", ";" },
	{ "seswar;", QChar(0x2929) },
	{ "setminus;", QChar(0x2216) },
	{ "setmn;", QChar(0x2216) },
	{ "sext;", QChar(0x2736) },
	{ "sfr;", QChar(0x0001d530) },
	{ "sfrown;", QChar(0x2322) },
	{ "sharp;", QChar(0x266f) },
	{ "shchcy;", QChar(0x0449) },
	{ "shcy;", QChar(0x0448) },
	{ "shortmid;", QChar(0x2223) },
	{ "shortparallel;", QChar(0x2225) },
	{ "shy", QChar(0xad) },
	{ "shy;", QChar(0xad) },
	{ "sigma;", QChar(0x03c3) },
	{ "sigmaf;", QChar(0x03c2) },
	{ "sigmav;", QChar(0x03c2) },
	{ "sim;", QChar(0x223c) },
	{ "simdot;", QChar(0x2a6a) },
	{ "sime;", QChar(0x2243) },
	{ "simeq;", QChar(0x2243) },
	{ "simg;", QChar(0x2a9e) },
	{ "simgE;", QChar(0x2aa0) },
	{ "siml;", QChar(0x2a9d) },
	{ "simlE;", QChar(0x2a9f) },
	{ "simne;", QChar(0x2246) },
	{ "simplus;", QChar(0x2a24) },
	{ "simrarr;", QChar(0x2972) },
	{ "slarr;", QChar(0x2190) },
	{ "smallsetminus;", QChar(0x2216) },
	{ "smashp;", QChar(0x2a33) },
	{ "smeparsl;", QChar(0x29e4) },
	{ "smid;", QChar(0x2223) },
	{ "smile;", QChar(0x2323) },
	{ "smt;", QChar(0x2aaa) },
	{ "smte;", QChar(0x2aac) },
	{ "smtes;", QChar(0x2aac) % QChar(0xfe00) },
	{ "softcy;", QChar(0x044c) },
	{ "sol;", "/" },
	{ "solb;", QChar(0x29c4) },
	{ "solbar;", QChar(0x233f) },
	{ "sopf;", QChar(0x0001d564) },
	{ "spades;", QChar(0x2660) },
	{ "spadesuit;", QChar(0x2660) },
	{ "spar;", QChar(0x2225) },
	{ "sqcap;", QChar(0x2293) },
	{ "sqcaps;", QChar(0x2293) % QChar(0xfe00) },
	{ "sqcup;", QChar(0x2294) },
	{ "sqcups;", QChar(0x2294) % QChar(0xfe00) },
	{ "sqsub;", QChar(0x228f) },
	{ "sqsube;", QChar(0x2291) },
	{ "sqsubset;", QChar(0x228f) },
	{ "sqsubseteq;", QChar(0x2291) },
	{ "sqsup;", QChar(0x2290) },
	{ "sqsupe;", QChar(0x2292) },
	{ "sqsupset;", QChar(0x2290) },
	{ "sqsupseteq;", QChar(0x2292) },
	{ "squ;", QChar(0x25a1) },
	{ "square;", QChar(0x25a1) },
	{ "squarf;", QChar(0x25aa) },
	{ "squf;", QChar(0x25aa) },
	{ "srarr;", QChar(0x2192) },
	{ "sscr;", QChar(0x0001d4c8) },
	{ "ssetmn;", QChar(0x2216) },
	{ "ssmile;", QChar(0x2323) },
	{ "sstarf;", QChar(0x22c6) },
	{ "star;", QChar(0x2606) },
	{ "starf;", QChar(0x2605) },
	{ "straightepsilon;", QChar(0x03f5) },
	{ "straightphi;", QChar(0x03d5) },
	{ "strns;", QChar(0xaf) },
	{ "sub;", QChar(0x2282) },
	{ "subE;", QChar(0x2ac5) },
	{ "subdot;", QChar(0x2abd) },
	{ "sube;", QChar(0x2286) },
	{ "subedot;", QChar(0x2ac3) },
	{ "submult;", QChar(0x2ac1) },
	{ "subnE;", QChar(0x2acb) },
	{ "subne;", QChar(0x228a) },
	{ "subplus;", QChar(0x2abf) },
	{ "subrarr;", QChar(0x2979) },
	{ "subset;", QChar(0x2282) },
	{ "subseteq;", QChar(0x2286) },
	{ "subseteqq;", QChar(0x2ac5) },
	{ "subsetneq;", QChar(0x228a) },
	{ "subsetneqq;", QChar(0x2acb) },
	{ "subsim;", QChar(0x2ac7) },
	{ "subsub;", QChar(0x2ad5) },
	{ "subsup;", QChar(0x2ad3) },
	{ "succ;", QChar(0x227b) },
	{ "succapprox;", QChar(0x2ab8) },
	{ "succcurlyeq;", QChar(0x227d) },
	{ "succeq;", QChar(0x2ab0) },
	{ "succnapprox;", QChar(0x2aba) },
	{ "succneqq;", QChar(0x2ab6) },
	{ "succnsim;", QChar(0x22e9) },
	{ "succsim;", QChar(0x227f) },
	{ "sum;", QChar(0x2211) },
	{ "sung;", QChar(0x266a) },
	{ "sup1", QChar(0xb9) },
	{ "sup1;", QChar(0xb9) },
	{ "sup2", QChar(0xb2) },
	{ "sup2;", QChar(0xb2) },
	{ "sup3", QChar(0xb3) },
	{ "sup3;", QChar(0xb3) },
	{ "sup;", QChar(0x2283) },
	{ "supE;", QChar(0x2ac6) },
	{ "supdot;", QChar(0x2abe) },
	{ "supdsub;", QChar(0x2ad8) },
	{ "supe;", QChar(0x2287) },
	{ "supedot;", QChar(0x2ac4) },
	{ "suphsol;", QChar(0x27c9) },
	{ "suphsub;", QChar(0x2ad7) },
	{ "suplarr;", QChar(0x297b) },
	{ "supmult;", QChar(0x2ac2) },
	{ "supnE;", QChar(0x2acc) },
	{ "supne;", QChar(0x228b) },
	{ "supplus;", QChar(0x2ac0) },
	{ "supset;", QChar(0x2283) },
	{ "supseteq;", QChar(0x2287) },
	{ "supseteqq;", QChar(0x2ac6) },
	{ "supsetneq;", QChar(0x228b) },
	{ "supsetneqq;", QChar(0x2acc) },
	{ "supsim;", QChar(0x2ac8) },
	{ "supsub;", QChar(0x2ad4) },
	{ "supsup;", QChar(0x2ad6) },
	{ "swArr;", QChar(0x21d9) },
	{ "swarhk;", QChar(0x2926) },
	{ "swarr;", QChar(0x2199) },
	{ "swarrow;", QChar(0x2199) },
	{ "swnwar;", QChar(0x292a) },
	{ "szlig", QChar(0xdf) },
	{ "szlig;", QChar(0xdf) },
	{ "target;", QChar(0x2316) },
	{ "tau;", QChar(0x03c4) },
	{ "tbrk;", QChar(0x23b4) },
	{ "tcaron;", QChar(0x0165) },
	{ "tcedil;", QChar(0x0163) },
	{ "tcy;", QChar(0x0442) },
	{ "tdot;", QChar(0x20db) },
	{ "telrec;", QChar(0x2315) },
	{ "tfr;", QChar(0x0001d531) },
	{ "there4;", QChar(0x2234) },
	{ "therefore;", QChar(0x2234) },
	{ "theta;", QChar(0x03b8) },
	{ "thetasym;", QChar(0x03d1) },
	{ "thetav;", QChar(0x03d1) },
	{ "thickapprox;", QChar(0x2248) },
	{ "thicksim;", QChar(0x223c) },
	{ "thinsp;", QChar(0x2009) },
	{ "thkap;", QChar(0x2248) },
	{ "thksim;", QChar(0x223c) },
	{ "thorn", QChar(0xfe) },
	{ "thorn;", QChar(0xfe) },
	{ "tilde;", QChar(0x02dc) },
	{ "times", QChar(0xd7) },
	{ "times;", QChar(0xd7) },
	{ "timesb;", QChar(0x22a0) },
	{ "timesbar;", QChar(0x2a31) },
	{ "timesd;", QChar(0x2a30) },
	{ "tint;", QChar(0x222d) },
	{ "toea;", QChar(0x2928) },
	{ "top;", QChar(0x22a4) },
	{ "topbot;", QChar(0x2336) },
	{ "topcir;", QChar(0x2af1) },
	{ "topf;", QChar(0x0001d565) },
	{ "topfork;", QChar(0x2ada) },
	{ "tosa;", QChar(0x2929) },
	{ "tprime;", QChar(0x2034) },
	{ "trade;", QChar(0x2122) },
	{ "triangle;", QChar(0x25b5) },
	{ "triangledown;", QChar(0x25bf) },
	{ "triangleleft;", QChar(0x25c3) },
	{ "trianglelefteq;", QChar(0x22b4) },
	{ "triangleq;", QChar(0x225c) },
	{ "triangleright;", QChar(0x25b9) },
	{ "trianglerighteq;", QChar(0x22b5) },
	{ "tridot;", QChar(0x25ec) },
	{ "trie;", QChar(0x225c) },
	{ "triminus;", QChar(0x2a3a) },
	{ "triplus;", QChar(0x2a39) },
	{ "trisb;", QChar(0x29cd) },
	{ "tritime;", QChar(0x2a3b) },
	{ "trpezium;", QChar(0x23e2) },
	{ "tscr;", QChar(0x0001d4c9) },
	{ "tscy;", QChar(0x0446) },
	{ "tshcy;", QChar(0x045b) },
	{ "tstrok;", QChar(0x0167) },
	{ "twixt;", QChar(0x226c) },
	{ "twoheadleftarrow;", QChar(0x219e) },
	{ "twoheadrightarrow;", QChar(0x21a0) },
	{ "uArr;", QChar(0x21d1) },
	{ "uHar;", QChar(0x2963) },
	{ "uacute", QChar(0xfa) },
	{ "uacute;", QChar(0xfa) },
	{ "uarr;", QChar(0x2191) },
	{ "ubrcy;", QChar(0x045e) },
	{ "ubreve;", QChar(0x016d) },
	{ "ucirc", QChar(0xfb) },
	{ "ucirc;", QChar(0xfb) },
	{ "ucy;", QChar(0x0443) },
	{ "udarr;", QChar(0x21c5) },
	{ "udblac;", QChar(0x0171) },
	{ "udhar;", QChar(0x296e) },
	{ "ufisht;", QChar(0x297e) },
	{ "ufr;", QChar(0x0001d532) },
	{ "ugrave", QChar(0xf9) },
	{ "ugrave;", QChar(0xf9) },
	{ "uharl;", QChar(0x21bf) },
	{ "uharr;", QChar(0x21be) },
	{ "uhblk;", QChar(0x2580) },
	{ "ulcorn;", QChar(0x231c) },
	{ "ulcorner;", QChar(0x231c) },
	{ "ulcrop;", QChar(0x230f) },
	{ "ultri;", QChar(0x25f8) },
	{ "umacr;", QChar(0x016b) },
	{ "uml", QChar(0xa8) },
	{ "uml;", QChar(0xa8) },
	{ "uogon;", QChar(0x0173) },
	{ "uopf;", QChar(0x0001d566) },
	{ "uparrow;", QChar(0x2191) },
	{ "updownarrow;", QChar(0x2195) },
	{ "upharpoonleft;", QChar(0x21bf) },
	{ "upharpoonright;", QChar(0x21be) },
	{ "uplus;", QChar(0x228e) },
	{ "upsi;", QChar(0x03c5) },
	{ "upsih;", QChar(0x03d2) },
	{ "upsilon;", QChar(0x03c5) },
	{ "upuparrows;", QChar(0x21c8) },
	{ "urcorn;", QChar(0x231d) },
	{ "urcorner;", QChar(0x231d) },
	{ "urcrop;", QChar(0x230e) },
	{ "uring;", QChar(0x016f) },
	{ "urtri;", QChar(0x25f9) },
	{ "uscr;", QChar(0x0001d4ca) },
	{ "utdot;", QChar(0x22f0) },
	{ "utilde;", QChar(0x0169) },
	{ "utri;", QChar(0x25b5) },
	{ "utrif;", QChar(0x25b4) },
	{ "uuarr;", QChar(0x21c8) },
	{ "uuml", QChar(0xfc) },
	{ "uuml;", QChar(0xfc) },
	{ "uwangle;", QChar(0x29a7) },
	{ "vArr;", QChar(0x21d5) },
	{ "vBar;", QChar(0x2ae8) },
	{ "vBarv;", QChar(0x2ae9) },
	{ "vDash;", QChar(0x22a8) },
	{ "vangrt;", QChar(0x299c) },
	{ "varepsilon;", QChar(0x03f5) },
	{ "varkappa;", QChar(0x03f0) },
	{ "varnothing;", QChar(0x2205) },
	{ "varphi;", QChar(0x03d5) },
	{ "varpi;", QChar(0x03d6) },
	{ "varpropto;", QChar(0x221d) },
	{ "varr;", QChar(0x2195) },
	{ "varrho;", QChar(0x03f1) },
	{ "varsigma;", QChar(0x03c2) },
	{ "varsubsetneq;", QChar(0x228a) % QChar(0xfe00) },
	{ "varsubsetneqq;", QChar(0x2acb) % QChar(0xfe00) },
	{ "varsupsetneq;", QChar(0x228b) % QChar(0xfe00) },
	{ "varsupsetneqq;", QChar(0x2acc) % QChar(0xfe00) },
	{ "vartheta;", QChar(0x03d1) },
	{ "vartriangleleft;", QChar(0x22b2) },
	{ "vartriangleright;", QChar(0x22b3) },
	{ "vcy;", QChar(0x0432) },
	{ "vdash;", QChar(0x22a2) },
	{ "vee;", QChar(0x2228) },
	{ "veebar;", QChar(0x22bb) },
	{ "veeeq;", QChar(0x225a) },
	{ "vellip;", QChar(0x22ee) },
	{ "verbar;", "|" },
	{ "vert;", "|" },
	{ "vfr;", QChar(0x0001d533) },
	{ "vltri;", QChar(0x22b2) },
	{ "vnsub;", QChar(0x2282) % QChar(0x20d2) },
	{ "vnsup;", QChar(0x2283) % QChar(0x20d2) },
	{ "vopf;", QChar(0x0001d567) },
	{ "vprop;", QChar(0x221d) },
	{ "vrtri;", QChar(0x22b3) },
	{ "vscr;", QChar(0x0001d4cb) },
	{ "vsubnE;", QChar(0x2acb) % QChar(0xfe00) },
	{ "vsubne;", QChar(0x228a) % QChar(0xfe00) },
	{ "vsupnE;", QChar(0x2acc) % QChar(0xfe00) },
	{ "vsupne;", QChar(0x228b) % QChar(0xfe00) },
	{ "vzigzag;", QChar(0x299a) },
	{ "wcirc;", QChar(0x0175) },
	{ "wedbar;", QChar(0x2a5f) },
	{ "wedge;", QChar(0x2227) },
	{ "wedgeq;", QChar(0x2259) },
	{ "weierp;", QChar(0x2118) },
	{ "wfr;", QChar(0x0001d534) },
	{ "wopf;", QChar(0x0001d568) },
	{ "wp;", QChar(0x2118) },
	{ "wr;", QChar(0x2240) },
	{ "wreath;", QChar(0x2240) },
	{ "wscr;", QChar(0x0001d4cc) },
	{ "xcap;", QChar(0x22c2) },
	{ "xcirc;", QChar(0x25ef) },
	{ "xcup;", QChar(0x22c3) },
	{ "xdtri;", QChar(0x25bd) },
	{ "xfr;", QChar(0x0001d535) },
	{ "xhArr;", QChar(0x27fa) },
	{ "xharr;", QChar(0x27f7) },
	{ "xi;", QChar(0x03be) },
	{ "xlArr;", QChar(0x27f8) },
	{ "xlarr;", QChar(0x27f5) },
	{ "xmap;", QChar(0x27fc) },
	{ "xnis;", QChar(0x22fb) },
	{ "xodot;", QChar(0x2a00) },
	{ "xopf;", QChar(0x0001d569) },
	{ "xoplus;", QChar(0x2a01) },
	{ "xotime;", QChar(0x2a02) },
	{ "xrArr;", QChar(0x27f9) },
	{ "xrarr;", QChar(0x27f6) },
	{ "xscr;", QChar(0x0001d4cd) },
	{ "xsqcup;", QChar(0x2a06) },
	{ "xuplus;", QChar(0x2a04) },
	{ "xutri;", QChar(0x25b3) },
	{ "xvee;", QChar(0x22c1) },
	{ "xwedge;", QChar(0x22c0) },
	{ "yacute", QChar(0xfd) },
	{ "yacute;", QChar(0xfd) },
	{ "yacy;", QChar(0x044f) },
	{ "ycirc;", QChar(0x0177) },
	{ "ycy;", QChar(0x044b) },
	{ "yen", QChar(0xa5) },
	{ "yen;", QChar(0xa5) },
	{ "yfr;", QChar(0x0001d536) },
	{ "yicy;", QChar(0x0457) },
	{ "yopf;", QChar(0x0001d56a) },
	{ "yscr;", QChar(0x0001d4ce) },
	{ "yucy;", QChar(0x044e) },
	{ "yuml", QChar(0xff) },
	{ "yuml;", QChar(0xff) },
	{ "zacute;", QChar(0x017a) },
	{ "zcaron;", QChar(0x017e) },
	{ "zcy;", QChar(0x0437) },
	{ "zdot;", QChar(0x017c) },
	{ "zeetrf;", QChar(0x2128) },
	{ "zeta;", QChar(0x03b6) },
	{ "zfr;", QChar(0x0001d537) },
	{ "zhcy;", QChar(0x0436) },
	{ "zigrarr;", QChar(0x21dd) },
	{ "zopf;", QChar(0x0001d56b) },
	{ "zscr;", QChar(0x0001d4cf) },
	{ "zwj;", QChar(0x200d) },
	{ "zwnj;", QChar(0x200c) },
	}.unite(named_entities);
}