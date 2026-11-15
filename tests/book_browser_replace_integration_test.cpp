#include "EmbedPython/EmbeddedPython.h"

#include <QAbstractButton>
#include <QFile>
#include <QFileDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QResource>
#include <QSignalMapper>
#include <QTimer>
#include <QWebEngineUrlScheme>
#include <iostream>
#include <stdexcept>

#include "BookManipulation/Book.h"
#include "BookManipulation/FolderKeeper.h"
#include "MainUI/BookBrowser.h"
#include "MainUI/MainApplication.h"
#include "MainUI/MainWindow.h"
#include "Misc/SettingsStore.h"
#include "ResourceObjects/FontResource.h"
#include "ResourceObjects/OPFResource.h"

namespace {
void require(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

QByteArray bytes(const QString& path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read replacement fixture");
    return file.readAll();
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
    app.setQuitOnLastWindowClosed(false);
    QResource::registerResource(QCoreApplication::applicationDirPath() + "/../Resources/main.rcc");
    try {
        require(argc == 4, "Expected root, disposable EPUB and dialog style");
        const QString root = QString::fromLocal8Bit(argv[1]);
        const QString input = QString::fromLocal8Bit(argv[2]);
        const QString style = QString::fromLocal8Bit(argv[3]);
        auto& python = EmbeddedPython::instance();
        python.addToPythonSysPath(qEnvironmentVariable("SIGIL_TEST_PYTHON_ROOT"));
        python.addToPythonSysPath(root + "/src/Resource_Files/plugin_launchers/python");
        python.addToPythonSysPath(root + "/src/Resource_Files/python3lib");
        SettingsStore settings;
        settings.setCleanOn(0);
        settings.setPreserveOPFSource(true);
        settings.setFileDialogStyle(style);
        MainWindow window(input);
        window.setAttribute(Qt::WA_DeleteOnClose, false);
        window.showNormal();
        auto book = window.GetCurrentBook();
        auto* browser = window.findChild<BookBrowser*>();
        require(browser, "Missing real Book Browser");
        const QString fontPath = root + "/tests/fixtures/fonts/OpenSans-Test.ttf";
        auto* font = qobject_cast<FontResource*>(book->GetFolderKeeper()->AddContentFileToFolder(fontPath));
        require(font, "Cannot import fixture font");
        browser->Refresh();
        const QString identifier = font->GetIdentifier();
        const QString bookPath = font->GetRelativePath();
        const QString opf = book->GetOPF()->GetText();
        const QByteArray expected = bytes(fontPath);
        window.OpenResourceAndWaitUntilLoaded(font);
        book->GetFolderKeeper()->SuspendWatchingResources();

        for (bool accept : {false, true}) {
            // Alter only the disposable book copy so replacing identical names
            // must actually restore different bytes, including an open font tab.
            QFile target(font->GetFullPath());
            require(target.open(QIODevice::WriteOnly | QIODevice::Append), "Cannot alter disposable font");
            target.write("old-font-tail");
            target.close();
            const QByteArray before = bytes(font->GetFullPath());
            book->SetModified(false);
            bool selected = false, confirmed = false;
            QTimer drive;
            QObject::connect(&drive, &QTimer::timeout, &window, [&]() {
                for (QWidget* widget : QApplication::topLevelWidgets()) {
                    if (auto* dialog = qobject_cast<QFileDialog*>(widget)) {
                        if (dialog->isVisible()) {
                            selected = true;
                            dialog->selectFile(fontPath);
                            // selectFile intentionally leaves focused filename
                            // edits alone in a visible Qt dialog. Enter the path
                            // as the user would, rather than relying on focus.
                            if (auto* filename = dialog->findChild<QLineEdit*>("fileNameEdit")) {
                                filename->setText(fontPath);
                            }
                            // QFileSystemModel populates asynchronously on the
                            // desktop platform. Retry selection until the real
                            // dialog accepts, rather than accepting only once.
                            QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
                        }
                    } else if (auto* message = qobject_cast<QMessageBox*>(widget)) {
                        if (message->isVisible() && !confirmed) {
                            confirmed = true;
                            message->button(accept ? QMessageBox::Yes : QMessageBox::No)->click();
                        }
                    }
                }
            });
            drive.start(10);
            browser->AddExisting();
            drive.stop();
            require(selected && confirmed, "Did not exercise file selection and replacement confirmation");
            require(bytes(font->GetFullPath()) == (accept ? expected : before), "Replacement or rejection wrote wrong bytes");
            require(book->IsModified() == accept, "Replacement dirty state is incorrect");
            require(book->GetFolderKeeper()->GetResourceByBookPath(bookPath) == font &&
                    font->GetIdentifier() == identifier && book->GetOPF()->GetText() == opf,
                    "Replacing a font changed its identity, path or manifest");
        }
        book->SetModified(false);
        std::cout << "File-dialog font replacement/rejection passed; style=" << style.toStdString() << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
