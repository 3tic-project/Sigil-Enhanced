#include <QMenu>
#include <QMimeData>
#include <QClipboard>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QFileInfo>
#include <QBuffer>

#include "ViewEditors/CodeViewEditor.h"
#include "sigil_constants.h"
#include "MainUI/MainWindow.h"
#include "MainUI/BookBrowser.h"
#include "Tabs/ContentTab.h"
#include "BookManipulation/FolderKeeper.h"

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