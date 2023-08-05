

#include <QtWidgets/QAbstractItemView>
#include <QtCore/QStringListModel>
#include <QtWidgets/QScrollBar>
#include "Parsers/CodeCompleterParser.h"
#include "Misc/Utility.h"
#include "Misc/SettingsStoreExtend.h"

CodeCompleterParser::CodeCompleterParser(QPlainTextEdit* editor, FileType filetype = FileType::HTML)
   :editor(editor),
	state({PosType::HTML_TEXT,QString()}),
	isForcePopup(false),
	filetype(filetype),
	candidates(new CompletionWords()),
	emmet(new Emmet())
{
	initSettings();
	initCompleter();
}

void CodeCompleterParser::initSettings() {
	SettingsStoreExtend sse;
	std::pair<bool, bool> sets = sse.getCodeCompleterSettings();
	SnippetEabled = sets.first;
	EmmetEnabled = sets.second;
}

QString CodeCompleterParser::completionPrefix() 
{
	return completion_prefix;
}
CodeCompleterParser::PosState CodeCompleterParser::getState()
{
	return state;
}

void CodeCompleterParser::popupCompleter() 
{
	if (isForcePopup) {
		isForcePopup = false;
	}
	else {
		bool isReady = prepartionForACompletion();
		if (!isReady) return;
	}

	correctCompleterModel();

	completer->setCompletionPrefix(completion_prefix);
	completer->popup()->setCurrentIndex(completer->completionModel()->index(0, 0));

	QRect cr = editor->cursorRect();
	cr.setWidth(completer->popup()->sizeHintForColumn(0) + 
				completer->popup()->verticalScrollBar()->sizeHint().width());
	completer->complete(cr); // popup it up!
}

bool CodeCompleterParser::isVisible()
{
	return completer->popup()->isVisible();
}

void CodeCompleterParser::insertSelectedCompletion()
{
	hide();
	QMap<int,QVariant> data = completer->completionModel()->itemData(completer->popup()->currentIndex());
	QString completion = data[0].toString();
	PosType postype = state.postype;
	if (postype == PosType::HTML_TAG || postype == PosType::HTML_STYLEVAL || postype == PosType::CSS_SELECTOR || postype == PosType::CSS_VALUE) {
		insertCompletion(completion, 0);
	}
	else if (postype == PosType::HTML_ATTR) {
		QString full_completion = completion + "=\"\"";
		insertCompletion(full_completion, -1);
	}
	else if (postype == PosType::HTML_STYLEATTR || postype == PosType::CSS_ATTR) {
		QString full_completion = completion + ": ;";
		insertCompletion(full_completion, -1);
		completion_prefix = "";
		state.postype = PosType::HTML_STYLEVAL;
		state.keyword = completion;
		isForcePopup = true;
		popupCompleter();
	}
	else if (postype == PosType::HTML_TEXT && !emmet->get_abbreviation().isEmpty()) {
		QString abbr = emmet->get_abbreviation();
		QString parsed_text = emmet->get_parsedText();
		QString completions = parsed_text;
		ushort move_offset = 0;
		if (completions.indexOf("\v") >= 0) {
			completions.replace("\v", "");
			move_offset = completions.size() - parsed_text.indexOf("\v");
		}
		QTextCursor tc = editor->textCursor();
		tc.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, abbr.size());
		tc.insertText(completions);
		tc.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, move_offset);
		editor->setTextCursor(tc);
	}
}

