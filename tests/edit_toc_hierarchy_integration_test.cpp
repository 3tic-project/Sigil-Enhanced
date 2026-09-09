#include "EmbedPython/EmbeddedPython.h" // Python must precede Qt's slots macro.

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QStandardItemModel>
#include <QToolButton>
#include <QTreeView>
#include <QXmlStreamReader>

#include <iostream>
#include <memory>
#include <stdexcept>

#include "BookManipulation/Book.h"
#include "BookManipulation/FolderKeeper.h"
#include "Dialogs/EditTOC.h"
#include "Importers/ImportEPUB.h"
#include "Misc/SettingsStore.h"
#include "ResourceObjects/HTMLResource.h"
#include "ResourceObjects/NavProcessor.h"
#include "ResourceObjects/NCXResource.h"

static void Require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

static QStandardItem *FindItem(QStandardItem *parent, const QString &text)
{
    for (int row = 0; row < parent->rowCount(); ++row) {
        QStandardItem *item = parent->child(row, 0);
        if (item->text() == text) return item;
        if (QStandardItem *nested = FindItem(item, text)) return nested;
    }
    return nullptr;
}

static QString ItemSignature(QStandardItem *item)
{
    QStringList children;
    for (int row = 0; row < item->rowCount(); ++row) {
        children.append(ItemSignature(item->child(row, 0)));
    }
    return item->text() + (children.isEmpty()
        ? QString() : QStringLiteral("[") + children.join(',') + QLatin1Char(']'));
}

static QString ModelSignature(QStandardItemModel *model)
{
    QStringList roots;
    QStandardItem *root = model->invisibleRootItem();
    for (int row = 0; row < root->rowCount(); ++row) {
        roots.append(ItemSignature(root->child(row, 0)));
    }
    return roots.join(',');
}

static void Select(QTreeView *view, const QList<QStandardItem *> &items)
{
    view->selectionModel()->clearSelection();
    bool current = false;
    for (QStandardItem *item : items) {
        Require(item, "Cannot select a missing fixture item");
        view->selectionModel()->select(
            item->index(), QItemSelectionModel::Select | QItemSelectionModel::Rows);
        if (!current) {
            view->setCurrentIndex(item->index());
            current = true;
        }
    }
    QApplication::processEvents();
}

static QAction *FindUndo(EditTOC &dialog)
{
    for (QAction *action : dialog.actions()) {
        for (const QKeySequence &shortcut : action->shortcuts()) {
            if (shortcut.matches(QKeySequence::Undo) == QKeySequence::ExactMatch) {
                return action;
            }
        }
    }
    return nullptr;
}

static QAction *FindRedo(EditTOC &dialog)
{
    for (QAction *action : dialog.actions()) {
        for (const QKeySequence &shortcut : action->shortcuts()) {
            if (shortcut.matches(QKeySequence::Redo) == QKeySequence::ExactMatch) {
                return action;
            }
        }
    }
    return nullptr;
}

static QSet<QString> SelectedLabels(QTreeView *view)
{
    QSet<QString> labels;
    for (const QModelIndex &index : view->selectionModel()->selectedRows(0)) {
        labels.insert(index.data().toString());
    }
    return labels;
}

static QString ParentLabelInNcx(const QString &source, const QString &wanted)
{
    QXmlStreamReader xml(source);
    QStringList navPointLabels;
    QString pendingLabel;
    bool inText = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            if (xml.name() == QLatin1String("navPoint")) {
                navPointLabels.append(QString());
            } else if (xml.name() == QLatin1String("text") && !navPointLabels.isEmpty()) {
                inText = true;
                pendingLabel.clear();
            }
        } else if (xml.isCharacters() && inText) {
            pendingLabel += xml.text();
        } else if (xml.isEndElement()) {
            if (xml.name() == QLatin1String("text") && inText) {
                inText = false;
                if (navPointLabels.last().isEmpty()) {
                    navPointLabels.last() = pendingLabel;
                    if (pendingLabel == wanted) {
                        return navPointLabels.size() > 1
                            ? navPointLabels.at(navPointLabels.size() - 2)
                            : QString();
                    }
                }
            } else if (xml.name() == QLatin1String("navPoint")) {
                navPointLabels.removeLast();
            }
        }
    }
    return QStringLiteral("!missing!");
}

static EditTOC *MakeDialog(const QSharedPointer<Book> &book)
{
    auto *dialog = new EditTOC(
        book, book->GetFolderKeeper()->GetResourceList());
    dialog->show();
    QApplication::processEvents();
    return dialog;
}

