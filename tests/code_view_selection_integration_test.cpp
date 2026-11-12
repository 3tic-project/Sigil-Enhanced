#include "EmbedPython/EmbeddedPython.h" // Python must precede Qt's slots macro.

#include <QComboBox>
#include <QMimeData>
#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPlainTextDocumentLayout>
#include <QTabWidget>
#include <QTextCursor>
#include <QTimer>
#include <QWebEngineUrlScheme>

#include <stdexcept>

#include "Dialogs/PreferenceWidgets/ModifiedVerPrefsWidget.h"
#include "MainUI/MainApplication.h"
#include "Misc/SettingsStore.h"
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

    void DoubleClickAt(int position, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QTextCursor cursor(document());
        cursor.setPosition(position);
        const QPoint local_position = cursorRect(cursor).center();
        const int resolved_position = cursorForPosition(local_position).position();
        Require(resolved_position == position,
                "The editor cursor rectangle did not resolve to the requested source position");
        const QPoint global_position = viewport()->mapToGlobal(local_position);
        QMouseEvent event(QEvent::MouseButtonDblClick,
                          QPointF(local_position), QPointF(global_position),
                          Qt::LeftButton, Qt::LeftButton, modifiers);
        mouseDoubleClickEvent(&event);
    }

    int text_drags = 0;

    bool MoveSelectionTo(int start, int end, int drop)
    {
        return RelocateSelectedText(start, end, drop);
    }

    void TriggerContextActionAt(int position, const QString &action_text)
    {
        QTextCursor cursor(document());
        cursor.setPosition(position);
        const QPoint local_position = cursorRect(cursor).center();
        const QPoint global_position = viewport()->mapToGlobal(local_position);
        bool action_found = false;
        QTimer::singleShot(0, this, [&action_found, action_text]() {
            for (QWidget *widget : QApplication::topLevelWidgets()) {
                QMenu *menu = qobject_cast<QMenu *>(widget);
                if (!menu || !menu->isVisible()) continue;
                for (QAction *action : menu->actions()) {
                    if (action->text() != action_text) continue;
                    action_found = true;
                    action->trigger();
                    break;
                }
                menu->close();
            }
        });
        QContextMenuEvent event(QContextMenuEvent::Mouse,
                                local_position, global_position);
        contextMenuEvent(&event);
        Require(action_found, "The expected XHTML selection context action is missing");
    }

