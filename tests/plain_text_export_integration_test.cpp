#include "EmbedPython/EmbeddedPython.h" // Python must precede Qt's slots macro.

#include <QAction>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QResource>
#include <QTextCursor>
#include <QTimer>
#include <QWebEngineUrlScheme>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

#include "BookManipulation/Book.h"
#include "BookManipulation/FolderKeeper.h"
#include "MainUI/MainApplication.h"
#include "MainUI/MainWindow.h"
#include "Misc/SettingsStore.h"
#include "ResourceObjects/TextResource.h"
#include "Tabs/ContentTab.h"
#include "ViewEditors/CodeViewEditor.h"

namespace
{

void require(bool condition, const QString& message)
{
    if (!condition) throw std::runtime_error(message.toStdString());
}

QByteArray read(const QString& path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read TXT test input/output");
    return file.readAll();
}

void check(MainWindow& window, bool modified, const QString& source, const QByteArray& original)
{
    require(window.GetCurrentBook()->IsModified() == modified && window.isWindowModified() == modified,
            "TXT export changed the publication's modified state");
    require(window.GetCurrentFilePath() == source && read(source) == original,
            "TXT export changed the source EPUB or the current filename");
}

void chooseDialog(MainWindow& window, const QString& path, bool cancel)
{
    QTimer timer;
    int attempts = 0;
    bool driven = false;
    QObject::connect(&timer, &QTimer::timeout, &window, [&]() {
        if (++attempts > 300) std::_Exit(2);
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* dialog = qobject_cast<QFileDialog*>(widget);
            if (!dialog || !dialog->isVisible() || driven) continue;
            driven = true;
            if (!cancel) dialog->selectFile(path);
            QMetaObject::invokeMethod(dialog, cancel ? "reject" : "accept", Qt::QueuedConnection);
        }
    });
    timer.start(10);
    auto* action = window.findChild<QAction*>("actionExportPlainText");
    auto* menu = window.findChild<QMenu*>("menuEnhancement");
    require(action && action->isEnabled() && menu && menu->actions().contains(action),
            "The TXT export action is missing from Enhancement");
    action->trigger();
    require(driven, "The TXT export action did not open a save dialog");
}

}

int main(int argc, char** argv)
{
    QCoreApplication::setAttribute(Qt::AA_DisableShaderDiskCache);
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QWebEngineUrlScheme scheme("sigil");
    scheme.setSyntax(QWebEngineUrlScheme::Syntax::Path);
    scheme.setFlags(QWebEngineUrlScheme::SecureScheme | QWebEngineUrlScheme::LocalScheme |
                    QWebEngineUrlScheme::LocalAccessAllowed | QWebEngineUrlScheme::ContentSecurityPolicyIgnored |
                    QWebEngineUrlScheme::FetchApiAllowed);
    QWebEngineUrlScheme::registerScheme(scheme);
    MainApplication app(argc, argv);
    QResource::registerResource(QCoreApplication::applicationDirPath() + "/../Resources/main.rcc");
    try {
        require(argc == 3 || (argc == 4 && QByteArray(argv[3]) == "smoke"),
                "Expected source root and EPUB fixture, optionally smoke");
        const QString root = QString::fromLocal8Bit(argv[1]);
        const QString source = QString::fromLocal8Bit(argv[2]);
        const QByteArray original = read(source);
        auto& python = EmbeddedPython::instance();
        python.addToPythonSysPath(qEnvironmentVariable("SIGIL_TEST_PYTHON_ROOT"));
        python.addToPythonSysPath(root + "/src/Resource_Files/plugin_launchers/python");
        python.addToPythonSysPath(root + "/src/Resource_Files/python3lib");
        SettingsStore settings;
        settings.setCleanOn(0);
        settings.setPreserveOPFSource(true);
        MainWindow window(source);
        if (argc == 4) {
            const bool modified = window.GetCurrentBook()->IsModified();
            QString error;
            QElapsedTimer elapsed;
            elapsed.start();
            require(window.ExportCurrentBookText(source + ".txt", &error), error);
            const QByteArray bytes = read(source + ".txt");
            require(bytes.size() > 1000 && QString::fromUtf8(bytes).toUtf8() == bytes,
                    "Real EPUB produced missing or invalid UTF-8 text");
            check(window, modified, source, original);
            std::cout << "Real EPUB TXT export passed: " << bytes.size() << " bytes, "
                      << bytes.count('\n') << " lines, " << elapsed.elapsed() << " ms\n";
            window.GetCurrentBook()->SetModified(false);
            return 0;
        }
        const QByteArray expected = read(source + ".expected");
        check(window, false, source, original);

        auto* resource = qobject_cast<TextResource*>(window.GetCurrentBook()->GetFolderKeeper()
            ->GetResourceByBookPathNoThrow("OEBPS/a.xhtml"));
        require(resource, "Fixture chapter is missing");
        window.OpenResourceAndWaitUntilLoaded(resource);
        auto* code = dynamic_cast<CodeViewEditor*>(window.GetCurrentContentTab()->GetSearchableContent());
        require(code, "The live Code View editor is missing");
        const QString originalText = resource->GetText();
        QString error;
        const QString output = source + ".clean.txt";
        require(window.ExportCurrentBookText(output, &error), error);
        require(read(output) == expected, "Clean export lost text or ignored spine order");
        check(window, false, source, original);

        QTextCursor cursor = code->textCursor();
        const QString from = QStringLiteral("待编辑正文");
        const QString to = QStringLiteral("尚未保存的修改𠮷");
        const int position = originalText.indexOf(from);
        require(position >= 0, "Fixture edit marker is missing");
        cursor.setPosition(position);
        cursor.setPosition(position + from.size(), QTextCursor::KeepAnchor);
        code->setTextCursor(cursor);
        code->insertPlainText(to);
        const int cursorPosition = code->textCursor().position();
        QByteArray changedExpected = expected;
        changedExpected.replace(from.toUtf8(), to.toUtf8());
        const QString changed = source + ".edited.txt";
        require(window.ExportCurrentBookText(changed, &error), error);
        require(read(changed) == changedExpected, "Export omitted the active editor's unsaved text");
        require(code->textCursor().position() == cursorPosition &&
                window.GetCurrentContentTab()->GetLoadedResource() == resource,
                "TXT export changed the editor/tab selection");
        check(window, true, source, original);

        const QString menuOutput = QFileInfo(source).dir().filePath("menu-export");
        chooseDialog(window, menuOutput, false);
        require(read(menuOutput + ".txt") == changedExpected, "The menu action/default TXT suffix did not export");
        check(window, true, source, original);
        const QVariant lastFolder = SettingsStore().value("plain_text_export/last_folder");
        chooseDialog(window, source + ".cancelled.txt", true);
        require(!QFile::exists(source + ".cancelled.txt") &&
                SettingsStore().value("plain_text_export/last_folder") == lastFolder,
                "Cancelling TXT export wrote a file or changed its remembered folder");
        check(window, true, source, original);
        require(!window.ExportCurrentBookText(source, &error) && !error.isEmpty(),
                "TXT export allowed overwriting the source EPUB");
        check(window, true, source, original);

        code->undo();
        require(resource->GetText() == originalText, "TXT export destroyed undo history");
        check(window, false, source, original);
        code->redo();
        check(window, true, source, original);
        window.GetCurrentBook()->SetModified(false); // Avoid the close prompt in the disposable harness.
        std::cout << "Native TXT export menu, order, unsaved edits and undo tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
