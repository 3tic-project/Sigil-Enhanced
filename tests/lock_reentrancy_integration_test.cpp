#include "EmbedPython/EmbeddedPython.h"

#include <QDialog>
#include <QCheckBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QResource>
#include <QSemaphore>
#include <QTimer>
#include <QTreeView>
#include <QtConcurrent>
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

#include "BookManipulation/Book.h"
#include "BookManipulation/FolderKeeper.h"
#include "Dialogs/HeadingSelector.h"
#include "Importers/ImportEPUB.h"
#include "MainUI/MainApplication.h"
#include "MainUI/TOCModel.h"
#include "Misc/SettingsStore.h"
#include "Misc/SpellCheck.h"
#include "ResourceObjects/HTMLResource.h"
#include "ResourceObjects/OPFResource.h"
#include "ResourceObjects/NCXResource.h"
#include "ResourceObjects/NavProcessor.h"

namespace {
void require(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

template<typename Predicate> void waitUntil(Predicate done)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!done() && elapsed.elapsed() < 5000) {
        QEventLoop loop;
        QTimer::singleShot(10, &loop, &QEventLoop::quit);
        loop.exec();
    }
    require(done(), "Timed out waiting for GUI work");
}

void checkPython(EmbeddedPython& python)
{
    int result = -1;
    QString traceback;
    const auto value = python.runInPython("builtins", "str", {42}, &result, traceback, false, false);
    require(result == 0 && value.toString() == "42", "Reentrant Python call failed");
}

void pythonErrors(const QByteArray& scenario)
{
    auto& python = EmbeddedPython::instance();
    int alerts = 0;
    bool reentered = false;
    QTimer closeAlert;
    QObject::connect(&closeAlert, &QTimer::timeout, [&]() {
        for (auto* widget : QApplication::topLevelWidgets()) {
            auto* dialog = qobject_cast<QDialog*>(widget);
            if (!dialog || !dialog->isVisible() || !dialog->inherits("AlertBox")) continue;
            require(dialog->thread() == QThread::currentThread(), "Python created an alert on a worker thread");
            ++alerts;
            std::cout << "Reenter Python from error dialog\n" << std::flush;
            checkPython(python);
            reentered = true;
            dialog->accept();
        }
    });
    closeAlert.start(10);
    auto fail = [&]() {
        int result = 0;
        QString traceback;
        if (scenario == "python-method") {
            auto value = python.runInPython("builtins", "dict", {}, &result, traceback, true, false);
            auto object = value.value<PyObjectPtr>();
            python.callPyObjMethod(object, "missing_method", {}, &result, traceback);
        } else {
            python.runInPython("operator", "truediv", {1, 0}, &result, traceback);
        }
        require(result < 0 && !traceback.isEmpty(), "Python failure lost its traceback");
    };
    if (scenario == "python-worker") {
        auto future = QtConcurrent::run(fail);
        waitUntil([&]() { return future.isFinished() && reentered; });
        future.waitForFinished();
    } else {
        fail();
        waitUntil([&]() { return reentered; });
    }
    require(reentered && alerts == 1, "Expected one Python error alert");
    int result = 0;
    QString traceback;
    python.runInPython("operator", "truediv", {1, 0}, &result, traceback, false, false);
    require(alerts == 1 && result < 0, "Silent Python failure showed a dialog");
    checkPython(python);
}

void pythonObjectCopies()
{
    auto& python = EmbeddedPython::instance();
    int result = -1;
    QString traceback;
    auto value = python.runInPython("builtins", "dict", {}, &result, traceback, true, false);
    auto original = value.value<PyObjectPtr>();
    require(result == 0 && original.object(), "Could not create Python object");
    const auto state = PyGILState_Ensure();
    const auto references = Py_REFCNT(original.object());
    PyGILState_Release(state);
    std::vector<std::thread> workers;
    for (int i = 0; i < 4; ++i) {
        workers.emplace_back([&]() {
            for (int j = 0; j < 2000; ++j) {
                PyObjectPtr copy(original);
                PyObjectPtr assigned;
                assigned = copy;
                PyObjectPtr borrowed(original.object());
                assigned = static_cast<PyObject*>(nullptr);
            }
        });
    }
    for (auto& worker : workers) worker.join();
    const auto endState = PyGILState_Ensure();
    const bool balanced = Py_REFCNT(original.object()) == references;
    PyGILState_Release(endState);
    require(balanced, "Concurrent Python pointer copies lost references");
    python.callPyObjMethod(original, "__len__", {}, &result, traceback);
    require(result == 0, "Python object became invalid after cross-thread copying");
}

// Pause an actual navigation read, so book switching is exercised while a
// parser is inside a resource operation rather than depending on thread timing.
class PausedNav : public HTMLResource
{
public:
    PausedNav(HTMLResource* original, FolderKeeper* keeper) :
        HTMLResource(original->GetFullPathToBookFolder(), original->GetFullPath(), keeper)
    {
        SetText(original->GetText());
        armed = true;
    }

