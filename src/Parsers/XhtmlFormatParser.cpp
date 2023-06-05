#include <QRegularExpression>
#include "Parsers/XhtmlFormatParser.h"

//------------------- modified: XHTML Fomat Configure ------------------------

XhtmlFormatParser::XhtmlFormatParser(QString conf_text)
	: m_oriConfText(conf_text)
{
	parse();
}

void XhtmlFormatParser::parse()
{
	QString clean_text = getCleanConfText();

	QRegExp test_int("^-?\\d+$");
	QRegExp test_int_int("^\\d+ \\d+$");
	QRegExp test_bool("true|false");
	//QRegExp test_invalid_pseudo_class(":$|::|: |:,");
	QRegExp test_invalid_wildcard("[^ ]\\*|\\*[^ :]");
	QRegularExpression re;

	re.setPattern("@indent (\\d+);");
	QRegularExpressionMatch m = re.match(clean_text);
	if (m.hasMatch()) {
		int indent_size = m.captured(1).toInt();
		m_gobal_props.indent = indent_size;
	}
	re.setPattern("@css-fold (true|false);");
	m = re.match(clean_text);
	if (m.hasMatch()) {
		int cssfold_value = m.captured(1) == "true" ? 1 : 0;
		m_gobal_props.cssfold = cssfold_value;
	}

	re.setPattern("([a-zA-Z?!_\\-\\*][a-zA-Z\\d_,\\- \\*]*?)\\{(.*?)\\}");
	QRegularExpressionMatchIterator iter = re.globalMatch(clean_text);
	while (iter.hasNext()) {
		QRegularExpressionMatch m = iter.next();
		QString selectors = m.captured(1);
		QString properties = m.captured(2);
		foreach(QString sel, selectors.split(",")) {
			//if (test_invalid_pseudo_class.indexIn(sel) > -1) continue;
			if (test_invalid_wildcard.indexIn(sel) > -1) continue;
			if (m_selectors.indexOf(sel) > -1) {
				m_selectors.removeAt(m_selectors.indexOf(sel));
			}
			m_selectors.append(sel);
			foreach(QString prop, properties.split(";")) {
				QStringList prop_value = prop.split(":");
				if (prop_value.length() != 2) continue;
				if (prop_value[0] == "opentag-br") {
					if (test_int_int.indexIn(prop_value[1]) < 0) continue;
					QStringList values = prop_value[1].split(' ');
					m_propertiesMap[sel].open_pre_br = values[0].toShort();
					m_propertiesMap[sel].open_post_br = values[1].toShort();
				}
				else if (prop_value[0] == "closetag-br") {
					if (test_int_int.indexIn(prop_value[1]) < 0) continue;
					QStringList values = prop_value[1].split(' ');
					m_propertiesMap[sel].close_pre_br = values[0].toShort();
					m_propertiesMap[sel].close_post_br = values[1].toShort();
				}
				else if (prop_value[0] == "ind-adj") {
					if (test_int.indexIn(prop_value[1]) < 0) continue;
					m_propertiesMap[sel].ind_adj = prop_value[1].toShort();
				}
				else if (prop_value[0] == "inner-ind-adj") {
					if (test_int.indexIn(prop_value[1]) < 0) continue;
					m_propertiesMap[sel].inner_ind_adj = prop_value[1].toShort();
				}
				else if (prop_value[0] == "attr-fm-resv") {
					if (test_bool.indexIn(prop_value[1]) < 0) continue;
					m_propertiesMap[sel].attr_fm_resv = prop_value[1] == "true" ? 1 : 0;
				}
				else if (prop_value[0] == "text-fm-resv") {
					if (test_bool.indexIn(prop_value[1]) < 0) continue;
					m_propertiesMap[sel].text_fm_resv = prop_value[1] == "true" ? 1 : 0;
				}
			}
		}
	}
}
QString XhtmlFormatParser::getConfText() {
	return m_oriConfText;
}
QString XhtmlFormatParser::getCleanConfText()
{
	QString text = m_oriConfText;
	QString new_text = "";
	QString blank_chars = " \n\t";
	bool annotation = false;
	// while brace > 0,the indicator inside the brace, while brace < 0, there are some unexpected right brace,the brace variable must be reset to 0.
	int index = -1;
	while (index < text.length() - 2)
	{
		++index;
		QChar ch = text.at(index);
		QChar next_ch = text.at(index + 1);
		if (annotation) {
			if (ch == QChar('*') && next_ch == QChar('/')) {
				annotation = false;
				index += 1;
			}
			continue;
		}

		if (blank_chars.contains(ch)) {
			if (new_text == "") continue;
			if (blank_chars.contains(next_ch)) continue;
			if (QString("{};:,").contains(new_text.right(1))) continue;
			if (QString("{};:,").contains(next_ch)) continue;
		}

		if (ch == QChar('/') && next_ch == QChar('*')) {
			annotation = true;
			index += 1;
			continue;
		}
		new_text = blank_chars.contains(ch) ? new_text.append(" ") : new_text.append(ch);

	}// end while
	if (index == text.length() - 2 && text.right(1) != " ") {
		new_text += text.right(1);
	}
	return new_text;
}
ulong XhtmlFormatParser::calcWeightForSelector(QString selector) {
	QChar lastChar;
	ulong weight = 0;
	QStringList segments= selector.split(" ");
	foreach(QString seg, segments) {
		if (seg == "*") {
			weight += 1;
		}
		else {
			weight += 1000;
		}
	}
	return weight;
}

QStringList XhtmlFormatParser::OrderingSelectors(bool descending)
{
	QStringList orderedSelectors;
	QList<std::pair<QString, ulong>> selectorsWithWeight;
	unsigned int order = -1;
	foreach(QString sel, m_selectors)
	{
		order += 1;
		ulong weight = calcWeightForSelector(sel)*1000 + order;
		selectorsWithWeight << std::pair<QString,ulong>(sel, weight);
	}
	std::sort(selectorsWithWeight.begin(), selectorsWithWeight.end(), 
		[&descending](std::pair<QString, ulong> a, std::pair<QString, ulong> b) {
			if (descending) {
				return a.second > b.second;
			}
			return a.second < b.second;
		});
	foreach(auto selWithWeight, selectorsWithWeight) {
		orderedSelectors << selWithWeight.first;
	}
	return orderedSelectors;
}

QStringList XhtmlFormatParser::getAllSelectors(sort_mode mode) 
{
	switch (mode) {
	case ORI:
		return m_selectors;
	case ASCEND:
		return OrderingSelectors(false);
	case DESCEND:
		return OrderingSelectors(true);
	}
}

XhtmlFormatParser::properties XhtmlFormatParser::getSelectorProperties(QString selector) {
	if (m_propertiesMap.contains(selector)) {
		return m_propertiesMap.value(selector);
	}
	else {
		XhtmlFormatParser::properties default_props;
		return default_props;
	}
}