void CodeCompleterParser::hide()
{
	completer->popup()->hide();
}
QString CodeCompleterParser::wordUnderCursor(QString eow)
{
	QTextCursor tc = editor->textCursor();
	int ori_pos = tc.position();
	tc.select(QTextCursor::LineUnderCursor);
	QString line = tc.selectedText();
	int pos = ori_pos - tc.selectionStart();
	int i = pos, j = pos;
	while (i > 0) {
		if ( eow.contains(line.at(i-1)) )
			break;
		--i;
	}
	while (j < line.size()) {
		if ( eow.contains(line.at(j)) )
			break;
		++j;
	}
	return line.mid(i, j - i);
}
QString CodeCompleterParser::lineBeforeCursor() 
{
	QTextCursor tc = editor->textCursor();
	int ori_pos = tc.position();
	tc.select(QTextCursor::BlockUnderCursor);
	QString line_text = tc.selectedText().startsWith(QChar(0x2029)) ? tc.selectedText().mid(1) : tc.selectedText();
	int line_start = tc.selectedText().startsWith(QChar(0x2029)) ? tc.selectionStart() + 1 : tc.selectionStart();
	if (ori_pos > line_start) {
		return line_text.left(ori_pos - line_start);
	}
	return "";
}
bool CodeCompleterParser::prepartionForACompletion() 
{
	if (filetype == FileType::HTML) {
		state = parseHtmlPosType();
	}
	else if (filetype == FileType::CSS) {
		state = parseCssPosType(0);
	}
	if (!SnippetEabled) {
		if (state.postype != HTML_TEXT) {
			return false;
		}
	}
	if (!EmmetEnabled) {
		if (state.postype == HTML_TEXT) {
			return false;
		}
	}

	PosType postype = state.postype;
	QString eow;
	switch (state.postype) {
	case PosType::HTML_TAG:
		eow = "@~!#$%^&*()_+{}|:\"<>?,./;'[]\\-= ";
		break;
	case PosType::HTML_CLOSE_TAG:
		eow = "@~!#$%^&*()_+{}|:\"<>?,./;'[]\\-=/ ";
		break;
	case PosType::HTML_ATTR:
		eow = "@~!#$%^&*()+{}|\"<>?,./;'[]\\= ";
		break;
	case PosType::HTML_STYLEATTR:
		eow = "@~!#$%^&*()+{}|:\"<>?,./;'[]\\= ";
		break;
	case PosType::HTML_STYLEVAL:
		eow = "@~$^&*()+{}|:\"<>?,./;'[]= ";
		break;
	case PosType::HTML_TEXT:
	{
		QString unverified_text = lineBeforeCursor();
		emmet->set_abbreviation(unverified_text);
		completion_prefix = "";
		if (!emmet->get_abbreviation().isEmpty()) {
			return true;
		}
		break;
	}
	case PosType::CSS_SELECTOR:
		eow = "~!$%^&*()_+{}|\"<>?/;'[]\\-= ";
		break;
	case PosType::CSS_ATTR:
		eow = "@~!#$%^&*()+{}|:\"<>?,./;'[]\\= ";
		break;
	case PosType::CSS_VALUE:
		eow = "@~$^&*()+{}|:\"<>?,./;'[]= ";
		break;
	default:
		completion_prefix = "";
		break;
	}
	if (!eow.isEmpty()) {
		completion_prefix = wordUnderCursor(eow);
	}
	if (completion_prefix.size() < 1) {
		completer->popup()->hide();
		return false;
	}
	return true;
}

void CodeCompleterParser::correctCompleterModel()
{
	PosType postype = state.postype;

	if (isVisible()) {
		return;
	}

	switch (postype) {
	case PosType::HTML_TAG:
		completer = htmlTagCompleter;
		break;
	case PosType::HTML_CLOSE_TAG:
		completer = htmlTagCompleter;
		break;
	case PosType::HTML_ATTR:
	{
		QString tag = state.keyword;
		QStringList completions = candidates->getHtmlAttrListForTag(tag);
		setTempCompleterModel(completions);
		completer = tempCompleter;
		break; 
	}
	case PosType::HTML_STYLEATTR:
		completer = cssAttrCompleter;
		break;
	case PosType::HTML_STYLEVAL: 
	{
		QString attrname = state.keyword;
		QStringList completions = candidates->getCssValueListForAttr(attrname);
		setTempCompleterModel(completions);
		completer = tempCompleter;
		break;
	}
	case PosType::HTML_TEXT:
		if (!emmet->get_abbreviation().isEmpty()) {
			setTempCompleterModel({" Emmet Abbreviation"});
		}
		else {
			setTempCompleterModel({});
		}
		completer = tempCompleter;
		break;
	case PosType::CSS_SELECTOR:
	{
		QString keyword = state.keyword;
		if (keyword.startsWith("@")) {
			QStringList completions = candidates->getCssAtKeywordList();
			setTempCompleterModel(completions);
			completer = tempCompleter;
		}
		else {
			setTempCompleterModel({});
			completer = tempCompleter;
		}
		break;
	}
	case PosType::CSS_ATTR:
		completer = cssAttrCompleter;
		break;
	case PosType::CSS_VALUE:
	{
		QString attrname = state.keyword;
		QStringList completions = candidates->getCssValueListForAttr(attrname);
		setTempCompleterModel(completions);
		completer = tempCompleter;
		break;
	}
	default:
		setTempCompleterModel({});
		completer = tempCompleter;
		break;
	}
	completer->popup()->setFocus();
}
void CodeCompleterParser::setTempCompleterModel(const QStringList &completions)
{
	QStringListModel *model = new QStringListModel();
	model->setStringList(completions);
	tempCompleter->setModel(model);
	tempCompleter->model()->sort(0, Qt::AscendingOrder);
}

