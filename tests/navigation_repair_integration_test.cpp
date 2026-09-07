#include "EmbedPython/EmbeddedPython.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextCursor>
#include <QTextEdit>
#include <QTimer>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>

#include "BookManipulation/NavigationRepair.h"
#include "BookManipulation/FolderKeeper.h"
#include "Dialogs/NavigationRepairDialog.h"
#include "Exporters/ExportEPUB.h"
#include "Importers/ImportEPUB.h"
#include "MainUI/OPFModel.h"
#include "MainUI/TableOfContents.h"
#include "Misc/SettingsStore.h"
#include "ResourceObjects/NavProcessor.h"
#include "ResourceObjects/NCXResource.h"
#include "Widgets/TextDocument.h"

static void Require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

static QByteArray ReadBytes(const QString &path)
{
    QFile file(path);
    Require(file.open(QIODevice::ReadOnly), "Cannot read test file");
    return file.readAll();
}

struct RestorePermissions {
    QString path;
    QFile::Permissions permissions;
    ~RestorePermissions() { QFile::setPermissions(path, permissions); }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *expectedDialog = nullptr;
    QTimer dialogGuard;
    QObject::connect(&dialogGuard, &QTimer::timeout, [&]() {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (widget->isVisible() && widget != expectedDialog) {
                std::cerr << "Unexpected dialog: " << widget->metaObject()->className() << '\n';
                for (auto *label : widget->findChildren<QLabel *>()) std::cerr << label->text().toStdString() << '\n';
                for (auto *details : widget->findChildren<QTextEdit *>()) std::cerr << details->toPlainText().toStdString() << '\n';
                std::_Exit(EXIT_FAILURE);
            }
        }
    });
    dialogGuard.start(100);
    try {
        Require(argc == 3, "Expected source root and fixture path");
        const QString sourceRoot = QString::fromLocal8Bit(argv[1]);
        const QString base = QString::fromLocal8Bit(argv[2]);
        auto &python = EmbeddedPython::instance();
        python.addToPythonSysPath(qEnvironmentVariable("SIGIL_TEST_PYTHON_ROOT"));
        python.addToPythonSysPath(sourceRoot + "/src/Resource_Files/plugin_launchers/python");
        python.addToPythonSysPath(sourceRoot + "/src/Resource_Files/python3lib");
        SettingsStore settings;
        settings.setPreserveOPFSource(true);
        settings.setCleanOn(0);
        NavProcessor absent(nullptr);
        Require(absent.GetTOC().isEmpty() && absent.GetLandmarkNameForPaths().isEmpty()
                && absent.GetAllLandmarkInfoByBookPath().isEmpty(), "Absent nav lookup was not empty");

        for (const QString variant : { "missing", "no-ncx", "declared", "bad-target", "bad-fragment", "warning" }) {
            std::cerr << "Checking navigation fixture: " << variant.toStdString() << '\n';
            const QString input = base + "." + variant;
            ImportEPUB importer(input);
            const auto book = importer.GetBook();
            auto *opf = book->GetOPF();
            auto folder = book->GetFolderKeeper();
            const QStringList originalPaths = folder->GetAllBookPaths();
            Require(!opf->GetNavResource(), "Import silently generated or misidentified navigation");
            Require(!book->IsModified(), "A read-only load warning marked the book modified");
            Require(!importer.GetLoadWarnings().isEmpty(), "Missing navigation has no diagnostic");
            Require(ReadBytes(opf->GetFullPath()) == ReadBytes(input + ".opf"), "Import changed OPF bytes");
            Require(!opf->GetSpineOrderResources(folder->GetResourceList()).contains(nullptr), "Spine watch list contains null nav");
            OPFModel browser;
            browser.SetBook(book);
            Require(!book->IsModified(), "Browser metadata lookup changed the book");
            std::unique_ptr<TableOfContents> panel;
            QEventLoop refreshLoop;
            QTimer refreshTimeout;
            refreshTimeout.setSingleShot(true);
            QObject::connect(&refreshTimeout, &QTimer::timeout, &refreshLoop, &QEventLoop::quit);
            bool refreshed = false;
            int repairRequests = 0;
            if (variant == "missing") {
                panel = std::make_unique<TableOfContents>();
                QObject::connect(panel->findChild<TOCModel *>(), &TOCModel::RefreshDone, [&]() {
                    refreshed = true;
                    refreshLoop.quit();
                });
                panel->SetBook(book);
                // Refreshes arriving while the first parse is active must be
                // coalesced, not dropped or published as a stale final model.
                panel->Refresh();
                panel->Refresh();
                refreshTimeout.start(5000);
                refreshLoop.exec();
                Require(refreshed, "NCX fallback panel did not finish refreshing");
                Require(!panel->findChild<QWidget *>("missingNavigationNotice")->isHidden(), "Missing-navigation notice is hidden");
                QObject::connect(panel.get(), &TableOfContents::NavigationRepairRequested, panel.get(), [&]() { ++repairRequests; });
                panel->findChild<QPushButton *>("generateNavigationDocument")->click();
                Require(repairRequests == 1 && folder->GetAllBookPaths() == originalPaths, "Repair button mutated resources instead of requesting preview");
            }
            TOCModel toc;
            toc.SetBook(book, false);
            const auto root = toc.GetRootTOCEntry();
            if (variant == "missing" || variant == "declared") {
                Require(root.children.size() == 1 && root.children[0].children.size() == 1, "NCX viewing lost hierarchy");
                Require(root.children[0].target == "OEBPS/a.xhtml#start", "NCX target was not resolved from its own folder");
            }
            NavigationRepair::Plan plan;
            QString error;
            const bool prepared = NavigationRepair::Prepare(book, plan, error);
            if (variant == "bad-target" || variant == "bad-fragment") {
                Require(!prepared, "Repair accepted a missing NCX target");
                continue;
            }
            if (!prepared) std::cerr << error.toStdString() << '\n';
            Require(prepared, "Cannot prepare navigation repair");
            Require(folder->GetAllBookPaths() == originalPaths && !book->IsModified(), "Planning changed book resources or state");
            if (variant == "warning") continue;
            ExportEPUB(input + ".viewed.epub", book).WriteBook();

            NavigationRepairDialog dialog(plan);
            expectedDialog = &dialog;
            const auto previews = dialog.findChildren<QPlainTextEdit *>();
            Require(previews.size() == 3, "Repair dialog does not display all source previews");
            for (auto *preview : previews) Require(preview->isReadOnly(), "Repair preview is editable");
            auto *buttons = dialog.findChild<QDialogButtonBox *>();
            Require(buttons->button(QDialogButtonBox::Cancel)->isDefault(), "Repair dialog should default to Cancel");
            QTimer::singleShot(0, buttons->button(QDialogButtonBox::Cancel), &QPushButton::click);
            Require(dialog.exec() == QDialog::Rejected, "Cancel did not reject repair preview");
            expectedDialog = nullptr;
            Require(folder->GetAllBookPaths() == originalPaths && !book->IsModified(), "Cancelling repair changed the book");

            expectedDialog = &dialog;
            QTimer::singleShot(0, buttons->button(QDialogButtonBox::Apply), &QPushButton::click);
            Require(dialog.exec() == QDialog::Accepted, "Apply button did not accept preview");
            expectedDialog = nullptr;
            Require(folder->GetAllBookPaths() == originalPaths && !book->IsModified(), "Accepting the preview alone mutated the book");

            auto altered = plan;
            altered.navText += "<!-- unapproved -->";
            Require(!NavigationRepair::Apply(book, altered, error), "A modified plan bypassed validation");
            Require(!QFileInfo::exists(folder->GetFullPathToMainFolder() + "/" + plan.navBookPath), "Rejected plan left a navigation file");
            if (variant == "missing") {
                const QString collisionPath = folder->GetFullPathToMainFolder() + "/" + plan.navBookPath;
                {
                    const QString parent = QFileInfo(collisionPath).absolutePath();
                    const auto permissions = QFile::permissions(parent);
                    RestorePermissions restore { parent, permissions };
                    Require(QFile::setPermissions(parent, permissions & ~(QFile::WriteOwner | QFile::WriteUser | QFile::WriteGroup | QFile::WriteOther)),
                            "Cannot make owned fixture directory read-only");
                    Require(!NavigationRepair::Apply(book, plan, error), "Repair succeeded despite a read-only destination");
                    Require(folder->GetAllBookPaths() == originalPaths && !book->IsModified()
                            && opf->GetSourceText() == plan.opfBefore, "Failed file creation changed book state");
                }
                QFile collision(collisionPath);
                Require(collision.open(QIODevice::WriteOnly | QIODevice::NewOnly), "Cannot create collision fixture");
                collision.write("external content");
                collision.close();
                Require(!NavigationRepair::Apply(book, plan, error), "Repair overwrote a newly occupied destination");
                Require(ReadBytes(collisionPath) == "external content", "Rejected repair damaged the colliding file");
                Require(collision.remove(), "Cannot remove owned collision fixture");
                auto &document = opf->GetTextDocumentForWriting();
                QTextCursor cursor(&document);
                cursor.movePosition(QTextCursor::End);
                cursor.insertText("<!-- changed after preview -->");
                Require(!NavigationRepair::Apply(book, plan, error), "A stale plan was applied");
                Require(document.isUndoAvailable(), "Rejected repair lost the current edit's undo stack");
                document.undo();
                Require(!NavigationRepair::Apply(book, plan, error), "A stale revision was accepted after undo");
                Require(NavigationRepair::Prepare(book, plan, error), "Cannot refresh repair plan after edit");
                auto *ncx = book->GetNCX();
                const QString ncxBefore = ncx->GetText();
                ncx->SetText(QString(ncxBefore).replace("Nested entry", "Changed entry"));
                Require(!NavigationRepair::Apply(book, plan, error), "A plan based on stale NCX was accepted");
                ncx->SetText(ncxBefore);
                Require(NavigationRepair::Prepare(book, plan, error), "Cannot refresh repair plan after NCX edit");
            }
            refreshed = false;
            Require(NavigationRepair::Apply(book, plan, error), "Cannot apply approved navigation repair");
            Require(opf->GetNavResource() && book->IsModified(), "Applied repair did not update navigation and modified state");
            Require(folder->GetAllBookPaths().size() == originalPaths.size() + 1, "Repair added more than one resource");
            Require(!opf->isNavInSpine(), "Repair unexpectedly changed reading order");
            Require(opf->GetSourceText() == plan.opfAfter, "Applied OPF differs from preview");
            Require(opf->GetNavResource()->GetText() == plan.navText, "Applied nav differs from preview");
            Require(!NavigationRepair::Apply(book, plan, error), "Reapplying a repair created duplicate navigation");
            if (panel) {
                refreshTimeout.start(5000);
                refreshLoop.exec();
                Require(refreshed, "Navigation panel did not refresh after repair");
                Require(panel->findChild<QWidget *>("missingNavigationNotice")->isHidden(), "Navigation notice remained after successful repair");
            }
            toc.SetBook(book, false);
            Require(!toc.GetRootTOCEntry().children.isEmpty(), "Repaired navigation cannot be read");
            if (variant == "missing") {
                for (int index = 0; index < 5; ++index) {
                    TOCModel temporary;
                    temporary.SetBook(book);
                    temporary.Refresh();
                } // Destroy models while a parse is live; their workers must finish safely.
            }
            ExportEPUB(input + ".repaired.epub", book).WriteBook();
        }
        {
            std::cerr << "Checking empty, split-token and truncated NCX labels\n";
            ImportEPUB labelImporter(base + ".missing");
            const auto book = labelImporter.GetBook();
            auto *ncx = book->GetNCX();
            const QString original = ncx->GetText();
            TOCModel toc;
            toc.SetBook(book, false);
            ncx->SetText(QString(original).replace("Chapter &amp; text", "Chapter <![CDATA[&]]> text"));
            Require(toc.GetRootTOCEntry().children[0].text == "Chapter & text", "NCX label lost adjacent text tokens");
            ncx->SetText(QString(original).replace("<text>Chapter &amp; text</text>", "<text/>"));
            const auto emptyLabel = toc.GetRootTOCEntry();
            Require(emptyLabel.children.size() == 1 && emptyLabel.children[0].text.isEmpty()
                    && emptyLabel.children[0].children.size() == 1
                    && emptyLabel.children[0].children[0].text == "Nested entry", "Empty NCX label consumed the next entry");
            NavigationRepair::Plan plan;
            QString error;
            const auto paths = book->GetFolderKeeper()->GetAllBookPaths();
            Require(!NavigationRepair::Prepare(book, plan, error), "Repair accepted an empty navigation label");
            Require(paths == book->GetFolderKeeper()->GetAllBookPaths(), "Rejected empty label created resources");
            const int labelOffset = original.indexOf("<navLabel><text>") + QString("<navLabel><text>").size();
            ncx->SetText(original.left(labelOffset));
            Require(toc.GetRootTOCEntry().children.isEmpty(), "Truncated NCX returned a partial tree");
        }
        {
            std::cerr << "Checking reserved navigation destination\n";
            ImportEPUB reservedImporter(base + ".declared");
            const auto book = reservedImporter.GetBook();
            auto *opf = book->GetOPF();
            opf->SetText(opf->GetSourceText().replace("href='nav.xhtml'", "href='../META-INF/nav.xhtml'"));
            const auto paths = book->GetFolderKeeper()->GetAllBookPaths();
            const QString before = opf->GetSourceText();
            NavigationRepair::Plan plan;
            QString error;
            Require(!NavigationRepair::Prepare(book, plan, error), "Repair accepted a non-XHTML resource destination");
            Require(paths == book->GetFolderKeeper()->GetAllBookPaths() && opf->GetSourceText() == before,
                    "Rejected reserved destination changed the publication");
        }
        settings.setCleanOn(CLEANON_OPEN);
        std::cerr << "Checking Clean on Open modified state\n";
        ImportEPUB cleanImporter(base + ".warning");
        Require(cleanImporter.GetBook()->IsModified(), "Actual Clean on Open repair was not marked modified");
        settings.setCleanOn(0);
        ImportEPUB epub2Importer(base + ".epub2");
        std::cerr << "Checking EPUB 2 compatibility\n";
        const auto epub2Book = epub2Importer.GetBook();
        Require(epub2Book->GetNCX() && epub2Book->IsModified(), "EPUB 2 NCX compatibility repair regressed");
        Require(epub2Book->GetOPF()->GetText().contains("toc="), "Generated EPUB 2 NCX was not linked from the spine");
        settings.setPreserveOPFSource(false);
        ImportEPUB legacyImporter(base + ".missing");
        std::cerr << "Checking legacy formatting modified state\n";
        const auto legacyBook = legacyImporter.GetBook();
        Require(!legacyBook->GetOPF()->GetNavResource(), "Legacy formatting preference silently repaired missing nav");
        Require(legacyBook->IsModified(), "Actual legacy OPF formatting was not marked modified");
        std::cout << "Native navigation diagnostic, preview cancellation, stale-plan and repair checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
