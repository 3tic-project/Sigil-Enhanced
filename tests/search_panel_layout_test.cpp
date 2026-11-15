#include <QApplication>
#include <QDockWidget>
#include <QEventLoop>
#include <QLabel>
#include <QMainWindow>
#include <QScreen>
#include <QTimer>
#include <iostream>
#include <stdexcept>

#include "MainUI/SearchPanelDock.h"

namespace {
void require(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

void settle()
{
    QEventLoop loop;
    QTimer::singleShot(30, &loop, &QEventLoop::quit);
    loop.exec();
}

struct Layout {
    QMainWindow window;
    QWidget* original = new QLabel("Find and Replace", &window);
    QWidget* enhanced = new QLabel("Enhanced Find and Replace", &window);
    QDockWidget* browser = new QDockWidget("Book Browser", &window);
    QDockWidget* preview = new QDockWidget("Preview", &window);
    SearchPanelDock* search;

    Layout()
    {
        auto* editor = new QWidget(&window);
        editor->setMinimumSize(40, 40);
        window.setCentralWidget(editor);
        const QSize screen = window.screen()->availableGeometry().size();
        const int width = qMin(1000, screen.width() - 60);
        original->setMinimumSize(width * 7 / 10, 90);
        enhanced->setMinimumSize(width * 3 / 4, 120);
        original->hide();
        enhanced->hide();
        search = new SearchPanelDock(original, enhanced, &window);
        window.addDockWidget(Qt::BottomDockWidgetArea, search);
        window.setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
        window.setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);
        browser->setObjectName("browser");
        preview->setObjectName("preview");
        browser->setWidget(new QLabel("Books", browser));
        preview->setWidget(new QLabel("Preview content", preview));
        window.addDockWidget(Qt::LeftDockWidgetArea, browser);
        window.addDockWidget(Qt::RightDockWidgetArea, preview);
        window.resize(width, qMin(650, screen.height() - 80));
        window.show();
        settle();
    }
};
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    try {
        Layout layout;
        layout.window.resizeDocks({layout.browser, layout.preview},
                                 {layout.window.width() / 6, layout.window.width() * 3 / 5}, Qt::Horizontal);
        settle();
        const int browserWidth = layout.browser->width(), previewWidth = layout.preview->width();
        require(layout.window.centralWidget()->width() < layout.original->minimumWidth(),
                "Fixture needs a narrow editor and wide Preview");
        for (auto* panel : {layout.original, layout.enhanced}) {
            for (int toggle = 0; toggle < 3; ++toggle) {
                panel->show();
                settle();
                require(layout.search->isVisible() && panel->isVisible(), "Showing search did not show its container");
                require(layout.browser->width() == browserWidth && layout.preview->width() == previewWidth,
                        "Opening search squeezed a side panel");
                require(panel->width() >= panel->minimumWidth(), "Search controls were clipped to the editor column");
                require(layout.search->widget()->y() >= 0, "The suppressed title bar clipped the first search row");
                panel->hide();
                settle();
                require(layout.search->isHidden(), "Closing search left an empty container");
                require(layout.browser->width() == browserWidth && layout.preview->width() == previewWidth,
                        "Closing search resized a side panel");
            }
        }
        layout.enhanced->show();
        settle();
        const QByteArray state = layout.window.saveState(), geometry = layout.window.saveGeometry();
        Layout reopened;
        reopened.window.restoreGeometry(geometry);
        reopened.enhanced->show();
        settle();
        require(reopened.window.restoreState(state), "Cannot restore the saved layout");
        reopened.search->syncVisibility();
        settle();
        require(reopened.browser->width() == browserWidth && reopened.preview->width() == previewWidth,
                "Restoring with open search compressed a side panel");
        std::cout << "Independent search and side panel layout passed; platform="
                  << QApplication::platformName().toStdString() << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
