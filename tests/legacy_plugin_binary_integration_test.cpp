#include "EmbedPython/EmbeddedPython.h" // Python must precede Qt's slots macro.

#include <QFile>
#include <QFileDialog>
#include <QPushButton>
#include <QResource>
#include <QTextEdit>
#include <QTimer>
#include <QWebEngineUrlScheme>

#include <cstdlib>
#include <iostream>
#include <stdexcept>

#include "BookManipulation/Book.h"
#include "BookManipulation/FolderKeeper.h"
#include "Dialogs/PluginRunner.h"
#include "MainUI/MainApplication.h"
#include "MainUI/MainWindow.h"
#include "Misc/PluginDB.h"
#include "Misc/SettingsStore.h"
#include "Tabs/TabManager.h"
#include "Widgets/AlertBox.h"

namespace
{

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

QByteArray readBytes(const QString& path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read test file");
    return file.readAll();
}

bool invokeSave(MainWindow& window, const char* slot)
{
    bool result = false;
    require(QMetaObject::invokeMethod(&window, slot, Qt::DirectConnection,
                                     Q_RETURN_ARG(bool, result)),
            "Could not invoke MainWindow save action");
    return result;
}

}

int main(int argc, char** argv)
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
    QResource::registerResource(QCoreApplication::applicationDirPath() + "/../Resources/main.rcc");

    try {
        require(argc == 4, "Expected source root, disposable EPUB fixture and scenario");
        const QString root = QString::fromLocal8Bit(argv[1]);
        const QString source = QString::fromLocal8Bit(argv[2]);
        const QString scenario = QString::fromLocal8Bit(argv[3]);
        const QString input = source + "." + scenario + ".epub";
        require(QFile::copy(source, input), "Could not isolate the scenario EPUB");
        qputenv("SIGIL_BINARY_SCENARIO", scenario.toLocal8Bit());
        qputenv("SIGIL_BINARY_EXPECTED_PATH", (input + ".expected").toLocal8Bit());
        auto& python = EmbeddedPython::instance();
        python.addToPythonSysPath(qEnvironmentVariable("SIGIL_TEST_PYTHON_ROOT"));
        python.addToPythonSysPath(root + "/src/Resource_Files/plugin_launchers/python");
        python.addToPythonSysPath(root + "/src/Resource_Files/python3lib");
        SettingsStore settings;
        settings.setPreserveOPFSource(true);
        settings.setCleanOn(0);
        settings.setUseBundledInterp(false);
        PluginDB::instance().set_engine_path("python3.4",
            qEnvironmentVariable("SIGIL_TEST_EXTERNAL_PYTHON"));
        PluginDB::instance().load_plugins_from_disk();

        MainWindow window(input);
        const auto book = window.GetCurrentBook();
        const QByteArray original = readBytes(input);
        require(!book->IsModified() && !window.isWindowModified(),
                "Opening fixture must leave the book clean");
        const auto images = book->GetFolderKeeper()->GetResourceTypeList<ImageResource>(false);
        require(images.size() == 1, "Fixture needs exactly one image");
        int image_updates = 0;
        QObject::connect(images.first(), &Resource::ResourceUpdatedOnDisk, &window,
                         [&]() { ++image_updates; });

        // Drive the actual modal runner, including failed/crashed/cancelled runs.
        QTimer driver;
        driver.setInterval(10);
        QObject::connect(&driver, &QTimer::timeout, &window, [&]() {
            for (QWidget* widget : QApplication::topLevelWidgets()) {
                if (auto* alert = qobject_cast<AlertBox*>(widget)) {
                    if (alert->isVisible()) {
                        std::cerr << "Unexpected plugin error: " << alert->text().toStdString() << '\n';
                        for (auto* console : window.findChildren<QTextEdit*>("textEdit")) {
                            std::cerr << console->toPlainText().toStdString() << '\n';
                        }
                        std::_Exit(EXIT_FAILURE);
                    }
                }
                auto* runner = qobject_cast<PluginRunner*>(widget);
                if (!runner || !runner->isVisible()) continue;
                auto* ok = runner->findChild<QPushButton*>("okButton");
                auto* console = runner->findChild<QTextEdit*>("textEdit");
                if (scenario == "cancelled" && console->toPlainText().contains(
                        "DIRECT_BINARY_WRITE_FINISHED") && !ok->isEnabled()) {
                    runner->findChild<QPushButton*>("cancelButton")->click();
                }
                if (!runner->findChild<QPushButton*>("startButton")->isEnabled()
                    && ok->isEnabled()) ok->click();
            }
        });
        driver.start();
        QTimer::singleShot(15000, &window, []() {
            std::cerr << "Timed out running the legacy plugin\n";
            std::_Exit(EXIT_FAILURE);
        });
        auto* tabs = window.findChild<TabManager*>();
        require(tabs, "MainWindow has no TabManager");
        PluginRunner runner(tabs, &window);
        runner.exec("DirectBinaryFixture");
        const bool success = runner.getResult() == "success";
        driver.stop();
        const bool failure = scenario == "failed" || scenario == "crashed" || scenario == "cancelled";
        require(success != failure, "Unexpected plugin result");
        const bool changed = scenario != "noop" && scenario != "identical" && scenario != "touch";
        require(book->IsModified() == changed,
                "Direct binary overwrite was not reflected in Book::IsModified");
        require(window.isWindowModified() == changed,
                "Direct binary overwrite was not reflected in the window modified indicator");
        if (changed && scenario != "api-write" && scenario != "binary") {
            require(image_updates > 0, "Direct image overwrite did not notify image views");
        }
        if (scenario == "binary") {
            require(image_updates == 0, "Unrelated binary edit refreshed the image");
        }
        if (!changed) {
            require(image_updates == 0, "Unchanged image unnecessarily refreshed image views");
            require(invokeSave(window, "Save"), "No-op Save failed");
            require(readBytes(input) == original, "No-op plugin changed the EPUB bytes");
        } else {
            // Save A Copy must export the new bytes while leaving the book dirty.
            QTimer save_driver;
            QObject::connect(&save_driver, &QTimer::timeout, &window, [&]() {
                for (QWidget* widget : QApplication::topLevelWidgets()) {
                    if (auto* dialog = qobject_cast<QFileDialog*>(widget)) {
                        if (!dialog->isVisible()) continue;
                        dialog->selectFile(input + ".copy.epub");
                        QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
                        save_driver.stop();
                    }
                }
            });
            save_driver.start(10);
            require(invokeSave(window, "SaveACopy"), "Save A Copy failed after plugin edit");
            require(book->IsModified(), "Save A Copy cleared pending plugin changes");
            require(readBytes(input) == original, "Save A Copy overwrote the original EPUB");
            require(invokeSave(window, "Save"), "Save failed after plugin edit");
            require(!book->IsModified() && !window.isWindowModified(),
                    "Save did not clear the modified indicators");
            const QByteArray saved = readBytes(input);
            require(saved != original, "Save discarded the plugin's binary edit");
            require(invokeSave(window, "Save") && readBytes(input) == saved,
                    "Repeated Save changed an already saved EPUB");
        }
        std::cout << "Legacy plugin binary integration passed: "
                  << scenario.toStdString() << '\n';
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
