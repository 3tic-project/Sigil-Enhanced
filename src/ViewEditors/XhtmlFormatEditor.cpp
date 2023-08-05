
#include "ViewEditors/XhtmlFormatEditor.h"
#include "Misc/Utility.h"
#include "Parsers/XhtmlFormatParser.h"
//---------------------- modified: XHTML Fomat Configure ---------------------

XhtmlFormatEditor::XhtmlFormatEditor(QWidget* parent)
    : QPlainTextEdit(parent),
    m_Highlighter(new CSSHighlighter(this))
{
    m_Highlighter->setDocument(this->document());
};

void XhtmlFormatEditor::keyPressEvent(QKeyEvent* event)
{
    if (CssViewKeyPressEvent(event)) return;
    QPlainTextEdit::keyPressEvent(event);
}

inline void XhtmlFormatEditor::insertTextAtCursor(QString text, QTextCursor cursor) {
    cursor.beginEditBlock();
    cursor.insertText(text);
    cursor.endEditBlock();
}

bool XhtmlFormatEditor::CommonKeyPressEvent(QKeyEvent* event) {

    if (QString("\x1\x2\x3").contains(QChar(event->key()))) {

        QTextCursor cursor = textCursor();

        if (event->key() == Qt::Key_Backspace) { // Key_Backspace
            if (cursor.hasSelection()) {
                return false;
            }
            int ori_pos = cursor.position();
            int offset = 0;
            cursor.movePosition(QTextCursor::StartOfLine, QTextCursor::KeepAnchor);
            QString selected_text = cursor.selectedText();

            int indent_index = Utility::StringTrimmedIndex(selected_text).before;
            QString indent = "";
            if (indent_index > 0 && indent_index == selected_text.size()) { // The String is entirely composed of blank characters.
                if (indent_index % 2 == 0) {
                    offset -= 2;
                    indent = selected_text.left(indent_index - 2);
                }
                else {
                    offset -= 1;
                    indent = selected_text.left(indent_index - 1);
                }
                cursor.beginEditBlock();
                cursor.insertText(indent);
                cursor.endEditBlock();
                cursor.setPosition(ori_pos + offset);
                return true;
            }
            return false;
        }

        if (cursor.hasSelection()) { // Key_Tab or Key_BackTab, cursor has selection
            int ori_start = cursor.selectionStart();
            int ori_end = cursor.selectionEnd();
            cursor.setPosition(cursor.selectionStart()); // Make sure the position on the first selected line as the selection range may span multiple lines.
            cursor.select(QTextCursor::LineUnderCursor);
            int selection_start = cursor.selectionStart();

            if (ori_start >= cursor.selectionStart() && ori_end >= cursor.selectionEnd()) {
                if (ori_end > cursor.selectionEnd()) {
                    cursor.setPosition(ori_end, QTextCursor::KeepAnchor);
                }
                QStringList text_splited = cursor.selectedText().split(QChar(0x2029)); // 0x2029 paragraph separator;
                QString new_text = "";
                int e_offset = 0;
                if (event->key() == Qt::Key_Tab) { // Tab
                    foreach(QString fragment, text_splited) {
                        int indent_index = Utility::StringTrimmedIndex(fragment).before;
                        int add_num = 0;
                        add_num = indent_index % 2 == 0 ? 2 : 1;
                        e_offset += add_num;
                        new_text += QString(add_num, ' ') + fragment + QChar(0x2029);
                    }
                }
                else { // Shift + Tab
                    foreach(QString fragment, text_splited) {
                        int indent_index = Utility::StringTrimmedIndex(fragment).before;
                        int sub_num = 0;
                        if (indent_index > 0) {
                            sub_num = indent_index % 2 == 0 ? 2 : 1;
                        }
                        e_offset -= sub_num;
                        new_text += fragment.right(fragment.length() - sub_num) + QChar(0x2029);
                    }
                }
                //修改文档
                insertTextAtCursor(new_text.left(new_text.length() - 1), cursor);
                //复原光标及选择范围
                cursor.setPosition(selection_start);
                cursor.setPosition(ori_end + e_offset, QTextCursor::KeepAnchor);
                setTextCursor(cursor);
            }
            else if (ori_end < cursor.selectionEnd()) {
                insertTextAtCursor("  ", textCursor());
            }
            return true;
        }
        else if (event->key() == Qt::Key_Tab) { // Key_Tab, cursor has no selection
            insertTextAtCursor("  ", cursor);
            return true;
        }
        else if (event->key() == Qt::Key_Backtab) { //// Key_BackTab, cursor has no selection
            int ori_pos = cursor.position();
            int offset = 0;
            cursor.select(QTextCursor::LineUnderCursor);
            QString selected_text = cursor.selectedText();

            if (selected_text.length() > 0) {
                bool strip_blank = false;
                QString new_text = "";

                int indent_index = Utility::StringTrimmedIndex(selected_text).before;
                if (indent_index > 0) {
                    if (indent_index % 2 == 0) {
                        offset -= 2;
                        new_text = selected_text.right(selected_text.length() - 2);
                        strip_blank = true;
                    }
                    else {
                        offset -= 1;
                        new_text = selected_text.right(selected_text.length() - 1);
                        strip_blank = true;
                    }
                }
                if (strip_blank) {
                    insertTextAtCursor(new_text, cursor);
                    cursor.setPosition(ori_pos + offset);
                    setTextCursor(cursor);
                }
            }
            return true;
        }
    }
    return false;
}

