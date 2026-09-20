#include "EmbedPython/EmbeddedPython.h" // Python must precede Qt's slots macro.

#include <QApplication>
#include <QLabel>
#include <QTextEdit>
#include <QTimer>

#include <cstdlib>
#include <iostream>
#include <stdexcept>

#include "BookManipulation/Book.h"
#include "BookManipulation/CleanSource.h"
#include "BookManipulation/FolderKeeper.h"
#include "BookManipulation/XhtmlDoc.h"
#include "Importers/ImportEPUB.h"
#include "Misc/SettingsStore.h"
#include "Parsers/GumboInterface.h"
#include "ResourceObjects/HTMLResource.h"

static void Require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTimer dialog_guard;
    QObject::connect(&dialog_guard, &QTimer::timeout, [&]() {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (widget->isVisible()) {
                std::cerr << "Unexpected modal dialog: " << widget->metaObject()->className() << '\n';
                for (QLabel *label : widget->findChildren<QLabel *>())
                    std::cerr << label->text().toStdString() << '\n';
                for (QTextEdit *details : widget->findChildren<QTextEdit *>())
                    std::cerr << details->toPlainText().toStdString() << '\n';
                // Do not run Python singleton destructors while a Python error
                // handler is itself displaying the dialog with the GIL held.
                std::_Exit(EXIT_FAILURE);
            }
        }
    });
    dialog_guard.start(100);
    try {
        Require(argc == 3, "Expected the source root and EPUB fixture");
        const QString root = QString::fromLocal8Bit(argv[1]);
        auto &python = EmbeddedPython::instance();
        python.addToPythonSysPath(qEnvironmentVariable("SIGIL_TEST_PYTHON_ROOT"));
        python.addToPythonSysPath(root + "/src/Resource_Files/plugin_launchers/python");
        python.addToPythonSysPath(root + "/src/Resource_Files/python3lib");

        // An empty source is never parsed by gumbo, so there is no output tree
        // to inspect. The syntax check must report "no errors" instead of
        // dereferencing the missing parse output.
        for (const QString version : {QString("2.0"), QString("3.0")}) {
            const XhtmlDoc::WellFormedError error =
                XhtmlDoc::GumboWellFormedErrorForSource(QString(), version);
            Require(error.line == -1, "An empty source must be reported as well formed");
        }

        GumboInterface empty_fragment(QString(), "3.0");
        Require(empty_fragment.fragment_error_check().isEmpty(),
                "An empty fragment must report no syntax errors");

        // Empty content stays empty when it is pretty printed.
        Require(CleanSource::PrettyPrint(QString(), false, "3.0").isEmpty(),
                "Pretty printing empty content must not invent content");

        // The same checks through the Book API that PrettyPrint uses, with a
        // fixture that really contains an empty XHTML resource.
        const QString input = QString::fromLocal8Bit(argv[2]);
        ImportEPUB importer(input);
        const auto book = importer.GetBook();
        auto *empty_resource = qobject_cast<HTMLResource *>(
            book->GetFolderKeeper()->GetResourceByBookPath("OEBPS/empty.xhtml"));
        Require(empty_resource != nullptr, "Fixture empty resource is missing");
        Require(empty_resource->GetText().isEmpty(), "Fixture empty resource is not empty");
        Require(book->IsDataGumboWellFormed(empty_resource),
                "An empty resource must be well formed");
        Require(book->SafePrettyPrint("OEBPS/empty.xhtml", empty_resource->GetText()).isEmpty(),
                "Pretty printing an empty resource must keep it empty");

        auto *chapter = qobject_cast<HTMLResource *>(
            book->GetFolderKeeper()->GetResourceByBookPath("OEBPS/a.xhtml"));
        Require(chapter != nullptr, "Fixture chapter is missing");
        Require(book->IsDataGumboWellFormed(chapter),
                "The well-formed fixture chapter must pass the syntax check");

        std::cout << "Empty-source gumbo syntax checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
