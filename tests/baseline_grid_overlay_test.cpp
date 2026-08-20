#include "ViewEditors/BaselineGridOverlay.h"
#include "ViewEditors/Overlay.h"
#include "Misc/Utility.h"

#include <QApplication>
#include <QCoreApplication>
#include <QWidget>

#include <iostream>

QColor Utility::WebViewBackgroundColor(bool)
{
    return QColor(Qt::white);
}

namespace
{

bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << std::endl;
    }
    return condition;
}

}

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    OverlayHelperWidget helper;
    QWidget preview(&helper);
    BaselineGridOverlay overlay(&helper);
    helper.resize(320, 200);
    helper.show();
    QCoreApplication::processEvents();

    bool okay = true;
    okay &= expect(preview.geometry() == helper.rect(),
                   "Preview layer must fill the overlay helper");
    okay &= expect(overlay.geometry() == helper.rect(),
                   "baseline layer must track the Preview viewport geometry");
    okay &= expect(overlay.testAttribute(Qt::WA_TransparentForMouseEvents),
                   "baseline layer must never intercept Preview input");

    BaselineGridSettings settings;
    settings.enabled = true;
    overlay.setGridSettings(settings);
    QCoreApplication::processEvents();
    okay &= expect(overlay.isVisible(), "enabled grid must become visible");

    overlay.setCleanPreviewActive(true);
    QCoreApplication::processEvents();
    okay &= expect(!overlay.isVisible(), "Clean Preview must hide the grid layer");
    overlay.setCleanPreviewActive(false);
    QCoreApplication::processEvents();
    okay &= expect(overlay.isVisible(),
                   "leaving Clean Preview must restore the previous grid state");

    settings.enabled = false;
    overlay.setGridSettings(settings);
    QCoreApplication::processEvents();
    okay &= expect(!overlay.isVisible(), "disabled grid must remain hidden");

    return okay ? 0 : 1;
}
