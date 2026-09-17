#include "EmbedPython/EmbeddedPython.h" // Python must precede Qt's slots macro.

#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QSignalMapper>
#include <QTimer>
#include <QWebEngineUrlScheme>

#include <cstdlib>
#include <iostream>
#include <stdexcept>

#include "BookManipulation/Book.h"
#include "BookManipulation/FolderKeeper.h"
#include "MainUI/BookBrowser.h"
#include "MainUI/MainApplication.h"
#include "MainUI/MainWindow.h"
#include "Misc/SettingsStore.h"
#include "ResourceObjects/HTMLResource.h"
#include "ResourceObjects/OPFResource.h"
#include "Tabs/FlowTab.h"
#include "Widgets/AlertBox.h"

namespace
{

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

QByteArray readBytes(const QString& path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read test EPUB");
    return file.readAll();
}

void writeBytes(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
            "Cannot write test EPUB");
    require(file.write(bytes) == bytes.size(), "Short write in test EPUB");
}

QString copyFixture(const QString& input, const QString& suffix)
{
    const QString output = input + suffix;
    QFile::remove(output);
    require(QFile::copy(input, output), "Cannot create an isolated scenario EPUB");
    return output;
}

bool invokeSave(MainWindow& window)
{
    bool result = false;
    require(QMetaObject::invokeMethod(
                &window, "Save", Qt::DirectConnection, Q_RETURN_ARG(bool, result)),
            "Could not invoke MainWindow Save");
    return result;
}

class ErrorDialogCloser final : public QObject
{
public:
    explicit ErrorDialogCloser(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    void closeNext()
    {
        m_Attempts = 0;
        QTimer::singleShot(0, this, [this]() { poll(); });
    }

private:
    void poll()
    {
        if (++m_Attempts > 500) {
            std::cerr << "Timed out waiting for an expected error dialog\n";
            std::_Exit(EXIT_FAILURE);
        }
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (widget->isVisible()
                && (qobject_cast<QMessageBox*>(widget)
                    || qobject_cast<AlertBox*>(widget))) {
                if (!QMetaObject::invokeMethod(
                        widget, "accept", Qt::QueuedConnection)) {
                    std::cerr << "Could not dismiss expected error dialog\n";
                    std::_Exit(EXIT_FAILURE);
                }
                return;
            }
        }
        QTimer::singleShot(10, this, [this]() { poll(); });
    }

    int m_Attempts = 0;
};

void configurePython(const QString& root)
{
    auto& python = EmbeddedPython::instance();
    python.addToPythonSysPath(qEnvironmentVariable("SIGIL_TEST_PYTHON_ROOT"));
    python.addToPythonSysPath(root + "/src/Resource_Files/plugin_launchers/python");
    python.addToPythonSysPath(root + "/src/Resource_Files/python3lib");
}

void checkRename(const QString& source)
{
    std::cerr << "scenario rename: construct\n" << std::flush;
    MainWindow window(source);
    const auto book = window.GetCurrentBook();
    auto* chapter = qobject_cast<HTMLResource*>(
        book->GetFolderKeeper()->GetResourceByBookPath(QStringLiteral("OEBPS/a.xhtml")));
    require(chapter, "Rename fixture chapter is missing");
    window.GetBookBrowser()->RenameResourceList(
        QList<Resource*>() << chapter,
        QStringList() << QStringLiteral("renamed.xhtml"));
    require(chapter->GetRelativePath() == QLatin1String("OEBPS/renamed.xhtml"),
            "Book Browser rename did not update the chapter path");
    require(book->IsModified(), "Book Browser rename did not mark the Book modified");
    if (HTMLResource* nav = book->GetOPF()->GetNavResource()) {
        require(nav->GetText().contains(QStringLiteral("renamed.xhtml")),
                "Book Browser rename did not update the live Nav target");
    }
    const QString output = source + QStringLiteral(".renamed.epub");
    require(window.ExportCurrentBookCopy(output),
            "Renamed EPUB could not be exported through MainWindow");
    std::cerr << "scenario rename: done\n" << std::flush;
}

