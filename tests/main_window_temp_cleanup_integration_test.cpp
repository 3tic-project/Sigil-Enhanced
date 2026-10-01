#include "EmbedPython/EmbeddedPython.h" // Python must precede Qt's slots macro.

#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QTimer>
#include <QWebEngineUrlScheme>

#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>

#include "BookManipulation/Book.h"
#include "BookManipulation/FolderKeeper.h"
#include "MainUI/MainApplication.h"
#include "MainUI/MainWindow.h"
#include "Misc/SettingsStore.h"
#include "Misc/TempCleanup.h"

namespace {

void Require(bool condition, const std::string &message)
{
    if (!condition) throw std::runtime_error(message);
}

void WriteFile(const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    Require(file.open(QIODevice::WriteOnly) && file.write("probe") == 5, "Cannot write " + path.toStdString());
}

// An immutable file cannot be unlinked, standing in for a file that another
// process keeps open on Windows.
void SetImmutable(const QString &path, bool immutable)
{
    Require(::chflags(QFile::encodeName(path).constData(), immutable ? UF_IMMUTABLE : 0) == 0,
            "chflags failed for " + path.toStdString());
}

struct ImmutableGuard {
    QString path;
    ~ImmutableGuard() { ::chflags(QFile::encodeName(path).constData(), 0); }
};

QString WorkRoot(MainWindow *window)
{
    return window->GetCurrentBook()->GetFolderKeeper()->GetFullPathToMainFolder();
}

void CloseWindow(MainWindow *window)
{
    QPointer<MainWindow> closed(window);
    Require(window->close(), "MainWindow refused to close an unchanged EPUB");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
    Require(closed.isNull(), "Closing the EPUB did not destroy its window");
}

// Runs the exit cleanup and performs action once its dialog is shown.
bool ExitWithCleanupDialog(const std::function<void(QDialog *)> &action)
{
    auto *poll = new QTimer(qApp);
    auto shown = std::make_shared<bool>(false);
    auto elapsed = std::make_shared<QElapsedTimer>();
    elapsed->start();
    QObject::connect(poll, &QTimer::timeout, poll, [=]() {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (dialog) {
            *shown = true;
            poll->deleteLater();
            action(dialog);
        } else if (elapsed->elapsed() > 10000) {
            poll->deleteLater();
        }
    });
    poll->start(100);
    TempCleanup::instance().FinishBeforeExit();
    return *shown;
}

void ExitAfterLargeBook(QApplication &app, MainWindow *window)
{
    const QString root = WorkRoot(window);
    for (int directory = 0; directory < 40; ++directory) {
        for (int file = 0; file < 500; ++file) {
            WriteFile(QStringLiteral("%1/probe/%2/%3.bin").arg(root).arg(directory).arg(file));
        }
    }
    app.setQuitOnLastWindowClosed(true);
    QTimer::singleShot(500, window, [window]() { window->close(); });
    app.exec();
    // The E11 background deletion was still running here, so anything that
    // ended the process during shutdown left the folder behind.
    Require(!QFileInfo::exists(root), "The working directory still existed when the event loop ended");
}

void ReadOnlyFolder(MainWindow *window)
{
    const QString root = WorkRoot(window);
    const QString locked = root + "/plugin-output/readonly";
    WriteFile(locked + "/payload.bin");
    Require(::chmod(QFile::encodeName(locked).constData(), 0555) == 0, "chmod failed");
    CloseWindow(window);
    Require(!QFileInfo::exists(root), "A read-only folder written by a plugin kept the working directory alive");
}

void WaitsUntilReleased(MainWindow *window)
{
    const QString root = WorkRoot(window);
    ImmutableGuard guard { root + "/OEBPS/held.bin" };
    WriteFile(guard.path);
    SetImmutable(guard.path, true);
    CloseWindow(window);
    Require(TempCleanup::instance().PendingPaths().contains(root), "A held working directory was not queued");
    Require(QFileInfo::exists(root + "/OEBPS/held.bin"), "The held file vanished unexpectedly");

    Require(ExitWithCleanupDialog([&](QDialog *) { SetImmutable(guard.path, false); }),
            "Exit did not show the cleanup dialog while files were held");
    Require(!QFileInfo::exists(root), "Exit finished before the released directory was removed");
    Require(TempCleanup::instance().PendingPaths().isEmpty(), "The removed directory is still pending");
    Require(SettingsStore().pendingTempCleanup().isEmpty(), "A completed cleanup was recorded as a leftover");
}

void GiveUpThenNextStart(MainWindow *window)
{
    const QString root = WorkRoot(window);
    const QString foreign = QFileInfo(root).absolutePath() + "/user-folder";
    WriteFile(foreign + "/keep.txt");
    ImmutableGuard guard { root + "/held.bin" };
    WriteFile(guard.path);
    SetImmutable(guard.path, true);
    CloseWindow(window);

    Require(ExitWithCleanupDialog([](QDialog *dialog) { dialog->reject(); }),
            "Exit did not show the cleanup dialog while files were held");
    Require(SettingsStore().pendingTempCleanup() == QStringList { root },
            "The leftover was not recorded for the next start");

    SettingsStore().setPendingTempCleanup({ root, foreign });
    SetImmutable(guard.path, false);
    TempCleanup::instance().RemoveLeftoversFromPreviousRun();
    Require(!QFileInfo::exists(root), "The next start did not remove the recorded leftover");
    Require(QFileInfo::exists(foreign + "/keep.txt"), "Startup cleanup deleted a folder Sigil did not create");
    Require(SettingsStore().pendingTempCleanup().isEmpty(), "Startup cleanup kept a stale record");
}

void PythonReleasesHolds(MainWindow *window)
{
    const QString root = WorkRoot(window);
    const QString held = root + "/OEBPS/python-held.bin";
    WriteFile(held);
    auto &python = EmbeddedPython::instance();
    int rv = 0;
    QString traceback;
    PyObjectPtr file(python.runInPython("builtins", "open", QVariantList { held, "rb" },
                                        &rv, traceback, true, false));
    Require(rv == 0 && !file.isNull(), "Python could not open the probe file");
    Require(QDir::setCurrent(root + "/OEBPS"), "Cannot enter the working directory");

    const QVariant closed = python.runInPython("tempcleanup", "release_path", QVariantList { root },
                                               &rv, traceback, false, false);
    Require(rv == 0, "tempcleanup.release_path failed: " + traceback.toStdString());
    Require(closed.toInt() == 1, "release_path did not close the Python file under the directory");
    const QVariant is_closed = python.callPyObjMethod(file, "__getattribute__", QVariantList { "closed" },
                                                      &rv, traceback);
    Require(rv == 0 && is_closed.toBool(), "The Python file object is still open");
    const QString canonical_root = QFileInfo(root).canonicalFilePath();
    Require(!QDir::currentPath().startsWith(canonical_root),
            "Python left the process working directory inside the book");

    Require(QDir::setCurrent(root + "/OEBPS"), "Cannot re-enter the working directory");
    CloseWindow(window);
    Require(!QFileInfo::exists(root), "Closing did not remove the directory Sigil was working in");
    Require(!QDir::currentPath().startsWith(canonical_root), "The working directory was not moved out");
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
        Require(argc == 4, "Expected source root, disposable EPUB fixture and scenario");
        const QString sourceRoot = QString::fromLocal8Bit(argv[1]);
        const QString input = QFileInfo(QString::fromLocal8Bit(argv[2])).absoluteFilePath();
        const QString scenario = QString::fromLocal8Bit(argv[3]);
        auto &python = EmbeddedPython::instance();
        python.addToPythonSysPath(qEnvironmentVariable("SIGIL_TEST_PYTHON_ROOT"));
        python.addToPythonSysPath(sourceRoot + "/src/Resource_Files/plugin_launchers/python");
        python.addToPythonSysPath(sourceRoot + "/src/Resource_Files/python3lib");

        SettingsStore settings;
        settings.setCleanOn(0);
        auto *window = new MainWindow(input);
        Require(QDir(WorkRoot(window)).exists(), "Opening EPUB did not create its working directory");
        window->show();
        app.processEvents();

        if (scenario == "exit-large") ExitAfterLargeBook(app, window);
        else if (scenario == "readonly") ReadOnlyFolder(window);
        else if (scenario == "held") WaitsUntilReleased(window);
        else if (scenario == "give-up") GiveUpThenNextStart(window);
        else if (scenario == "python") PythonReleasesHolds(window);
        else throw std::runtime_error("Unknown scenario " + scenario.toStdString());
        std::cout << "Temporary cleanup scenario passed: " << scenario.toStdString() << '\n';
        return EXIT_SUCCESS;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