void CodeCompleterParser::initCompleter()
{
	htmlTagCompleter = new QCompleter(candidates->getHtmlTagList());
	cssAttrCompleter = new QCompleter(candidates->getCssAttrList());
	tempCompleter = new QCompleter();

	htmlTagCompleter->model()->sort(0, Qt::AscendingOrder);
	htmlTagCompleter->setCompletionMode(QCompleter::PopupCompletion);
	htmlTagCompleter->setModelSorting(QCompleter::CaseInsensitivelySortedModel);
	htmlTagCompleter->setCaseSensitivity(Qt::CaseInsensitive);
	htmlTagCompleter->setWrapAround(false);
	htmlTagCompleter->setWidget(editor);
	cssAttrCompleter->model()->sort(0, Qt::AscendingOrder);
	cssAttrCompleter->setCompletionMode(QCompleter::PopupCompletion);
	cssAttrCompleter->setModelSorting(QCompleter::CaseInsensitivelySortedModel);
	htmlTagCompleter->setCaseSensitivity(Qt::CaseInsensitive);
	cssAttrCompleter->setWrapAround(false);
	cssAttrCompleter->setWidget(editor);
	tempCompleter->setCompletionMode(QCompleter::PopupCompletion);
	tempCompleter->setModelSorting(QCompleter::CaseInsensitivelySortedModel);
	htmlTagCompleter->setCaseSensitivity(Qt::CaseInsensitive);
	tempCompleter->setWrapAround(false);
	tempCompleter->setWidget(editor);

	if (filetype == FileType::HTML) {
		completer = htmlTagCompleter;
	}
	else if (filetype ==FileType::CSS) {
		completer = cssAttrCompleter;
	}
}
void CodeCompleterParser::insertCompletion(QString completion, int move_cursor=0)
{
	if (completer->widget() != editor) {
		return;
	}
	QTextCursor tc = editor->textCursor();
	tc.movePosition(QTextCursor::Left,
					QTextCursor::KeepAnchor,
					completer->completionPrefix().size());
	tc.insertText(completion);

	if (move_cursor > 0) {
		tc.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, move_cursor);
	}
	else if (move_cursor < 0) {
		tc.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, -move_cursor);
	}
	editor->setTextCursor(tc);
}
void CodeCompleterParser::parseCursorPosType() {
	if (filetype == FileType::HTML) {
		state = parseHtmlPosType();
	}
	else if (filetype == FileType::CSS) {
		state = parseCssPosType(0);
	}
}