void checkUndoRestoresNoOp(const QString& source, const QByteArray& original)
{
    std::cerr << "scenario undo: construct\n" << std::flush;
    MainWindow window(source);
    require(QMetaObject::invokeMethod(
                &window, "OpenFile", Qt::DirectConnection,
                Q_ARG(QString, QStringLiteral("OEBPS/a.xhtml")),
                Q_ARG(int, -1), Q_ARG(int, -1)),
            "Could not open the chapter through MainWindow");
    FlowTab* tab = window.GetCurrentFlowTab();
    require(tab, "Opening the fixture chapter did not create a FlowTab");
    for (int attempt = 0; attempt < 100 && !tab->IsLoadingFinished(); ++attempt) {
        MainApplication::processEvents();
    }
    require(tab->IsLoadingFinished(),
            "FlowTab did not finish its deferred document initialization");
    auto* chapter = qobject_cast<HTMLResource*>(tab->GetLoadedResource());
    require(chapter, "FlowTab is not backed by the fixture chapter");
    const QString before = chapter->GetText();
    const QString closingBody = QStringLiteral("</body>");
    const int bodyPosition = before.indexOf(closingBody);
    require(bodyPosition >= 0,
            "Could not locate the body element for the undo probe edit");
    require(tab->SetSelectionRange(
                bodyPosition, bodyPosition + closingBody.size()),
            "Could not select the body element for the undo probe edit");
    require(tab->ReplaceSelectedText(
                QStringLiteral("<!-- undo probe --></body>"), false),
            "Could not create the undo probe edit");
    MainApplication::processEvents();
    require(tab->IsModified(),
            "The undo probe edit did not mark the document modified");
    require(window.GetCurrentBook()->IsModified(),
            "The undo probe edit did not mark the Book modified");
    tab->Undo();
    MainApplication::processEvents();
    require(chapter->GetText() == before && !tab->IsModified(),
            "Undo did not restore the exact original chapter text/state");
    require(!window.GetCurrentBook()->IsModified(),
            "Exact Undo did not restore the Book's original-save eligibility");
    require(invokeSave(window), "Save after exact Undo failed");
    require(readBytes(source) == original,
            "Save after exact Undo lost byte-identical no-op eligibility");

    window.GetCurrentBook()->SetModified();
    require(tab->SetSelectionRange(
                bodyPosition, bodyPosition + closingBody.size()),
            "Could not select the body element for the structural-dirty probe");
    require(tab->ReplaceSelectedText(
                QStringLiteral("<!-- structural-dirty probe --></body>"), false),
            "Could not create the structural-dirty probe edit");
    MainApplication::processEvents();
    tab->Undo();
    MainApplication::processEvents();
    require(chapter->GetText() == before && !tab->IsModified(),
            "Structural-dirty probe Undo did not restore the chapter");
    require(window.GetCurrentBook()->IsModified(),
            "Text Undo incorrectly cleared a non-text Book modification");
    window.GetCurrentBook()->SetModified(false);

    require(tab->SetSelectionRange(
                bodyPosition, bodyPosition + closingBody.size()),
            "Could not select the body element for the persisted edit probe");
    require(tab->ReplaceSelectedText(
                QStringLiteral("<!-- persisted probe --></body>"), false),
            "Could not create the persisted edit probe");
    MainApplication::processEvents();
    window.SaveTabData();
    require(!tab->IsModified() && window.GetCurrentBook()->IsModified(),
            "Writing a tab to the work folder lost Book dirtiness");
    const QString persisted = chapter->GetText();
    const int persistedBodyPosition = persisted.indexOf(closingBody);
    require(persistedBodyPosition >= 0,
            "Could not locate the body element after persisting the probe");
    require(tab->SetSelectionRange(
                persistedBodyPosition,
                persistedBodyPosition + closingBody.size()),
            "Could not select the body element for the second probe edit");
    require(tab->ReplaceSelectedText(
                QStringLiteral("<!-- second probe --></body>"), false),
            "Could not create the second probe edit");
    MainApplication::processEvents();
    tab->Undo();
    MainApplication::processEvents();
    require(chapter->GetText() == persisted && !tab->IsModified(),
            "Undo did not return to the persisted work-folder state");
    require(window.GetCurrentBook()->IsModified(),
            "Undo to a dirty work-folder state incorrectly cleared the Book");
    tab->Undo();
    MainApplication::processEvents();
    require(chapter->GetText() == before,
            "Undo across a work-folder write did not restore the source text");
    require(!window.GetCurrentBook()->IsModified(),
            "Undo across a work-folder write did not restore source eligibility");
    std::cerr << "scenario undo: done\n" << std::flush;
}

void checkExternalConflict(const QString& source, const QByteArray& original)
{
    std::cerr << "scenario conflict: construct\n" << std::flush;
    MainWindow window(source);
    const QByteArray external = original + QByteArrayLiteral("external-change");
    writeBytes(source, external);
    ErrorDialogCloser closer(&window);
    closer.closeNext();
    require(!invokeSave(window), "Save overwrote an externally changed source EPUB");
    require(readBytes(source) == external,
            "Rejected Save damaged the externally changed source EPUB");
    std::cerr << "scenario conflict: done\n" << std::flush;
}

void checkWriteFailure(const QString& source, const QByteArray& original)
{
    std::cerr << "scenario write failure: construct\n" << std::flush;
    MainWindow window(source);
    const QString blocked = source + QStringLiteral(".blocked.epub");
    QDir().mkpath(blocked);
    ErrorDialogCloser closer(&window);
    closer.closeNext();
    require(!window.ExportCurrentBookCopy(blocked),
            "Save A Copy unexpectedly succeeded when destination was a directory");
    require(QFileInfo(blocked).isDir(),
            "Failed Save A Copy replaced or removed the blocking destination");
    require(readBytes(source) == original,
            "Failed Save A Copy changed the source EPUB");
    std::cerr << "scenario write failure: done\n" << std::flush;
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

    try {
        require(argc == 4,
                "Expected source root, disposable EPUB fixture and scenario");
        const QString root = QString::fromLocal8Bit(argv[1]);
        const QString input = QFileInfo(QString::fromLocal8Bit(argv[2])).absoluteFilePath();
        const QString scenario = QString::fromLocal8Bit(argv[3]);
        configurePython(root);
        SettingsStore settings;
        settings.setPreserveOPFSource(true);
        settings.setCleanOn(0);
        const QByteArray original = readBytes(input);

        if (scenario == QLatin1String("rename")) {
            checkRename(copyFixture(input, QStringLiteral(".rename.epub")));
        } else if (scenario == QLatin1String("undo")) {
            checkUndoRestoresNoOp(
                copyFixture(input, QStringLiteral(".undo.epub")), original);
        } else if (scenario == QLatin1String("conflict")) {
            checkExternalConflict(
                copyFixture(input, QStringLiteral(".conflict.epub")), original);
        } else if (scenario == QLatin1String("write-failure")) {
            checkWriteFailure(
                copyFixture(input, QStringLiteral(".failure.epub")), original);
        } else {
            require(false, "Unknown MainWindow OPF completion scenario");
        }
        require(readBytes(input) == original,
                "OPF completion scenarios changed their base fixture");

        std::cout << "MainWindow OPF completion scenario passed: "
                  << scenario.toStdString() << '\n';
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
