#include <cstdlib>
#include <iostream>

#include <QGuiApplication>
#include <QTextCursor>

#include "Widgets/TextDocument.h"

namespace
{

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

}

int main(int argc, char* argv[])
{
    QGuiApplication application(argc, argv);

    TextDocument document;
    document.setPlainText(QStringLiteral("alpha"));

    QTextCursor userEdit(&document);
    userEdit.movePosition(QTextCursor::End);
    userEdit.insertText(QStringLiteral(" beta"));
    document.setModified(false);
    Require(document.isUndoAvailable(), "pre-existing undo history should exist");

    const QString beforeBatch = document.toText();
    const QString batchText = QStringLiteral("batch\u00a0result\nsecond line");
    document.replaceTextAsSingleUndoStep(batchText);
    Require(document.toText() == batchText, "undoable whole-document replacement mismatch");
    Require(document.isUndoAvailable(), "batch replacement should enable undo");

    document.undo();
    Require(document.toText() == beforeBatch, "one undo should restore the pre-batch text");
    Require(!document.isModified(), "undoing the batch should restore the clean state");

    document.undo();
    Require(document.toText() == QStringLiteral("alpha"),
            "batch replacement must preserve earlier undo history");

    document.redo();
    document.redo();
    Require(document.toText() == batchText, "redo should restore the batch text");

    document.replaceTextAsSingleUndoStep(batchText);
    document.undo();
    Require(document.toText() == beforeBatch,
            "same-text replacement must not add an empty undo command");

    std::cout << "text document undo tests passed\n";
    return 0;
}
