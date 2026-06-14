#include <QRegExp>
#include <QRegularExpression>
#include "Parsers/XhtmlFormatParser.h"
#include "qtextcodec.h" // modified: XHTML Fomat Configure

static const char* DEFAULT_CONF = "/* global settings */\n@indent 2;\n@css-fold false;\n\n/* block-level elements */\nhtml, body, p, di"
"v, h1, h2, h3, h4, h5, h6, ol, ul, li, address, blockquote, dd, dl, fieldset, form, hr, nav, menu, p"
"re, table, tr, td, th, article\n{ \n  opentag-br : 1 0;\n  closetag-br: 0 1;\n}\n\n/* head elements "
"*/\nhead, meta, link, title, style, script \n{\n  opentag-br : 1 0;\n  closetag-br: 0 1;\n}\n\n/* xm"
"l header */\n?xml { \n  opentag-br: 0 1;\n}\n\n/* doctype */\n!DOCTYPE { \n  opentag-br  : 1 2;\n  a"
"ttr-fm-resv: true;\n}\n\n/* xhtml element */\nhtml { \n  inner-ind-adj:-1;\n}\n\n/* comment */\n!-- "
"{\n  attr-fm-resv: true;\n}\n\n/* main */\nbody{ \n  opentag-br : 2 1;\n  closetag-br: 1 1;\n}\n\nh1"
",h2,h3,h4,h5,h6 { \n  opentag-br : 2 0;\n  closetag-br: 0 2;\n}\npre {\n  text-fm-resv: true;\n}\n\n"
"/* 配置说明：\n1. HTML代码格式化的配置语言类似CSS，通过选择器筛选具体节点，并由特定的属性指定该类节点的前后换行符数量和缩进级别。(仅)支持元素选择器、通配符选择器，除此以外的选择器类型"
"都不支持。\n    选择器写法示例：\n    div { ... }\n    div * p { ... }\n    h1,h2,h3 { ... }\n选择器仅支持通过“后代”(A B)，“"
"并集”(A,B)两种方式进行组合。\n需要注意的是，“后代选择器”的空格后面衔接的【只能是子代，不能隔代】，这点与标准CSS不同。\n\n2. 支持的属性包括：\n   opentag-br: 指定节"
"点的【开始标签】前后的换行符个数。\n               值输入两个整数，格式为 【 opentag-br: n1 n2 】\n               n1 代表标签前面换行符个数，n"
"2 代表标签后面换行符个数。\n               n1、n2 范围 0 ~ 9，默认值0。\n  closetag-br: 指定节点的【结束标签】前后的换行符个数。\n          "
"     范围 0 ~ 9，默认值 0。\n               格式同上。\n      ind-adj: 调整节点的缩进级别。\n               正数进级，负数退级，例如-2"
"代表缩进退二级。\n               范围 -9 ~ 9，默认值 0。\ninner-ind-adj: 调整节点内部的缩进级别(不带上节点本身)。\n               范围 -"
"9 ~ 9，默认值 0。\n attr-fm-resv: 指定开口标签内部是否保留非必需的空格和换行符。\n               范围 true 或 false，默认值 false。\n te"
"xt-fm-resv: 指定节点内部文本是否保留非必需的空格和换行符。\n               范围 true 或 false，默认值 false。\n               启用该属性"
"会让节点内部关于换行符和缩进的计算全部失效,\n               建议只给 pre 这类需要保留换行符和缩进的节点使用!\n    @css-fold: 指定Style节点的CSS是否折叠"
"。\n               范围 true 或 false，默认值 false。\n               特殊属性，不需要写在花括号内部。\n      @indent: 指定每级缩进"
"符的空格个数。\n               范围 0 ~ 4，默认值 2。\n               特殊属性，不需要写在花括号内部。\n注意：\na) 对于单标签（开口和闭合在同一标签，如"
" <img/>），开始标签和结束标签换行属性会叠加。\n     注释 <!-- comment --> 同样视为单标签，标签名为“!--”。\nb) 以上所有属性，除了 opentag-br 和 c"
"losetag-br 的值需要输入两个整数，其他属性的值均为一个整数。\n\n3. 换行符合并\n类似于相邻元素的上下margin会合并，在两个相邻节点之间，通过 opentag-br、closeta"
"g-br 指定的换行符数量取决于更大的那个，而非叠加。这种情况称为“换行符合并”。例如有CSS规则如下： \n    p { opentag-br: 2 0; closetag-br: 0 2; }\n"
"那么两个相邻 p 元素之间的换行符数是 2，而非 2+2=4，因为节点之间换行符数量不会叠加。*/";

//------------------- modified: XHTML Fomat Configure ------------------------

XhtmlFormatParser::XhtmlFormatParser(QString conf_text)
	: m_oriConfText(conf_text)
{
	if (m_oriConfText.isEmpty()) {
		m_oriConfText = getDefaultConfigure();
	}
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
	default:
		return m_selectors;
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

QString XhtmlFormatParser::getDefaultConfigure() {
	QTextCodec* codec = QTextCodec::codecForName("UTF-8"); // turn the UTF-8 chars to unicode codec.
	return codec->toUnicode(DEFAULT_CONF);
}