bool XhtmlFormatEditor::CssViewKeyPressEvent(QKeyEvent* event)
{
    if (CommonKeyPressEvent(event)) return true;

    auto getIndexOfLineWithLBrace = [](const QString& source, int start_pos, bool enterkey = false)->int {
        int indexOfLineWithLBrace = 0;
        int brace = 1;
        bool get_break = false;
        for (int i = start_pos - 1; i >= 0; --i) {
            QChar ch = source.at(i);
            if (!enterkey && !get_break) {
                if (ch != QChar(0x20) && ch != QChar(0x2029) && ch != QChar(0x9)) {
                    return -1;
                }
            }
            if (ch == '{') {
                if (!get_break) {
                    return -1;
                }
                --brace;
            }
            else if (ch == '}') {
                ++brace;
            }
            else if (ch == QChar(0x2029)) {
                if (!get_break) get_break = true;
                if (brace == 0) {
                    indexOfLineWithLBrace = i + 1;
                    break;
                }
            }
        }
        if (brace != 0) return -1;
        return indexOfLineWithLBrace;
    };

    if (QString("\x4\x5{}").contains(QChar(event->key()))) { // \x4 Key_Return  \x5 Key_Enter
        QTextCursor cursor = textCursor();
        int ori_pos = cursor.position();
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) { //Detect Key_Return or Key_Enter

            //cursor.movePosition(QTextCursor::StartOfLine, QTextCursor::KeepAnchor);
            cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
            int line_start = cursor.selectionStart();
            QString textLeftOfCursor = cursor.selectedText();
            QString trimmed_text = Utility::trimmed(textLeftOfCursor, " \t");
            int indent_len = Utility::StringTrimmedIndex(textLeftOfCursor).before;
            QString insert_text = "";
            if (ori_pos <= line_start + indent_len) { // 光标位于缩进空白符位置
                //cursor.setPosition(ori_pos);
                insert_text = textLeftOfCursor + QChar(0x2029) + textLeftOfCursor;
                insertTextAtCursor(insert_text, cursor);
            }

            else if (trimmed_text.endsWith('{') || trimmed_text.endsWith('}')) {
                if (trimmed_text.endsWith("{")) {
                    //cursor.movePosition(QTextCursor::EndOfLine, QTextCursor::KeepAnchor);
                    cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
                    QString textRightOfCursor = cursor.selectedText();
                    QString indent = textLeftOfCursor.left(indent_len);
                    QString inner_indent = indent + "  ";
                    if (Utility::trimmed(textRightOfCursor, " \t").startsWith("}")) {
                        insert_text = QChar(0x2029) + inner_indent + QChar(0x2029) + indent + textRightOfCursor;
                    }
                    else {
                        insert_text = QChar(0x2029) + inner_indent + textRightOfCursor;
                    }
                    insertTextAtCursor(insert_text, cursor);
                    cursor.setPosition(ori_pos + inner_indent.size() + 1);
                    setTextCursor(cursor);
                }
                else { // trimmed_text.endsWith('}')
                    cursor.select(QTextCursor::Document);
                    const QString& source = cursor.selectedText();
                    int indexOfLineWithLBrace = getIndexOfLineWithLBrace(source, line_start + indent_len + trimmed_text.size() - 1, true);
                    if (indexOfLineWithLBrace < 0) {
                        indexOfLineWithLBrace = ori_pos;
                    }
                    cursor.setPosition(indexOfLineWithLBrace);
                    //cursor.select(QTextCursor::LineUnderCursor);
                    cursor.select(QTextCursor::BlockUnderCursor);
                    QString matched_line = cursor.selectionStart() > 0 && cursor.hasSelection() ? cursor.selectedText().mid(1) : cursor.selectedText();
                    indent_len = Utility::StringTrimmedIndex(matched_line).before;
                    QString indent = matched_line.left(indent_len);
                    cursor.setPosition(ori_pos);
                    QString insert_text = QChar(0x2029) + indent;
                    insertTextAtCursor(insert_text, cursor);
                }
            }
            else {
                cursor.setPosition(ori_pos);
                QString indent = textLeftOfCursor.left(indent_len);
                insert_text = QChar(0x2029) + indent;
                insertTextAtCursor(insert_text, cursor);
            }
            return true;
        }
        else if (event->key() == Qt::Key_BraceLeft) { // 检测到左花括号 "{" 输入
            cursor.movePosition(QTextCursor::EndOfWord);
            if (cursor.position() == ori_pos) {
                insertTextAtCursor("{}", cursor);
                cursor.setPosition(ori_pos + 1);
                setTextCursor(cursor);
                return true;
            }
            return false;
        }
        else if (event->key() == Qt::Key_BraceRight) { // 检测到右花括号 "}" 输入
            cursor.select(QTextCursor::Document);
            const QString& source = cursor.selectedText();
            int indexOfLineWithLBrace = getIndexOfLineWithLBrace(source, ori_pos);
            if (indexOfLineWithLBrace < 0) {
                return false;
            }
            cursor.setPosition(indexOfLineWithLBrace);
            //cursor.select(QTextCursor::LineUnderCursor);
            cursor.select(QTextCursor::BlockUnderCursor);
            QString current_line = cursor.selectionStart() > 0 && cursor.hasSelection() ? cursor.selectedText().mid(1) : cursor.selectedText();
            int indent_len = Utility::StringTrimmedIndex(current_line).before;
            QString indent = current_line.left(indent_len);
            cursor.setPosition(ori_pos);
            cursor.select(QTextCursor::LineUnderCursor);
            QString insert_text = indent + "}";
            insertTextAtCursor(insert_text, cursor);
            return true;
        }
    }
    return false;
}

void XhtmlFormatEditor::setDefaultText() {
    setPlainText(XhtmlFormatParser::getDefaultConfigure());
}