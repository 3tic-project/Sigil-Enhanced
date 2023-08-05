#pragma once
#ifndef CODECOMPLETERPARSER_H
#define CODECOMPLETERPARSER_H

#include <QtCore/QList>
#include <QtWidgets/QCompleter>
#include <QtWidgets/QPlainTextEdit>
#include "Parsers/CompletionWords.h"
#include "Parsers/Emmet.h"

class CodeCompleterParser
{
public:
	enum FileType {
		HTML,
		CSS
	};
	enum PosType {
		HTML_TEXT = 1,
		HTML_TAG = 2,
		HTML_CLOSE_TAG = 4,
		HTML_ATTR = 8,
		HTML_VALUE = 16,
		HTML_STYLEATTR = 32,
		HTML_STYLEVAL = 64,
		HTML_TEXT_FOR_STYLE = 128,
		HTML_COMMENT = 256,
		CSS_SELECTOR = 256 * 2,
		CSS_ATTR = 256 * 4,
		CSS_VALUE = 256 * 8,
		CSS_COMMENT = 256 * 16
	};
	struct PosState {
		PosType postype;
		QString keyword;
	};
	CodeCompleterParser(QPlainTextEdit *editor, FileType filetype);
	QString completionPrefix();
	PosState getState();
	void insertSelectedCompletion();
	void parseCursorPosType();
	void popupCompleter();
	bool isVisible();
	void hide();
private:
	QPlainTextEdit* editor;
	QString completion_prefix;
	FileType filetype;
	PosState state;
	CompletionWords* candidates;
	Emmet* emmet;
	QCompleter * completer,
		       * htmlTagCompleter,
		       * cssAttrCompleter,
		       * tempCompleter;

	bool isForcePopup;
	bool EmmetEnabled;
	bool SnippetEabled;

	QString wordUnderCursor(QString eow);
	QString lineBeforeCursor();
	bool prepartionForACompletion();
	void correctCompleterModel();
	void setTempCompleterModel(const QStringList &completions);
	void initCompleter();
	void initSettings();
	void insertCompletion(QString completion,int move_cursor);
	PosState parseHtmlPosType();
	PosState parseCssPosType(uint startPosOfDoc);
};
#endif
