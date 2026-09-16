#include "EmbedPython/EmbeddedPython.h" // Python must precede Qt's slots macro.

#include <QAction>
#include <QCheckBox>
#include <QMenu>
#include <QPixmap>
#include <QToolBar>
#include <QToolButton>
#include <QWebEngineUrlScheme>

#include <iostream>
#include <memory>
#include <stdexcept>

#include "Dialogs/PreferenceWidgets/AppearanceWidget.h"
#include "MainUI/MainApplication.h"
#include "MainUI/MainWindow.h"
#include "Misc/KeyboardShortcutManager.h"
#include "Misc/SettingsStore.h"
#include "MiscEditors/ClipEditorModel.h"
#include "Tabs/FlowTab.h"
#include "Widgets/ActionShortcutBadge.h"

static void Require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

static void AddClip(ClipEditorModel &model, int slot)
{
    ClipEditorModel::clipEntry entry;
    entry.is_group = false;
    entry.name = slot == 1
        ? QStringLiteral("Ruby & quote")
        : QStringLiteral("Entry %1").arg(slot);
    entry.text = slot == 1
        ? QString::fromUtf8("<ruby>風&雨</ruby> \"quoted\"")
        : QStringLiteral("<mark data-slot=\"%1\">\\1</mark>").arg(slot);
    model.AddEntryToModel(&entry);
}

static QAction *ClipAction(MainWindow &window, int slot)
{
    QAction *action = window.findChild<QAction *>(
        QStringLiteral("actionClip%1").arg(slot));
    Require(action, "A Clip QAction is missing from the main window");
    return action;
}

static QToolButton *ClipButton(QToolBar *toolbar, QAction *action)
{
    QToolButton *button = qobject_cast<QToolButton *>(
        toolbar->widgetForAction(action));
    Require(button, "A Clip QAction is not backed by its standard toolbar button");
    return button;
}

static ActionShortcutBadge *ClipBadge(QToolButton *button)
{
    ActionShortcutBadge *badge = button->findChild<ActionShortcutBadge *>(
        QStringLiteral("actionShortcutBadge"), Qt::FindDirectChildrenOnly);
    Require(badge, "A first-ten Clip button has no shortcut badge layer");
    return badge;
}

