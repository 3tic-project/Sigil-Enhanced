#include <QApplication>
#include <QMainWindow>
#include <QScreen>
#include <iostream>
#include <stdexcept>

#include "Misc/WindowStartupPolicy.h"

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    try {
        struct Case {
            bool windows, restore, fullscreen, maximized, migrated;
            Qt::WindowState expected;
        };
        const Case cases[] = {
            {true, false, false, false, false, Qt::WindowMaximized},
            {true, false, true, true, true, Qt::WindowMaximized},
            {true, true, false, false, false, Qt::WindowNoState},
            {true, true, false, true, false, Qt::WindowMaximized},
            {true, true, true, false, false, Qt::WindowMaximized},
            {true, true, true, false, true, Qt::WindowFullScreen},
            {false, false, false, false, false, Qt::WindowFullScreen},
            {false, false, false, true, true, Qt::WindowFullScreen},
            {false, true, false, false, false, Qt::WindowNoState},
            {false, true, false, true, false, Qt::WindowMaximized},
            {false, true, true, false, false, Qt::WindowFullScreen},
        };
        for (const auto& test : cases) {
            const auto state = WindowStartupPolicy::Resolve(test.windows, test.restore, test.fullscreen,
                                                            test.maximized, test.migrated);
            if (state != test.expected) throw std::runtime_error("Unexpected startup or migrated window state");
        }
        QMainWindow window;
        window.setWindowState(WindowStartupPolicy::Resolve(true, false, false, false, false));
        window.show();
        QApplication::processEvents();
        if (!window.isMaximized() || window.isFullScreen() ||
            !(window.windowFlags() & Qt::WindowTitleHint)) {
            throw std::runtime_error("Windows default did not use a maximized, decorated window");
        }
        // Check a point in the title region, not the invisible resize border
        // which Windows intentionally allows outside the maximized work area.
        const QRect frame = window.frameGeometry();
        const QPoint title(frame.center().x(), (frame.top() + window.geometry().top()) / 2);
        if (!window.screen()->availableGeometry().contains(title)) {
            throw std::runtime_error("Default startup placed the title region outside the available screen");
        }
        std::cout << "Window startup defaults, migration and title-region checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
