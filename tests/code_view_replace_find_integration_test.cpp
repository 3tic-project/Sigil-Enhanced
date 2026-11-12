#include "EmbedPython/EmbeddedPython.h" // Python must precede Qt's slots macro.

#include <QApplication>
#include <QFocusEvent>
#include <QPlainTextDocumentLayout>
#include <QTextCursor>
#include <QWebEngineUrlScheme>

#include <stdexcept>

#include "MainUI/MainApplication.h"
#include "ViewEditors/CodeViewEditor.h"
#include "Widgets/TextDocument.h"

static void Require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

class TestCodeViewEditor : public CodeViewEditor
{
public:
    explicit TestCodeViewEditor(TextDocument &document,
                                HighlighterType highlighter = Highlight_XHTML)
        : CodeViewEditor(highlighter)
    {
        CustomSetDocument(document);
        resize(1000, 300);
        show();
    }
};

class TestTextDocument : public TextDocument
{
public:
    TestTextDocument()
    {
        setDocumentLayout(new QPlainTextDocumentLayout(this));
    }
};

static void Load(TestCodeViewEditor &editor, const QString &source)
{
    editor.setPlainText(source);
    editor.SetReformatHTMLEnabled(true);
    editor.document()->setModified(false);
    editor.document()->clearUndoRedoStacks();
    MainApplication::processEvents();
}

static void CollapseCursor(TestCodeViewEditor &editor)
{
    QTextCursor cursor = editor.textCursor();
    cursor.clearSelection();
    editor.setTextCursor(cursor);
    MainApplication::processEvents();
}

static void HighlightAndMoveFocus(TestCodeViewEditor &editor)
{
    // Focus changes repaint the current line, which also emits selectionChanged.
    QFocusEvent out(QEvent::FocusOut);
    QApplication::sendEvent(&editor, &out);
    QFocusEvent in(QEvent::FocusIn);
    QApplication::sendEvent(&editor, &in);
    MainApplication::processEvents();
}

static int ReplaceEveryMatch(TestCodeViewEditor &editor,
                             const QString &find,
                             const QString &replacement)
{
    int replacements = 0;
    while (editor.FindNext(find, Searchable::Direction_Down, false, false, true)) {
        Require(editor.ReplaceSelected(find, replacement, Searchable::Direction_Down, false),
                "A selected match was not replaced");
        ++replacements;
        Require(replacements < 50, "Replace/Find did not terminate");
    }
    return replacements;
}

static void TestOriginalReplaceFindDoesNotInsertWhenExhausted()
{
    const QString source = QStringLiteral("<html><body><p>a cat ate a rat</p></body></html>");
    TestTextDocument document;
    TestCodeViewEditor editor(document);
    Load(editor, source);

    const QString find = QStringLiteral("a");
    const QString replacement = QStringLiteral("b");
    const int replacements = ReplaceEveryMatch(editor, find, replacement);
    Require(replacements > 0, "No original-mode matches were replaced");
    Require(!editor.toPlainText().contains(QLatin1Char('a')),
            "Original-mode replace left unmatched find text");

    const QString exhausted = editor.toPlainText();
    Require(!editor.ReplaceSelected(find, replacement, Searchable::Direction_Down, false),
            "Replace/Find after the last match still replaced");
    Require(editor.toPlainText() == exhausted,
            "Replace/Find inserted replacement text with no remaining match");
    Require(!editor.FindNext(find, Searchable::Direction_Down, false, false, true),
            "Find after exhaustion still reported a match");
    Require(!editor.ReplaceSelected(find, replacement, Searchable::Direction_Down, false),
            "A second extra Replace/Find inserted replacement text");
    Require(editor.toPlainText() == exhausted,
            "A second extra Replace/Find changed the document");
}

static void TestStaleSelectionDoesNotReplace()
{
    const QString source = QStringLiteral("<html><body><p>alpha</p></body></html>");
    TestTextDocument document;
    TestCodeViewEditor editor(document);
    Load(editor, source);

    const QString find = QStringLiteral("alpha");
    Require(editor.FindNext(find, Searchable::Direction_Down, false, false, true),
            "The initial match was not selected");
    CollapseCursor(editor);
    const QString before = editor.toPlainText();
    Require(!editor.ReplaceSelected(find, QStringLiteral("beta"), Searchable::Direction_Down, false),
            "Replace used a stale match after the selection was cleared");
    Require(editor.toPlainText() == before,
            "Clearing the selection still allowed a replacement insert");
}