    QString GetText() const override
    {
        if (armed.exchange(false)) {
            entered.release();
            proceed.acquire();
        }
        return HTMLResource::GetText();
    }

    mutable std::atomic_bool armed {false};
    mutable QSemaphore entered;
    mutable QSemaphore proceed;
};

void tocSwitch(const QString& input)
{
    auto loadBook = [&]() {
        ImportEPUB importer(input);
        return importer.GetBook();
    };
    auto oldBook = loadBook();
    auto newBook = loadBook();
    auto* replacementNav = newBook->GetOPF()->GetNavResource();
    replacementNav->SetText(QString(replacementNav->GetText()).replace(">Chapter<", ">Replacement<"));
    auto* original = oldBook->GetOPF()->GetNavResource();
    require(original, "Missing navigation fixture");
    PausedNav probe(original, oldBook->GetFolderKeeper());
    oldBook->GetOPF()->SetNavResource(&probe, false);
    TOCModel model;
    bool refreshed = false;
    QObject::connect(&model, &TOCModel::RefreshDone, [&]() { refreshed = true; });
    std::atomic_bool destroyed {false};
    std::atomic_bool destroyedOnGUI {false};
    QObject::connect(oldBook.data(), &QObject::destroyed, &model, [&]() {
        destroyedOnGUI = QThread::currentThread() == QCoreApplication::instance()->thread();
        destroyed = true;
    }, Qt::DirectConnection);
    model.SetBook(oldBook);
    waitUntil([&]() { return probe.entered.available() > 0; });
    probe.entered.acquire();
    std::cout << "Switch book while navigation read is paused\n" << std::flush;
    model.SetBook(newBook);
    model.Refresh();
    oldBook.reset();
    require(!destroyed, "Old book was destroyed while its navigation was being read");
    probe.proceed.release();
    waitUntil([&]() { return refreshed && destroyed; });
    require(destroyedOnGUI, "Worker destroyed the old book and its GUI documents");
    require(model.rowCount() == 1 && model.item(0)->text() == "Replacement", "Superseded background parse replaced the new book's TOC");
    const auto root = model.GetRootTOCEntry();
    require(root.children[0].target == "OEBPS/a.xhtml", "Navigation lost its book context");
}

void tocNCX(const QString& input)
{
    ImportEPUB importer(QFileInfo(input).absolutePath() + "/fixture-epub2.epub");
    auto book = importer.GetBook();
    auto* ncx = book->GetNCX();
    require(ncx, "Missing NCX fixture");
    ncx->SetText("<ncx><navMap><navPoint><navLabel><text>Parent</text></navLabel>"
                 "<content src='a.xhtml#heading'/><navPoint><navLabel><text>Child</text></navLabel>"
                 "<content src='#n1'/></navPoint></navPoint><navPoint><navLabel><text>Remote</text></navLabel>"
                 "<content src='https://example.test/toc'/></navPoint></navMap></ncx>");
    TOCModel model;
    model.SetBook(book, false);
    auto root = model.GetRootTOCEntry();
    require(root.children.size() == 2 && root.children[0].target == "OEBPS/a.xhtml#heading" &&
            root.children[0].children[0].target == "OEBPS/toc.ncx#n1" &&
            root.children[1].target == "https://example.test/toc", "NCX snapshot lost nested or relative link context");
}

void opfWarning(const QString& input)
{
    ImportEPUB importer(input);
    auto book = importer.GetBook();
    auto* opf = book->GetOPF();
    auto invalid = opf->GetText();
    invalid.replace("version='3.0'", "version='2.0'");
    invalid.replace("version=\"3.0\"", "version=\"2.0\"");
    require(invalid != opf->GetText(), "Fixture did not change package version");
    int alerts = 0;
    QTimer closeAlert;
    QObject::connect(&closeAlert, &QTimer::timeout, [&]() {
        for (auto* widget : QApplication::topLevelWidgets()) {
            auto* dialog = qobject_cast<QDialog*>(widget);
            if (!dialog || !dialog->isVisible() || !dialog->inherits("AlertBox")) continue;
            ++alerts;
            std::cout << "Read OPF from version warning\n" << std::flush;
            require(opf->GetPackageVersion() == "3.0", "Package version was not restored");
            opf->SetText(invalid); // A second edit must not show another warning.
            dialog->accept();
        }
    });
    closeAlert.start(10);
    opf->SetText(invalid);
    waitUntil([&]() { return alerts > 0; });
    require(alerts == 1 && opf->GetPackageVersion() == "3.0", "Package version warning repeated");
}

