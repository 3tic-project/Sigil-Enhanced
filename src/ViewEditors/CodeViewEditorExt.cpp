#include <QMenu>
#include <QMimeData>
#include <QClipboard>
#include <QGuiApplication>
#include <QRegExp>
#include <QRegularExpression>
#include <QFileInfo>
#include <QBuffer>

#include "ViewEditors/CodeViewEditor.h"
#include "sigil_constants.h"
#include "MainUI/MainWindow.h"
#include "MainUI/BookBrowser.h"
#include "Tabs/ContentTab.h"
#include "BookManipulation/FolderKeeper.h"

#define TagInfo TagLister::TagInfo

static const QString TAG_NAME_SEARCH = "<\\s*([^\\s>]+)";
static const QList<Qt::Key> SYMBOLS_TO_DETECT_IN_ALL({ Qt::Key_Tab,Qt::Key_Backtab,Qt::Key_Backspace,Qt::Key_Home,Qt::Key_End,Qt::Key_Left,Qt::Key_Up,Qt::Key_Right,Qt::Key_Down });
static const QList<Qt::Key> SYMBOLS_TO_DETECT_IN_CSSVIEW({ Qt::Key_Return,Qt::Key_Enter,Qt::Key_BraceLeft,Qt::Key_BraceRight });
static const QList<Qt::Key> SYMBOLS_TO_DETECT_IN_HTMLVIEW({ Qt::Key_Return,Qt::Key_Enter, Qt::Key_Slash,Qt::Key_Greater });
static const QList<Qt::Key> SYMBOLS_TO_DETECT_IN_QUICKSWITCH({ Qt::Key_Left,Qt::Key_Up,Qt::Key_Right,Qt::Key_Down });

//-------------------------------------------------------------- modified: keyborad event --------------------------------------------------------------
inline void CodeViewEditor::insertTextAtCursor(QString text, QTextCursor cursor) {
    cursor.beginEditBlock();
    cursor.insertText(text);
    cursor.endEditBlock();
}

