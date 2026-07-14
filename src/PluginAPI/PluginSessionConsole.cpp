/************************************************************************
**
**  This file is part of Sigil.
**
*************************************************************************/

#include "PluginAPI/PluginSessionConsole.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

PluginSessionConsole::PluginSessionConsole(const QString &plugin_name, QWidget *parent) :
    QDialog(parent),
    m_StatusLabel(new QLabel(tr("Starting"), this)),
    m_Output(new QPlainTextEdit(this)),
    m_CancelButton(new QPushButton(tr("Cancel"), this)),
    m_CloseButton(new QPushButton(tr("Close"), this))
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Live Plugin: %1").arg(plugin_name));
    setModal(false);
    resize(640, 360);

    m_Output->setReadOnly(true);
    m_CloseButton->setEnabled(false);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(m_CancelButton);
    buttons->addWidget(m_CloseButton);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_StatusLabel);
    layout->addWidget(m_Output, 1);
    layout->addLayout(buttons);

    connect(m_CancelButton, &QPushButton::clicked, this, &PluginSessionConsole::CancelRequested);
    connect(m_CloseButton, &QPushButton::clicked, this, &QDialog::close);
}

void PluginSessionConsole::AppendOutput(const QString &text)
{
    if (!text.isEmpty()) {
        m_Output->appendPlainText(text.trimmed());
    }
}

void PluginSessionConsole::SetStatus(const QString &status)
{
    m_StatusLabel->setText(status);
}

void PluginSessionConsole::SetFinished()
{
    m_CancelButton->setEnabled(false);
    m_CloseButton->setEnabled(true);
}
