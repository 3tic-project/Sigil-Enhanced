#include "EmbedPython/EmbeddedPython.h"

#include <QComboBox>
#include <QAbstractButton>
#include <QMessageBox>
#include <QResource>
#include <QRadioButton>
#include <QTimer>
#include <QWebEngineUrlScheme>
#include <iostream>
#include <memory>
#include <stdexcept>

#include "BookManipulation/Book.h"
#include "BookManipulation/FolderKeeper.h"
#include "Dialogs/Preferences.h"
#include "MainUI/FindReplace.h"
#include "MainUI/FindReplacePlus.h"
#include "MainUI/MainApplication.h"
#include "MainUI/MainWindow.h"
#include "Misc/SettingsStore.h"
#include "Misc/SettingsStoreExtend.h"
#include "Tabs/ContentTab.h"
#include "Widgets/AlertBox.h"

namespace
{
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

QComboBox* field(QWidget* search, const char* name)
{
    auto* combo = search->findChild<QComboBox*>(name);
    require(combo, "Missing search field");
    return combo;
}

void invoke(QObject* object, const char* slot)
{
    require(QMetaObject::invokeMethod(object, slot, Qt::DirectConnection), "Cannot invoke search slot");
}

bool invokeBool(QObject* object, const char* slot)
{
    bool result = false;
    require(QMetaObject::invokeMethod(object, slot, Qt::DirectConnection, Q_RETURN_ARG(bool, result)),
            "Cannot invoke search operation");
    return result;
}

void switchMode(MainWindow& window, bool enhanced)
{
    QTimer timer;
    bool handled = false;
    QObject::connect(&timer, &QTimer::timeout, &window, [&]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* preferences = qobject_cast<Preferences*>(widget);
            if (!preferences || !preferences->isVisible() || handled) continue;
            handled = true;
            preferences->makeActive(Preferences::ModifiedVersionPrefs);
            auto* radio = preferences->findChild<QRadioButton*>(enhanced ? "rbEnhancedMode" : "rbOriginalMode");
            require(radio, "Missing search mode preference");
            radio->setChecked(true);
            QMetaObject::invokeMethod(preferences, "accept", Qt::QueuedConnection);
        }
    });
    timer.start(5);
    invoke(&window, "PreferencesDialog");
    require(handled, "Cannot change search mode through Preferences");
    require((window.GetFindReplaceMode() == MainWindow::EnhancedMode) == enhanced,
            "The actual search mode did not change");
}

