/************************************************************************
**
**  This file is part of Sigil.
**
*************************************************************************/

#pragma once
#ifndef PLUGINSESSIONCONSOLE_H
#define PLUGINSESSIONCONSOLE_H

#include <QDialog>

class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;

class PluginSessionConsole : public QDialog
{
    Q_OBJECT

public:
    explicit PluginSessionConsole(const QString &plugin_name, QWidget *parent = nullptr);

    void AppendOutput(const QString &text);
    void SetStatus(const QString &status);
    void SetProgress(const QString &label, int value, int maximum);
    void ClearProgress();
    void SetFinished();

signals:
    void CancelRequested();

private:
    QLabel *m_StatusLabel;
    QPlainTextEdit *m_Output;
    QProgressBar *m_ProgressBar;
    QPushButton *m_CancelButton;
    QPushButton *m_CloseButton;
};

#endif // PLUGINSESSIONCONSOLE_H
