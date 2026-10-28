#include "EmbedPython/EmbeddedPython.h" // Python must precede Qt's slots macro.

#include <QAction>
#include <QComboBox>
#include <QFile>
#include <QMessageBox>
#include <QResource>
#include <QTextCursor>
#include <QTimer>
#include <QToolButton>
#include <QWebEngineUrlScheme>

#include <cstdlib>
#include <algorithm>
#include <iostream>
#include <stdexcept>

#include "BookManipulation/Book.h"
#include "BookManipulation/FolderKeeper.h"
#include "MainUI/FindReplace.h"
#include "MainUI/FindReplacePlus.h"
#include "MainUI/MainApplication.h"
#include "MainUI/MainWindow.h"
#include "Misc/SettingsStore.h"
#include "Misc/SettingsStoreExtend.h"
#include "ResourceObjects/TextResource.h"
#include "Tabs/ContentTab.h"
#include "ViewEditors/CodeViewEditor.h"
#include "Widgets/AlertBox.h"

namespace
{

void require(bool condition, const QString& message)
{
    if (!condition) throw std::runtime_error(message.toStdString());
}

bool invokeBool(QObject* object, const char* slot)
{
    bool result = false;
    require(QMetaObject::invokeMethod(object, slot, Qt::DirectConnection, Q_RETURN_ARG(bool, result)),
            QString("Cannot invoke %1").arg(slot));
    return result;
}

CodeViewEditor* editor(MainWindow& window)
{
    auto* result = dynamic_cast<CodeViewEditor*>(window.GetCurrentContentTab()->GetSearchableContent());
    require(result, "Current tab has no Code View editor");
    return result;
}

TextResource* open(MainWindow& window, const QString& path)
{
    auto* resource = qobject_cast<TextResource*>(
        window.GetCurrentBook()->GetFolderKeeper()->GetResourceByBookPathNoThrow(path));
    require(resource, "Cannot find fixture resource: " + path);
    window.OpenResourceAndWaitUntilLoaded(resource);
    return resource;
}

void checkModified(MainWindow& window, bool expected, const QString& step)
{
    require(window.GetCurrentBook()->IsModified() == expected, "Book modified state is wrong after " + step);
    require(window.isWindowModified() == expected, "Window modified indicator is wrong after " + step);
}

void undoRedoSave(MainWindow& window, bool css)
{
    auto* resource = open(window, css ? "OEBPS/style.css" : "OEBPS/a.xhtml");
    auto* code = editor(window);
    const QString original = resource->GetText();
    const QString from = css ? "red" : "Original paragraph";
    const QString to = css ? "blue" : "Redo changed paragraph";
    QString changed = original;
    changed.replace(from, to);
    checkModified(window, false, "opening the fixture");
    QTextCursor cursor = code->textCursor();
    const int position = original.indexOf(from);
    require(position >= 0, "Fixture does not contain the text to edit");
    cursor.setPosition(position);
    cursor.setPosition(position + from.size(), QTextCursor::KeepAnchor);
    code->setTextCursor(cursor);
    code->insertPlainText(to);
    require(resource->GetText() == changed, "Typing did not edit the shared resource document");
    checkModified(window, true, "typing");
    // Tab/focus changes flush backing files and reset QTextDocument::modified,
    // but the EPUB remains unsaved until the actual MainWindow Save completes.
    window.GetCurrentContentTab()->SaveTabContent();
    checkModified(window, true, "flushing tab content");
    require(QMetaObject::invokeMethod(window.GetCurrentContentTab(), "Undo"), "Cannot undo");
    require(resource->GetText() == original, "Undo did not restore the original text");
    checkModified(window, false, "undoing to the last book save");
    require(QMetaObject::invokeMethod(window.GetCurrentContentTab(), "Redo"), "Cannot redo");
    require(resource->GetText() == changed, "Redo did not restore the changed text");
    checkModified(window, true, "redoing an unsaved edit");
    require(invokeBool(&window, "Save"), "Cannot save the redone edit");
    checkModified(window, false, "saving the redone edit");
    require(QMetaObject::invokeMethod(window.GetCurrentContentTab(), "Undo"), "Cannot undo a saved edit");
    require(resource->GetText() == original, "Undo after Save did not restore the original text");
    require(resource->HasChangesSinceBookSave(), "Undo after Save matches an incorrect book-save baseline");
    checkModified(window, true, "undoing a saved edit");
    require(QMetaObject::invokeMethod(window.GetCurrentContentTab(), "Redo"), "Cannot redo a saved edit");
    checkModified(window, false, "redoing back to the saved text");
    require(QMetaObject::invokeMethod(window.GetCurrentContentTab(), "Undo"), "Cannot undo again");
    require(invokeBool(&window, "Save"), "Cannot save the undone text");
    checkModified(window, false, "saving the undone text");
    require(QMetaObject::invokeMethod(window.GetCurrentContentTab(), "Redo"), "Save cleared redo history");
    require(resource->GetText() == changed, "Redo after saving the undone text changed the result");
    checkModified(window, true, "redoing after a save");
    require(invokeBool(&window, "Save"), "Cannot save redo after a save");
    checkModified(window, false, "the final save");
}

void setCombo(QWidget* widget, const char* name, int data)
{
    auto* combo = widget->findChild<QComboBox*>(name);
    require(combo && combo->findData(data) >= 0, QString("Search control %1 is missing").arg(name));
    combo->setCurrentIndex(combo->findData(data));
}

void restartSearch(MainWindow& window, bool plus, bool presearch)
{
    auto* original = window.findChild<FindReplace*>();
    auto* enhanced = window.findChild<FindReplacePlus*>();
    QWidget* search = plus ? static_cast<QWidget*>(enhanced) : static_cast<QWidget*>(original);
    require(search, "Cannot find the actual Find/Replace widget");
    original->SetOptionWrap(false);
    enhanced->SetOptionWrap(false);

    // Plus shows an end-of-search dialog before preparing its next search.
    QTimer dialogs;
    QObject::connect(&dialogs, &QTimer::timeout, &window, []() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            QString text;
            if (auto* box = qobject_cast<QMessageBox*>(widget)) text = box->text();
            if (auto* box = qobject_cast<AlertBox*>(widget)) text = box->text();
            if (widget->isVisible() && text.contains("The search has ended")) {
                QMetaObject::invokeMethod(widget, "accept", Qt::QueuedConnection);
            }
        }
    });
    dialogs.start(5);

    for (bool up : {false, true}) {
        for (int scope : {0, 1, 2}) { // current file, all HTML files, marked text
            const bool all = scope == 1;
            const bool marked = scope == 2;
            auto* resource = open(window, all && up ? "OEBPS/b.xhtml" : "OEBPS/a.xhtml");
            auto* code = editor(window);
            code->ClearMarkedText();
            original->ShowHideMarkedText(false);
            enhanced->ShowHideMarkedText(false);
            setCombo(search, "cbSearchMode", presearch ? FindReplacePlus::SearchMode_PreSearch : 0);
            if (plus) search->findChild<QComboBox*>("cbPreFind")->setEditText("needle");
            setCombo(search, "cbLookWhere", all ? 1 : 0);
            const int direction = plus ? (up ? FindReplacePlus::Direction_Up : FindReplacePlus::Direction_Down)
                                       : (up ? FindReplace::SearchDirection_Up : FindReplace::SearchDirection_Down);
            setCombo(search, "cbSearchDirection", direction);
            search->findChild<QComboBox*>("cbFind")->setEditText("needle");
            QList<QPair<QString, int>> expected;
            for (const QString& path : all ? QStringList {"OEBPS/a.xhtml", "OEBPS/b.xhtml"}
                                          : QStringList {"OEBPS/a.xhtml"}) {
                auto* text = qobject_cast<TextResource*>(window.GetCurrentBook()->GetFolderKeeper()
                    ->GetResourceByBookPathNoThrow(path));
                for (int pos = text->GetText().indexOf("needle"); pos >= 0;
                     pos = text->GetText().indexOf("needle", pos + 6)) expected.append({path, pos});
            }
            if (marked) {
                expected.removeFirst();
                QTextCursor cursor(code->document());
                cursor.setPosition(expected.first().second);
                cursor.setPosition(expected.last().second + 6, QTextCursor::KeepAnchor);
                code->setTextCursor(cursor);
                require(code->MarkSelection(), "Could not mark the search region");
                original->ShowHideMarkedText(true);
                enhanced->ShowHideMarkedText(true);
            }
            if (up) std::reverse(expected.begin(), expected.end());
            code->ScrollToPosition(up ? resource->GetText().size() : 0);
            require(QMetaObject::invokeMethod(search, "RestartClicked"), "Cannot prepare initial search");

            for (int round = 0; round < 3; ++round) {
                if (round == 1 && !plus) {
                    auto* button = search->findChild<QToolButton*>("restart");
                    require(button, "Restart button is missing");
                    button->click();
                } else if (round > 0) {
                    auto* action = window.findChild<QAction*>("actionRestartSearch");
                    require(action, "Restart Search action is missing");
                    action->trigger();
                }
                for (const auto& match : expected) {
                    const QString step = QString("mode=%1 up=%2 scope=%3 round=%4")
                        .arg(plus).arg(up).arg(scope).arg(round);
                    require(invokeBool(search, up ? "DoFindPrevious" : "DoFindNext"),
                            "Search missed a match: " + step);
                    require(window.GetCurrentContentTab()->GetLoadedResource()->GetRelativePath() == match.first &&
                            editor(window)->textCursor().selectionStart() == match.second,
                            "Search restarted from the wrong match: " + step);
                }
                require(!invokeBool(search, up ? "DoFindPrevious" : "DoFindNext"),
                        QString("Search did not stop at the end: mode=%1 up=%2 scope=%3 round=%4")
                            .arg(plus).arg(up).arg(scope).arg(round));
            }
            editor(window)->ClearMarkedText();
            original->ShowHideMarkedText(false);
            enhanced->ShowHideMarkedText(false);
        }
    }
    checkModified(window, false, "searching and restarting");
}

}