void spellReload()
{
    SettingsStore settings;
    settings.setDictionary("en_US");
    auto& checker = SpellCheck::instance();
    require(checker.dictionaries().contains("en_US"), "Missing real Hunspell dictionary");
    require(checker.spellPS("hello") && !checker.spellPS("zzzxxyynotaword"), "Primary dictionary was not loaded");
    checker.UnloadDictionary("en_US");
    std::cout << "Use spelling checks after dictionary unload\n" << std::flush;
    require(checker.spellPS("zzzxxyynotaword") && checker.suggestPS("helo").isEmpty(),
            "Unloaded dictionary left a cached primary pointer");
    checker.setDictionary("en_US");
    std::atomic_bool start {false};
    std::vector<std::thread> workers;
    for (int worker = 0; worker < 4; ++worker) {
        workers.emplace_back([&]() {
            while (!start.load()) std::this_thread::yield();
            for (int i = 0; i < 60; ++i) {
                checker.spellPS("hello");
                checker.spell("en: hello");
                checker.getWordChars("en");
                checker.ignoreWord("temporary");
                checker.isIgnored("temporary");
                if (i % 20 == 0) checker.suggestPS("helo");
            }
        });
    }
    start = true;
    for (int i = 0; i < 8; ++i) {
        checker.setDictionary("en_US", true);
        checker.UpdateLangCodeToDictMapping();
        checker.clearIgnoredWords();
    }
    for (auto& worker : workers) worker.join();
    checker.clearIgnoredWords();
    require(checker.spellPS("hello") && !checker.spellPS("zzzxxyynotaword"), "Concurrent reload corrupted dictionary");
}

void headingDialog(const QString& input)
{
    ImportEPUB importer(input);
    auto book = importer.GetBook();
    auto* chapter = qobject_cast<HTMLResource*>(book->GetFolderKeeper()->GetResourceByBookPath("OEBPS/a.xhtml"));
    require(chapter, "Missing heading fixture");
    chapter->SetText(QString(chapter->GetText())
        .replace("<body>", "<body><h1>Opening</h1>")
        .replace("</body>", "\n<h1 class=\"sigil_not_in_toc\">Hidden heading</h1>\n<h1>Chapter title</h1>\n</body>"));
    HeadingSelector dialog(book);
    auto* includedOnly = dialog.findChild<QCheckBox*>("cbTOCItemsOnly");
    require(includedOnly, "Missing heading visibility control");
    includedOnly->setChecked(true);
    bool read = false;
    QTimer::singleShot(0, &dialog, [&]() {
        std::cout << "Read navigation while heading dialog is open\n" << std::flush;
        // The Agent's tocHierarchy() uses this same navigation reader on GUI.
        const auto root = NavProcessor(book->GetOPF()->GetNavResource()).GetRootTOCEntry();
        require(root.children.size() == 1, "Heading dialog prevented navigation reading");
        chapter->GetManifestProperties();
        auto* tree = dialog.findChild<QTreeView*>("tvTOCDisplay");
        auto* model = tree ? qobject_cast<QStandardItemModel*>(tree->model()) : nullptr;
        require(model && model->rowCount() == 2, "Missing heading editor rows");
        model->item(1)->setText("Retitled");
        tree->setCurrentIndex(model->index(1, 0));
        tree->selectionModel()->select(model->index(1, 0), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        require(QMetaObject::invokeMethod(&dialog, "IncreaseHeadingLevel", Qt::DirectConnection), "Cannot change heading level");
        chapter->GetManifestProperties(); // Readers must also work after each edit.
        read = true;
        dialog.accept();
    });
    dialog.exec();
    require(read, "Heading dialog did not process reader");
    const QString changed = chapter->GetText();
    if (!changed.contains("Chapter title") || !changed.contains("sigil_toc_id_") ||
            !changed.contains("<h2") || !changed.contains("title=\"Retitled\"") ||
            !changed.contains("<h1 class=\"sigil_not_in_toc\">Hidden heading</h1>")) {
        std::cerr << changed.toStdString() << '\n';
        throw std::runtime_error("Heading update did not preserve the title, level and ID edits");
    }
}
}

int main(int argc, char** argv)
{
    MainApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    QResource::registerResource(QCoreApplication::applicationDirPath() + "/../Resources/main.rcc");
    try {
        require(argc == 4, "Expected source root, EPUB fixture and scenario");
        const QString source = QString::fromLocal8Bit(argv[1]);
        auto& python = EmbeddedPython::instance();
        python.addToPythonSysPath(qEnvironmentVariable("SIGIL_TEST_PYTHON_ROOT"));
        python.addToPythonSysPath(source + "/src/Resource_Files/plugin_launchers/python");
        python.addToPythonSysPath(source + "/src/Resource_Files/python3lib");
        SettingsStore settings;
        settings.setCleanOn(0);
        settings.setPreserveOPFSource(true);
        const QByteArray scenario(argv[3]);
        if (scenario == "python-objects") pythonObjectCopies();
        else if (scenario.startsWith("python-")) pythonErrors(scenario);
        else if (scenario == "toc-switch") tocSwitch(QString::fromLocal8Bit(argv[2]));
        else if (scenario == "toc-ncx") tocNCX(QString::fromLocal8Bit(argv[2]));
        else if (scenario == "opf-warning") opfWarning(QString::fromLocal8Bit(argv[2]));
        else if (scenario == "spell-reload") spellReload();
        else if (scenario == "heading-dialog") headingDialog(QString::fromLocal8Bit(argv[2]));
        else throw std::runtime_error("Unknown scenario");
        std::cout << scenario.constData() << " passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
