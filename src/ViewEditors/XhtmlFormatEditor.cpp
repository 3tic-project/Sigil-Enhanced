
#include "ViewEditors/XhtmlFormatEditor.h"
#include "Misc/Utility.h"

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

bool XhtmlFormatEditor::CommonKeyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab) { // 单按Tab键，键码为Key_Tab；按下Shift键后按Tab键，键码改为Key_Backtab
        //在光标选择文本的条件下
        if (textCursor().hasSelection()) {  // 侦测到文本选择下按 Tab 键或 Shift + Tab 键，进行多行缩进（退缩进）
            long ori_Start = textCursor().selectionStart();
            long ori_End = textCursor().selectionEnd();
            QTextCursor cursor = textCursor();
            cursor.setPosition(textCursor().selectionStart());
            cursor.select(QTextCursor::LineUnderCursor);

            if (ori_Start >= cursor.selectionStart() && ori_End >= cursor.selectionEnd()) {
                if (textCursor().selectionEnd() > cursor.selectionEnd()) {
                    // KeepAnchor表示光标保持起点不变，移动终点，用于改变选择范围。与之相对的是MoveAnchor，起点与终点相同，失去选择范围。
                    cursor.setPosition(ori_End, QTextCursor::KeepAnchor);
                }
                QStringList text_splited = cursor.selectedText().split(QChar(0x2029)); // 0x2029 段落分隔符;
                QRegExp re = QRegExp("^[ \t]+");
                QString new_text = "";
                int e_offset = 0;
                if (event->key() == Qt::Key_Tab) { // Tab
                    foreach(QString fragment, text_splited) {
                        int indent_index = re.indexIn(fragment);
                        int add_num = 0;
                        if (indent_index > -1) {
                            QString indent = re.cap(0);
                            add_num = indent.length() % 2 == 0 ? 2 : 1;
                        }
                        else {
                            add_num = 2;
                        }
                        e_offset += add_num;
                        new_text += QString(add_num, ' ') + fragment + QChar(0x2029);
                    }
                }
                else { // Shift + Tab
                    foreach(QString fragment, text_splited) {
                        int indent_index = re.indexIn(fragment);
                        int sub_num = 0;
                        if (indent_index > -1) {
                            QString indent = re.cap(0);
                            sub_num = indent.length() % 2 == 0 ? 2 : 1;
                        }
                        e_offset -= sub_num;
                        new_text += fragment.right(fragment.length() - sub_num) + QChar(0x2029);
                    }
                }
                ori_Start = cursor.selectionStart();
                //修改文档
                cursor.beginEditBlock();
                cursor.insertText(new_text.left(new_text.length() - 1));
                cursor.endEditBlock();
                //复原光标及选择范围
                cursor.setPosition(ori_Start);
                cursor.setPosition(ori_End + e_offset, QTextCursor::KeepAnchor);
                setTextCursor(cursor);
            }
            return true;
        }
        else if (event->key() == Qt::Key_Tab) { // 单行（光标非选择状态）tab 键
            textCursor().beginEditBlock();
            textCursor().insertText("  ");
            textCursor().endEditBlock();
            return true;
        }
        else if (event->key() == Qt::Key_Backtab) { // 单行（光标非选择状态） Shift + Tab 键
            QTextCursor cursor = textCursor();
            int ori_pos = cursor.position();
            int offset = 0;
            cursor.select(QTextCursor::LineUnderCursor);
            QString selected_text = cursor.selectedText();

            if (selected_text.length() > 0) {
                bool strip_blank = false;
                QString new_text = "";
                QRegExp re = QRegExp("^[ \t]+");
                int index = re.indexIn(selected_text);
                if (index > -1) {
                    QString indent = re.cap(0);
                    if (indent.length() % 2 == 0) {
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
                    cursor.beginEditBlock();
                    cursor.insertText(new_text);
                    cursor.endEditBlock();
                    cursor.setPosition(ori_pos + offset);
                    setTextCursor(cursor);
                }
            }
            return true;
        }
    }
    else if (event->key() == Qt::Key_Backspace) { //侦测到退格键，判断是否退缩进（判断退2个空白符还是退1个字符）
        QTextCursor cursor = textCursor();
        if (cursor.hasSelection()) {
            QPlainTextEdit::keyPressEvent(event);
            return true;
        }

        int ori_pos = cursor.position();
        int offset = 0;
        cursor.movePosition(QTextCursor::StartOfLine, QTextCursor::KeepAnchor);
        QString selected_text = cursor.selectedText();
        QRegExp re = QRegExp("^[ \t]+");
        int index = re.indexIn(selected_text);
        if (index > -1 && re.cap(0) == selected_text) {
            QString indent = re.cap(0);
            if (indent.length() % 2 == 0) {
                offset -= 2;
                indent = indent.left(indent.length() - 2);
            }
            else {
                offset -= 1;
                indent = indent.left(indent.length() - 1);
            }
            cursor.beginEditBlock();
            cursor.insertText(indent);
            cursor.endEditBlock();
            cursor.setPosition(ori_pos + offset);
        }
        else {
            QPlainTextEdit::keyPressEvent(event);
        }
        return true;
    }
    return false;
}

