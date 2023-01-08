#pragma once
#ifndef EPUBVERCONV_H
#define EPUBVERCONV_H

#include <QDateTime>
#include <QObject>
#include <QtCore/QList>
#include "BookManipulation/Book.h"

class QString;
class EpubVersionConv
{
public:
	// constructor
	EpubVersionConv(QSharedPointer<Book> book);
	// destructor
	~EpubVersionConv();

	// main work functions
	void convert_to_epub2();
	void convert_to_epub3();
	QString convert_named_entities(QString text);

private:

	struct OpenTagInfo
	{
		TagAtts atts;
		QString tagname;
	};
	struct NavPointInfo {
		int lvl;
		QString navlabel;
		QString bookhref;
	};
	struct PageInfo {
		QString pagenum;
		QString bookhref;
	};
	struct MetadataDcInfo {
		QString role;
		QString file_as;
		QString group_position;
	};

	// common
	QSharedPointer<Book> m_Book;
	void init_members();
	QString RegExpSub(const QString &regexp, const QString &alt_pattern, const QString &text, int max_count=0);
	OpenTagInfo parseAttribute(QString tagstring);
	QString opentaginfo_to_string(OpenTagInfo &opentag, QString type = "begin");
	QHash<QString, QString> opfhref_to_id;
	QStringList all_ids;
	QHash<QString, QString> _guide_epubtype_map, _ncx_tagname_map, _namespace_map;
	QHash<QString, QString> named_entities;
	QString getDatetime(bool is_epub3 = true);
	QString has_ncx = "";

	// epub2 to epub3
	void convert_opf_to3();
	void convert_xhtml_to3();
	void build_nav();
	void parse_ncx(QString text);
	QString format_nav_text(QString text);

	QList<NavPointInfo> toclist;
	QList<PageInfo> pagelist;
	QString doctitle = "";
	QString valid_id(QString candidate);
	MetaEntry epub3_map_meta(MetaEntry attr);
	MetaEntry epub3_map_dc(MetaEntry entry);
	int title_count = 0,
		creator_count = 0,
		contributor_count = 0;
	QString title_id= "",cover_id= "",ppd= "",
			series= "", series_index= "", title_sort= "", lang= "";
	QString has_pmap = "";
	QStringList _epub3_allowed_dctypes;
	QHash<QString, QStringList> extra_manifest_props; // (manifest_id, props_list)
	QHash<QString, TagAtts> extra_spine_props; // (manifest_id, TagAtts)
	QList<GuideEntry> guide_res;
	bool has_html_toc = false;

	// epub3 to epub2
	void convert_opf_to2();
	void convert_xhtml_to2();
	void build_toc();
	void parse_nav(QString text);

	QHash<QString, MetadataDcInfo> meta_props_map;
};
#endif //EPUBVERCONV_H