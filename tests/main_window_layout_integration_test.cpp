#include "EmbedPython/EmbeddedPython.h" // Python must precede Qt's slots macro.

#include <QCheckBox>
#include <QDockWidget>
#include <QEventLoop>
#include <QFileInfo>
#include <QResource>
#include <QLabel>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QStyleOptionDockWidget>
#include <QScreen>
#include <QSplitter>
#include <QTimer>
#include <QToolBar>
#include <QWebEngineUrlScheme>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>

#include "Dialogs/PreferenceWidgets/GeneralSettingsWidget.h"
#include "MainUI/MainApplication.h"
#include "MainUI/MainWindow.h"
#include "MainUI/FindReplace.h"
#include "MainUI/FindReplacePlus.h"
#include "Misc/SettingsStore.h"
#include "Misc/SettingsStoreExtend.h"
#include "Tabs/TabManager.h"

namespace
{

void require(bool condition, const QString& message)
{
    if (!condition) throw std::runtime_error(message.toStdString());
}

void settle()
{
    QEventLoop loop;
    QTimer::singleShot(100, &loop, &QEventLoop::quit);
    loop.exec();
}

// Dock restoration must work even if the OS does not activate the window.
class IgnoreActivation final : public QObject
{
    bool eventFilter(QObject*, QEvent* event) override
    {
        return event->type() == QEvent::ActivationChange;
    }
};

class DockPaintStyle final : public QProxyStyle
{
public:
    DockPaintStyle() : QProxyStyle(QStyleFactory::create("Windows")) {}
    mutable int customTitlePaints = 0;
    mutable QHash<const QWidget*, QString> titles;