bool XhtmlFormatEditor::CssViewKeyPressEvent(QKeyEvent* event)
{
    if (CommonKeyPressEvent(event)) return true;

    auto getIndexOfLineWithRBrace = [](const QString& source, int start_pos, bool enterkey = false)->int {
        int indexOfLineWithRBrace = 0;
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
                    indexOfLineWithRBrace = i + 1;
                    break;
                }
            }
        }
        if (brace != 0) return -1;
        return indexOfLineWithRBrace;
    };

    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) { //侦测到回车键或Enter键，判断是否补偿缩进。
        QTextCursor cursor = textCursor();
        int ori_pos = cursor.position();
        cursor.select(QTextCursor::LineUnderCursor);
        int line_start = cursor.selectionStart();
        QString current_line = cursor.selectedText();
        Utility::TrimmedIndex trimmedIndex = Utility::StringTrimmedIndex(current_line);
        int indent_len = trimmedIndex.before;
        QString indent = current_line.left(indent_len);
        QString trim_before_cursor = Utility::trimmed(current_line.left(ori_pos), " ");

        QString insert_text = "";
        if (ori_pos <= line_start + indent_len) { // 光标位于缩进空白符位置
            insert_text = indent.left(ori_pos - line_start) + QChar(0x2029);
            insert_text += indent + current_line.right(current_line.size() - indent_len);
            cursor.beginEditBlock();
            cursor.insertText(insert_text);
            cursor.endEditBlock();
            cursor.setPosition(ori_pos * 2 - line_start + 1);
            setTextCursor(cursor);
        }
        else if (trim_before_cursor.endsWith('{') || trim_before_cursor.endsWith('}')) {
            if (trim_before_cursor.endsWith("{")) {
                indent += "  ";
                insert_text = current_line.left(ori_pos - line_start) + QChar(0x2029);
                insert_text += indent + Utility::trimmed(current_line.right(line_start + current_line.size() - ori_pos), " ");
                cursor.beginEditBlock();
                cursor.insertText(insert_text);
                cursor.endEditBlock();
                cursor.setPosition(ori_pos + indent.size() + 1);
                setTextCursor(cursor);
            }
            else if (trim_before_cursor.endsWith("}")) {
                cursor.select(QTextCursor::Document);
                const QString& source = cursor.selectedText();
                int indexOfLineWithRBrace = getIndexOfLineWithRBrace(source, line_start + indent_len + trim_before_cursor.size() - 1, true);
                if (indexOfLineWithRBrace < 0) {
                    return false;
                }
                cursor.setPosition(indexOfLineWithRBrace);
                cursor.select(QTextCursor::LineUnderCursor);
                QString current_line = cursor.selectedText();
                int indent_len = Utility::StringTrimmedIndex(current_line).before;
                QString indent = current_line.left(indent_len);
                cursor.setPosition(ori_pos);
                QString insert_text = QChar(0x2029) + indent;
                cursor.beginEditBlock();
                cursor.insertText(insert_text);
                cursor.endEditBlock();
            }
        }
        else {
            cursor.setPosition(ori_pos);
            insert_text = QChar(0x2029) + indent;
            cursor.beginEditBlock();
            cursor.insertText(insert_text);
            cursor.endEditBlock();
        }
        return true;
    }
    else if (event->key() == Qt::Key_BraceRight) { // 检测到右花括号 "}" 输入
        QTextCursor cursor = textCursor();
        int ori_pos = cursor.position();
        cursor.select(QTextCursor::Document);
        const QString& source = cursor.selectedText();
        int indexOfLineWithRBrace = getIndexOfLineWithRBrace(source, ori_pos);
        if (indexOfLineWithRBrace < 0) {
            return false;
        }
        cursor.setPosition(indexOfLineWithRBrace);
        cursor.select(QTextCursor::LineUnderCursor);
        QString current_line = cursor.selectedText();
        int indent_len = Utility::StringTrimmedIndex(current_line).before;
        QString indent = current_line.left(indent_len);
        cursor.setPosition(ori_pos);
        cursor.select(QTextCursor::LineUnderCursor);
        QString insert_text = indent + "}";
        cursor.beginEditBlock();
        cursor.insertText(insert_text);
        cursor.endEditBlock();
        return true;
    }
    return false;
}