static void TestCssReplaceSurvivesHighlightAndFocus()
{
    const QString source = QStringLiteral("body { color: red; }\nh1 { color: red; }\n");
    TestTextDocument document;
    TestCodeViewEditor editor(document, CodeViewEditor::Highlight_CSS);
    Load(editor, source);

    const QString find = QStringLiteral("red");
    Require(editor.FindNext(find, Searchable::Direction_Down, false, false, true),
            "The first CSS color was not found");
    HighlightAndMoveFocus(editor);
    Require(editor.ReplaceSelected(find, QStringLiteral("blue"), Searchable::Direction_Down, false),
            "CSS replace did nothing after the line highlight and focus change");
    Require(editor.toPlainText().startsWith(QStringLiteral("body { color: blue; }")),
            "The highlighted CSS color was not replaced");
    Require(editor.toPlainText().contains(QStringLiteral("h1 { color: red; }")),
            "CSS replace changed a match that was not selected");

    CollapseCursor(editor);
    const QString after_collapse = editor.toPlainText();
    Require(!editor.ReplaceSelected(find, QStringLiteral("green"), Searchable::Direction_Down, false),
            "CSS replace ran after the match selection was cleared");
    Require(editor.toPlainText() == after_collapse,
            "Clearing the CSS selection still changed the stylesheet");
}

static void TestPlusCaptureReplaceKeepsGroup()
{
    const QString source = QStringLiteral("body { color: red; }\n");
    TestTextDocument document;
    TestCodeViewEditor editor(document, CodeViewEditor::Highlight_CSS);
    Load(editor, source);

    const QString find = QStringLiteral("(red)");
    Require(editor.FindNextPlus(QString(), find), "The CSS capture was not found");
    HighlightAndMoveFocus(editor);
    Require(editor.ReplaceSelectedPlus(find, QStringLiteral("\\1-blue"),
                                       Searchable::Direction_Down, false),
            "Plus capture replace was rejected");
    Require(editor.toPlainText().contains(QStringLiteral("color: red-blue;")),
            "Plus replace dropped the captured CSS text");
    Require(!editor.toPlainText().contains(QStringLiteral("\\1")),
            "Plus replace wrote the backreference literally");
}

static void TestRestartSearchFindsEarlierStyleMatch()
{
    const QString source = QStringLiteral("a { color: red; }\nb { color: red; }\n");
    TestTextDocument document;
    TestCodeViewEditor editor(document, CodeViewEditor::Highlight_CSS);
    Load(editor, source);

    const QString find = QStringLiteral("red");
    Require(editor.FindNextPlus(QString(), find), "The first style color was not found");
    Require(editor.FindNextPlus(QString(), find), "The second style color was not found");
    const int later_match = editor.textCursor().selectionStart();
    editor.RestartSearch(Searchable::Direction_Down);
    Require(editor.FindNextPlus(QString(), find),
            "Restarting the style search did not find the earlier color");
    Require(editor.textCursor().selectionStart() < later_match,
            "Restarting the style search skipped the earlier color");
    Require(editor.ReplaceSelectedPlus(find, QStringLiteral("blue"),
                                       Searchable::Direction_Down, false),
            "Replace did nothing for the style match found after restart");
    Require(editor.toPlainText().startsWith(QStringLiteral("a { color: blue; }")),
            "The earlier style color was not replaced after restart");
}

static bool CursorIsInView(TestCodeViewEditor &editor)
{
    MainApplication::processEvents();
    const QRect cursor = editor.cursorRect();
    return editor.viewport()->rect().intersects(cursor);
}

static void TestFindAndReplaceMoveToTheMatch()
{
    QString source = QStringLiteral("start\n");
    for (int i = 0; i < 80; ++i) source += QStringLiteral("line %1\n").arg(i);
    source += QStringLiteral("target-one\n");
    for (int i = 0; i < 20; ++i) source += QStringLiteral("gap %1\n").arg(i);
    source += QStringLiteral("target-two\nend\n");

    TestTextDocument document;
    TestCodeViewEditor editor(document, CodeViewEditor::Highlight_CSS);
    Load(editor, source);
    editor.setFocus();
    editor.ScrollToPosition(0);
    MainApplication::processEvents();
    QTextCursor distant(editor.document());
    distant.setPosition(source.indexOf(QStringLiteral("target-one")));
    Require(!editor.viewport()->rect().intersects(editor.cursorRect(distant)),
            "The off-screen fixture is already in view");

    const QString find = QStringLiteral("target-one");
    Require(editor.FindNext(find, Searchable::Direction_Down, false, false, false),
            "Find did not select the off-screen match");
    Require(editor.textCursor().selectedText() == find, "Find selected the wrong text");
    Require(CursorIsInView(editor), "Find did not bring the match into view");
    Require(editor.hasFocus(), "Find gave away the editor focus");

    Require(editor.ReplaceSelected(find, QStringLiteral("replaced-one"),
                                   Searchable::Direction_Down, false),
            "Replace did not change the selected match");
    Require(editor.FindNext(QStringLiteral("target-two"), Searchable::Direction_Down, false, false, false),
            "The following find did not select the next match");
    Require(editor.textCursor().selectedText() == QStringLiteral("target-two"),
            "Replace/Find did not move to the next match");
    Require(CursorIsInView(editor), "Replace/Find did not bring the next match into view");
    Require(editor.hasFocus(), "Replace/Find gave away the editor focus");
}

