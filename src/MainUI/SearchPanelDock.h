#pragma once

#include <QDockWidget>
#include <QEvent>
#include <QVBoxLayout>

// Search spans the bottom of the window so opening it does not increase the
// editor column's minimum width and squeeze the Book Browser or Preview.
class SearchPanelDock final : public QDockWidget
{
public:
    SearchPanelDock(QWidget* original, QWidget* enhanced, QWidget* parent)
        : QDockWidget(parent), m_Original(original), m_Enhanced(enhanced)
    {
        setObjectName(QStringLiteral("findreplacedock"));
        setAllowedAreas(Qt::BottomDockWidgetArea);
        setFeatures(QDockWidget::NoDockWidgetFeatures);
        auto* title = new QWidget(this);
        auto* title_layout = new QVBoxLayout(title);
        title_layout->setContentsMargins(0, 0, 0, 0);
        title->setFixedHeight(0);
        setTitleBarWidget(title);
        auto* contents = new QWidget(this);
        contents->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        auto* layout = new QVBoxLayout(contents);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        layout->addWidget(original);
        layout->addWidget(enhanced);
        setWidget(contents);
        original->installEventFilter(this);
        enhanced->installEventFilter(this);
        hide();
    }

    void syncVisibility()
    {
        setVisible(!m_Original->isHidden() || !m_Enhanced->isHidden());
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if ((watched == m_Original || watched == m_Enhanced) &&
            (event->type() == QEvent::ShowToParent || event->type() == QEvent::HideToParent)) {
            syncVisibility();
        }
        return QDockWidget::eventFilter(watched, event);
    }

private:
    QWidget* m_Original;
    QWidget* m_Enhanced;
};
