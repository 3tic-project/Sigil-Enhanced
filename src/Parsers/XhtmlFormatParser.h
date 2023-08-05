#pragma once
#ifndef XHTMLFORMATPARSER_H
#define XHTMLFORMATPARSER_H

#include <QString>
#include <QList>
#include <QHash>

//------------------- modified: XHTML Fomat Configure ------------------------
class XhtmlFormatParser 
{
public:
	XhtmlFormatParser(QString conf_text = "");

	enum state { UNDEFINED_PROP = -100 };
	enum sort_mode { ORI, ASCEND, DESCEND };

	struct globalprops {
		short indent = UNDEFINED_PROP;
		short cssfold = UNDEFINED_PROP;
	};
	struct properties {
		short open_pre_br = UNDEFINED_PROP;
		short open_post_br = UNDEFINED_PROP;
		short close_pre_br = UNDEFINED_PROP;
		short close_post_br = UNDEFINED_PROP;
		short ind_adj = UNDEFINED_PROP;
		short inner_ind_adj = UNDEFINED_PROP;
		short attr_fm_resv = UNDEFINED_PROP;
		short text_fm_resv = UNDEFINED_PROP;
	};
	globalprops m_gobal_props;
	QHash<QString, properties> m_pathPropsCache; // Temporarily store the path properties to avoid duplicate caculations for the same path.
	QStringList getAllSelectors(sort_mode mode);
	properties getSelectorProperties(QString selector);
	QString getCleanConfText();
	QString getConfText();
	static QString getDefaultConfigure();
private:
	void parse();
	QStringList m_selectors;
	QHash<QString, properties> m_propertiesMap; // { selector, properties }
	QString m_oriConfText;
	QStringList OrderingSelectors(bool descending = false);
	ulong calcWeightForSelector(QString selector);
};
# endif // XHTMLFORMATPARSER_H