CodeCompleterParser::PosState CodeCompleterParser::parseHtmlPosType()
{
	QString text;

	auto find_attr_name = [&text](int index)->QString {
		int j = index - 1;
		bool passBlank = false;
		QString attributeName = "";
		while (j - 1 > 0) {
			if (!passBlank) {
				if (QString(" \n").contains(text.at(j - 1))) {
					--j;
					continue;
				}
				passBlank = true;
			}
			if (!QString(" \n").contains(text.at(j - 1))) {
				attributeName = text.at(j - 1) + attributeName;
				--j;
				continue;
			}
			break;
		}
		return attributeName;
	};
	
	auto find_tag = [&text](int index) -> QString {
		if (index - 1 >= 0 && text.at(index - 1) != "<") {
			return "";
		}

		int j = index;
		int text_size = text.size();
		QString eow = " \n\t";
		while (j < text_size) {
			if (!eow.contains(text.at(j))) {
				++j;
			}
			else {
				break;
			}
		}
		QString tag = text.mid(index, j - index);
		return tag;
	};

	auto find_styleAttrName = [&text](int index)->QString {
		if (!(index - 1 >= 0)) {
			return QString("");
		}
		int j = index;
		bool findColon = false;
		QString styleAttrName = "";
		while (j - 1 > 0) {
			if (!findColon) {
				if (text.at(j - 1) == ':')
					findColon = true;
				--j;
				continue;
			}
			if (!QString(" \n\"\';").contains(text.at(j - 1))) {
				styleAttrName = text.at(j - 1) + styleAttrName;
				--j;
				continue;
			}
			break;
		}
		return styleAttrName;
	};

	QTextCursor tc = editor->textCursor();
	uint pos = tc.position();
	uint i = pos;
	text = editor->toPlainText();
	uint Len = text.size();
	short int quote_count = 0;
	bool colonFound = false,
		closingAngleBracketFound = false,
		semicolonFound = false,
		closingSlashFound = false;
	uint textStartPos = 0;
	bool maybeInTag = false,
		maybeInValue = false,
		maybeInAttr = false,
		maybeInStyleAttr = false,
		maybeInStyleValue = false;
	QChar quote_symbol;
	PosState state = { PosType::HTML_TEXT,"" };

	while (i > 0) {
		QChar pre_ch = text.at(i - 1);
		if (pre_ch == "<") {
			if (closingAngleBracketFound) {
				if (text.at(textStartPos-2)!="/" && i+5<Len && text.mid(i, 5) == "style" && QString(">\n ").contains(text.at(i + 5))) {
					state.postype = PosType::HTML_TEXT_FOR_STYLE;
				}
				else {
					state.postype = PosType::HTML_TEXT;
				}
			}
			else if (i < Len && text.at(i) == "/") {
				state.postype = PosType::HTML_CLOSE_TAG;
			}
			else if (text.at(pos - 1) == '/') {
				state.postype = PosType::HTML_CLOSE_TAG;
			}
			else if (maybeInValue) {
				if (maybeInStyleAttr) {
					state.postype = PosType::HTML_STYLEATTR;
				}
				else if (maybeInStyleAttr) {
					state.postype = PosType::HTML_STYLEVAL;
				}
				else {
					state.postype = PosType::HTML_VALUE;
				}
			}
			else if (maybeInAttr) {
				state.postype = PosType::HTML_ATTR;
			}
			else if (!text.mid(i, pos - i).contains(" ") && !text.mid(i, pos - i).contains("\n")) {
				if (text.mid(i, 3) == "!--") {
					state.postype = PosType::HTML_COMMENT;
				}
				else {
					state.postype = PosType::HTML_TAG;
				}
				
			}
			else {
				state.postype = PosType::HTML_ATTR;
			}
			break;
		}

		if (maybeInTag || maybeInValue || maybeInAttr || closingAngleBracketFound) {
			if (pre_ch == '>') {
				closingAngleBracketFound = true;
				textStartPos = i;
			}
			--i;
			continue;
		}
		else if (pre_ch == '>') { // maybe text || text for style
			closingAngleBracketFound = true;
			textStartPos = i;
		}
		else if (pre_ch == '"' || pre_ch == '\'') { // maybe value || attr || style_attr | style_val
			if (quote_symbol.isNull()) {
				quote_count += 1;
				quote_symbol = pre_ch;
			}
			else if (pre_ch == quote_symbol && quote_count == 1)
				quote_count += 1;
		}
		else if (pre_ch == '=') { // maybe value || style_val
			if (quote_count == 1) {
				if (text.mid(i, pos - i).trimmed().startsWith('"')) {
					maybeInValue = true;
					QString attrname = find_attr_name(i);
					if (attrname == "style") {
						if (colonFound) {
							maybeInStyleValue = true;
						}
						else {
							maybeInStyleAttr = true;
						}
					}
				}
			}
			else if (quote_count == 2) {
				if (text.mid(i, pos - i).trimmed().startsWith('"')) {
					maybeInAttr = true;
				}
			}
			quote_count = 0;
		}
		else if (pre_ch == ":") {
			if (quote_count == 0 && !(maybeInStyleAttr || colonFound || semicolonFound)) {
				colonFound = true;
			}
		}
		else if (pre_ch == ";") {
			if (quote_count == 0 && !(maybeInStyleAttr || colonFound || semicolonFound)) {
				semicolonFound = true;
			}
		}
		--i;
	} // end while
	switch (state.postype) {
	case PosType::HTML_TEXT_FOR_STYLE:
		state = parseCssPosType(textStartPos);
		break;
	case PosType::HTML_TAG:
		state.keyword = text.mid(i, pos - i);
		break;
	case PosType::HTML_TEXT:
		state.keyword = find_tag(i);
		break;
	case PosType::HTML_ATTR:
	{
		QString tag = find_tag(i);
		if (tag.startsWith("!--")) {
			state.postype = PosType::HTML_COMMENT;
		}
		else {
			state.keyword = find_tag(i);
		}
		break;
	}
	case PosType::HTML_STYLEVAL:
		state.keyword = find_styleAttrName(pos);
		break;
	}
	return state;
}
CodeCompleterParser::PosState CodeCompleterParser::parseCssPosType(uint startPosOfDoc = 0)
{
	QString text;
	auto find_selector = [&text,&startPosOfDoc](uint index)->QString {
		uint j = index - 1;
		while (j > startPosOfDoc) {
			if (QString("{};").contains(text.at(j - 1)))
				break;
			j -= 1;
		}
		QString selector = Utility::trimmed(text.mid(j, index - j), "\n\t ");
		return selector;
	};
	auto find_styleAttrName = [&text,&startPosOfDoc](uint index)->QString {
		if (!(index - 1 >= startPosOfDoc))
			return "";
		uint j = index;
		bool findColon = false;
		QString styleAttrName = "";
		while (j - 1 > startPosOfDoc) {
			if (!findColon) {
				if (text.at(j - 1) == ':') {
					findColon = true;
				}
				--j;
				continue;
			}
			if (!QString(" \n\t;/{").contains(text.at(j - 1))) {
				styleAttrName = styleAttrName.prepend(text.at(j - 1));
				--j;
				continue;
			}
			break;
		}
		return styleAttrName;
	};

	auto find_selectorKeyword = [&text,&startPosOfDoc](uint index) -> QString {
		uint j = index;
		while (j > startPosOfDoc) {
			QChar pre_ch = text.at(j - 1);
			if (QString("\n\t;{}#!|()<>$%^*/ ").contains(pre_ch))
				break;
			--j;
		}
		QString keyword = text.mid(j, index - j);
		if (keyword.startsWith("@")) {
			return keyword;
		}
		else {
			return "";
		}
	};
	
	PosState state = { PosType::CSS_SELECTOR,"" };
	QTextCursor tc = editor->textCursor();
	uint pos = tc.position(), i = pos;
	text = editor->toPlainText();

	bool colonFound = false,
		semicolonFound = false,
		endOfConment = false,
		maybeInvalue = false,
		maybeInAttr = false;

	while (i > startPosOfDoc) {
		QChar pre_ch = text.at(i - 1);
		if (i - 2 >= startPosOfDoc) {
			if (pre_ch == "/" && text.at(i - 2) == "*") {
				endOfConment = true;
			}
			else if (pre_ch == "*" && text.at(i - 2) == "/") {
				if (endOfConment) {
					i -= 2;
					endOfConment = false;
					continue;
				}
				else {
					state.postype = PosType::CSS_COMMENT;
					break;
				}
			}
		}
		if (endOfConment) {
			--i;
			continue;
		}

		if (pre_ch == '}') {
			state.postype = PosType::CSS_SELECTOR;
			break;
		}

		if (pre_ch == '{') {
			if (colonFound) {
				maybeInvalue = true;
			}
			else if (semicolonFound) {
				maybeInAttr = true;
			}
			QString selector = find_selector(i);

			if (selector.indexOf(QRegExp("^@media|^@supports|^@layer|^@keyframes")) >= 0) {
				state.postype = PosType::CSS_SELECTOR;
			}
			else {
				if (maybeInvalue) {
					state.postype = PosType::CSS_VALUE;
				}
				else if (maybeInAttr) {
					state.postype = PosType::CSS_ATTR;
				}
				else {
					state.postype = PosType::CSS_ATTR;
				}
				break;
			}
		}
		if (colonFound || semicolonFound) {
			--i;
			continue;
		}
		if (pre_ch == ';') {
			semicolonFound = true;
		}
		else if (pre_ch == ':') {
			colonFound = true;
		}
		--i;
	}
	switch (state.postype) {
	case PosType::CSS_SELECTOR:
		state.keyword = find_selectorKeyword(pos);
		break;
	case PosType::CSS_VALUE:
		state.keyword = find_styleAttrName(pos);
		break;
	}
	return state;
}