protected:
    Qt::DropAction ExecuteTextContentDrag(QMimeData *mime) override
    {
        ++text_drags;
        delete mime;
        return Qt::IgnoreAction;
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

static QString SelectedText(const CodeViewEditor &editor)
{
    return editor.textCursor().selectedText();
}

static void Load(TestCodeViewEditor &editor, const QString &source, bool is_xhtml = true)
{
    editor.setPlainText(source);
    editor.SetReformatHTMLEnabled(is_xhtml);
    editor.document()->setModified(false);
    editor.document()->clearUndoRedoStacks();
    MainApplication::processEvents();
}

static void TestSettingsContract()
{
    SettingsStore settings;
    settings.remove(QStringLiteral("user_preferences/code_view_double_click_selection"));
    Require(settings.codeViewDoubleClickSelection() == QLatin1String("element-content"),
            "The default double-click mode is not element-content");

    settings.setCodeViewDoubleClickSelection(QStringLiteral("unsupported"));
    Require(settings.codeViewDoubleClickSelection() == QLatin1String("element-content"),
            "An unsupported double-click mode was not canonicalized");
    settings.setCodeViewDoubleClickSelection(QStringLiteral("word"));
    Require(settings.codeViewDoubleClickSelection() == QLatin1String("word"),
            "The compatible word mode was not persisted");
    settings.setCodeViewDoubleClickSelection(QStringLiteral("sentence"));
    Require(settings.codeViewDoubleClickSelection() == QLatin1String("sentence"),
            "The sentence mode was not persisted");
}

static void TestPreferencesWidget()
{
    SettingsStore settings;
    settings.setCodeViewDoubleClickSelection(QStringLiteral("element-content"));
    ModifiedVerPrefsWidget widget;
    QComboBox *mode = widget.findChild<QComboBox *>(QStringLiteral("cbDoubleClickSelection"));
    QTabWidget *tabs = widget.findChild<QTabWidget *>(QStringLiteral("tabWidget"));
    Require(mode && mode->count() == 3, "The preferences selection mode control is missing");
    Require(mode->currentData() == QLatin1String("element-content"),
            "The preferences control did not read the stored selection mode");
    Require(widget.windowTitle() == QLatin1String("Editor") && tabs
                && tabs->tabText(0) == QLatin1String("Code View"),
            "The selection setting is not presented on the Editor / Code View page");

    mode->setCurrentIndex(1);
    widget.saveSettings();
    Require(SettingsStore().codeViewDoubleClickSelection() == QLatin1String("word"),
            "The preferences control did not save word mode");
    mode->setCurrentIndex(2);
    widget.saveSettings();
    Require(SettingsStore().codeViewDoubleClickSelection() == QLatin1String("sentence"),
            "The preferences control did not save sentence mode");
    mode->setCurrentIndex(0);
    widget.saveSettings();
    Require(SettingsStore().codeViewDoubleClickSelection() == QLatin1String("element-content"),
            "The preferences control did not restore element-content mode");
}

static void TestSentenceMode()
{
    const QString source = QString::fromUtf8(
        "<html><body><p>第一句。第二句含<ruby>風<rt>かぜ</rt></ruby>！第三句？</p>"
        "<p id='english'>Value 3.14 stays. Visit example.com now.</p>"
        "</body></html>");
    SettingsStore settings;
    settings.setCodeViewDoubleClickSelection(QStringLiteral("sentence"));

    TestTextDocument document;
    TestCodeViewEditor editor(document);
    Load(editor, source);
    editor.DoubleClickAt(source.indexOf(QString::fromUtf8("かぜ")));
    Require(SelectedText(editor) == QString::fromUtf8(
                "第二句含<ruby>風<rt>かぜ</rt></ruby>！"),
            "The real editor did not map a Ruby annotation to its CJK sentence");
    Require(!editor.document()->isModified() && !editor.document()->isUndoAvailable(),
            "Sentence selection modified the document or its undo stack");

    editor.DoubleClickAt(source.indexOf(QStringLiteral("3.14")));
    Require(SelectedText(editor) == QStringLiteral("Value 3.14 stays."),
            "The real editor split a sentence at decimal punctuation");
    editor.DoubleClickAt(source.indexOf(QStringLiteral("example.com")));
    Require(SelectedText(editor) == QStringLiteral("Visit example.com now."),
            "The real editor split a sentence inside a URL");
    editor.DoubleClickAt(source.indexOf(QStringLiteral("english")));
    Require(SelectedText(editor) == QStringLiteral("english"),
            "Sentence mode expanded an attribute click");
    editor.DoubleClickAt(source.indexOf(QStringLiteral("Value")),
                         Qt::ControlModifier);
    Require(SelectedText(editor) == QStringLiteral("Value"),
            "A navigation-modified sentence click did not preserve word selection");

    Load(editor, source, false);
    editor.DoubleClickAt(source.indexOf(QStringLiteral("Value")));
    Require(SelectedText(editor) == QStringLiteral("Value"),
            "A non-XHTML editor used sentence selection");
}

static void TestElementContentAndFallbacks()
{
    const QString source = QString::fromUtf8(
        "<html><body><p id='intro'>alpha <ruby>風<rt>かぜ</rt></ruby> omega</p>"
        "<p>second unit</p></body></html>");
    const QString expected = QString::fromUtf8(
        "alpha <ruby>風<rt>かぜ</rt></ruby> omega");
    SettingsStore settings;
    settings.setCodeViewDoubleClickSelection(QStringLiteral("element-content"));

    TestTextDocument document;
    TestCodeViewEditor editor(document);
    Load(editor, source);
    editor.DoubleClickAt(source.indexOf(QString::fromUtf8("かぜ")));
    Require(SelectedText(editor) == expected,
            "The real editor did not select the containing XHTML text unit");
    Require(!editor.document()->isModified() && !editor.document()->isUndoAvailable(),
            "Changing the selection modified the document or its undo stack");

    editor.DoubleClickAt(source.indexOf(QStringLiteral("intro")));
    Require(SelectedText(editor) == QLatin1String("intro"),
            "An attribute click did not preserve normal word selection");

    editor.DoubleClickAt(source.indexOf(QStringLiteral("alpha")), Qt::ControlModifier);
    Require(SelectedText(editor) == QLatin1String("alpha"),
            "A navigation-modified click did not preserve normal word selection");

    settings.setCodeViewDoubleClickSelection(QStringLiteral("word"));
    editor.DoubleClickAt(source.indexOf(QStringLiteral("second")));
    Require(SelectedText(editor) == QLatin1String("second"),
            "The compatible setting did not restore word selection");

    settings.setCodeViewDoubleClickSelection(QStringLiteral("element-content"));
    Load(editor, source, false);
    editor.DoubleClickAt(source.indexOf(QStringLiteral("second")));
    Require(SelectedText(editor) == QLatin1String("second"),
            "A non-XHTML editor used XHTML element-content selection");
}

static void TestTagModifiersAndEditorConsistency()
{
    const QString source = QStringLiteral(
        "<html><body><p id='one'>first <em>unit</em></p><p>second</p></body></html>");
    const int opening_tag = source.indexOf(QStringLiteral("<p id")) + 1;
    const QString inner = QStringLiteral("first <em>unit</em>");
    const QString outer = QStringLiteral("<p id='one'>first <em>unit</em></p>");
    SettingsStore settings;
    settings.setCodeViewDoubleClickSelection(QStringLiteral("element-content"));

    TestTextDocument first_document;
    TestCodeViewEditor first_editor(first_document);
    Load(first_editor, source);
    first_editor.DoubleClickAt(opening_tag, Qt::ShiftModifier);
    Require(SelectedText(first_editor) == inner,
            "Shift-double-click no longer selects tag contents");
    first_editor.DoubleClickAt(opening_tag, Qt::AltModifier);
    Require(SelectedText(first_editor) == outer,
            "Alt-double-click no longer selects the full tag pair");

    TestTextDocument peer_document;
    TestCodeViewEditor split_peer(peer_document);
    Load(split_peer, source);
    const int inline_text = source.indexOf(QStringLiteral("unit"));
    first_editor.DoubleClickAt(inline_text);
    split_peer.DoubleClickAt(inline_text);
    Require(SelectedText(first_editor) == inner
                && SelectedText(split_peer) == SelectedText(first_editor),
            "Two code editors did not apply the same selection policy");

    first_editor.TriggerContextActionAt(
        inline_text, QStringLiteral("Select Element Content"));
    Require(SelectedText(first_editor) == inner,
            "The context menu did not select element content");
    first_editor.TriggerContextActionAt(
        inline_text, QStringLiteral("Select Whole Element"));
    Require(SelectedText(first_editor) == outer,
            "The context menu did not select the whole element");
}

static void SendMouse(TestCodeViewEditor &editor, QEvent::Type type, int position,
                      Qt::MouseButton button, Qt::MouseButtons buttons,
                      Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QTextCursor cursor(editor.document());
    cursor.setPosition(position);
    const QPoint local_position = editor.cursorRect(cursor).center();
    QMouseEvent event(type, QPointF(local_position),
                      QPointF(editor.viewport()->mapToGlobal(local_position)),
                      button, buttons, modifiers);
    QApplication::sendEvent(editor.viewport(), &event);
}

static void TestSelectedTextDrag()
{
    QApplication::setStartDragDistance(1);
    const QString source = QStringLiteral("alpha beta gamma delta");
    const int selection_start = source.indexOf(QStringLiteral("beta"));
    const int selection_end = source.indexOf(QStringLiteral("delta"));
    TestTextDocument document;
    TestCodeViewEditor editor(document);
    Load(editor, source);
    QTextCursor selection(editor.document());
    selection.setPosition(selection_start);
    selection.setPosition(selection_end, QTextCursor::KeepAnchor);
    editor.setTextCursor(selection);

    const int press = source.indexOf(QStringLiteral("gamma")) + 2;
    SendMouse(editor, QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::LeftButton);
    Require(editor.textCursor().selectionStart() == selection_start
                && editor.textCursor().selectionEnd() == selection_end,
            "A press inside the selection collapsed it before a drag could start");
    SendMouse(editor, QEvent::MouseButtonRelease, press, Qt::LeftButton, Qt::NoButton);
    Require(!editor.textCursor().hasSelection() && editor.textCursor().position() == press,
            "Releasing inside the selection without movement did not place the caret");
    Require(editor.toPlainText() == source && !editor.document()->isModified() && editor.text_drags == 0,
            "A click inside the selection changed the document or started a drag");

    selection.setPosition(selection_start);
    selection.setPosition(selection_end, QTextCursor::KeepAnchor);
    editor.setTextCursor(selection);
    SendMouse(editor, QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::LeftButton,
              Qt::ShiftModifier);
    SendMouse(editor, QEvent::MouseMove, source.indexOf(QStringLiteral("alpha")),
              Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
    SendMouse(editor, QEvent::MouseButtonRelease, source.indexOf(QStringLiteral("alpha")),
              Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
    Require(editor.text_drags == 0 && editor.toPlainText() == source,
            "Shift-dragging inside the selection started a content move");

    selection.setPosition(selection_start);
    selection.setPosition(selection_end, QTextCursor::KeepAnchor);
    editor.setTextCursor(selection);
    SendMouse(editor, QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::LeftButton,
              Qt::ControlModifier);
    SendMouse(editor, QEvent::MouseMove, source.indexOf(QStringLiteral("alpha")),
              Qt::NoButton, Qt::LeftButton, Qt::ControlModifier);
    SendMouse(editor, QEvent::MouseButtonRelease, source.indexOf(QStringLiteral("alpha")),
              Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
    Require(editor.text_drags == 0 && editor.toPlainText() == source,
            "Ctrl-dragging inside the selection started either content move or Qt's text drag");

    selection.setPosition(selection_start);
    selection.setPosition(selection_end, QTextCursor::KeepAnchor);
    editor.setTextCursor(selection);
    editor.document()->setModified(false);
    const int drag_target = source.indexOf(QStringLiteral("alpha"));
    SendMouse(editor, QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::LeftButton);
    SendMouse(editor, QEvent::MouseMove, drag_target, Qt::NoButton, Qt::LeftButton);
    Require(editor.text_drags == 1 && editor.toPlainText() == source && !editor.document()->isModified(),
            "Starting a content drag edited the document inside the drag loop");
    SendMouse(editor, QEvent::MouseButtonRelease, drag_target, Qt::LeftButton, Qt::NoButton);

    const QString moved = QStringLiteral("beta gamma alpha delta");
    Require(editor.MoveSelectionTo(selection_start, selection_end, drag_target)
                && editor.toPlainText() == moved,
            "The selected text did not move before the drop caret");
    Require(editor.textCursor().selectionStart() == drag_target
                && editor.textCursor().selectedText() == QStringLiteral("beta gamma "),
            "The moved text was not selected at its new location");
    Require(editor.document()->isModified(), "Moving selected text did not mark the document modified");
    editor.undo();
    Require(editor.toPlainText() == source, "Undo did not restore the text from before the drag");
    editor.redo();
    Require(editor.toPlainText() == moved, "Redo did not restore the dragged text");

    const int inside = editor.textCursor().selectionStart() + 2;
    Require(!editor.MoveSelectionTo(editor.textCursor().selectionStart(),
                                    editor.textCursor().selectionEnd(), inside)
                && editor.toPlainText() == moved,
            "Dropping inside the dragged selection changed the document");
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
        TestSettingsContract();
        TestPreferencesWidget();
        TestElementContentAndFallbacks();
        TestSentenceMode();
        TestTagModifiersAndEditorConsistency();
        TestSelectedTextDrag();
    } catch (const std::exception &error) {
        qCritical("%s", error.what());
        return 1;
    }
    return 0;
}