void CodeViewEditor::keyPressEvent(QKeyEvent* event)
{
    if (m_hightype == CodeViewEditor::Highlight_XHTML) {
        if (VisibleCompleterEvent(event)) return;
        if (CommonKeyPressEvent(event)) return;
        if (HtmlViewKeyPressEvent(event)) return;
        if (quickSwitchOfCursor(event)) return;
        if (CodeCompleterEvent(event)) return;
    }
    else if (m_hightype == CodeViewEditor::Highlight_CSS) {
        if (VisibleCompleterEvent(event)) return;
        if (CommonKeyPressEvent(event)) return;
        if (CssViewKeyPressEvent(event)) return;
        if (CodeCompleterEvent(event)) return;
    }
    else {
        if (CommonKeyPressEvent(event)) return;
    }
    QPlainTextEdit::keyPressEvent(event);
}
bool CodeViewEditor::CommonKeyPressEvent(QKeyEvent* event) {
    if (SYMBOLS_TO_DETECT_IN_ALL.contains(event->key())) {

        QTextCursor cursor = textCursor();

        int key = event->key();
        Qt::KeyboardModifiers kmod = event->modifiers();

        if (key == Qt::Key_Backspace) { // Key_Backspace
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

        if (key == Qt::Key_Home || key == Qt::Key_End || key == Qt::Key_Left || key == Qt::Key_Right) {
            if (((Qt::AltModifier | Qt::GroupSwitchModifier | Qt::MetaModifier | Qt::KeypadModifier) & kmod) != 0)
                return false;
            if (key == Qt::Key_End || ((kmod & Qt::ControlModifier) != 0 && key == Qt::Key_Right)) {
                if ((kmod == Qt::NoModifier && key == Qt::Key_End) || (kmod == Qt::ControlModifier && key == Qt::Key_Right)) {
                    cursor.movePosition(QTextCursor::EndOfLine, QTextCursor::MoveAnchor);
                }
                else if ((kmod == Qt::ShiftModifier && key == Qt::Key_End) ||
                    (kmod == (Qt::ShiftModifier | Qt::ControlModifier) && key == Qt::Key_Right)) {
                    cursor.movePosition(QTextCursor::EndOfLine, QTextCursor::KeepAnchor);
                }
                setTextCursor(cursor);
                return true;
            }

            if (key == Qt::Key_Home || ((kmod & Qt::ControlModifier) != 0 && key == Qt::Key_Left)) {
                int ori_pos = cursor.position(),
                    ori_selecionBegin = -1;
                bool hasSelection = false;
                if (cursor.hasSelection()) {
                    ori_selecionBegin = cursor.selectionStart() == ori_pos ? cursor.selectionEnd() : cursor.selectionStart();
                    hasSelection = true;
                }

                cursor.select(QTextCursor::LineUnderCursor);
                ushort indent_ofsset = Utility::StringTrimmedIndex(cursor.selectedText()).before;
                if ((kmod == Qt::NoModifier && key == Qt::Key_Home) || ((kmod & Qt::ShiftModifier) == 0 && Qt::Key_Left)) {
                    if (ori_pos != cursor.selectionStart() + indent_ofsset) {
                        cursor.setPosition(cursor.selectionStart() + indent_ofsset);
                    }
                    else {
                        cursor.setPosition(cursor.selectionStart());
                    }
                }
                else if ((kmod == Qt::SHIFT && key == Qt::Key_Home) ||
                    (kmod == (Qt::ShiftModifier | Qt::ControlModifier) && key == Qt::Key_Left)) {
                    int line_start = cursor.selectionStart();
                    if (hasSelection) {
                        cursor.setPosition(ori_selecionBegin);
                    }
                    else {
                        cursor.setPosition(ori_pos);
                    }
                    if (ori_pos != line_start + indent_ofsset) {
                        cursor.setPosition(line_start + indent_ofsset, QTextCursor::KeepAnchor);
                    }
                    else {
                        cursor.setPosition(line_start, QTextCursor::KeepAnchor);
                    }
                }
                setTextCursor(cursor);
                return true;
            }
            return false;
        }

        if (key == Qt::Key_Up || key == Qt::Key_Down) {
            if (kmod == Qt::ControlModifier || kmod == (Qt::ControlModifier | Qt::ShiftModifier)) {
                event->setModifiers(kmod & Qt::ShiftModifier);
                QPlainTextEdit::keyPressEvent(event);
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
                if (key == Qt::Key_Tab) { // Tab
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
                insertTextAtCursor(new_text.left(new_text.length() - 1), cursor);
                cursor.setPosition(selection_start);
                cursor.setPosition(ori_end + e_offset, QTextCursor::KeepAnchor);
                setTextCursor(cursor);
            }
            else if (ori_end < cursor.selectionEnd()) {
                insertTextAtCursor("  ", textCursor());
            }
            return true;
        }
        else if (key == Qt::Key_Tab) { // Key_Tab, cursor has no selection
            insertTextAtCursor("  ", cursor);
            return true;
        }
        else if (key == Qt::Key_Backtab) { //// Key_BackTab, cursor has no selection
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
bool CodeViewEditor::HtmlViewKeyPressEvent(QKeyEvent* event)
{
    if (SYMBOLS_TO_DETECT_IN_CSSVIEW.contains(event->key())) {
        m_completeParser->parseCursorPosType();
        CodeCompleterParser::PosType postype = m_completeParser->getState().postype;
        if ((postype & (CodeCompleterParser::CSS_SELECTOR |
            CodeCompleterParser::CSS_ATTR |
            CodeCompleterParser::CSS_COMMENT |
            CodeCompleterParser::CSS_VALUE)) != 0) {
            return CssViewKeyPressEvent(event);
        }
    }

    if (SYMBOLS_TO_DETECT_IN_HTMLVIEW.contains(event->key())) {
        QTextCursor cursor = textCursor();
        int ori_pos = cursor.position();

        if (event->key() == Qt::Key_Greater) { // Key_Greater ">"
            m_completeParser->parseCursorPosType();
            CodeCompleterParser::PosState state = m_completeParser->getState();
            if (!(state.postype ^ CodeCompleterParser::HTML_ATTR) || !(state.postype ^ CodeCompleterParser::HTML_TAG)) {
                QString tagname = state.keyword;
                if (tagname.isEmpty())
                    return false;
                insertTextAtCursor(QString("></%1>").arg(tagname), cursor);
                ushort offset = tagname.size() + 3;
                cursor.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, offset);
                setTextCursor(cursor);
                if (m_completeParser->isVisible())
                    m_completeParser->hide();
                return true;
            }
            return false;
        }

        if (event->key() == Qt::Key_Slash) { // Detect Key_Slash "/"
            cursor.movePosition(QTextCursor::PreviousCharacter, QTextCursor::KeepAnchor);
            if (cursor.selectedText() == QString('<')) {
                if (!IsInsertClosingTagAllowed()) {
                    cursor.setPosition(ori_pos);
                    return false; // close tag failed
                }
                const QStringList unmatched_tags = GetUnmatchedTagsForBlock(ori_pos);
                if (unmatched_tags.isEmpty()) {
                    cursor.setPosition(ori_pos);
                    return false; // close tag failed
                }
                QString tag = unmatched_tags.last();
                QRegularExpression tag_name_search(TAG_NAME_SEARCH);
                QRegularExpressionMatch mo = tag_name_search.match(tag);
                int tag_name_index = mo.capturedStart();

                if (tag_name_index >= 0) {
                    QString closing_tag = "</" % mo.captured(1) % ">";
                    insertTextAtCursor(closing_tag, cursor); // close tag
                }
                return true;
            }
            return false;
        }
        else { // Detect Key_Return or Key_Enter
            auto getIndexOfMatchingTag = [](const QString& source, int start_pos)->int {
                int i = start_pos,
                    RAngleBracketPos = -1;
                int closeTagNum = 1;

                while (i > 1) {
                    QChar pre_ch = source.at(i - 1);
                    if (pre_ch == '>') {
                        RAngleBracketPos = i;
                    }
                    else if (pre_ch == '<' && RAngleBracketPos > -1) {
                        QString tagString = source.mid(i - 1, RAngleBracketPos - i + 1);
                        if (tagString.endsWith("/>")) {
                        }
                        else if (tagString.startsWith("</")) {
                            ++closeTagNum;
                        }
                        else {
                            --closeTagNum;
                        }
                        RAngleBracketPos = -1;
                    }
                    if (closeTagNum == 0) {
                        --i;
                        break;
                    }
                    --i;
                }
                return i;
            };
            // The para SelectionType must be "BlockUnderCursor", not "LineUnderCurosr".
            cursor.select(QTextCursor::BlockUnderCursor);
            // cursor has noo selection means the block where cursor at has no any characters.
            if (!cursor.hasSelection()) {
                return false;
            }
            int line_start;
            QString current_line;
            QString insert_text = "";
            if (cursor.selectionStart() > 0) {
                line_start = cursor.selectionStart() + 1;
                /* When the cursor SelectionType is "BlockUnderCursor", the first character of selected text
                 * is a linefeed with Code Points U+2029 unless the cursor is in the first block of document.
                 * We should take the linefeed off the selected text.
                 */
                current_line = cursor.selectedText().mid(1);
            }
            else {
                line_start = cursor.selectionStart();
                current_line = cursor.selectedText();
            }
            if (current_line.mid(ori_pos - line_start, 2) == "</") {
                int j = getIndexOfMatchingTag(toPlainText(), ori_pos);
                if (j >= line_start) {
                    int indent_len = Utility::StringTrimmedIndex(current_line).before;
                    QString indent = current_line.left(indent_len);
                    QString default_indent = QString(2, ' ');
                    cursor = textCursor();
                    insert_text = "\n" + indent + default_indent + "\n" + indent;
                    insertTextAtCursor(insert_text, cursor);
                    cursor.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, indent.size() + 1);
                    setTextCursor(cursor);
                }
                else {
                    cursor.setPosition(j);
                    cursor.select(QTextCursor::BlockUnderCursor);
                    QString new_line = cursor.selectedText().startsWith(QChar(0x2029)) ? cursor.selectedText().mid(1) : cursor.selectedText();
                    int indent_len = Utility::StringTrimmedIndex(new_line).before;
                    QString indent = new_line.left(indent_len);
                    cursor = textCursor();
                    insertTextAtCursor(QChar(0x2029) + indent, cursor);
                    setTextCursor(cursor);
                }
            }
            else {
                int indent_len = Utility::StringTrimmedIndex(current_line).before;
                QString indent = current_line.left(indent_len);
                if (ori_pos <= line_start + indent_len) { // Cursor at position end of indentation.
                    cursor = textCursor();
                    insert_text = QChar(0x2029) + QString(ori_pos - line_start, ' ');
                    insertTextAtCursor(insert_text, cursor);
                    cursor.setPosition(ori_pos * 2 - line_start + 1);
                    setTextCursor(cursor);
                }
                else {
                    cursor = textCursor();
                    insert_text = QChar(0x2029) + indent;
                    insertTextAtCursor(insert_text, cursor);
                }
            }
            return true;
        }
    }
    return false;
}
bool CodeViewEditor::CssViewKeyPressEvent(QKeyEvent* event)
{
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

    if (SYMBOLS_TO_DETECT_IN_CSSVIEW.contains(event->key())) {
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
                    //cursor.select(QTextCursor::Document);
                    //const QString& source = cursor.selectedText();
                    const QString& source = toPlainText();
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
bool CodeViewEditor::VisibleCompleterEvent(QKeyEvent* event) {
    if (m_completeParser->isVisible()) {
        if (event->key() == Qt::Key_Tab) {
            m_completeParser->insertSelectedCompletion();
            return true;
        }
        else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            m_completeParser->hide();
            return true;
        }
        else if (event->modifiers() != Qt::NoModifier && event->modifiers() != Qt::ShiftModifier) {
            m_completeParser->hide();
        }
        else if (event->modifiers() == Qt::ShiftModifier && event->key() != Qt::Key_Shift && event->text().isEmpty()) {
            m_completeParser->hide();
        }
        else if (event->modifiers() == Qt::NoModifier && event->text().isEmpty()) {
            m_completeParser->hide();
        }
    }
    return false;
}
bool CodeViewEditor::CodeCompleterEvent(QKeyEvent* event)
{
    if (!m_completeParser->isVisible() && event->key() == Qt::Key_Backspace) {
        return false;
    }

    if (event->text().isEmpty())
        return false;
    if (event->modifiers() != Qt::NoModifier && event->modifiers() != Qt::ShiftModifier)
        return false;

    QPlainTextEdit::keyPressEvent(event);
    m_completeParser->popupCompleter();
    return true;
}
//----------------------------------------------------------------------------------------------------------------------------------------------------------------

bool CodeViewEditor::quickSwitchOfCursor(QKeyEvent* e) {
    if (!SYMBOLS_TO_DETECT_IN_QUICKSWITCH.contains(e->key()))
        return false;
    if (e->modifiers() != Qt::AltModifier && e->modifiers() != (Qt::AltModifier | Qt::ShiftModifier))
        return false;

    int key = e->key();
    Qt::KeyboardModifiers mod = e->modifiers();

    if (key == Qt::Key_Up || key == Qt::Key_Down) {
        e->setModifiers(mod & Qt::ShiftModifier);
        QPlainTextEdit::keyPressEvent(e);
        return true;
    }

    auto getTagBoundary = [](const QString& text, int pos, bool reverseDirection = false)->int {
        int Len = text.size();
        int RAngleBracketPos = -1;
        int insertPos = -1;
        if (reverseDirection) {
            int i = pos - 1;
            insertPos = 0;
            while (i > 0) {
                QChar pre_ch = text.at(i - 1);
                if (pre_ch == '>')
                    RAngleBracketPos = i;
                else if (pre_ch == '<')
                {
                    if (RAngleBracketPos < 0) {
                        insertPos = i - 1;
                    }
                    else {
                        insertPos = RAngleBracketPos;
                    }
                    break;
                }
                --i;
            }
        }
        else {
            int i = pos + 1;
            insertPos = Len;
            while (i < Len) {
                QChar ch = text.at(i);
                if (ch == '<') {
                    insertPos = i;
                    break;
                }
                else if (ch == '>') {
                    if (text.at(pos) == '<') {
                        insertPos = i + 1;
                        break;
                    }
                    int j = pos;
                    while (j > 0) {
                        if (text.at(j - 1) == '>')
                            break;
                        if (text.at(j - 1) == '<') {
                            insertPos = i + 1;
                            goto loop_end;
                        }
                        --j;
                    }
                }
                ++i;
            }
        }
    loop_end:
        return insertPos;
    };
    QTextCursor tc = textCursor();
    int ori_pos = tc.position();
    bool hasShift = mod == (Qt::AltModifier | Qt::ShiftModifier);
    QString text = toPlainText();
    QTextCursor::MoveMode mode = hasShift ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor;
    bool reverseDir = key == Qt::Key_Left ? true : false;

    int next_pos = getTagBoundary(text, ori_pos, reverseDir);
    if (next_pos != ori_pos) {
        tc.setPosition(next_pos, mode);
        setTextCursor(tc);
    }

    return true;
}

// ---------------------------------- modified: SplitBlockOrAddBreak ---------------------------------
// 该函数的作用是，根据光标在行中的位置插入空行或者分割段落（块元素）。
// 如果光标处于行尾，则向下插入空行，如果光标处于行头缩进位置，则向前插入空行。
// 如果光标处于标签内部，则自动判断该补充多少闭合标签并从该处分割段落。
void CodeViewEditor::SplitBlockOrAddBreak()
{
    QTextCursor cursor = textCursor();
    QString text = toPlainText();
    int ori_pos = cursor.position();
    MaybeRegenerateTagList();
    if (!IsPositionInBody(ori_pos)) return;

    cursor.select(QTextCursor::BlockUnderCursor);
    QString insert_text = "";
    QString selected_text = cursor.selectedText().startsWith(QChar(0x2029)) ? cursor.selectedText().mid(1) : cursor.selectedText();
    int line_start = cursor.selectedText().startsWith(QChar(0x2029)) ? cursor.selectionStart() + 1 : cursor.selectionStart();
    int line_end = cursor.selectionEnd();
    cursor.setPosition(ori_pos);

    Utility::TrimmedIndex trimmed_pos = Utility::StringTrimmedIndex(selected_text);
    int indent_length = trimmed_pos.before,
        nonblank_end = trimmed_pos.after;
    // 光标位于行末端或行尾空白字符处
    if (cursor.atBlockEnd() || ori_pos >= line_start + nonblank_end) {
        if (!cursor.atBlockEnd()) cursor.movePosition(QTextCursor::EndOfBlock);
        if (selected_text.trimmed().isEmpty()) { // trimmed()可除去字符串首尾端的空格和制表符等空白字符。
            insert_text = "<p><br/></p>";
        }
        else {
            QString indent = selected_text.left(indent_length);
            insert_text = QChar(0x2029) % indent % "<p><br/></p>";
        }
    }
    // 光标位于缩进空白字符处
    else if (ori_pos <= line_start + indent_length) {
        QString indent = selected_text.left(indent_length);
        cursor.setPosition(line_start);
        insert_text = indent % "<p><br/></p>" % QChar(0x2029);
    }
    // 其他情况，光标位于内容上
    else {
        QStringList openTags_foundList, // 储存查找过程中不被闭合元素匹配的元素；
            closeTags_foundList; // 储存查找过程中的闭合元素；
        // i 是标签在页面中的序号
        int i = m_TagList.findLastTagOnOrBefore(ori_pos);
        TagInfo ti = m_TagList.at(i);
        // 光标恰好处于标签尖括号内部
        if (ori_pos > ti.pos && ori_pos < ti.pos + ti.len) {
            return;
        }
        else if (ti.ttype == "end" && ori_pos == ti.pos) {
            --i;
        }
        // 向前查找标签，直到找到第一个不被闭合节点匹配的块元素标签。
        //（不被闭合节点匹配的意思是，标签在前向查找的时候，如果前方有成对的标签，必然是先找到闭合标签，
        //  如果找不到闭合标签说明闭合标签在光标后方，那么分割时应该对该类标签进行补偿。）
        while (i > 0) {
            ti = m_TagList.at(i);

            if (ti.tname == "body") {
                break;
            }
            if (ti.ttype == "end") {
                closeTags_foundList.append(ti.tname);
            }
            else if (ti.ttype == "begin") {
                if (closeTags_foundList.contains(ti.tname)) {
                    closeTags_foundList.removeOne(ti.tname);
                }
                else {
                    QString tagStr = text.mid(ti.pos + 1, ti.len - 2);
                    openTags_foundList.append(tagStr);
                }
            }
            //找到目标块元素
            if (!openTags_foundList.isEmpty() && BLOCK_LEVEL_TAGS.contains(openTags_foundList.last())) {
                break;
            }
            --i;
        }
        if (openTags_foundList.size() > 0) {
            QString last_para_text = "",
                next_para_text = "";
            QString indent = selected_text.left(indent_length);
            foreach(QString tag, openTags_foundList) {
                QString tagname = tag.split(" ").first();
                last_para_text.append("</" + tagname + ">");
                next_para_text.prepend("<" + tag + ">");
            }
            insert_text = last_para_text + QChar(0x2029) + indent + next_para_text;
        }
    }
    if (!insert_text.isEmpty()) {
        cursor.beginEditBlock();
        cursor.insertText(insert_text);
        cursor.endEditBlock();
    }
}
// -------------------------------------------------------------------------------------------------------------

//------------------------------------------------ modified: Add labels for block elements ( ctrl + 1 ~ ctrl + 8 )------------------------------------------------
void CodeViewEditor::FormatBlock_multiline(const QString& element_name, bool preserve_attributes)
{
    if (element_name.isEmpty()) {
        return;
    }

    MaybeRegenerateTagList();
    QString text = m_TagList.getSource();
    TagInfo bodyOpenTag = m_TagList.at(m_TagList.findBodyOpenTag());
    TagInfo bodyCloseTag = m_TagList.at(m_TagList.findBodyCloseTag());
    // 找不到body节点
    if (bodyOpenTag.pos == -1 || bodyCloseTag.pos == -1) {
        return;
    }
    // 选择范围不在body节点内部
    if (textCursor().selectionEnd() <= bodyOpenTag.pos + bodyOpenTag.len || textCursor().selectionStart() >= bodyCloseTag.pos) {
        return;
    }
    QTextCursor cursor = textCursor();
    bool hasSelection = cursor.hasSelection();
    cursor.setPosition(textCursor().selectionEnd(), QTextCursor::MoveAnchor);
    cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::MoveAnchor);
    cursor.setPosition(textCursor().selectionStart(), QTextCursor::KeepAnchor);
    cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
    if (!textCursor().selectedText().startsWith(QChar(0x2029)) && cursor.selectedText().startsWith(QChar(0x2029))) {
        cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 1);
    }

    // 选择范围限制在body节点内部
    int body_inner_start = text[bodyOpenTag.pos + bodyOpenTag.len] == QChar(0xA) ? bodyOpenTag.pos + bodyOpenTag.len + 1 : bodyOpenTag.pos + bodyOpenTag.len;
    int body_inner_end = text[bodyCloseTag.pos - 1] == QChar(0xA) ? bodyCloseTag.pos - 1 : bodyCloseTag.pos;
    int start_pos = cursor.selectionStart() > body_inner_start ? cursor.selectionStart() : body_inner_start;
    int end_pos = cursor.selectionEnd() < body_inner_end ? cursor.selectionEnd() : body_inner_end;
    if (start_pos != cursor.selectionStart() || end_pos != cursor.selectionEnd()) {
        cursor.setPosition(start_pos, QTextCursor::MoveAnchor);
        cursor.setPosition(end_pos, QTextCursor::KeepAnchor);
    }

    QString selected_text = cursor.selectedText();
    QStringList splited_list = selected_text.split(QChar(0x2029)); // 被光标选中后的文本，换行符(0xA)会转化为段落分隔符(0x2029)

    QStringList newText_list = QStringList();
    foreach(QString line_text, splited_list) {
        Utility::TrimmedIndex trimmed_pos = Utility::StringTrimmedIndex(line_text); // trimmed_pos 储存字符串前端非空白字符起点和后端非空白字符截止点。
        if (trimmed_pos.before == trimmed_pos.after) {
            QString pre_blank = line_text.left(trimmed_pos.before);
            QString open_element = "<" % element_name % ">",
                close_element = "</" % element_name % ">",
                inner_content = "";
            if (splited_list.size() > 1 && element_name == "p") {
                inner_content = "<br/>";
            }
            newText_list.append(pre_blank % open_element % inner_content % close_element);
        }
        else {
            QString pre_blank = line_text.left(trimmed_pos.before);
            QString post_blank = line_text.right(line_text.size() - trimmed_pos.after);
            QString trimmed_text = line_text.mid(trimmed_pos.before, trimmed_pos.after - trimmed_pos.before);
            //
            bool wrapIt = true;
            // 接下来是侦测它的是否节点以及节点类型。
            QRegExp re("^<([a-z0-9]+)( +[^>]*)?>(.*)</\\1>$", Qt::CaseInsensitive); // 该表达式用于匹配字符串首位是否节点特征，同属捕获标签，属性和内容。
            int index = re.indexIn(trimmed_text);
            if (index > -1) {
                QString tag = re.cap(1),
                    attr = re.cap(2),
                    inner = re.cap(3);
                if (BLOCK_LEVEL_TAGS.contains(tag, Qt::CaseInsensitive)) {
                    wrapIt = false;
                    QString open_element = "<" % element_name % attr % ">",
                        close_element = "</" % element_name % ">";
                    newText_list.append(pre_blank % open_element % inner % close_element % post_blank);
                }
            }
            if (wrapIt) {
                QString open_element = "<" % element_name % ">",
                    close_element = "</" % element_name % ">";
                newText_list.append(pre_blank % open_element % trimmed_text % close_element % post_blank);
            }
        }
    }
    QString replace_text = newText_list.join(QChar(0x2029));
    cursor.beginEditBlock();
    cursor.insertText(replace_text);
    cursor.endEditBlock();
    // 如果光标没有选择范围，则光标移动到节点内部。
    if (!hasSelection) {
        int indent_length = Utility::StringTrimmedIndex(replace_text).before;
        int new_pos = start_pos + indent_length + QString("<" % element_name % ">").length();
        cursor.setPosition(new_pos);
        setTextCursor(cursor);
    }
    return;
}
//-------------------------------------------------------------------------------------------------------------------------------------------


//---------------------------------------- modified: paste event:  when you do a actionPaste in the codeview editor, this function will be called.-----------------------------------------------------
void CodeViewEditor::insertFromMimeData(const QMimeData* source) {
    if (m_hightype == CodeViewEditor::Highlight_XHTML) {
        if (HtmlViewPasteEvent(source)) return;
    }
    else if (m_hightype == CodeViewEditor::Highlight_CSS) {
        if (CssViewPasteEvent(source)) return;
    }
    CommonPasteEvent(source);
}

void CodeViewEditor::CommonPasteEvent(const QMimeData* source) {
    if (!source->hasText())
        return;
    QString src_txt = toPlainText();
    QTextCursor cursor = textCursor();

    int insert_pos = cursor.hasSelection() ? cursor.selectionStart() : cursor.position();
    int i = insert_pos;
    // Skipping the space chars, when the indicator i point a non-space char which is '\n', it means that i has reached the start of line, if not '\n', it is not the start of line.
    // If indicator i could skip all space chars to reach the start of line, it means the cursor is preceded by an indentation.
    while (i >= 1 && src_txt.at(i - 1) == QChar(' ')) --i;
    if (i > 0 && src_txt.at(i - 1) == QChar('\n')) {
        QString indentation = src_txt.mid(i, insert_pos - i);
        QString paste_txt = source->text();
        Utility::TrimmedIndex trimmed_pos = Utility::StringTrimmedIndex(paste_txt);
        QString paste_indent = paste_txt.left(trimmed_pos.before);
        if (indentation == paste_indent) {
            paste_txt = paste_txt.mid(trimmed_pos.before);
            textCursor().insertText(paste_txt);
            return;
        }
    }
    QPlainTextEdit::insertFromMimeData(source);
}
bool CodeViewEditor::HtmlViewPasteEvent(const QMimeData* s) {
    bool insert_on_css = false;
    if (s->hasImage() || s->hasUrls()) {
        m_completeParser->parseCursorPosType();
        CodeCompleterParser::PosType postype = m_completeParser->getState().postype;
        if ((postype & (CodeCompleterParser::HTML_TEXT |
            CodeCompleterParser::CSS_SELECTOR |
            CodeCompleterParser::CSS_ATTR |
            CodeCompleterParser::CSS_VALUE)) == 0) {
            return true;
        }
        else if (postype != CodeCompleterParser::HTML_TEXT) {
            insert_on_css = true;
        }
    }
    if (s->hasImage()) {
        if (s->hasUrls() && s->urls().size() == 1) {
            return insertImagesFromUrls(s->urls(), insert_on_css);
        }
        QBuffer buf;
        QImage img = qvariant_cast<QImage>(s->imageData());
        img.save(&buf, "PNG");
        return insertImageFromByteData(buf.buffer(), insert_on_css);
    }
    else if (s->hasUrls()) {
        return insertImagesFromUrls(s->urls(), insert_on_css);
    }
    return false;
}
bool CodeViewEditor::CssViewPasteEvent(const QMimeData* s) {
    if (s->hasImage()) {
        if (s->hasUrls() && s->urls().size() == 1) {
            return insertImagesFromUrls(s->urls(), true);
        }
        QBuffer buf;
        QImage img = qvariant_cast<QImage>(s->imageData());
        img.save(&buf, "PNG");
        return insertImageFromByteData(buf.buffer(), true);
    }
    else if (s->hasUrls()) {
        return insertImagesFromUrls(s->urls(), true);
    }
    return false;
}
bool CodeViewEditor::insertImageFromByteData(const QByteArray& data, bool insert_on_css) {
    QString filename = "Images0001.png";
    QWidget* mainwin_w = Utility::GetMainWindow();
    MainWindow* mainwin = qobject_cast<MainWindow*>(mainwin_w);
    QString added_bookpath = mainwin->GetBookBrowser()->AddImageFromClipboard(data, filename);
    Resource* res = mainwin->GetCurrentContentTab()->GetLoadedResource();
    QString insert_text;
    QString new_filename = QFileInfo(added_bookpath).baseName();
    if (new_filename.contains("."))
        new_filename = new_filename.left(new_filename.lastIndexOf("."));
    QString relative_path = Utility::relativePath(added_bookpath, res->GetFolder());
    if (!insert_on_css) {
        insert_text += QString("<img alt=\"%1\" src=\"%2\"/>").arg(new_filename).arg(relative_path);
    }
    else {
        insert_text += QString("url(\"%1\") ").arg(relative_path);
    }
    if (!insert_text.isEmpty()) {
        if (insert_on_css)
            insert_text = insert_text.left(insert_text.size() - 1);
        InsertText(insert_text);
    }
    return true;
}

bool CodeViewEditor::insertImagesFromUrls(const QList<QUrl>& urls, bool insert_on_css) {
    QStringList imgpaths;
    QStringList added_bookpaths;
    foreach(QUrl url, urls) {
        QString filepath = url.toLocalFile();
        if (!IMAGE_EXTENSIONS.contains(QFileInfo(filepath).suffix().toLower()))
            return false;
        imgpaths << filepath;
    }
    QWidget* mainwin_w = Utility::GetMainWindow();
    MainWindow* mainwin = qobject_cast<MainWindow*>(mainwin_w);
    added_bookpaths = mainwin->GetBookBrowser()->AddImagesFromFilePaths(imgpaths);
    Resource* res = mainwin->GetCurrentContentTab()->GetLoadedResource();
    QString insert_text;
    foreach(QString added_bookpath, added_bookpaths) {
        QString filename = QFileInfo(added_bookpath).baseName();
        if (filename.contains("."))
            filename = filename.left(filename.lastIndexOf("."));
        QString relative_path = Utility::relativePath(added_bookpath, res->GetFolder());
        if (!insert_on_css) {
            insert_text += QString("<img alt=\"%1\" src=\"%2\"/>").arg(filename).arg(relative_path);
        }
        else {
            insert_text += QString("url(\"%1\"),").arg(relative_path);
        }
    }
    if (!insert_text.isEmpty()) {
        if (insert_on_css)
            insert_text = insert_text.left(insert_text.size() - 1);
        InsertText(insert_text);
    }
    return true;
}

//-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

//---------------- modified: AddPasteRichText: add an action of pasting rich text to right click menu within codeview editor.------------------
void CodeViewEditor::AddPasteRichText(QMenu* menu)
{
    bool sucess = false;

    QObject* mw = Utility::GetMainWindow();
    QAction* action = mw->findChild<QAction*>("actionPasteRichText");
    if (action == NULL) {
        action = new QAction(tr("Paste Rich Text"), menu);
    }
#ifdef Q_OS_MAC
    action = new QAction(tr("Paste Rich Text"), menu);
#endif
    for (int i = 0; i < menu->actions().size(); ++i) {
        QAction* locatorAction = menu->actions().at(i);
        if (locatorAction->objectName() == "edit-paste" && i + 1 < menu->actions().size()) {
            menu->insertAction(menu->actions().at(i + 1), action);
            sucess = true;
        }
    }
    if (!sucess) {
        if (menu->actions().isEmpty()) {
            menu->addAction(action);
            sucess = true;
        }
        else {
            QAction* topAction = 0;
            if (topAction) {
                menu->insertAction(topAction, action);
                menu->insertSeparator(topAction);
            }
        }
    }
#ifdef Q_OS_MAC
    if (sucess) {
        connect(action, SIGNAL(triggered()), this, SLOT(PasteRichText()));
    }
#endif
}
void CodeViewEditor::PasteRichText() {
    // This function is to clean entities "&quot;" inside opentag,
    // which might make the Rich Text Engine of QTextDocument work error.

    auto cleanRichText = [](const QString& source)->QString {
        QString new_text("");
        QRegularExpression insideOfTag("<[^>]*>");
        QRegularExpressionMatchIterator miter = insideOfTag.globalMatch(source);
        int lastIndex = 0;
        while (miter.hasNext()) {
            QRegularExpressionMatch mo = miter.next();
            int start = mo.capturedStart(),
                end = mo.capturedEnd();
            QString cap = mo.captured();
            new_text += source.mid(lastIndex, start - lastIndex);
            new_text += cap.replace("&quot;", "\"");
            lastIndex = end;
        }
        return new_text;
    };

    QClipboard* cb = QGuiApplication::clipboard();
    const QMimeData* mimedata = cb->mimeData();
    QString text;

    if (mimedata->hasHtml()) {
        QTextDocument* qdoc = new QTextDocument();
        QString html = cleanRichText(mimedata->html());
        qdoc->setHtml(html); // This step is to organize the source code to reduce redundancy. 
        text = Utility::RegExpSub("[\\s\\S]*<body[^>]*>([\\s\\S]*)</body>[\\s\\S]*", "\\1", qdoc->toHtml());
        text = Utility::trimmed(text, " \n\r\t");
    }
    else if (mimedata->hasText()) {
        text = mimedata->text();
    }
    InsertText(text);
}
//--------------------------------------------------------------------------------------------------------------------------------------------------------

//----------------------------- modified: MergeNextElement --------------------------------------
void CodeViewEditor::MergeNextElement() {
    QTextCursor cursor = textCursor();
    QString text = toPlainText();
    int ori_pos = cursor.position();
    MaybeRegenerateTagList();
    if (!IsPositionInBody(ori_pos)) 
        return;

    int i = m_TagList.findLastTagOnOrBefore(ori_pos);

    TagInfo cur_ti, next_ti, next_two_ti, end_ti;

    cur_ti = m_TagList.at(i);
    while (cur_ti.ttype != "begin" && cur_ti.ttype != "end") {
        cur_ti = m_TagList.at(--i);
    }
    if (ori_pos == cur_ti.pos)
        cur_ti = m_TagList.at(--i);
    else if (ori_pos > cur_ti.pos && ori_pos < cur_ti.pos + cur_ti.len)
        return;

    QString source = m_TagList.getSource();
    if (cur_ti.ttype == "begin") { // The tag before TextCursor is a Openning Tag;
        while (QStringList({ "single","comment","cdata" }).contains(m_TagList.at(i + 1).ttype))
            ++i;
        next_ti = m_TagList.at(++i);
        next_two_ti = m_TagList.at(++i);
        if (next_ti.ttype == "end" && next_two_ti.ttype == "begin") {
            QString interval_text = source.mid(next_ti.pos + next_ti.len, next_two_ti.pos - next_ti.pos - next_ti.len);
            if (!interval_text.trimmed().isEmpty())
                return;
            while (true) {
                if (m_TagList.at(++i).open_pos == next_two_ti.pos) {
                    end_ti = m_TagList.at(i);
                    break;
                }
                else if (i == m_TagList.size())
                    return;
            }
            cursor.setPosition(next_ti.pos);
            cursor.setPosition(end_ti.pos + end_ti.len, QTextCursor::KeepAnchor);
            QString insert_text = source.mid(next_two_ti.pos + next_two_ti.len, end_ti.pos - next_two_ti.pos - next_two_ti.len);
            insert_text += "</" + next_ti.tname + ">";
            insertTextAtCursor(insert_text, cursor);
        }
    }
    else if (cur_ti.ttype == "end") { // The tag before TextCursor is a Closing Tag;
        while (m_TagList.at(i + 1).ttype == "comment")
            ++i;
        next_ti = m_TagList.at(++i);
        if (next_ti.ttype == "begin") {
            while (true) {
                if (m_TagList.at(++i).open_pos == next_ti.pos) {
                    end_ti = m_TagList.at(i);
                    break;
                }
                else if (i == m_TagList.size())
                    return;
            }
            cursor.setPosition(cur_ti.pos);
            cursor.setPosition(end_ti.pos + end_ti.len, QTextCursor::KeepAnchor);
            QString insert_text = source.mid(next_ti.pos + next_ti.len, end_ti.pos - next_ti.pos - next_ti.len);
            insert_text += "</" + cur_ti.tname + ">";
            insertTextAtCursor(insert_text, cursor);
        }
    }

}
//-----------------------------------------------------------------------------------------------