static void TestSingleAndBatchReplaceUndo()
{
    const QString original = QStringLiteral("keep red and red\n");
    TestTextDocument document;
    TestCodeViewEditor editor(document);
    Load(editor, original);

    QTextCursor typed = editor.textCursor();
    typed.insertText(QStringLiteral("X"));
    const QString with_typed = editor.toPlainText();

    const QString find = QStringLiteral("red");
    Require(editor.FindNext(find, Searchable::Direction_Down, false, false, true),
            "Undo fixture did not find the first match");
    Require(editor.ReplaceSelected(find, QStringLiteral("blue"), Searchable::Direction_Down, false),
            "Single replace failed");
    const QString after_one = QStringLiteral("Xkeep blue and red\n");
    Require(editor.toPlainText() == after_one, "Single replace changed the wrong match");
    Require(editor.document()->isUndoAvailable(), "Single replace created no undo step");
    editor.undo();
    Require(editor.toPlainText() == with_typed, "One undo did not restore only the single replace");
    editor.undo();
    Require(editor.toPlainText() == original, "The edit before the replace was lost");
    Require(!editor.document()->isUndoAvailable(), "Undo continued past the recorded edits");

    editor.redo();
    editor.redo();
    Require(editor.ReplaceAll(find, QStringLiteral("blue"), Searchable::Direction_Down, true, false) == 1,
            "Replace All did not replace the remaining match");
    Require(editor.toPlainText() == QStringLiteral("Xkeep blue and blue\n"),
            "Replace All changed text outside its matches");
    editor.undo();
    Require(editor.toPlainText() == after_one, "One undo did not revert the whole Replace All");
    Require(editor.document()->isUndoAvailable(), "Replace All undo also removed the earlier edit");

    const QString batch = QStringLiteral("Xkeep green and green\n");
    document.replaceTextAsSingleUndoStep(batch);
    Require(editor.toPlainText() == batch, "The batch resource write did not reach the editor");
    editor.undo();
    Require(editor.toPlainText() == after_one,
            "One undo did not revert the whole batch replacement");
    editor.undo();
    Require(editor.toPlainText() == with_typed,
            "Batch undo also removed the single replace");
    editor.undo();
    Require(editor.toPlainText() == original, "The edit before the replacements was lost");
}

static void TestPlusReplaceFindDoesNotInsertWhenExhausted()
{
    const QString source = QStringLiteral("<html><body><p>a cat ate a rat</p></body></html>");
    TestTextDocument document;
    TestCodeViewEditor editor(document);
    Load(editor, source);

    const QString find = QStringLiteral("a");
    const QString replacement = QStringLiteral("b");
    int replacements = 0;
    while (editor.FindNextPlus(QString(), find)) {
        Require(editor.ReplaceSelectedPlus(find, replacement, Searchable::Direction_Down, false),
                "A Plus selected match was not replaced");
        ++replacements;
        Require(replacements < 50, "Plus Replace/Find did not terminate");
    }
    Require(replacements > 0, "No Plus-mode matches were replaced");
    const QString exhausted = editor.toPlainText();
    Require(!exhausted.contains(QLatin1Char('a')),
            "Plus-mode replace left unmatched find text");
    Require(!editor.ReplaceSelectedPlus(find, replacement, Searchable::Direction_Down, false),
            "Plus Replace/Find after the last match still replaced");
    Require(editor.toPlainText() == exhausted,
            "Plus Replace/Find inserted replacement text with no remaining match");
}

int main(int argc, char **argv)
{
    QCoreApplication::setAttribute(Qt::AA_DisableShaderDiskCache);
    QWebEngineUrlScheme scheme("sigil");
    scheme.setSyntax(QWebEngineUrlScheme::Syntax::Path);
    scheme.setFlags(QWebEngineUrlScheme::SecureScheme | QWebEngineUrlScheme::LocalScheme
                    | QWebEngineUrlScheme::LocalAccessAllowed
                    | QWebEngineUrlScheme::ContentSecurityPolicyIgnored
                    | QWebEngineUrlScheme::FetchApiAllowed);
    QWebEngineUrlScheme::registerScheme(scheme);
    MainApplication app(argc, argv);
    try {
        Require(argc == 3, "Expected source root and disposable fixture arguments");
        TestOriginalReplaceFindDoesNotInsertWhenExhausted();
        TestStaleSelectionDoesNotReplace();
        TestCssReplaceSurvivesHighlightAndFocus();
        TestPlusCaptureReplaceKeepsGroup();
        TestRestartSearchFindsEarlierStyleMatch();
        TestFindAndReplaceMoveToTheMatch();
        TestSingleAndBatchReplaceUndo();
        TestPlusReplaceFindDoesNotInsertWhenExhausted();
    } catch (const std::exception &error) {
        qCritical("%s", error.what());
        return 1;
    }
    return 0;
}
