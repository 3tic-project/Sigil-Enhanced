#pragma once

#include <functional>

#include <QKeySequence>
#include <QWidget>

#include "Widgets/ShortcutBadgeModel.h"

class QAction;
class QToolButton;

class ActionShortcutBadge : public QWidget
{
    Q_OBJECT

public:
    ActionShortcutBadge(QToolButton *button, QAction *action,
                        const QKeySequence &defaultShortcut);

    void setBadgesVisible(bool visible);
    void setDefaultShortcut(const QKeySequence &shortcut);
    void setShortcutChangedCallback(const std::function<void()> &callback);

    QString displayedText() const { return m_displayedText; }
    QRect badgeRect() const;
    bool usesNumericBadge() const { return m_presentation.numeric; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void Refresh();
    void UpdateButtonPadding();

    QToolButton *m_button;
    QAction *m_action;
    QKeySequence m_defaultShortcut;
    QKeySequence m_lastShortcut;
    ShortcutBadgePresentation m_presentation;
    QString m_displayedText;
    QString m_originalStyleSheet;
    QString m_appliedStyleSheet;
    std::function<void()> m_shortcutChangedCallback;
    bool m_badgesVisible = true;
    bool m_adjustingStyle = false;
};
