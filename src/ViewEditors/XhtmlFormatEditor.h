#pragma once
#ifndef XHTMLFORMATEDITOR_H
#define XHTMLFORMATEDITOR_H
#include <QtWidgets/QPlainTextEdit>
#include "Misc/CSSHighlighter.h"

//---------------------- modified: XHTML Fomat Configure ----------------------

class XhtmlFormatEditor : public QPlainTextEdit
{
public:
    XhtmlFormatEditor(QWidget* parent = 0);
    void setDefaultText();
protected:
    void keyPressEvent(QKeyEvent* event);
private:
    CSSHighlighter* m_Highlighter;
    inline void insertTextAtCursor(QString text, QTextCursor cursor);
    bool CommonKeyPressEvent(QKeyEvent* event);
    bool CssViewKeyPressEvent(QKeyEvent* event);
};

#endif // XHTMLFORMATEDITOR_H