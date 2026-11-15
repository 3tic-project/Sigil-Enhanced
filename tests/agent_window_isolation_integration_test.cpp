#include "EmbedPython/EmbeddedPython.h"
#include <QComboBox>
#include <QAbstractItemView>
#include <QFileInfo>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QLineEdit>
#include <QProcess>
#include <QResource>
#include <QProcessEnvironment>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QWebEngineUrlScheme>
#include <iostream>
#include <stdexcept>
#include "Agent/Execution/SigilBookWorkspace.h"
#include "Agent/Tools/BookTools.h"
#include "Agent/Tools/ToolRegistry.h"
#include "BookManipulation/Book.h"
#include "BookManipulation/FolderKeeper.h"
#include "Dialogs/Preferences.h"
#include "MainUI/MainApplication.h"
#include "MainUI/MainWindow.h"
#include "Misc/SettingsStore.h"
#include "ResourceObjects/TextResource.h"
#include "ResourceObjects/OPFResource.h"
#include "Tabs/FlowTab.h"

static void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
static void settle(int milliseconds = 80)
{
    QElapsedTimer timer; timer.start();
    while (timer.elapsed() < milliseconds) { QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(1); }
}

int main(int argc, char **argv)
{
    QCoreApplication::setAttribute(Qt::AA_DisableShaderDiskCache);
    QWebEngineUrlScheme scheme("sigil"); scheme.setSyntax(QWebEngineUrlScheme::Syntax::Path);
    scheme.setFlags(QWebEngineUrlScheme::SecureScheme | QWebEngineUrlScheme::LocalScheme | QWebEngineUrlScheme::LocalAccessAllowed
        | QWebEngineUrlScheme::ContentSecurityPolicyIgnored | QWebEngineUrlScheme::FetchApiAllowed);
    QWebEngineUrlScheme::registerScheme(scheme);
    MainApplication app(argc, argv);
    QResource::registerResource(QCoreApplication::applicationDirPath() + "/../Resources/main.rcc");
    try {
        require(argc >= 3, "Expected source root and synthetic EPUB");
        const QString root = QString::fromLocal8Bit(argv[1]), source = QString::fromLocal8Bit(argv[2]);
        auto &python = EmbeddedPython::instance();
        python.addToPythonSysPath(qEnvironmentVariable("SIGIL_TEST_PYTHON_ROOT"));
        python.addToPythonSysPath(root + "/src/Resource_Files/plugin_launchers/python");
        python.addToPythonSysPath(root + "/src/Resource_Files/python3lib");
        SettingsStore settings; settings.setCleanOn(0); settings.setPreserveOPFSource(true);
        if (argc == 4) {
            MainWindow other(source); other.show(); settle(150);
            Preferences preferences(&other);
            QComboBox combo(&preferences); combo.addItems({"keep this popup open", "another choice"});
            combo.move(20, 20); combo.show();
            preferences.open(); preferences.activateWindow(); settle(); combo.showPopup(); settle();
            require(preferences.isVisible() && combo.view()->isVisible(), "Separate-process popup did not open");
            std::cout << "READY\n" << std::flush;
            QTimer probe;
            QObject::connect(&probe, &QTimer::timeout, &other, [&]() {
                if (!preferences.isVisible() || !combo.view()->isVisible()) {
                    std::cout << "UNEXPECTED_CLOSE\n" << std::flush; app.exit(2);
                }
            });
            probe.start(25); QTimer::singleShot(20000, &app, &QCoreApplication::quit);
            return app.exec();
        }
        MainWindow writer(source); writer.resize(1000, 750); writer.show();
        auto *resource = qobject_cast<TextResource *>(writer.GetCurrentBook()->GetFolderKeeper()->GetResourceByBookPathNoThrow("OEBPS/a.xhtml"));
        require(resource, "Writer chapter missing"); writer.OpenResourceAndWaitUntilLoaded(resource);
        auto *tab = qobject_cast<FlowTab *>(writer.GetCurrentContentTab()); require(tab, "Writer Code View missing"); settle(150);
        SigilAgent::SigilBookWorkspace workspace; workspace.setBook(writer.GetCurrentBook());
        SigilAgent::ToolRegistry tools; SigilAgent::registerBookTools(&tools, &workspace);
        int edits = 0;
        const auto backgroundEdit = [&]() {
            require(tools.find("book.summary")->execute({}).ok, "Background read failed");
            require(tools.find("transaction.begin")->execute({{"label", "background fixture edit"}}).ok, "Begin failed");
            QString text = resource->GetText();
            text.replace("</body>", QStringLiteral("<!--background edit %1--></body>").arg(++edits));
            require(tools.find("resource.replace_text")->execute({{"resource_id", resource->GetIdentifier()}, {"text", text},
                {"expected_revision", qint64(workspace.resourceRevision(resource->GetIdentifier()))}}).ok, "Stage failed");
            require(tools.find("transaction.commit")->execute({{"expected_book_revision", qint64(workspace.revision())}}).ok, "Commit failed");
            // Exercise the editor notification paths used by native refreshes,
            // including linked-resource reloads, after an Agent commit.
            tab->ContentChangedExternally(); tab->ReloadTabIfPending(); settle();
        };
        {
            MainWindow other(source); other.resize(900, 700); other.show(); settle(150);
            const auto unchanged = other.GetCurrentBook()->GetOPF()->GetText();
            auto *otherChapter = qobject_cast<TextResource *>(other.GetCurrentBook()->GetFolderKeeper()->GetResourceByBookPathNoThrow("OEBPS/a.xhtml"));
            require(otherChapter, "Observer chapter missing");
            if (!otherChapter->IsLoaded()) otherChapter->InitialLoad();
            const QString unchangedChapter = otherChapter->GetText();
            bool stoleFocus = false;
            const auto checkDialog = [&](QDialog &dialog) {
                bool finished = false; QObject lifetime;
                QObject::connect(&dialog, &QDialog::finished, &lifetime, [&](int) { finished = true; });
                dialog.setWindowModality(Qt::WindowModal); dialog.open(); dialog.activateWindow(); settle();
                require(dialog.isVisible(), "Observer dialog did not open");
                backgroundEdit();
                require(dialog.isVisible() && !finished, "Agent work closed another book window's dialog");
                stoleFocus |= QApplication::activeWindow() == &writer;
                dialog.reject();
            };
            { QDialog dialog(&other); auto *layout = new QVBoxLayout(&dialog); layout->addWidget(new QLineEdit("unsaved dialog input", &dialog)); checkDialog(dialog); }
            { Preferences dialog(&other); checkDialog(dialog); }
            { QFileDialog dialog(&other); dialog.setOption(QFileDialog::DontUseNativeDialog); dialog.setDirectory(QFileInfo(source).absolutePath()); checkDialog(dialog); }
            if (QApplication::platformName() == "cocoa") {
                QFileDialog nativeDialog(&other); nativeDialog.setDirectory(QFileInfo(source).absolutePath()); checkDialog(nativeDialog);
            }
            {
                QDialog dialog(&other); auto *layout = new QVBoxLayout(&dialog); auto *combo = new QComboBox(&dialog);
                combo->addItems({"preserve popup", "another choice"}); layout->addWidget(combo);
                dialog.show(); dialog.activateWindow(); settle(); combo->showPopup(); settle();
                backgroundEdit();
                require(dialog.isVisible() && combo->view()->isVisible(), "Background refresh dismissed another window's popup");
                combo->hidePopup(); dialog.reject();
            }
            require(!stoleFocus, "Background refresh activated the writer over another dialog");
            require(other.GetCurrentBook()->GetOPF()->GetText() == unchanged && otherChapter->GetText() == unchangedChapter && !other.GetCurrentBook()->IsModified(), "Writer touched the observer book");
            other.GetCurrentBook()->SetModified(false); require(other.close(), "Observer did not close");
        }
        {
            QProcess child; auto environment = QProcessEnvironment::systemEnvironment();
            const QString childPreferences = qEnvironmentVariable("SIGIL_PREFS_DIR") + "/child";
            QDir().mkpath(childPreferences + "/workspace"); environment.insert("SIGIL_PREFS_DIR", childPreferences); child.setProcessEnvironment(environment);
            child.start(QFileInfo(QString::fromLocal8Bit(argv[0])).absoluteFilePath(), {root, source, "--dialog-child"});
            require(child.waitForStarted(5000), "Separate Sigil test process did not start");
            QByteArray output; QElapsedTimer timer; timer.start();
            while (!output.contains("READY") && timer.elapsed() < 10000 && child.state() != QProcess::NotRunning) {
                settle(10); output += child.readAllStandardOutput();
            }
            if (!output.contains("READY")) std::cerr << "Child state=" << int(child.state()) << " exit=" << child.exitCode()
                << " stdout=" << output.constData() << " stderr=" << child.readAllStandardError().constData() << '\n';
            require(output.contains("READY"), "Separate process dialog was not ready");
            backgroundEdit(); settle(120); output += child.readAllStandardOutput();
            const bool preserved = child.state() == QProcess::Running && !output.contains("UNEXPECTED_CLOSE");
            child.terminate(); if (!child.waitForFinished(3000)) { child.kill(); child.waitForFinished(3000); }
            require(preserved, "Agent refresh dismissed a dialog or popup in another process");
        }
        writer.GetCurrentBook()->SetModified(false);
        std::cout << "Agent background edits preserve other-window and separate-process dialogs/popup focus; platform="
                  << QApplication::platformName().toStdString() << "\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
    return 0;
}
