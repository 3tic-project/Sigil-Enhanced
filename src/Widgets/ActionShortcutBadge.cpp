#include "Widgets/ActionShortcutBadge.h"

#include <QAction>
#include <QEvent>
#include <QFontMetrics>
#include <QPainter>
#include <QPaintEvent>
#include <QTimer>
#include <QToolButton>

ActionShortcutBadge::ActionShortcutBadge(
    QToolButton *button, QAction *action, const QKeySequence &defaultShortcut)
    : QWidget(button),
      m_button(button),
      m_action(action),
      m_defaultShortcut(defaultShortcut),
      m_lastShortcut(action ? action->shortcut() : QKeySequence()),
      m_originalStyleSheet(button ? button->styleSheet() : QString()),
      m_appliedStyleSheet(m_originalStyleSheet)
{
    setObjectName(QStringLiteral("actionShortcutBadge"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::NoFocus);
    if (m_button) {
        setGeometry(m_button->rect());
        m_button->installEventFilter(this);
    }
    if (m_action) {
        connect(m_action, &QAction::changed, this, [this]() {
            const QKeySequence shortcut = m_action->shortcut();
            const bool shortcut_changed = shortcut != m_lastShortcut;
            m_lastShortcut = shortcut;
            Refresh();
            if (shortcut_changed && m_shortcutChangedCallback) {
                m_shortcutChangedCallback();
            }
        });
    }
    Refresh();
}

void ActionShortcutBadge::setBadgesVisible(bool visible)
{
    if (m_badgesVisible == visible) return;
    m_badgesVisible = visible;
    Refresh();
}

void ActionShortcutBadge::setDefaultShortcut(const QKeySequence &shortcut)
{
    if (m_defaultShortcut == shortcut) return;
    m_defaultShortcut = shortcut;
    Refresh();
}

void ActionShortcutBadge::setShortcutChangedCallback(
    const std::function<void()> &callback)
{
    m_shortcutChangedCallback = callback;
}

QRect ActionShortcutBadge::badgeRect() const
{
    if (m_displayedText.isEmpty()) return QRect();
    const QFontMetrics metrics(font());
    const int height = qMax(14, metrics.height() + 2);
    const int width = qMax(height, metrics.horizontalAdvance(m_displayedText) + 8);
    return QRect(qMax(2, this->width() - width - 2), 2, width, height);
}

bool ActionShortcutBadge::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_button) {
        if (event->type() == QEvent::Resize || event->type() == QEvent::Show) {
            setGeometry(m_button->rect());
            raise();
            Refresh();
        } else if (event->type() == QEvent::MouseButtonPress
                   || event->type() == QEvent::MouseButtonRelease
                   || event->type() == QEvent::Enter
                   || event->type() == QEvent::Leave) {
            update();
        } else if (!m_adjustingStyle
                   && (event->type() == QEvent::StyleChange
                       || event->type() == QEvent::PaletteChange
                       || event->type() == QEvent::FontChange
                       || event->type() == QEvent::EnabledChange)) {
            // A stylesheet update can send palette/style events before the
            // new local stylesheet is observable. Refresh after that event
            // sequence so badge padding never overwrites the caller's style.
            QTimer::singleShot(0, this, [this]() {
                if (m_button->styleSheet() != m_appliedStyleSheet) {
                    m_originalStyleSheet = m_button->styleSheet();
                }
                Refresh();
            });
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ActionShortcutBadge::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    if (!isVisible() || m_displayedText.isEmpty()) return;

    const bool enabled = m_button && m_button->isEnabled();
    const QPalette::ColorGroup group = enabled
        ? QPalette::Active : QPalette::Disabled;
    QColor background = palette().color(group, QPalette::Highlight);
    QColor foreground = palette().color(group, QPalette::HighlightedText);
    if (enabled && m_button->isDown()) {
        background = palette().color(group, QPalette::Dark);
        foreground = palette().color(group, QPalette::ButtonText);
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QColor border = foreground;
    border.setAlpha(190);
    painter.setPen(QPen(border, 1));
    painter.setBrush(background);
    painter.drawRoundedRect(QRectF(badgeRect()).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
    painter.setPen(foreground);
    painter.setFont(font());
    painter.drawText(badgeRect(), Qt::AlignCenter, m_displayedText);
}

void ActionShortcutBadge::Refresh()
{
    if (!m_button || !m_action) {
        hide();
        return;
    }

    m_presentation = ShortcutBadgeModel::Present(
        m_action->shortcut(), m_defaultShortcut);
    m_displayedText = m_presentation.badgeText;

    QFont badge_font = m_button->font();
    badge_font.setBold(true);
    badge_font.setPixelSize(qMax(9, qMin(11, m_button->fontMetrics().height() - 2)));
    setFont(badge_font);

    if (!m_presentation.numeric && !m_displayedText.isEmpty()) {
        const QFontMetrics metrics(font());
        const int available = qMax(24, m_button->width() / 2);
        if (metrics.horizontalAdvance(m_displayedText) + 8 > available) {
            m_displayedText = tr("Key");
        }
    }

    const bool should_show = m_badgesVisible && m_action->isVisible()
        && !m_displayedText.isEmpty();
    setVisible(should_show);
    UpdateButtonPadding();
    setGeometry(m_button->rect());
    raise();
    update();
}

void ActionShortcutBadge::UpdateButtonPadding()
{
    QString style = m_originalStyleSheet;
    if (isVisible()) {
        if (!style.isEmpty()) style += QLatin1Char('\n');
        style += QStringLiteral("QToolButton { padding-right: %1px; }")
            .arg(badgeRect().width() + 5);
    }
    if (m_button->styleSheet() == style) return;
    m_adjustingStyle = true;
    m_button->setStyleSheet(style);
    m_appliedStyleSheet = style;
    m_button->updateGeometry();
    m_adjustingStyle = false;
}
