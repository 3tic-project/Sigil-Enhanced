#include "EmbedPython/EmbeddedPython.h"

#include <QEventLoop>
#include <QFile>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPointer>
#include <QResource>
#include <QSignalMapper>
#include <QTextCursor>
#include <QTimer>
#include <QTreeView>
#include <QWebEngineUrlScheme>
#include <iostream>
#include <stdexcept>

#include "BookManipulation/Book.h"
#include "BookManipulation/FolderKeeper.h"
#include "MainUI/BookBrowser.h"
#include "MainUI/MainApplication.h"
#include "MainUI/MainWindow.h"
#include "MainUI/OPFModel.h"
#include "Misc/SettingsStore.h"
#include "ResourceObjects/HTMLResource.h"
#include "ResourceObjects/OPFResource.h"
#include "Tabs/ContentTab.h"

namespace {
void require(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

void settle()
{
    QEventLoop loop;
    QTimer::singleShot(100, &loop, &QEventLoop::quit);
    loop.exec();
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
        require(argc == 4, "Expected source root, EPUB fixture and editing scenario");
        const QString root = QString::fromLocal8Bit(argv[1]);
        const QString input = QString::fromLocal8Bit(argv[2]);
        const bool edit_first = QByteArray(argv[3]) == "edited";
        const bool wait_for_editor = QByteArray(argv[3]) != "immediate";
        const bool observe_text = QByteArray(argv[3]) == "observed";
        auto& python = EmbeddedPython::instance();
        python.addToPythonSysPath(qEnvironmentVariable("SIGIL_TEST_PYTHON_ROOT"));
        python.addToPythonSysPath(root + "/src/Resource_Files/plugin_launchers/python");
        python.addToPythonSysPath(root + "/src/Resource_Files/python3lib");
        SettingsStore settings;
        settings.setCleanOn(0);
        settings.setPreserveOPFSource(true);
        MainWindow window(input);
        window.setAttribute(Qt::WA_DeleteOnClose, false);
        window.showNormal();
        auto book = window.GetCurrentBook();
        auto* browser = window.findChild<BookBrowser*>();
        auto* model = browser ? browser->findChild<OPFModel*>() : nullptr;
        auto* original = qobject_cast<HTMLResource*>(book->GetFolderKeeper()->GetResourceByBookPath("OEBPS/a.xhtml"));
        require(browser && model && original, "Missing copy/rename fixture");
        window.OpenResourceAndWaitUntilLoaded(original);
        for (int iteration = 0; iteration < 3; ++iteration) {
            const QString current_original = original->GetText();
            browser->UpdateSelection(original);
            std::cout << "Copy " << iteration << '\n' << std::flush;
            browser->CopyHTML();
            auto* copy = qobject_cast<HTMLResource*>(window.GetCurrentContentTab()->GetLoadedResource());
            require(copy && copy != original && copy->GetText() == current_original, "Add Copy lost the original text");
            const QString identifier = copy->GetIdentifier();
            if (wait_for_editor) window.OpenResourceAndWaitUntilLoaded(copy);
            QObject observer;
            QString observed_text;
            if (observe_text) {
                // GUI consumers (including book dirty-state tracking) may read
                // resource text synchronously from document notifications.
                QObject::connect(&copy->GetTextDocumentForWriting(), &QTextDocument::contentsChanged,
                                 &observer, [&, copy]() { observed_text = copy->GetText(); });
            }
            if (edit_first) {
                QTextCursor cursor(&copy->GetTextDocumentForWriting());
                cursor.movePosition(QTextCursor::End);
                cursor.insertText(QStringLiteral("\n<!-- edited before rename -->"));
            }
            const QString name = QStringLiteral("copy-%1.xhtml").arg(iteration);
            const QString old_path = copy->GetFullPath();
            const auto index = model->GetModelItemIndex(copy, OPFModel::IndexChoice_Current);
            require(index.isValid(), "Cannot locate the copied XHTML in Book Browser");
            browser->UpdateSelection(copy);
            std::cout << "Rename " << iteration << '\n' << std::flush;
            require(QMetaObject::invokeMethod(browser, "Rename", Qt::DirectConnection), "Cannot start inline rename");
            auto* tree = browser->findChild<QTreeView*>();
            QPointer<QLineEdit> editor = tree ? tree->findChild<QLineEdit*>() : nullptr;
            require(editor && editor->isVisible(), "The actual filename editor did not open");
            editor->setText(name);
            editor->setModified(true);
            QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
            QApplication::sendEvent(editor, &enter);
            if (editor) {
                QKeyEvent release(QEvent::KeyRelease, Qt::Key_Return, Qt::NoModifier);
                QApplication::sendEvent(editor, &release);
            }
            std::cout << "Apply queued updates " << iteration << '\n' << std::flush;
            settle();
            require(copy->Filename() == name && QFile::exists(copy->GetFullPath()) && !QFile::exists(old_path),
                    "Rename did not relocate the copied XHTML");
            require(copy->GetIdentifier() == identifier && copy->GetText().contains("Original paragraph") &&
                    copy->GetText().contains("edited before rename") == edit_first,
                    "Rename changed the copy identity or unsaved text");
            require(original->GetText().contains("Original paragraph") &&
                    !original->GetText().contains("edited before rename"), "Renaming the copy changed the original");
            require(!observe_text || observed_text == copy->GetText(), "Document notification returned stale text");
        }
        require(window.ExportCurrentBookCopy(input + "." + QString::fromLocal8Bit(argv[3]) + ".epub"),
                "Cannot export the copied and renamed book");
        book->SetModified(false);
        std::cout << "Copy/rename integration passed; edited=" << edit_first << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
