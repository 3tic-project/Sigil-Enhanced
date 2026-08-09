#include <cstdlib>
#include <iostream>

#include <QtCore/QCoreApplication>
#include <QtGui/QMouseEvent>
#include <QtWidgets/QApplication>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QWidget>

#include "Tabs/TabBar.h"

namespace
{

class TestTabWidget : public QTabWidget
{
public:
    void InstallTabBar(QTabBar *tab_bar)
    {
        setTabBar(tab_bar);
    }
};

void Require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void SendMousePress(TabBar *tab_bar,
                    Qt::MouseButton button,
                    const QPoint &position)
{
    QMouseEvent event(QEvent::MouseButtonPress,
                      QPointF(position),
                      QPointF(tab_bar->mapToGlobal(position)),
                      button,
                      button,
                      Qt::NoModifier);
    QCoreApplication::sendEvent(tab_bar, &event);
}

}

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);

    TestTabWidget tab_widget;
    TabBar *tab_bar = new TabBar(&tab_widget);
    tab_widget.InstallTabBar(tab_bar);
    tab_bar->setExpanding(false);
    tab_widget.setTabsClosable(true);
    tab_widget.resize(480, 200);
    tab_widget.addTab(new QWidget(), QStringLiteral("one.xhtml"));
    tab_widget.addTab(new QWidget(), QStringLiteral("two.xhtml"));
    tab_widget.addTab(new QWidget(), QStringLiteral("three.xhtml"));
    tab_widget.show();
    application.processEvents();
    tab_bar->resize(tab_widget.width(), tab_bar->height());

    int request_count = 0;
    int requested_index = -1;
    QObject::connect(&tab_widget, &QTabWidget::tabCloseRequested,
                     [&request_count, &requested_index](int index) {
        ++request_count;
        requested_index = index;
    });

    SendMousePress(tab_bar, Qt::MiddleButton, tab_bar->tabRect(1).center());
    Require(request_count == 1,
            "middle-clicking a tab must request exactly one close");
    Require(requested_index == 1,
            "middle-clicking a tab must request the clicked tab index");
    Require(tab_widget.currentIndex() == 0,
            "middle-clicking a background tab must not activate it first");

    const QPoint blank_position(tab_bar->width() - 2,
                                tab_bar->tabRect(0).center().y());
    Require(tab_bar->tabAt(blank_position) == -1,
            "test setup must provide blank tab-bar space");
    SendMousePress(tab_bar, Qt::MiddleButton, blank_position);
    Require(request_count == 1,
            "middle-clicking blank tab-bar space must not request a close");

    SendMousePress(tab_bar, Qt::LeftButton, tab_bar->tabRect(0).center());
    Require(request_count == 1,
            "left-clicking a tab must not request a close");

    return EXIT_SUCCESS;
}