static void PromoteCAndE(EditTOC &dialog)
{
    auto *view = dialog.findChild<QTreeView *>(QStringLiteral("TOCTree"));
    auto *model = qobject_cast<QStandardItemModel *>(view->model());
    Select(view, {FindItem(model->invisibleRootItem(), QStringLiteral("C")),
                  FindItem(model->invisibleRootItem(), QStringLiteral("E"))});
    dialog.findChild<QToolButton *>(QStringLiteral("MoveLeft"))->click();
    QApplication::processEvents();
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    try {
        Require(argc == 3, "Expected source root and fixture path");
        const QString sourceRoot = QString::fromLocal8Bit(argv[1]);
        const QString fixture = QString::fromLocal8Bit(argv[2]);
        auto &python = EmbeddedPython::instance();
        python.addToPythonSysPath(qEnvironmentVariable("SIGIL_TEST_PYTHON_ROOT"));
        python.addToPythonSysPath(sourceRoot + "/src/Resource_Files/plugin_launchers/python");
        python.addToPythonSysPath(sourceRoot + "/src/Resource_Files/python3lib");
        SettingsStore settings;
        settings.remove(QStringLiteral("edit_toc"));
        settings.setPreserveOPFSource(true);

        ImportEPUB importer(fixture);
        const auto book = importer.GetBook();
        HTMLResource *nav = book->GetOPF()->GetNavResource();
        NCXResource *ncx = book->GetNCX();
        Require(nav && ncx && !book->IsModified(),
                "The dual-navigation fixture did not load cleanly");
        const QString navBefore = nav->GetText();
        const QString ncxBefore = ncx->GetText();

        {
            std::unique_ptr<EditTOC> dialog(MakeDialog(book));
            auto *view = dialog->findChild<QTreeView *>(QStringLiteral("TOCTree"));
            auto *model = qobject_cast<QStandardItemModel *>(view->model());
            auto *promote = dialog->findChild<QToolButton *>(QStringLiteral("MoveLeft"));
            auto *demote = dialog->findChild<QToolButton *>(QStringLiteral("MoveRight"));
            auto *adopt = dialog->findChild<QCheckBox *>(QStringLiteral("PromoteAdoptsFollowing"));
            auto *sync = dialog->findChild<QCheckBox *>(QStringLiteral("SyncNcx"));
            auto *statusLabel = dialog->findChild<QLabel *>(QStringLiteral("OperationStatus"));
            Require(view && model && promote && demote && adopt && sync && statusLabel,
                    "EditTOC hierarchy controls are incomplete");
            Require(ModelSignature(model) == QLatin1String("A[B,C[C1],D,E,F],X"),
                    "EditTOC did not load the source hierarchy");
            Require(adopt->isChecked() && sync->isVisible() && !sync->isChecked()
                        && statusLabel->text().contains(QStringLiteral("unchanged")),
                    "Hierarchy defaults or dual-navigation status are incorrect");
            Require(promote->accessibleName() == QLatin1String("Promote one level")
                        && demote->accessibleName() == QLatin1String("Demote one level")
                        && promote->toolTip().contains(QStringLiteral("reading order")),
                    "Hierarchy actions are not described accessibly");
            Require(!promote->isEnabled() && !demote->isEnabled(),
                    "A top-level boundary selection enabled an invalid hierarchy action");

            QStandardItem *oldC = FindItem(model->invisibleRootItem(), QStringLiteral("C"));
            view->setExpanded(oldC->index(), true);
            PromoteCAndE(*dialog);
            Require(ModelSignature(model) == QLatin1String("A[B],C[C1,D],E[F],X"),
                    "Promoting C and E did not adopt the correct sibling partitions");
            Require(SelectedLabels(view) == QSet<QString>({QStringLiteral("C"), QStringLiteral("E")})
                        && view->isExpanded(FindItem(model->invisibleRootItem(), QStringLiteral("C"))->index()),
                    "Promote did not preserve the stable selection and expansion state");
            Require(statusLabel->text().contains(QStringLiteral("Promoted 2"))
                        && statusLabel->text().contains(QStringLiteral("reassigned 2")),
                    "Promote did not report moved and reassigned counts");

            QAction *undo = FindUndo(*dialog);
            QAction *redo = FindRedo(*dialog);
            Require(undo && redo && undo->isEnabled(),
                    "Dialog-local undo/redo actions are unavailable");
            undo->trigger();
            Require(ModelSignature(model) == QLatin1String("A[B,C[C1],D,E,F],X")
                        && redo->isEnabled(),
                    "Undo did not exactly restore the original hierarchy");
            redo->trigger();
            Require(ModelSignature(model) == QLatin1String("A[B],C[C1,D],E[F],X"),
                    "Redo did not exactly restore the promoted hierarchy");

            FindItem(model->invisibleRootItem(), QStringLiteral("C"))->setText(
                QStringLiteral("C renamed"));
            Require(undo->text().contains(QStringLiteral("Edit TOC entry")),
                    "A committed label edit was not added to the dialog undo stack");
            undo->trigger();
            Require(ModelSignature(model) == QLatin1String("A[B],C[C1,D],E[F],X"),
                    "Undoing a label edit also discarded an earlier hierarchy edit");
            redo->trigger();
            Require(ModelSignature(model) == QLatin1String("A[B],C renamed[C1,D],E[F],X"),
                    "Redo did not restore the label edit");
            undo->trigger();
            undo->trigger();
            Require(ModelSignature(model) == QLatin1String("A[B,C[C1],D,E,F],X"),
                    "Multiple local undo commands did not return to the opening tree");

            QStandardItem *c = FindItem(model->invisibleRootItem(), QStringLiteral("C"));
            Select(view, {c});
            view->edit(c->index());
            QApplication::processEvents();
            auto *editor = qobject_cast<QLineEdit *>(QApplication::focusWidget());
            Require(editor, "The title delegate did not create an inline editor");
            editor->selectAll();
            QKeyEvent textKey(QEvent::KeyPress, Qt::Key_D, Qt::NoModifier,
                              QStringLiteral("draft"));
            QApplication::sendEvent(editor, &textKey);
            Require(editor->text() == QLatin1String("draft")
                        && editor->isUndoAvailable(),
                    "The test title editor did not establish a local undo step");
            const QKeyCombination undoCombination = QKeySequence(QKeySequence::Undo)[0];
            QKeyEvent undoKey(QEvent::KeyPress, undoCombination.key(),
                              undoCombination.keyboardModifiers());
            QApplication::sendEvent(editor, &undoKey);
            Require(editor->text() == QLatin1String("C"),
                    "The dialog shortcut stole undo from the focused title editor");
            QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            QApplication::sendEvent(editor, &escape);
            QApplication::processEvents();

            Select(view, {FindItem(model->invisibleRootItem(), QStringLiteral("C"))});
            dialog->findChild<QPushButton *>(QStringLiteral("AddEntryBelow"))->click();
            Require(ModelSignature(model) == QLatin1String("A[B,C[C1],,D,E,F],X"),
                    "Adding a TOC entry did not mutate the local tree");
            undo->trigger();
            Require(ModelSignature(model) == QLatin1String("A[B,C[C1],D,E,F],X"),
                    "Undo did not restore an added entry");

            dialog->reject();
        }
        Require(nav->GetText() == navBefore && ncx->GetText() == ncxBefore
                    && !book->IsModified(),
                "Cancelling EditTOC changed a navigation resource or dirty state");

        {
            std::unique_ptr<EditTOC> dialog(MakeDialog(book));
            dialog->accept();
        }
        Require(nav->GetText() == navBefore && ncx->GetText() == ncxBefore
                    && !book->IsModified(),
                "Accepting an unchanged EditTOC rewrote navigation");

        ImportEPUB changedImporter(fixture);
        const auto changedBook = changedImporter.GetBook();
        HTMLResource *changedNav = changedBook->GetOPF()->GetNavResource();
        NCXResource *unchangedNcx = changedBook->GetNCX();
        const QString secondaryBefore = unchangedNcx->GetText();
        {
            std::unique_ptr<EditTOC> dialog(MakeDialog(changedBook));
            PromoteCAndE(*dialog);
            dialog->accept();
        }
        NavProcessor changedProcessor(changedNav);
        const TOCModel::TOCEntry changedRoot = changedProcessor.GetRootTOCEntry();
        Require(changedRoot.children.size() == 4
                    && changedRoot.children[1].text == QLatin1String("C")
                    && changedRoot.children[1].children.size() == 2
                    && changedRoot.children[1].children[1].text == QLatin1String("D")
                    && changedRoot.children[2].children.size() == 1
                    && changedRoot.children[2].children[0].text == QLatin1String("F"),
                "Accept did not commit the promoted Nav hierarchy");
        Require(unchangedNcx->GetText() == secondaryBefore
                    && changedNav->GetText().contains(QStringLiteral("keep-between-navs"))
                    && changedProcessor.GetLandmarks().size() == 1
                    && changedProcessor.GetPageList().size() == 1,
                "Default EPUB 3 save changed NCX or unrelated navigation sections");

        ImportEPUB syncedImporter(fixture);
        const auto syncedBook = syncedImporter.GetBook();
        {
            std::unique_ptr<EditTOC> dialog(MakeDialog(syncedBook));
            dialog->findChild<QCheckBox *>(QStringLiteral("SyncNcx"))->setChecked(true);
            PromoteCAndE(*dialog);
            dialog->accept();
        }
        const QString syncedNcx = syncedBook->GetNCX()->GetText();
        Require(ParentLabelInNcx(syncedNcx, QStringLiteral("D")) == QLatin1String("C")
                    && ParentLabelInNcx(syncedNcx, QStringLiteral("F")) == QLatin1String("E"),
                "Explicit compatibility synchronization did not update NCX hierarchy");

        std::cout << "EditTOC hierarchy, local undo, no-op, cancel, and dual-nav checks passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