int main(int argc, char **argv)
{
    QCoreApplication::setAttribute(Qt::AA_DisableShaderDiskCache);
    QWebEngineUrlScheme scheme("sigil");
    scheme.setSyntax(QWebEngineUrlScheme::Syntax::Path);
    scheme.setFlags(QWebEngineUrlScheme::SecureScheme | QWebEngineUrlScheme::LocalScheme
                    | QWebEngineUrlScheme::LocalAccessAllowed
                    | QWebEngineUrlScheme::ContentSecurityPolicyIgnored
                    | QWebEngineUrlScheme::FetchApiAllowed);
    QWebEngineUrlScheme::registerScheme(scheme);
    MainApplication app(argc, argv);
    try {
        Require(argc == 3, "Expected source root and disposable EPUB fixture");
        const QString root = QString::fromLocal8Bit(argv[1]);
        auto &python = EmbeddedPython::instance();
        python.addToPythonSysPath(qEnvironmentVariable("SIGIL_TEST_PYTHON_ROOT"));
        python.addToPythonSysPath(root + "/src/Resource_Files/plugin_launchers/python");
        python.addToPythonSysPath(root + "/src/Resource_Files/python3lib");
        SettingsStore settings;
        settings.remove(QStringLiteral("user_preferences/show_clip_shortcut_badges"));
        Require(settings.showClipShortcutBadges(),
                "Clip shortcut badges are not enabled by default");
        settings.setShowClipShortcutBadges(false);
        settings.clearAppearanceSettings();
        Require(settings.showClipShortcutBadges(),
                "Resetting Appearance did not restore the default badge setting");

        ClipEditorModel &model = ClipEditorModel::instance();
        model.removeRows(0, model.rowCount());
        for (int slot = 1; slot <= 10; ++slot) AddClip(model, slot);
        model.UpdateNumber();

        MainWindow window(QString::fromLocal8Bit(argv[2]));
        window.resize(1800, 800);
        window.show();
        QToolBar *toolbar = window.findChild<QToolBar *>(QStringLiteral("toolBarClips"));
        Require(toolbar, "The primary Clips toolbar is missing");
        toolbar->setVisible(true);
        MainApplication::processEvents();

        for (int slot = 1; slot <= 10; ++slot) {
            QAction *action = ClipAction(window, slot);
            QToolButton *button = ClipButton(toolbar, action);
            ActionShortcutBadge *badge = ClipBadge(button);
            const QString expected = QString::number(slot % 10);
            std::unique_ptr<ClipEditorModel::clipEntry> entry(
                model.GetEntryFromNumber(slot));
            Require(action->data().toInt() == slot && entry
                        && action->text() == entry->name,
                    "A Clip toolbar action lost its fixed model slot identity");
            Require(action->isVisible() && badge->isVisible()
                        && badge->usesNumericBadge()
                        && badge->displayedText() == expected,
                    "The first ten Clip actions did not show 1-9 and 0 badges");
        }

        QAction *first = ClipAction(window, 1);
        QToolButton *firstButton = ClipButton(toolbar, first);
        Require(first->toolTip().contains(QStringLiteral("Ruby &amp; quote"))
                    && first->toolTip().contains(QStringLiteral("&lt;ruby&gt;"))
                    && !first->toolTip().contains(QStringLiteral("<ruby>"))
                    && first->toolTip().contains(
                        first->shortcut().toString(QKeySequence::NativeText).toHtmlEscaped()),
                "Clip tooltip text is unsafe or missing the effective native shortcut");
        Require(firstButton->accessibleName().contains(QStringLiteral("Clip 1"))
                    && firstButton->accessibleName().contains(
                        first->shortcut().toString(QKeySequence::NativeText)),
                "The Clip button accessible name omits its slot or shortcut");

        const QString screenshot = qEnvironmentVariable("SIGIL_CLIP_BADGE_SCREENSHOT");
        if (!screenshot.isEmpty()) {
            const QPixmap image = toolbar->grab();
            Require(!image.isNull() && image.save(screenshot),
                    "Could not save the Clip shortcut badge screenshot");
        }

        QAction *third = ClipAction(window, 3);
        QToolButton *thirdButton = ClipButton(toolbar, third);
        ActionShortcutBadge *thirdBadge = ClipBadge(thirdButton);
        const QKeySequence custom(
            Qt::MetaModifier | Qt::ShiftModifier | Qt::Key_F24);
        Require(KeyboardShortcutManager::instance().setKeySequence(
                    QStringLiteral("MainWindow.Clip3"), custom)
                    && third->shortcut() == custom,
                "The shortcut manager did not update Clip 3");
        MainApplication::processEvents();
        Require(thirdBadge->isVisible() && !thirdBadge->usesNumericBadge()
                    && thirdBadge->displayedText() != QLatin1String("3")
                    && third->toolTip().contains(
                        custom.toString(QKeySequence::NativeText).toHtmlEscaped())
                    && thirdButton->accessibleName().contains(
                        custom.toString(QKeySequence::NativeText)),
                "A customized Clip shortcut did not refresh all presentations");
        QMenu overflowMenu;
        overflowMenu.addAction(third);
        Require(overflowMenu.actions().constFirst() == third
                    && overflowMenu.actions().constFirst()->shortcut() == custom,
                "The standard overflow-menu action lost the effective shortcut");

        KeyboardShortcutManager::instance().setKeySequence(
            QStringLiteral("MainWindow.Clip3"), QKeySequence());
        MainApplication::processEvents();
        Require(third->shortcut().isEmpty() && !thirdBadge->isVisible()
                    && third->toolTip().contains(QStringLiteral("No shortcut assigned"))
                    && thirdButton->accessibleName().contains(
                        QStringLiteral("no shortcut assigned")),
                "Clearing a Clip shortcut left a misleading badge or description");
        KeyboardShortcutManager::instance().resetKeySequence(
            QStringLiteral("MainWindow.Clip3"));
        MainApplication::processEvents();

        AppearanceWidget appearance;
        appearance.setParent(&window);
        QCheckBox *toggle = appearance.findChild<QCheckBox *>(
            QStringLiteral("chkClipShortcutBadges"));
        Require(toggle && toggle->isChecked()
                    && toggle->toolTip().contains(QStringLiteral("first ten")),
                "Appearance / Main UI does not expose the enabled badge setting");
        window.activateWindow();
        MainApplication::processEvents();
        toggle->setChecked(false);
        appearance.saveSettings();
        MainApplication::processEvents();
        Require(!SettingsStore().showClipShortcutBadges()
                    && !ClipBadge(firstButton)->isVisible(),
                "Disabling the Appearance preference did not hide badges live");
        toggle->setChecked(true);
        appearance.saveSettings();
        MainApplication::processEvents();
        Require(SettingsStore().showClipShortcutBadges()
                    && ClipBadge(firstButton)->isVisible(),
                "Re-enabling the Appearance preference did not restore badges live");

        model.removeRows(0, model.rowCount());
        ClipEditorModel::clipEntry updated;
        updated.is_group = false;
        updated.name = QStringLiteral("Updated ruby");
        updated.text = QString::fromUtf8("<ruby>新&語</ruby>");
        model.AddEntryToModel(&updated);
        model.UpdateNumber();
        MainApplication::processEvents();
        QAction *second = ClipAction(window, 2);
        Require(first == ClipAction(window, 1) && first->isVisible()
                    && first->text() == QLatin1String("Updated ruby")
                    && ClipBadge(firstButton)->displayedText() == QLatin1String("1")
                    && !second->isVisible()
                    && !ClipBadge(ClipButton(toolbar, second))->isVisible(),
                "A live Clip model update renumbered the remaining badge or kept an empty slot");
        Require(first->toolTip().contains(QStringLiteral("&lt;ruby&gt;"))
                    && !first->toolTip().contains(QStringLiteral("<ruby>")),
                "A live Clip model update bypassed safe tooltip formatting");

        FlowTab *flow = window.GetCurrentFlowTab();
        Require(flow && !flow->GetText().isEmpty(),
                "The EPUB fixture did not open an editable XHTML tab");
        const int insertionPoint = flow->GetText().indexOf(QStringLiteral("</body>"));
        Require(insertionPoint >= 0 && flow->SetSelectionRange(insertionPoint, insertionPoint),
                "Could not position the real editor for a Clip action test");
        flow->setFocus();
        window.activateWindow();
        MainApplication::processEvents();
        first->trigger();
        MainApplication::processEvents();
        Require(flow->GetText().contains(QString::fromUtf8("<ruby>新&語</ruby></body>")),
                "Triggering the fixed Clip 1 QAction did not insert Clip 1 content");

        std::cout << "Native Clip shortcut badge integration checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
