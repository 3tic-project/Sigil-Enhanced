#include "EmbedPython/EmbeddedPython.h" // Python must precede Qt's slots macro.

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
    explicit TestCodeViewEditor(TextDocument &document)
        : CodeViewEditor(CodeViewEditor::Highlight_XHTML)
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
        TestPlusReplaceFindDoesNotInsertWhenExhausted();
    } catch (const std::exception &error) {
        qCritical("%s", error.what());
        return 1;
    }
    return 0;
}
