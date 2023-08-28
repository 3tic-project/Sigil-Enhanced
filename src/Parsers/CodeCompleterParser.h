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
		HTML_TEXT			= 1 << 0,
		HTML_TAG			= 1 << 1,
		HTML_CLOSE_TAG		= 1 << 2,
		HTML_ATTR			= 1 << 3,
		HTML_VALUE			= 1 << 4,
		HTML_STYLEATTR		= 1 << 5,
		HTML_STYLEVAL		= 1 << 6,
		HTML_TEXT_FOR_STYLE = 1 << 7,
		HTML_COMMENT		= 1 << 8,
		CSS_SELECTOR		= 1 << 9,
		CSS_ATTR			= 1 << 10,
		CSS_VALUE			= 1 << 11,
		CSS_COMMENT			= 1 << 12
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
