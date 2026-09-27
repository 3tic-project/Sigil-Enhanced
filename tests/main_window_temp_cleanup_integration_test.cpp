#include "EmbedPython/EmbeddedPython.h" // Python must precede Qt's slots macro.

#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QWebEngineUrlScheme>

#include <cstdlib>
#include <iostream>
#include <stdexcept>

#include "BookManipulation/Book.h"
#include "BookManipulation/FolderKeeper.h"
#include "MainUI/MainApplication.h"
#include "MainUI/MainWindow.h"
#include "Misc/SettingsStore.h"

namespace {

void Require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

}

int main(int argc, char **argv)
{
    QCoreApplication::setAttribute(Qt::AA_DisableShaderDiskCache);
    QWebEngineUrlScheme scheme("sigil");
    scheme.setSyntax(QWebEngineUrlScheme::Syntax::Path);
    scheme.setFlags(QWebEngineUrlScheme::SecureScheme |
                    QWebEngineUrlScheme::LocalScheme |
                    QWebEngineUrlScheme::LocalAccessAllowed |
                    QWebEngineUrlScheme::ContentSecurityPolicyIgnored |
                    QWebEngineUrlScheme::FetchApiAllowed);
    QWebEngineUrlScheme::registerScheme(scheme);
    MainApplication app(argc, argv);

    try {
        Require(argc == 3, "Expected source root and disposable EPUB fixture");
        const QString sourceRoot = QString::fromLocal8Bit(argv[1]);
        const QString input = QFileInfo(QString::fromLocal8Bit(argv[2])).absoluteFilePath();
        auto &python = EmbeddedPython::instance();
        python.addToPythonSysPath(qEnvironmentVariable("SIGIL_TEST_PYTHON_ROOT"));
        python.addToPythonSysPath(sourceRoot + "/src/Resource_Files/plugin_launchers/python");
        python.addToPythonSysPath(sourceRoot + "/src/Resource_Files/python3lib");

        SettingsStore settings;
        settings.setCleanOn(0);
        auto *window = new MainWindow(input);
        QPointer<MainWindow> closedWindow(window);
        const QString workRoot = window->GetCurrentBook()
            ->GetFolderKeeper()->GetFullPathToMainFolder();
        Require(QDir(workRoot).exists(), "Opening EPUB did not create its working directory");

        const QString extraDirectory = QDir(workRoot).filePath("cleanup-probe/nested");
        Require(QDir().mkpath(extraDirectory), "Cannot create an unregistered nested working file");
        QFile extraFile(QDir(extraDirectory).filePath("payload.bin"));
        Require(extraFile.open(QIODevice::WriteOnly) && extraFile.write("probe") == 5,
                "Cannot write an unregistered nested working file");
        extraFile.close();

        window->show();
        app.processEvents();
        Require(window->close(), "MainWindow refused to close an unchanged EPUB");
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        app.processEvents();
        Require(closedWindow.isNull(), "Closing the EPUB did not destroy its window");
        Require(!QFileInfo::exists(workRoot),
                "Closing the EPUB left its extracted working directory behind");
        std::cout << "Closing an EPUB removed its complete working directory\n";
        return EXIT_SUCCESS;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
