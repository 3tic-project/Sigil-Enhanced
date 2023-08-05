#pragma once
#ifndef EMMET_H
#define EMMET_H

#include <QtCore/QHash>
#include <QtCore/QString>
#include "Parsers/XhtmlFormatParser.h"
class Emmet
{
public:
	Emmet(QString unverified_text="");
	QString get_abbreviation();
	void set_abbreviation(QString unverified_text);
	QString get_parsedText();
	
private:
	struct Element {
		Element(QString tag = "") :tagName(tag) {}
		QString tagName;
		QString id="\x12";
		QStringList classlist;
		QHash<QString, QString> attrdict;
		QString text;
		QList<Element*> children;
	};
	XhtmlFormatParser xmlFormatParser;
	QChar last_quote;
	QString abbrev;
	QString curent_indent;
	QStringList filter(QStringList& _list);
	ushort getTokenType(QChar ch);
	QString getValidAbbrev(QString unverified_text);
	QString parse_abbrev();
	QString convertDollarSymbols(QString text, short num);
	QString elementToString(Element* em);
	QString elementToTagString(Element* em, bool closed);
	QString getCurrentIndent(QString line_text);
	QString formatCode(QString text);
	Element* copyElement(Element* e,short dollar_num = 1);
	QList<Element*> multipleCopyElement(Element* e,short times);
};
#endif