const QString draftFind = QStringLiteral("草稿 é𠮷 (?<word>\\w+)\\n");
const QString draftReplace = QStringLiteral("替换 $1 \\1 𠮷");
const QString draftPreFind = QStringLiteral("<p>(.*?)</p>");
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
    QResource::registerResource(QCoreApplication::applicationDirPath() + "/../Resources/main.rcc");
    try {
        require(argc == 5, "Expected source root, EPUB fixture, mode and phase");
        const QString root = QString::fromLocal8Bit(argv[1]);
        const QString input = QString::fromLocal8Bit(argv[2]);
        const bool plus = QByteArray(argv[3]) == "plus";
        const QString phase = QString::fromLocal8Bit(argv[4]);
        auto& python = EmbeddedPython::instance();
        python.addToPythonSysPath(qEnvironmentVariable("SIGIL_TEST_PYTHON_ROOT"));
        python.addToPythonSysPath(root + "/src/Resource_Files/plugin_launchers/python");
        python.addToPythonSysPath(root + "/src/Resource_Files/python3lib");
        SettingsStore preferences;
        preferences.setCleanOn(0);
        preferences.setPreserveOPFSource(true);
        SettingsStoreExtend extended;
        extended.setFindReplaceEnhancedMode(plus);
        QSettings& settings = plus ? static_cast<QSettings&>(extended) : preferences;
        settings.beginGroup(plus ? "find_replace_plus" : "find_replace");
        if (phase == "legacy") {
            settings.remove("");
            settings.setValue("find_strings", QStringList{"Original", "paragraph"});
            settings.setValue("replace_strings", QStringList{"old", "older"});
            settings.setValue("prefind_strings", QStringList{"pre-one", "pre-two"});
            settings.setValue("find_text", "legacy find input");
            settings.setValue("replace_text", "legacy replacement input");
            settings.setValue("prefind_text", "legacy pre-search input");
            settings.sync();
        }

        MainWindow window(input);
        QWidget* search = plus ? static_cast<QWidget*>(window.findChild<FindReplacePlus*>())
                               : static_cast<QWidget*>(window.findChild<FindReplace*>());
        require(search, "Missing real Find/Replace widget");
        auto* find = field(search, "cbFind");
        auto* replace = field(search, "cbReplace");
        auto* prefind = plus ? field(search, "cbPreFind") : nullptr;
        if (phase == "legacy") {
            require(find->currentText().isEmpty() && replace->currentText().isEmpty() &&
                    (!prefind || prefind->currentText().isEmpty()),
                    "A new instance automatically applied previous search inputs");
            require(find->count() == 2 && replace->count() == 2,
                    "Opening with blank inputs discarded dropdown history");
        } else if (phase == "draft") {
            find->setEditText(draftFind);
            replace->setEditText(draftReplace);
            if (prefind) prefind->setEditText(draftPreFind);
            invoke(search, "HideFindReplace");
            require(find->currentText() == draftFind && replace->currentText() == draftReplace,
                    "Hiding search lost the current operation's inputs");
            window.OpenResourceAndWaitUntilLoaded(window.GetCurrentBook()->GetFolderKeeper()
                ->GetResourceByBookPathNoThrow("OEBPS/a.xhtml"));
            if (plus) static_cast<FindReplacePlus*>(search)->FindAnyText("Original");
            else static_cast<FindReplace*>(search)->FindAnyText("Original");
            require(replace->currentText() == draftReplace && (!prefind || prefind->currentText() == draftPreFind),
                    "Reloading search options discarded current replacement/pre-search inputs");
        } else if (phase == "read-draft") {
            require(find->currentText().isEmpty() && replace->currentText().isEmpty() &&
                    (!prefind || prefind->currentText().isEmpty()),
                    "Restarting automatically applied unexecuted drafts");
            require(find->count() == 2, "Typing a draft polluted executed search history");
            replace->setEditText("");
            invoke(search, "HideFindReplace");
        } else if (phase == "read-empty") {
            require(replace->currentText().isEmpty(), "An intentionally empty replacement was not remembered");
            find->setCurrentIndex(1);
            invoke(search, "HideFindReplace");
        } else if (phase == "history") {
            require(find->currentText().isEmpty() && find->itemText(1) == "paragraph",
                    "A new instance prefilled a selected history item or lost its dropdown entry");
            window.OpenResourceAndWaitUntilLoaded(window.GetCurrentBook()->GetFolderKeeper()
                ->GetResourceByBookPathNoThrow("OEBPS/a.xhtml"));
            field(search, "cbSearchMode")->setCurrentIndex(0);
            field(search, "cbLookWhere")->setCurrentIndex(0);
            find->setEditText("Original");
            replace->setEditText(QStringLiteral("记忆替换"));
            invoke(search, "RestartClicked");
            require(invokeBool(search, "DoFindNext"), "Cannot run actual Find");
            require(settings.value("find_strings").toStringList().first() == "Original",
                    "Executed Find history was not saved immediately");
            require(invokeBool(search, "ReplaceCurrent"), "Cannot run actual Replace Current");
            require(settings.value("replace_strings").toStringList().first() == QStringLiteral("记忆替换"),
                    "Executed Replace history was not saved immediately");
            invoke(window.GetCurrentContentTab(), "Undo");
            require(!window.GetCurrentBook()->IsModified(), "Undo did not restore the fixture");
            invoke(search, "RestartClicked");
            require(invokeBool(search, "DoFindNext"), "Cannot repeat Find");
            require(settings.value("find_strings").toStringList().count("Original") == 1,
                    "Reusing a query duplicated its history entry");
        } else if (phase == "mode-switch") {
            find->setEditText(draftFind);
            replace->setEditText(draftReplace);
            if (prefind) prefind->setEditText(draftPreFind);
            const QString beforeFind = find->currentText();
            const QString beforeReplace = replace->currentText();
            for (int i = 0; i < 3; ++i) {
                switchMode(window, !plus);
                switchMode(window, plus);
            }
            require(find->currentText() == beforeFind && replace->currentText() == beforeReplace &&
                    (!prefind || prefind->currentText() == draftPreFind),
                    "Switching search modes lost the current find/replace text");
        } else if (phase == "exit-draft") {
            find->setEditText("close app draft");
            replace->setEditText("close app replacement");
        } else if (phase == "read-exit") {
            require(find->currentText().isEmpty() && replace->currentText().isEmpty(),
                    "A new instance restored the previous instance's draft");
        } else if (phase == "stale-window") {
            auto stale = std::make_unique<MainWindow>(input);
            window.OpenResourceAndWaitUntilLoaded(window.GetCurrentBook()->GetFolderKeeper()
                ->GetResourceByBookPathNoThrow("OEBPS/a.xhtml"));
            field(search, "cbSearchMode")->setCurrentIndex(0);
            field(search, "cbLookWhere")->setCurrentIndex(0);
            find->setEditText("paragraph");
            invoke(search, "RestartClicked");
            require(invokeBool(search, "DoFindNext"), "Cannot update history while another window is open");
            find->setEditText("newer draft");
            replace->setEditText("newer replacement");
            invoke(search, "HideFindReplace");
            stale.reset();
            require(find->currentText() == "newer draft" && replace->currentText() == "newer replacement",
                    "Closing another window changed the current operation's inputs");
            require(settings.value("find_strings").toStringList().first() == "paragraph",
                    "Closing an unchanged older window overwrote the newer search history");
        } else if (phase == "clear") {
            QTimer dialogs;
            QObject::connect(&dialogs, &QTimer::timeout, &window, []() {
                for (QWidget* widget : QApplication::topLevelWidgets()) {
                    QAbstractButton* yes = nullptr;
                    if (auto* box = qobject_cast<QMessageBox*>(widget)) {
                        if (box->text().contains("clear your Find and Replace")) yes = box->button(QMessageBox::Yes);
                    }
                    if (auto* box = qobject_cast<AlertBox*>(widget)) {
                        if (box->text().contains("clear your Find and Replace")) yes = box->button(QDialogButtonBox::Yes);
                    }
                    if (widget->isVisible() && yes) {
                        QMetaObject::invokeMethod(yes, "click", Qt::QueuedConnection);
                    }
                }
            });
            dialogs.start(5);
            invoke(search, "ClearHistory");
            require(settings.value("find_strings").toStringList().isEmpty() &&
                    settings.value("replace_strings").toStringList().isEmpty(),
                    "Clear History did not clear persistent history immediately");
        } else if (phase == "read-clear") {
            require(find->currentText().isEmpty() && replace->currentText().isEmpty() &&
                    find->count() == 0 && replace->count() == 0,
                    "Restarting resurrected cleared history or current values");
            require(!prefind || (prefind->count() == 0 && prefind->currentText().isEmpty()),
                    "Restarting resurrected cleared pre-search history");
        }
        settings.sync();
        require(!settings.contains("find_text") && !settings.contains("replace_text") &&
                !settings.contains("prefind_text"), "Current input was persisted for automatic restore");
        require(!window.GetCurrentBook()->IsModified(), "Search memory testing unexpectedly changed the book");
        std::cout << "Search memory passed: " << argv[3] << '/' << argv[4] << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