int main(int argc, char** argv)
{
    QCoreApplication::setAttribute(Qt::AA_DisableShaderDiskCache);
    QWebEngineUrlScheme scheme("sigil");
    scheme.setSyntax(QWebEngineUrlScheme::Syntax::Path);
    scheme.setFlags(QWebEngineUrlScheme::SecureScheme | QWebEngineUrlScheme::LocalScheme |
                    QWebEngineUrlScheme::LocalAccessAllowed | QWebEngineUrlScheme::ContentSecurityPolicyIgnored |
                    QWebEngineUrlScheme::FetchApiAllowed);
    QWebEngineUrlScheme::registerScheme(scheme);
    MainApplication app(argc, argv);
    QResource::registerResource(QCoreApplication::applicationDirPath() + "/../Resources/main.rcc");
    try {
        require(argc == 4, "Expected source root, EPUB fixture and scenario");
        const QString root = QString::fromLocal8Bit(argv[1]);
        const QString scenario = QString::fromLocal8Bit(argv[3]);
        const QString input = QString::fromLocal8Bit(argv[2]) + "." + scenario + ".epub";
        require(QFile::copy(QString::fromLocal8Bit(argv[2]), input), "Cannot copy fixture");
        auto& python = EmbeddedPython::instance();
        python.addToPythonSysPath(qEnvironmentVariable("SIGIL_TEST_PYTHON_ROOT"));
        python.addToPythonSysPath(root + "/src/Resource_Files/plugin_launchers/python");
        python.addToPythonSysPath(root + "/src/Resource_Files/python3lib");
        SettingsStore settings;
        settings.setCleanOn(0);
        settings.setPreserveOPFSource(true);
        SettingsStoreExtend().setFindReplaceEnhancedMode(scenario.startsWith("restart-plus"));
        MainWindow window(input);
        if (scenario.startsWith("redo-")) undoRedoSave(window, scenario == "redo-css");
        else restartSearch(window, scenario.startsWith("restart-plus"), scenario.endsWith("presearch"));
        std::cout << "Editor undo/search integration passed: " << scenario.toStdString() << '\n';
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
