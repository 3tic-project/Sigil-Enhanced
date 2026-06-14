#pragma once
#ifndef COMPLETIONWORDS_H
#define COMPLETIONWORDS_H

#include <QtCore/QList>
#include <QtCore/QMap>
#include <QJsonObject>

class CompletionWords
{
public:
	CompletionWords();
	QStringList getHtmlTagList();
	QStringList getHtmlAttrListForTag(QString tag);
	QStringList getCssAttrList();
	QStringList getCssValueListForAttr(QString attr);
	QStringList getCssAtKeywordList();
private:
	void initHtmlCompletionMap();
	void initCssCompletionMap();
	QMap<QString, QStringList> htmlTagAttrMap;
	QMap<QString, QStringList> cssAttrValueMap;
	QMap<QString, QString> cssAtKeywordMap;
	QJsonObject genDefaultHTMLJson();
	QJsonObject genDefaultCSSJson();
};
#endif