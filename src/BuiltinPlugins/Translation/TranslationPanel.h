#pragma once

#include <functional>
#include <QDockWidget>
#include <QJsonObject>

class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTextBrowser;

namespace SigilAgent { class IBookWorkspace; }
namespace BuiltinPlugins::Translation {
class TranslationController;

class TranslationPanel : public QDockWidget {
    Q_OBJECT
public:
    TranslationPanel(TranslationController *controller, SigilAgent::IBookWorkspace *workspace, QWidget *parent = nullptr);
    void setScopeProvider(std::function<QJsonObject(const QString &)> provider);
    void refreshModels();

signals:
    void settingsRequested();
    void locationRequested(const QString &resource, int line);

private:
    void refresh();
    void start();
    void applyContext();
    void importGlossary();
    void refreshContext(bool force = false);
    void showResult(bool ok, const QString &message);
    void saveOptions();
    void updateThinking();
    TranslationController *m_controller;
    SigilAgent::IBookWorkspace *m_workspace;
    std::function<QJsonObject(const QString &)> m_scopeProvider;
    QComboBox *m_scope;
    QComboBox *m_model;
    QComboBox *m_thinking;
    QComboBox *m_preset;
    QComboBox *m_misc;
    QSpinBox *m_concurrency;
    QCheckBox *m_scan;
    QCheckBox *m_review;
    QPushButton *m_start;
    QPushButton *m_pause;
    QPushButton *m_stop;
    QLabel *m_progress;
    QTextBrowser *m_notice;
    QPlainTextEdit *m_memory;
    QPlainTextEdit *m_terms;
    QPlainTextEdit *m_plan;
    QString m_contextId;
    bool m_loading = false;
    bool m_refreshPending = false;
};
}