    void drawControl(ControlElement element, const QStyleOption* option, QPainter* painter,
                     const QWidget* widget = nullptr) const override
    {
        if (element == CE_DockWidgetTitle) {
            if (auto* dock = qobject_cast<const QDockWidget*>(widget)) {
                if (dock->titleBarWidget()) ++customTitlePaints;
                if (auto* title = qstyleoption_cast<const QStyleOptionDockWidget*>(option)) {
                    titles.insert(dock, title->title);
                }
            }
        }
        QProxyStyle::drawControl(element, option, painter, widget);
    }
};

QVariantMap layoutSettings()
{
    SettingsStore settings;
    QVariantMap result;
    for (const QString& key : settings.allKeys()) {
        if (key == "mainwindow/geometry" || key == "mainwindow/maximized" ||
            key == "mainwindow/fullscreen" || key == "mainwindow/toolbars" ||
            key == "mainwindow/windowStateVersion" ||
            key == "mainwindow/windowsStartupVersion" ||
            key.startsWith("editorGroups/") || key.startsWith("previewwindow/devTools")) {
            result.insert(key, settings.value(key));
        }
    }
    return result;
}

void checkPreferences()
{
    SettingsStore settings;
    require(settings.rememberWindowLayout(), "Remember window layout must default to enabled");
    GeneralSettingsWidget preferences;
    auto* check = preferences.findChild<QCheckBox*>("RememberWindowLayout");
    require(check && check->isChecked(), "Default preference checkbox is missing or unchecked");
    check->setChecked(false);
    require(settings.rememberWindowLayout(), "Editing an unsaved preference changed stored settings");
    preferences.saveSettings();
    require(!settings.rememberWindowLayout(), "Saving the unchecked preference did not persist");
    GeneralSettingsWidget reopened;
    check = reopened.findChild<QCheckBox*>("RememberWindowLayout");
    require(check && !check->isChecked(), "Reopened preferences forgot the disabled option");
    check->setChecked(true);
    reopened.saveSettings();
    require(settings.rememberWindowLayout(), "Saving the checked preference did not persist");
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
    // Give the real toolbars and docks enough room to test exact geometry.
    // Qt correctly clamps oversized windows to the default 800px test screen.
    if (argc > 2 && qEnvironmentVariable("QT_QPA_PLATFORM") == "offscreen") {
        const QString config = QFileInfo(QString::fromLocal8Bit(argv[2])).absolutePath() + "/offscreen.json";
        qputenv("QT_QPA_PLATFORM", ("offscreen:configfile=" + config).toLocal8Bit());
    }
    MainApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    QResource::registerResource(QCoreApplication::applicationDirPath() + "/../Resources/main.rcc");

    try {
        require(argc == 4, "Expected source root, disposable EPUB and scenario");
        const QString root = QString::fromLocal8Bit(argv[1]);
        const QString input = QString::fromLocal8Bit(argv[2]);
        const QString scenario = QString::fromLocal8Bit(argv[3]);
        DockPaintStyle* paint_style = nullptr;
        if (scenario.startsWith("search-") || scenario == "dock-paint") {
            paint_style = new DockPaintStyle;
            app.setStyle(paint_style);
        }
        auto& python = EmbeddedPython::instance();
        python.addToPythonSysPath(qEnvironmentVariable("SIGIL_TEST_PYTHON_ROOT"));
        python.addToPythonSysPath(root + "/src/Resource_Files/plugin_launchers/python");
        python.addToPythonSysPath(root + "/src/Resource_Files/python3lib");
        SettingsStore settings;
        settings.setPreserveOPFSource(true);
        settings.setCleanOn(0);
        const bool search_case = scenario.startsWith("search-");
        if (search_case) {
            SettingsStoreExtend extended;
            extended.setFindReplaceEnhancedMode(scenario.endsWith("plus"));
            if (scenario.startsWith("search-write-")) {
                settings.remove("mainwindow/toolbars");
                settings.setValue("find_replace/visible", false);
                extended.setValue("find_replace_plus/visible", false);
            } else {
                // Old installations can have both modes marked visible.
                settings.setValue("find_replace/visible", true);
                extended.setValue("find_replace_plus/visible", true);
            }
        }
        if (scenario == "fresh") checkPreferences();
        if (scenario == "legacy") {
            QMainWindow legacy;
            legacy.setGeometry(40, 50, 700, 650);
            settings.setValue("mainwindow/geometry", legacy.saveGeometry());
            settings.setValue("mainwindow/fullscreen", false);
            settings.setValue("mainwindow/maximized", false);
            settings.remove("mainwindow/windowStateVersion");
        }
        if (scenario == "disabled") settings.setRememberWindowLayout(false);
        if (scenario == "reenabled") settings.setRememberWindowLayout(true);
        if (scenario == "corrupt") settings.setValue("mainwindow/geometry", QByteArray("invalid"));
        if (scenario == "offscreen") {
            QMainWindow previous;
            previous.setGeometry(5000, -300, 2200, 1800);
            settings.setValue("mainwindow/geometry", previous.saveGeometry());
            settings.setValue("mainwindow/windowStateVersion", 1);
            settings.setValue("mainwindow/fullscreen", false);
            settings.setValue("mainwindow/maximized", false);
        }
        const QVariantMap before = layoutSettings();

        auto window = std::make_unique<MainWindow>(input);
        require(!window->isVisible(), "Restoring state showed the window before construction completed");
        window->setAttribute(Qt::WA_DeleteOnClose, false);
        window->setAttribute(Qt::WA_ShowWithoutActivating);
        IgnoreActivation ignore_activation;
        window->installEventFilter(&ignore_activation);
        window->show();
        settle();
        auto* browser = window->findChild<QDockWidget*>("bookbrowser");
        auto* toc = window->findChild<QDockWidget*>("tableofcontents");
        auto* preview = window->findChild<QDockWidget*>("previewwindow");
        auto* toolbar = window->findChild<QToolBar*>("toolBarFileActions");
        auto* tabs = window->findChild<TabManager*>();
        auto* splitter = window->findChild<QSplitter*>("editorGroupSplitter");
        require(browser && toc && preview && toolbar && tabs && splitter,
                "Cannot find the real dock widgets, toolbar or editor splitter");

        if (scenario == "dock-paint") {
            window->showNormal();
            window->resize(1500, 900);
            auto* validation = window->findChild<QDockWidget*>("validationresultsname");
            auto* clips = window->findChild<QDockWidget*>("clipswindow");
            require(validation && clips && paint_style, "Cannot find real dock paint subjects");
            require(QMetaObject::invokeMethod(preview, "setTitleText", Qt::DirectConnection,
                                             Q_ARG(QString, QString("Independent Preview title"))),
                    "Cannot set the independent Preview title");
            for (auto* dock : {browser, toc, preview, validation, clips}) {
                window->removeDockWidget(dock);
                window->addDockWidget(dock == validation ? Qt::BottomDockWidgetArea : Qt::LeftDockWidgetArea, dock);
                dock->show();
                settle();
                if (dock == preview) {
                    require(QMetaObject::invokeMethod(preview, "setTitleText", Qt::DirectConnection,
                                                     Q_ARG(QString, QString("Independent Preview title"))),
                            "Cannot set Preview's independent title after layout");
                }
                dock->grab();
                require(paint_style->titles.contains(dock), "Default dock title was not painted");
                if (dock == preview) {
                    require(paint_style->titles.value(dock) == "Independent Preview title",
                            "Native dock painting lost Preview's independent title");
                }
                dock->setTitleBarWidget(new QLabel("Custom title", dock));
                settle();
                dock->grab();
            }
            require(paint_style->customTitlePaints == 0,
                    "A dock painted its old title over a custom or suppressed title bar");
        } else if (search_case) {
            auto* original = window->findChild<FindReplace*>();
            auto* enhanced = window->findChild<FindReplacePlus*>();
            require(original && enhanced, "Cannot find both real search panels");
            QWidget* active = scenario.endsWith("plus") ? static_cast<QWidget*>(enhanced) : original;
            QWidget* inactive = active == original ? static_cast<QWidget*>(enhanced) : original;
            const auto checkSearchPosition = [&]() {
                require(active->parentWidget() == window->centralWidget() &&
                        tabs->parentWidget() == window->centralWidget(),
                        "Search must stay in the editor column");
                require(active->geometry().top() >= tabs->geometry().bottom() &&
                        active->width() == tabs->width(),
                        "Search moved away from its original position below the editor");
            };
            if (scenario.startsWith("search-write-")) {
                window->showNormal();
                window->setGeometry(window->screen()->availableGeometry().adjusted(30, 40, -30, -40));
                window->addDockWidget(Qt::LeftDockWidgetArea, browser);
                window->addDockWidget(Qt::RightDockWidgetArea, preview);
                toc->hide();
                browser->show();
                preview->show();
                original->HideFindReplace();
                enhanced->HideFindReplace();
                settle();
                window->resizeDocks({browser, preview}, {260, 1100}, Qt::Horizontal);
                settle();
                require(QMetaObject::invokeMethod(window.get(), "Find", Qt::DirectConnection), "Cannot open search");
                settle();
                require(active->isVisible() && inactive->isHidden(), "Find did not open only the active search panel");
                checkSearchPosition();
                // Keep the original editor column. With search open, give it
                // enough width for the real controls before saving a wide Preview.
                window->resizeDocks({browser, preview}, {260, 900}, Qt::Horizontal);
                settle();
                const int browser_width = browser->width(), preview_width = preview->width();
                require(browser_width >= 240 && preview_width >= 850,
                        "The saved layout fixture already compressed a side panel");
                for (int toggle = 0; toggle < 3; ++toggle) {
                    if (active == original) original->HideFindReplace(); else enhanced->HideFindReplace();
                    settle();
                    require(!active->isVisible(), "Closing search left its panel visible");
                    require(qAbs(browser->width() - browser_width) <= 1 && qAbs(preview->width() - preview_width) <= 1,
                            "Closing search changed the side panel widths");
                    require(QMetaObject::invokeMethod(window.get(), "Find", Qt::DirectConnection), "Cannot reopen search");
                    settle();
                    checkSearchPosition();
                    require(qAbs(browser->width() - browser_width) <= 1 && qAbs(preview->width() - preview_width) <= 1,
                            "Reopening search changed a fitting layout's side panel widths");
                }
                settings.setValue("windowLayoutTest/browserWidth", browser->width());
                settings.setValue("windowLayoutTest/previewWidth", preview->width());
                std::cout << "Saved search layout: browser=" << browser->width() << " preview=" << preview->width()
                          << " central=" << window->centralWidget()->width() << " search minimum="
                          << active->minimumSizeHint().width() << '\n';
                require(window->close(), "Cannot save layout with the search panel open");
                window.reset();
                settings.sync();
            } else {
                require(active->isVisible() && inactive->isHidden(), "Startup restored the wrong search panel visibility");
                checkSearchPosition();
                require(qAbs(browser->width() - settings.value("windowLayoutTest/browserWidth").toInt()) <= 5,
                        "Reopening with search and a wide Preview squeezed the Book Browser");
                require(qAbs(preview->width() - settings.value("windowLayoutTest/previewWidth").toInt()) <= 5,
                        "Reopening changed the saved Preview width");
            }
        } else if (scenario.startsWith("write-")) {
            window->showNormal();
            window->setGeometry(window->screen()->availableGeometry().adjusted(30, 40, -30, -40));
            window->addDockWidget(Qt::BottomDockWidgetArea, browser);
            browser->show();
            window->addDockWidget(Qt::LeftDockWidgetArea, preview);
            preview->show();
            toc->hide();
            window->addToolBar(Qt::BottomToolBarArea, toolbar);
            tabs->SplitEditorDown();
            settle();
            window->resizeDocks({browser}, {150}, Qt::Vertical);
            splitter->setSizes({150, 350});
            settle();
            settings.setValue("windowLayoutTest/normalGeometry", window->geometry());
            if (scenario == "write-maximized") window->showMaximized();
            if (scenario == "write-fullscreen") window->showFullScreen();
            if (scenario == "write-minimized") window->showMinimized();
            settle();
            settings.setValue("windowLayoutTest/browserHeight", browser->height());
            const auto sizes = splitter->sizes();
            settings.setValue("windowLayoutTest/splitRatio",
                              double(sizes.at(0)) / (sizes.at(0) + sizes.at(1)));
            require(window->close(), "Cannot close window to save its real layout");
            window.reset();
            settings.sync();
            require(settings.status() == QSettings::NoError, "Window settings could not be written");
        } else if (scenario == "fresh" || scenario == "legacy" || scenario == "disabled" || scenario == "corrupt") {
#ifdef Q_OS_WIN
            require(window->isMaximized() && !window->isFullScreen(),
                    "A Windows default startup did not retain the title bar in maximized mode");
#else
            require(window->isFullScreen(), "A default startup did not enter full screen");
#endif
            require(!window->isMinimized(), "A default startup opened minimized");
            if (scenario == "legacy") {
                require(window->dockWidgetArea(browser) == Qt::BottomDockWidgetArea &&
                        window->dockWidgetArea(preview) == Qt::LeftDockWidgetArea,
                        "Migrating the startup state discarded the previous panel layout");
            }
            if (scenario == "disabled") {
                require(window->dockWidgetArea(browser) == Qt::LeftDockWidgetArea && !tabs->IsSplit(),
                        "Disabling the preference still restored a previous layout");
                window->addDockWidget(Qt::RightDockWidgetArea, browser);
                tabs->SplitEditorDown();
                require(window->close(), "Cannot close window with remembering disabled");
                window.reset();
                settings.sync();
                require(layoutSettings() == before, "Disabled remembering overwrote the saved layout");
            }
        } else if (scenario == "offscreen") {
            require(!window->isFullScreen() && !window->isMaximized(), "Offscreen geometry changed normal state");
            const QRect frame = window->frameGeometry();
            const QPoint title(frame.center().x(), (frame.top() + window->geometry().top()) / 2);
            require(window->screen()->availableGeometry().contains(title),
                    "Restoring offscreen/oversized geometry left the title region inaccessible");
        } else {
            const bool fullscreen = scenario == "read-fullscreen";
            const bool maximized = scenario == "read-maximized";
            require(window->isFullScreen() == fullscreen && window->isMaximized() == maximized &&
                    !window->isMinimized(), "The previous normal/maximized/full-screen state was not restored");
            require(window->dockWidgetArea(browser) == Qt::BottomDockWidgetArea &&
                    window->dockWidgetArea(preview) == Qt::LeftDockWidgetArea && toc->isHidden() &&
                    window->toolBarArea(toolbar) == Qt::BottomToolBarArea,
                    "Panel locations, visibility or toolbar position were not restored without activation");
            require(tabs->IsSplit(), "Editor group layout was not restored");
            const auto sizes = splitter->sizes();
            const double ratio = double(sizes.at(0)) / (sizes.at(0) + sizes.at(1));
            require(qAbs(ratio - settings.value("windowLayoutTest/splitRatio").toDouble()) < 0.05,
                    "Editor group sizes were not restored");
            require(qAbs(browser->height() - settings.value("windowLayoutTest/browserHeight").toInt()) <= 5,
                    "Dock panel size was not restored");
            if (!fullscreen && !maximized) {
                const QRect expected = settings.value("windowLayoutTest/normalGeometry").toRect();
                require(window->geometry() == expected,
                        QString("Normal window geometry was not restored: got %1,%2 %3x%4; expected %5,%6 %7x%8")
                            .arg(window->x()).arg(window->y()).arg(window->width()).arg(window->height())
                            .arg(expected.x()).arg(expected.y()).arg(expected.width()).arg(expected.height()));
            }
            // Hiding/showing or reactivating a live window must not reset a layout the user just changed.
            window->addDockWidget(Qt::RightDockWidgetArea, browser);
            window->hide();
            window->show();
            settle();
            require(window->dockWidgetArea(browser) == Qt::RightDockWidgetArea,
                    "Showing the same window reapplied an old saved layout");
        }
        std::cout << "MainWindow layout integration passed: " << scenario.toStdString() << '\n';
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
