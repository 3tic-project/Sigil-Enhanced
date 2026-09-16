#include "EmbedPython/EmbeddedPython.h" // Python must precede Qt's slots macro.

#include <QComboBox>
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
    } catch (const std::exception &error) {
        qCritical("%s", error.what());
        return 1;
    }
    return 0;
}
