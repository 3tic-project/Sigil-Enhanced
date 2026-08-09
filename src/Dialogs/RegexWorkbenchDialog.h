/************************************************************************
**
**  Copyright (C) 2026 Sigil-Ebook contributors
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef REGEX_WORKBENCH_DIALOG_H
#define REGEX_WORKBENCH_DIALOG_H

#include <atomic>
#include <memory>

#include <QDialog>
#include <QHash>
#include <QStringList>

#include "BuiltinPlugins/RegexWorkbench/RegexWorkbenchBatchRunner.h"
#include "MainUI/SearchBatchCoordinator.h"

class MainWindow;
class TextResource;

class QCheckBox;
class QCloseEvent;
class QComboBox;
class QDialogButtonBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QWidget;

template<typename T> class QFutureWatcher;

class RegexWorkbenchDialog final : public QDialog
{
    Q_OBJECT

public:
    struct TargetSet {
        QHash<QString, TextResource*> resources;
        QString currentPath;
        QStringList selectedPaths;
        QStringList htmlPaths;
        QStringList cssPaths;
        QStringList allTextPaths;
    };

    explicit RegexWorkbenchDialog(MainWindow* mainWindow,
                                  const TargetSet& targets,
                                  QWidget* parent = nullptr);
    ~RegexWorkbenchDialog() override;

signals:
    void OpenFileRequest(QString bookpath, int line, int start, int end);

protected:
    void closeEvent(QCloseEvent* event) override;

public slots:
    void reject() override;

private slots:
    void NewRecipe();
    void OpenRecipe();
    void SaveRecipe();
    void ImportSearchTemplate();
    void AddRule();
    void DuplicateRule();
    void RemoveRule();
    void MoveRuleUp();
    void MoveRuleDown();
    void RuleSelectionChanged(int row);
    void RuleEnabledChanged();
    void UpdateRuleControlState();
    void StartDryRun();
    void StartApply();
    void CancelRun();
    void RunFinished();
    void OpenResultRow(int row, int column);
    void ClearVariables();

private:
    enum class RunMode {
        None,
        DryRun,
        Apply
    };

    enum class TargetScope {
        CurrentFile,
        SelectedFiles,
        AllHtml,
        AllCss,
        AllText
    };

    void BuildUi();
    void RestoreSettings();
    void SaveSettings() const;
    void LoadRecipeIntoUi(const BuiltinPlugins::RegexWorkbench::RegexRecipe& recipe,
                          const QString& path = QString());
    void RefreshRuleList(int selectedRow = -1);
    void SaveCurrentRule();
    void LoadCurrentRule();
    bool RecipeFromUi(BuiltinPlugins::RegexWorkbench::RegexRecipe& recipe,
                      QString* error);
    QStringList SelectedTargetPaths() const;
    void StartRun(RunMode mode);
    void SetBusy(bool busy);
    void PopulateReport(const BuiltinPlugins::RegexWorkbench::RegexWorkbenchBatchResult& result,
                        bool applied);
    void PopulateVariables(
        const BuiltinPlugins::RegexWorkbench::SearchVariableStore::Snapshot& snapshot);
    void SetStatus(const QString& message, bool error = false);

    MainWindow* m_MainWindow;
    TargetSet m_Targets;
    BuiltinPlugins::RegexWorkbench::RegexRecipe m_Recipe;
    BuiltinPlugins::RegexWorkbench::SearchVariableStore m_Store;
    BuiltinPlugins::RegexWorkbench::RegexWorkbenchBatchResult m_LastResult;
    SearchBatchCoordinator::Snapshot m_RunSnapshot;
    QFutureWatcher<BuiltinPlugins::RegexWorkbench::RegexWorkbenchBatchResult>* m_Watcher;
    std::shared_ptr<std::atomic_bool> m_CancelFlag;
    QString m_RecipePath;
    QString m_LastRecipePath;
    int m_CurrentRuleRow;
    RunMode m_RunMode;
    bool m_Busy;

    QWidget* m_EditingPanel;
    QLineEdit* m_RecipeName;
    QListWidget* m_RuleList;
    QPushButton* m_NewButton;
    QPushButton* m_OpenButton;
    QPushButton* m_SaveButton;
    QPushButton* m_ImportButton;
    QPushButton* m_AddRuleButton;
    QPushButton* m_DuplicateRuleButton;
    QPushButton* m_RemoveRuleButton;
    QPushButton* m_UpRuleButton;
    QPushButton* m_DownRuleButton;
    QCheckBox* m_RuleEnabled;
    QLineEdit* m_RuleName;
    QComboBox* m_SecondaryMode;
    QPlainTextEdit* m_SecondaryPattern;
    QPlainTextEdit* m_FindPattern;
    QPlainTextEdit* m_ReplacePattern;
    QCheckBox* m_Recursive;
    QSpinBox* m_MaxIterations;
    QCheckBox* m_AllowEmpty;
    QCheckBox* m_VariableExpansion;
    QCheckBox* m_AutoIngest;
    QLineEdit* m_CaptureNames;
    QComboBox* m_TargetScope;
    QComboBox* m_VariableScope;
    QComboBox* m_WritePolicy;
    QPushButton* m_DryRunButton;
    QPushButton* m_ApplyButton;
    QPushButton* m_CancelButton;
    QPushButton* m_ClearVariablesButton;
    QTableWidget* m_ReportTable;
    QTableWidget* m_VariableTable;
    QLabel* m_Status;
    QProgressBar* m_Progress;
    QDialogButtonBox* m_ButtonBox;
};

#endif // REGEX_WORKBENCH_DIALOG_H
