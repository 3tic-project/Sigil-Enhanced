/************************************************************************
**
**  Copyright (C) 2026 Sigil-Ebook contributors
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Dialogs/RegexWorkbenchDialog.h"

#include <algorithm>
#include <utility>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QFont>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QReadLocker>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardItem>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUuid>
#include <QVBoxLayout>
#include <QtConcurrent>

#include "BuiltinPlugins/RegexWorkbench/RegexRecipeSearchEditorAdapter.h"
#include "BookManipulation/FolderKeeper.h"
#include "MainUI/BookBrowser.h"
#include "MainUI/MainWindow.h"
#include "MainUI/RegexWorkbenchBatchCommitter.h"
#include "Misc/SettingsStore.h"
#include "MiscEditors/SearchEditorModelPlus.h"
#include "ResourceObjects/CSSResource.h"
#include "ResourceObjects/HTMLResource.h"
#include "ResourceObjects/Resource.h"
#include "ResourceObjects/TextResource.h"
#include "Tabs/ContentTab.h"

namespace
{

using BuiltinPlugins::RegexWorkbench::CoordinateSpace;
using BuiltinPlugins::RegexWorkbench::RegexRecipe;
using BuiltinPlugins::RegexWorkbench::RegexRecipeImportResult;
using BuiltinPlugins::RegexWorkbench::RegexRecipeSearchEditorAdapter;
using BuiltinPlugins::RegexWorkbench::RegexRecipeStore;
using BuiltinPlugins::RegexWorkbench::RegexWorkbenchBatchOptions;
using BuiltinPlugins::RegexWorkbench::RegexWorkbenchBatchResult;
using BuiltinPlugins::RegexWorkbench::RegexWorkbenchBatchRunner;
using BuiltinPlugins::RegexWorkbench::RegexWorkbenchReportRow;
using BuiltinPlugins::RegexWorkbench::RegexWorkbenchRule;
using BuiltinPlugins::RegexWorkbench::SearchVariableStore;
using BuiltinPlugins::RegexWorkbench::SecondaryMode;
using BuiltinPlugins::RegexWorkbench::VariableScope;
using BuiltinPlugins::RegexWorkbench::WritePolicy;

const QString GeometryKey = QStringLiteral("regex_workbench/geometry");
const QString LastRecipeKey = QStringLiteral("regex_workbench/last_recipe_path");
const QString DefaultIterationsKey =
    QStringLiteral("regex_workbench/default_max_iterations");

QString NewRuleId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

RegexWorkbenchRule NewRule(int number, int maxIterations)
{
    RegexWorkbenchRule rule;
    rule.id = NewRuleId();
    rule.name = QObject::tr("Rule %1").arg(number);
    rule.maxIterations = maxIterations;
    return rule;
}

int FindData(QComboBox* combo, int value)
{
    const int index = combo->findData(value);
    return index < 0 ? 0 : index;
}

QStringList CaptureNames(const QString& text)
{
    QStringList names = text.split(QRegularExpression(QStringLiteral("[,\\s]+")),
                                   Qt::SkipEmptyParts);
    names.removeDuplicates();
    return names;
}

void AddVariableRows(QTableWidget* table,
                     const QString& scope,
                     const QString& resource,
                     const SearchVariableStore::Frame& frame)
{
    QStringList names = frame.keys();
    names.sort();
    for (const QString& name : names) {
        const int row = table->rowCount();
        table->insertRow(row);
        table->setItem(row, 0, new QTableWidgetItem(scope));
        table->setItem(row, 1, new QTableWidgetItem(resource));
        table->setItem(row, 2, new QTableWidgetItem(name));
        table->setItem(row, 3, new QTableWidgetItem(
            frame.value(name).join(QStringLiteral("\n"))));
    }
}

}

RegexWorkbenchDialog::TargetSet RegexWorkbenchDialog::CollectTargets(
    MainWindow* mainWindow)
{
    TargetSet targets;
    if (!mainWindow) {
        return targets;
    }
    const QSharedPointer<Book> book = mainWindow->GetCurrentBook();
    if (!book || !book->GetFolderKeeper()) {
        return targets;
    }
    for (Resource* resource : book->GetFolderKeeper()->GetResourceList()) {
        TextResource* text = qobject_cast<TextResource*>(resource);
        if (!text) {
            continue;
        }
        const QString path = text->GetRelativePath();
        targets.resources.insert(path, text);
        targets.allTextPaths.append(path);
        if (qobject_cast<HTMLResource*>(text)) {
            targets.htmlPaths.append(path);
        } else if (qobject_cast<CSSResource*>(text)) {
            targets.cssPaths.append(path);
        } else {
            targets.specialPaths.append(path);
        }
    }
    if (ContentTab* currentTab = mainWindow->GetCurrentContentTab()) {
        if (TextResource* current =
                qobject_cast<TextResource*>(currentTab->GetLoadedResource())) {
            targets.currentPath = current->GetRelativePath();
        }
    }
    if (BookBrowser* bookBrowser = mainWindow->GetBookBrowser()) {
        for (Resource* resource : bookBrowser->AllSelectedResources()) {
            if (TextResource* selected = qobject_cast<TextResource*>(resource)) {
                targets.selectedPaths.append(selected->GetRelativePath());
            }
        }
        targets.selectedPaths.removeDuplicates();
        targets.selectedPaths.sort();
    }
    targets.htmlPaths.sort();
    targets.cssPaths.sort();
    targets.specialPaths.sort();
    targets.allTextPaths.sort();
    return targets;
}

RegexWorkbenchDialog::RegexWorkbenchDialog(MainWindow* mainWindow,
                                           const TargetSet& targets,
                                           QWidget* parent)
    : QDialog(parent, Qt::Window | Qt::WindowTitleHint |
                          Qt::WindowSystemMenuHint |
                          Qt::WindowMinimizeButtonHint |
                          Qt::WindowMaximizeButtonHint |
                          Qt::WindowCloseButtonHint),
      m_MainWindow(mainWindow),
      m_BookContext(mainWindow ? mainWindow->GetCurrentBook()
                               : QSharedPointer<Book>()),
      m_Targets(targets),
      m_Watcher(new QFutureWatcher<RegexWorkbenchBatchResult>(this)),
      m_CurrentRuleRow(-1),
      m_RunMode(RunMode::None),
      m_Busy(false),
      m_CloseWhenIdle(false),
      m_ReportApplied(false),
      m_EditingPanel(nullptr),
      m_RecipeName(nullptr),
      m_RuleList(nullptr),
      m_NewButton(nullptr),
      m_OpenButton(nullptr),
      m_SaveButton(nullptr),
      m_ImportButton(nullptr),
      m_AddRuleButton(nullptr),
      m_DuplicateRuleButton(nullptr),
      m_RemoveRuleButton(nullptr),
      m_UpRuleButton(nullptr),
      m_DownRuleButton(nullptr),
      m_RuleEnabled(nullptr),
      m_RuleName(nullptr),
      m_SecondaryMode(nullptr),
      m_SecondaryPattern(nullptr),
      m_FindPattern(nullptr),
      m_ReplacePattern(nullptr),
      m_Recursive(nullptr),
      m_MaxIterations(nullptr),
      m_AllowEmpty(nullptr),
      m_CaptureOnly(nullptr),
      m_VariableExpansion(nullptr),
      m_AutoIngest(nullptr),
      m_CaptureNames(nullptr),
      m_TargetScope(nullptr),
      m_VariableScope(nullptr),
      m_WritePolicy(nullptr),
      m_DryRunButton(nullptr),
      m_ApplyButton(nullptr),
      m_CancelButton(nullptr),
      m_ClearVariablesButton(nullptr),
      m_ReportTable(nullptr),
      m_VariableTable(nullptr),
      m_Status(nullptr),
      m_Progress(nullptr),
      m_ButtonBox(nullptr)
{
    setWindowModality(Qt::NonModal);
    for (auto it = targets.resources.constBegin();
         it != targets.resources.constEnd(); ++it) {
        m_ResourceGuards.insert(it.key(), it.value());
    }
    BuildUi();
    RestoreSettings();
    NewRecipe();
    connect(m_Watcher, &QFutureWatcher<RegexWorkbenchBatchResult>::finished,
            this, &RegexWorkbenchDialog::RunFinished);
}

RegexWorkbenchDialog::~RegexWorkbenchDialog()
{
    if (m_CancelFlag) {
        m_CancelFlag->store(true, std::memory_order_relaxed);
    }
    if (m_Watcher->isRunning()) {
        m_Watcher->waitForFinished();
    }
    SaveSettings();
}

void RegexWorkbenchDialog::BuildUi()
{
    setWindowTitle(tr("Advanced Regex Workbench"));
    setMinimumSize(1050, 680);
    resize(1280, 820);

    auto* root = new QVBoxLayout(this);
    auto* fileBar = new QHBoxLayout;
    m_NewButton = new QPushButton(tr("New"), this);
    m_OpenButton = new QPushButton(tr("Open Recipe..."), this);
    m_SaveButton = new QPushButton(tr("Save Recipe..."), this);
    m_ImportButton = new QPushButton(tr("Import Search Template..."), this);
    m_ImportButton->setObjectName(QStringLiteral("regexImportSearchTemplate"));
    m_ImportButton->setVisible(false);
    m_RecipeName = new QLineEdit(this);
    m_RecipeName->setObjectName(QStringLiteral("regexRecipeName"));
    fileBar->addWidget(m_NewButton);
    fileBar->addWidget(m_OpenButton);
    fileBar->addWidget(m_SaveButton);
    fileBar->addWidget(m_ImportButton);
    fileBar->addSpacing(12);
    fileBar->addWidget(new QLabel(tr("Recipe:"), this));
    fileBar->addWidget(m_RecipeName, 1);
    root->addLayout(fileBar);

    m_EditingPanel = new QWidget(this);
    auto* editingLayout = new QHBoxLayout(m_EditingPanel);
    editingLayout->setContentsMargins(0, 0, 0, 0);
    auto* editingSplitter = new QSplitter(Qt::Horizontal, m_EditingPanel);
    editingSplitter->setObjectName(QStringLiteral("regexEditingSplitter"));
    editingSplitter->setChildrenCollapsible(false);
    editingLayout->addWidget(editingSplitter);

    auto* rulesBox = new QGroupBox(tr("Rules"), m_EditingPanel);
    auto* rulesLayout = new QVBoxLayout(rulesBox);
    m_RuleList = new QListWidget(rulesBox);
    m_RuleList->setObjectName(QStringLiteral("regexRuleList"));
    m_RuleList->setSelectionMode(QAbstractItemView::SingleSelection);
    rulesLayout->addWidget(m_RuleList, 1);
    auto* ruleButtons = new QGridLayout;
    m_AddRuleButton = new QPushButton(tr("Add"), rulesBox);
    m_DuplicateRuleButton = new QPushButton(tr("Duplicate"), rulesBox);
    m_RemoveRuleButton = new QPushButton(tr("Remove"), rulesBox);
    m_UpRuleButton = new QPushButton(tr("Up"), rulesBox);
    m_DownRuleButton = new QPushButton(tr("Down"), rulesBox);
    ruleButtons->addWidget(m_AddRuleButton, 0, 0);
    ruleButtons->addWidget(m_DuplicateRuleButton, 0, 1);
    ruleButtons->addWidget(m_RemoveRuleButton, 1, 0, 1, 2);
    ruleButtons->addWidget(m_UpRuleButton, 2, 0);
    ruleButtons->addWidget(m_DownRuleButton, 2, 1);
    rulesLayout->addLayout(ruleButtons);

    auto* editorBox = new QGroupBox(tr("Rule editor"), m_EditingPanel);
    auto* editor = new QVBoxLayout(editorBox);
    m_RuleEnabled = new QCheckBox(tr("Enabled"), editorBox);
    m_RuleName = new QLineEdit(editorBox);
    m_RuleName->setObjectName(QStringLiteral("regexRuleName"));

    auto* identity = new QGridLayout;
    identity->addWidget(new QLabel(tr("Name:"), editorBox), 0, 0);
    identity->addWidget(m_RuleName, 0, 1);
    identity->addWidget(m_RuleEnabled, 0, 2);
    identity->setColumnStretch(1, 1);
    editor->addLayout(identity);

    auto* patternsBox = new QGroupBox(tr("Patterns"), editorBox);
    auto* patterns = new QGridLayout(patternsBox);
    m_SecondaryMode = new QComboBox(editorBox);
    m_SecondaryMode->addItem(tr("None"), static_cast<int>(SecondaryMode::None));
    m_SecondaryMode->addItem(tr("PreSearch range"),
                             static_cast<int>(SecondaryMode::PreSearch));
    m_SecondaryMode->addItem(tr("Accept when secondary matches"),
                             static_cast<int>(SecondaryMode::FilterAccept));
    m_SecondaryMode->addItem(tr("Reject when secondary matches"),
                             static_cast<int>(SecondaryMode::FilterReject));
    m_SecondaryPattern = new QPlainTextEdit(editorBox);
    m_SecondaryPattern->setObjectName(QStringLiteral("regexSecondaryPattern"));
    m_SecondaryPattern->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_SecondaryPattern->setMaximumHeight(64);
    m_FindPattern = new QPlainTextEdit(editorBox);
    m_FindPattern->setObjectName(QStringLiteral("regexFindPattern"));
    m_FindPattern->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_FindPattern->setMaximumHeight(76);
    m_ReplacePattern = new QPlainTextEdit(editorBox);
    m_ReplacePattern->setObjectName(QStringLiteral("regexReplacePattern"));
    m_ReplacePattern->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_ReplacePattern->setMaximumHeight(76);

    patterns->addWidget(new QLabel(tr("Secondary mode:"), patternsBox), 0, 0);
    patterns->addWidget(m_SecondaryMode, 0, 1);
    patterns->addWidget(new QLabel(tr("Secondary regex:"), patternsBox), 1, 0, 1, 2);
    patterns->addWidget(m_SecondaryPattern, 2, 0, 1, 2);
    patterns->addWidget(new QLabel(tr("Find regex:"), patternsBox), 3, 0);
    patterns->addWidget(new QLabel(tr("Replacement:"), patternsBox), 3, 1);
    patterns->addWidget(m_FindPattern, 4, 0);
    patterns->addWidget(m_ReplacePattern, 4, 1);
    patterns->setColumnStretch(0, 1);
    patterns->setColumnStretch(1, 1);
    editor->addWidget(patternsBox, 1);

    auto* optionsBox = new QGroupBox(tr("Options"), editorBox);
    auto* options = new QGridLayout(optionsBox);
    m_Recursive = new QCheckBox(tr("Repeat until no matches remain"), editorBox);
    m_MaxIterations = new QSpinBox(editorBox);
    m_MaxIterations->setRange(1, 10000);
    m_AllowEmpty = new QCheckBox(tr("Allow zero-length matches"), editorBox);
    m_CaptureOnly = new QCheckBox(
        tr("Capture variables only (do not replace)"), editorBox);
    m_CaptureOnly->setObjectName(QStringLiteral("regexCaptureOnly"));
    m_CaptureOnly->setToolTip(
        tr("Enumerate accepted matches and store named captures without changing text."));
    m_VariableExpansion = new QCheckBox(tr("Expand ${var:name} in replacement"), editorBox);
    options->addWidget(m_CaptureOnly, 0, 0, 1, 3);
    options->addWidget(m_Recursive, 1, 0);
    options->addWidget(new QLabel(tr("Maximum iterations:"), optionsBox), 1, 1);
    options->addWidget(m_MaxIterations, 1, 2);
    options->addWidget(m_AllowEmpty, 2, 0, 1, 3);
    options->addWidget(m_VariableExpansion, 3, 0, 1, 3);
    options->setColumnStretch(0, 1);
    editor->addWidget(optionsBox);

    auto* capturesBox = new QGroupBox(tr("Named captures"), editorBox);
    auto* captures = new QGridLayout(capturesBox);
    m_AutoIngest = new QCheckBox(tr("Store all named captures"), editorBox);
    m_CaptureNames = new QLineEdit(editorBox);
    m_CaptureNames->setPlaceholderText(tr("name1, name2 (optional allowlist)"));
    captures->addWidget(m_AutoIngest, 0, 0, 1, 2);
    captures->addWidget(new QLabel(tr("Capture variables:"), capturesBox), 1, 0);
    captures->addWidget(m_CaptureNames, 1, 1);
    captures->setColumnStretch(1, 1);
    editor->addWidget(capturesBox);

    auto* runBox = new QGroupBox(tr("Run"), m_EditingPanel);
    auto* runLayout = new QVBoxLayout(runBox);
    auto* runForm = new QFormLayout;
    m_TargetScope = new QComboBox(runBox);
    PopulateTargetScopes(static_cast<int>(TargetScope::CurrentFile));
    m_VariableScope = new QComboBox(runBox);
    m_VariableScope->addItem(tr("Resource"), static_cast<int>(VariableScope::Resource));
    m_VariableScope->addItem(tr("Batch"), static_cast<int>(VariableScope::Batch));
    m_VariableScope->addItem(tr("Session"), static_cast<int>(VariableScope::Session));
    m_WritePolicy = new QComboBox(runBox);
    m_WritePolicy->addItem(tr("Last value wins"), static_cast<int>(WritePolicy::LastWins));
    m_WritePolicy->addItem(tr("Keep first value"), static_cast<int>(WritePolicy::FirstOnly));
    m_WritePolicy->addItem(tr("Append values"), static_cast<int>(WritePolicy::Append));
    m_DryRunButton = new QPushButton(tr("Dry Run"), runBox);
    m_DryRunButton->setObjectName(QStringLiteral("regexDryRunButton"));
    m_ApplyButton = new QPushButton(tr("Apply"), runBox);
    m_ApplyButton->setObjectName(QStringLiteral("regexApplyButton"));
    m_ApplyButton->setDefault(true);
    m_CancelButton = new QPushButton(tr("Cancel Run"), runBox);
    m_CancelButton->setEnabled(false);
    runForm->addRow(tr("Files:"), m_TargetScope);
    runForm->addRow(tr("Variable scope:"), m_VariableScope);
    runForm->addRow(tr("Write policy:"), m_WritePolicy);
    runLayout->addLayout(runForm);
    auto* runButtons = new QHBoxLayout;
    runButtons->addWidget(m_DryRunButton);
    runButtons->addWidget(m_ApplyButton);
    runLayout->addLayout(runButtons);
    runLayout->addWidget(m_CancelButton);

    auto* variablesBox = new QGroupBox(runBox);
    variablesBox->setObjectName(QStringLiteral("regexVariablesPanel"));
    auto* variablesLayout = new QVBoxLayout(variablesBox);
    m_ClearVariablesButton = new QPushButton(tr("Clear Variables"), variablesBox);
    auto* variableActions = new QHBoxLayout;
    auto* variableTitle = new QLabel(tr("Variables"), variablesBox);
    variableTitle->setObjectName(QStringLiteral("regexVariablesTitle"));
    QFont variableTitleFont = variableTitle->font();
    variableTitleFont.setBold(true);
    variableTitle->setFont(variableTitleFont);
    variableActions->addWidget(variableTitle);
    variableActions->addStretch();
    variableActions->addWidget(m_ClearVariablesButton);
    variablesLayout->addLayout(variableActions);
    m_VariableTable = new QTableWidget(variablesBox);
    m_VariableTable->setObjectName(QStringLiteral("regexVariableTable"));
    m_VariableTable->setColumnCount(4);
    m_VariableTable->setHorizontalHeaderLabels(
        {tr("Scope"), tr("Resource"), tr("Name"), tr("Value")});
    m_VariableTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_VariableTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_VariableTable->verticalHeader()->setVisible(false);
    m_VariableTable->horizontalHeader()->setSectionResizeMode(
        QHeaderView::ResizeToContents);
    m_VariableTable->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::Stretch);
    m_VariableTable->horizontalHeader()->setSectionResizeMode(
        3, QHeaderView::Stretch);
    variablesLayout->addWidget(m_VariableTable, 1);
    runLayout->addWidget(variablesBox, 1);

    editingSplitter->addWidget(rulesBox);
    editingSplitter->addWidget(editorBox);
    editingSplitter->addWidget(runBox);
    editingSplitter->setStretchFactor(0, 2);
    editingSplitter->setStretchFactor(1, 6);
    editingSplitter->setStretchFactor(2, 3);
    editingSplitter->setSizes({220, 680, 340});
    root->addWidget(m_EditingPanel, 3);

    auto* resultsBox = new QGroupBox(tr("Dry-Run results"), this);
    auto* resultsLayout = new QVBoxLayout(resultsBox);
    m_ReportTable = new QTableWidget(resultsBox);
    m_ReportTable->setObjectName(QStringLiteral("regexReportTable"));
    m_ReportTable->setColumnCount(7);
    m_ReportTable->setHorizontalHeaderLabels(
        {tr("Rule"), tr("File"), tr("Line"), tr("Before"), tr("After"),
         tr("Iteration"), tr("Variables")});
    m_ReportTable->setAlternatingRowColors(true);
    m_ReportTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_ReportTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_ReportTable->verticalHeader()->setVisible(false);
    m_ReportTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_ReportTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_ReportTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    resultsLayout->addWidget(m_ReportTable);
    root->addWidget(resultsBox, 2);

    auto* bottom = new QHBoxLayout;
    m_Progress = new QProgressBar(this);
    m_Progress->setTextVisible(false);
    m_Progress->setVisible(false);
    m_Status = new QLabel(tr("Ready."), this);
    m_Status->setWordWrap(true);
    m_ButtonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    bottom->addWidget(m_Progress);
    bottom->addWidget(m_Status, 1);
    bottom->addWidget(m_ButtonBox);
    root->addLayout(bottom);

    connect(m_NewButton, &QPushButton::clicked, this, &RegexWorkbenchDialog::NewRecipe);
    connect(m_OpenButton, &QPushButton::clicked, this, &RegexWorkbenchDialog::OpenRecipe);
    connect(m_SaveButton, &QPushButton::clicked, this, &RegexWorkbenchDialog::SaveRecipe);
    connect(m_ImportButton, &QPushButton::clicked,
            this, &RegexWorkbenchDialog::ImportSearchTemplate);
    connect(m_AddRuleButton, &QPushButton::clicked, this, &RegexWorkbenchDialog::AddRule);
    connect(m_DuplicateRuleButton, &QPushButton::clicked,
            this, &RegexWorkbenchDialog::DuplicateRule);
    connect(m_RemoveRuleButton, &QPushButton::clicked,
            this, &RegexWorkbenchDialog::RemoveRule);
    connect(m_UpRuleButton, &QPushButton::clicked, this, &RegexWorkbenchDialog::MoveRuleUp);
    connect(m_DownRuleButton, &QPushButton::clicked,
            this, &RegexWorkbenchDialog::MoveRuleDown);
    connect(m_RuleList, &QListWidget::currentRowChanged,
            this, &RegexWorkbenchDialog::RuleSelectionChanged);
    connect(m_RuleList, &QListWidget::itemChanged, this,
            [this](QListWidgetItem* item) {
                const int row = m_RuleList->row(item);
                if (row < 0 || row >= m_Recipe.rules.size()) {
                    return;
                }
                const bool enabled = item->checkState() == Qt::Checked;
                m_Recipe.rules[row].enabled = enabled;
                if (row == m_CurrentRuleRow) {
                    const QSignalBlocker blocker(m_RuleEnabled);
                    m_RuleEnabled->setChecked(enabled);
                }
            });
    connect(m_RuleEnabled, &QCheckBox::toggled,
            this, &RegexWorkbenchDialog::RuleEnabledChanged);
    connect(m_SecondaryMode, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &RegexWorkbenchDialog::UpdateRuleControlState);
    connect(m_Recursive, &QCheckBox::toggled,
            this, &RegexWorkbenchDialog::UpdateRuleControlState);
    connect(m_CaptureOnly, &QCheckBox::toggled,
            this, &RegexWorkbenchDialog::UpdateRuleControlState);
    connect(m_DryRunButton, &QPushButton::clicked,
            this, &RegexWorkbenchDialog::StartDryRun);
    connect(m_ApplyButton, &QPushButton::clicked,
            this, &RegexWorkbenchDialog::StartApply);
    connect(m_CancelButton, &QPushButton::clicked,
            this, &RegexWorkbenchDialog::CancelRun);
    connect(m_ClearVariablesButton, &QPushButton::clicked,
            this, &RegexWorkbenchDialog::ClearVariables);
    connect(m_ReportTable, &QTableWidget::cellDoubleClicked,
            this, &RegexWorkbenchDialog::OpenResultRow);
    connect(m_ButtonBox, &QDialogButtonBox::rejected,
            this, &RegexWorkbenchDialog::reject);
}

void RegexWorkbenchDialog::RestoreSettings()
{
    SettingsStore settings;
    const QByteArray geometry = settings.value(GeometryKey).toByteArray();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    }
    m_LastRecipePath = settings.value(LastRecipeKey).toString();
}

void RegexWorkbenchDialog::SaveSettings() const
{
    SettingsStore settings;
    settings.setValue(GeometryKey, saveGeometry());
    settings.setValue(LastRecipeKey, m_LastRecipePath);
    settings.setValue(DefaultIterationsKey, m_MaxIterations->value());
}

void RegexWorkbenchDialog::closeEvent(QCloseEvent* event)
{
    if (m_Busy) {
        SetStatus(tr("Cancel the active run before closing the workbench."), true);
        event->ignore();
        return;
    }
    SaveSettings();
    QDialog::closeEvent(event);
}

void RegexWorkbenchDialog::changeEvent(QEvent* event)
{
    QDialog::changeEvent(event);
    if (event->type() == QEvent::WindowActivate && !m_Busy) {
        RefreshTargets();
    }
}

void RegexWorkbenchDialog::reject()
{
    if (m_Busy) {
        SetStatus(tr("Cancel the active run before closing the workbench."), true);
        return;
    }
    QDialog::reject();
}

void RegexWorkbenchDialog::SyncHostWindowTitle(const QString& hostTitle,
                                               bool modified)
{
    QString visibleHostTitle = hostTitle;
    visibleHostTitle.replace(QStringLiteral("[*]"),
                             modified ? QStringLiteral("*") : QString());
    setWindowTitle(tr("Advanced Regex Workbench — %1").arg(visibleHostTitle));
}

void RegexWorkbenchDialog::CloseForBookChange()
{
    m_CloseWhenIdle = true;
    if (m_Busy) {
        CancelRun();
        hide();
        return;
    }
    close();
}

void RegexWorkbenchDialog::NewRecipe()
{
    if (!IsPristineNewRecipe() &&
        QMessageBox::question(
            this, tr("New Regex Workbench Recipe"),
            tr("Creating a new recipe will clear the current rules and unsaved "
               "rule changes. Continue?"),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel) != QMessageBox::Yes) {
        return;
    }
    SettingsStore settings;
    const int maxIterations = std::clamp(
        settings.value(DefaultIterationsKey, 32).toInt(), 1, 10000);
    RegexRecipe recipe;
    recipe.name = tr("Untitled Recipe");
    recipe.rules.append(NewRule(1, maxIterations));
    LoadRecipeIntoUi(recipe);
    SetStatus(tr("New recipe created."));
}

bool RegexWorkbenchDialog::IsPristineNewRecipe()
{
    if (m_Recipe.rules.isEmpty()) {
        return true;
    }
    SaveCurrentRule();
    m_Recipe.name = m_RecipeName->text().trimmed();
    m_Recipe.variableScope = static_cast<VariableScope>(
        m_VariableScope->currentData().toInt());
    m_Recipe.writePolicy = static_cast<WritePolicy>(
        m_WritePolicy->currentData().toInt());
    SettingsStore settings;
    const int defaultMaxIterations = std::clamp(
        settings.value(DefaultIterationsKey, 32).toInt(), 1, 10000);
    if (!m_RecipePath.isEmpty() || m_Recipe.name != tr("Untitled Recipe") ||
        m_Recipe.variableScope != VariableScope::Batch ||
        m_Recipe.writePolicy != WritePolicy::LastWins ||
        m_Recipe.rules.size() != 1) {
        return false;
    }
    const RegexWorkbenchRule& rule = m_Recipe.rules.constFirst();
    return rule.name == QObject::tr("Rule %1").arg(1) && rule.find.isEmpty() &&
           rule.secondaryPattern.isEmpty() && rule.replace.isEmpty() &&
           rule.secondaryMode == SecondaryMode::None && !rule.recursive &&
           rule.maxIterations == defaultMaxIterations &&
           !rule.allowEmpty && !rule.captureOnly &&
           !rule.variableExpansionEnabled && !rule.autoIngestNamedCaptures &&
           rule.captureToVar.isEmpty() && rule.enabled;
}

bool RegexWorkbenchDialog::HasCurrentBook() const
{
    return m_MainWindow && m_BookContext &&
           m_MainWindow->GetCurrentBook().data() == m_BookContext.data();
}

QHash<QString, TextResource*> RegexWorkbenchDialog::LiveResources() const
{
    QHash<QString, TextResource*> resources;
    for (auto it = m_ResourceGuards.constBegin();
         it != m_ResourceGuards.constEnd(); ++it) {
        if (it.value()) {
            resources.insert(it.key(), it.value().data());
        }
    }
    return resources;
}

void RegexWorkbenchDialog::RefreshTargets()
{
    if (!HasCurrentBook()) {
        return;
    }
    const int preferredScope = m_TargetScope && m_TargetScope->currentIndex() >= 0
                                   ? m_TargetScope->currentData().toInt()
                                   : static_cast<int>(TargetScope::CurrentFile);
    m_Targets = CollectTargets(m_MainWindow);
    m_ResourceGuards.clear();
    for (auto it = m_Targets.resources.constBegin();
         it != m_Targets.resources.constEnd(); ++it) {
        m_ResourceGuards.insert(it.key(), it.value());
    }
    PopulateTargetScopes(preferredScope);
}

void RegexWorkbenchDialog::PopulateTargetScopes(int preferredScope)
{
    if (!m_TargetScope) {
        return;
    }
    const QSignalBlocker blocker(m_TargetScope);
    m_TargetScope->clear();
    m_TargetScope->addItem(tr("Current file"),
                           static_cast<int>(TargetScope::CurrentFile));
    m_TargetScope->addItem(tr("Selected text files (%1)")
                               .arg(m_Targets.selectedPaths.size()),
                           static_cast<int>(TargetScope::SelectedFiles));
    if (!m_Targets.htmlPaths.isEmpty()) {
        m_TargetScope->addItem(tr("All XHTML files (%1)")
                                   .arg(m_Targets.htmlPaths.size()),
                               static_cast<int>(TargetScope::AllHtml));
    }
    if (!m_Targets.cssPaths.isEmpty()) {
        m_TargetScope->addItem(tr("All CSS files (%1)")
                                   .arg(m_Targets.cssPaths.size()),
                               static_cast<int>(TargetScope::AllCss));
    }
    if (!m_Targets.specialPaths.isEmpty()) {
        m_TargetScope->addItem(tr("All special text files (%1)")
                                   .arg(m_Targets.specialPaths.size()),
                               static_cast<int>(TargetScope::AllSpecial));
    }
    m_TargetScope->addItem(tr("All text resources (%1)")
                               .arg(m_Targets.allTextPaths.size()),
                           static_cast<int>(TargetScope::AllText));
    m_TargetScope->setCurrentIndex(FindData(m_TargetScope, preferredScope));
}

void RegexWorkbenchDialog::OpenRecipe()
{
    const QString start = m_LastRecipePath.isEmpty()
                              ? RegexRecipeStore::DefaultDirectory()
                              : m_LastRecipePath;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Regex Workbench Recipe"), start,
        tr("Regex Workbench recipes (*.json);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    RegexRecipe recipe;
    QString error;
    if (!RegexRecipeStore::LoadFile(path, recipe, &error)) {
        QMessageBox::warning(this, tr("Open Regex Workbench Recipe"), error);
        return;
    }
    LoadRecipeIntoUi(recipe, path);
    SetStatus(tr("Loaded recipe: %1").arg(path));
}

void RegexWorkbenchDialog::SaveRecipe()
{
    RegexRecipe recipe;
    QString error;
    if (!RecipeFromUi(recipe, &error)) {
        QMessageBox::warning(this, tr("Save Regex Workbench Recipe"), error);
        return;
    }
    QString path = m_RecipePath;
    if (path.isEmpty()) {
        path = m_LastRecipePath.isEmpty()
                   ? RegexRecipeStore::DefaultDirectory() + QLatin1Char('/') +
                         QStringLiteral("recipe.json")
                   : m_LastRecipePath;
    }
    path = QFileDialog::getSaveFileName(
        this, tr("Save Regex Workbench Recipe"), path,
        tr("Regex Workbench recipes (*.json);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    if (!path.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".json");
    }
    if (!RegexRecipeStore::SaveFile(path, recipe, &error)) {
        QMessageBox::warning(this, tr("Save Regex Workbench Recipe"), error);
        return;
    }
    m_Recipe = recipe;
    m_RecipePath = path;
    m_LastRecipePath = path;
    SetStatus(tr("Saved recipe: %1").arg(path));
}

void RegexWorkbenchDialog::ImportSearchTemplate()
{
    SearchEditorModelPlus* model = SearchEditorModelPlus::instance();
    const QList<QStandardItem*> items = model->GetNonGroupItems(
        model->invisibleRootItem());
    QStringList names;
    for (QStandardItem* item : items) {
        names.append(model->GetFullName(item));
    }
    names.removeDuplicates();
    names.sort();
    if (names.isEmpty()) {
        QMessageBox::information(this, tr("Import Search Template"),
                                 tr("No saved search templates are available."));
        return;
    }
    bool accepted = false;
    const QString name = QInputDialog::getItem(
        this, tr("Import Search Template"), tr("Saved search:"), names, 0,
        false, &accepted);
    if (!accepted || name.isEmpty()) {
        return;
    }
    std::unique_ptr<SearchEditorModelPlus::searchEntry> entry(
        model->GetEntryFromName(name));
    if (!entry) {
        QMessageBox::warning(this, tr("Import Search Template"),
                             tr("The selected saved search no longer exists."));
        return;
    }
    const RegexRecipeImportResult imported =
        RegexRecipeSearchEditorAdapter::Import(*entry);
    if (!imported.success) {
        QMessageBox::warning(this, tr("Import Search Template"),
                             imported.errorMessage);
        return;
    }
    SaveCurrentRule();
    m_Recipe.rules.append(imported.rule);
    RefreshRuleList(m_Recipe.rules.size() - 1);
    if (!imported.warnings.isEmpty()) {
        QMessageBox::information(this, tr("Import Search Template"),
                                 imported.warnings.join(QLatin1Char('\n')));
    }
    SetStatus(tr("Imported saved search: %1").arg(name));
}

void RegexWorkbenchDialog::AddRule()
{
    SaveCurrentRule();
    m_Recipe.rules.append(NewRule(m_Recipe.rules.size() + 1,
                                  m_MaxIterations->value()));
    RefreshRuleList(m_Recipe.rules.size() - 1);
}

void RegexWorkbenchDialog::DuplicateRule()
{
    SaveCurrentRule();
    if (m_CurrentRuleRow < 0 || m_CurrentRuleRow >= m_Recipe.rules.size()) {
        return;
    }
    RegexWorkbenchRule copy = m_Recipe.rules.at(m_CurrentRuleRow);
    copy.id = NewRuleId();
    copy.name = tr("%1 copy").arg(copy.name);
    m_Recipe.rules.insert(m_CurrentRuleRow + 1, copy);
    RefreshRuleList(m_CurrentRuleRow + 1);
}

void RegexWorkbenchDialog::RemoveRule()
{
    if (m_Recipe.rules.size() <= 1 || m_CurrentRuleRow < 0 ||
        m_CurrentRuleRow >= m_Recipe.rules.size()) {
        return;
    }
    const int next = std::min(m_CurrentRuleRow,
                              static_cast<int>(m_Recipe.rules.size()) - 2);
    m_Recipe.rules.removeAt(m_CurrentRuleRow);
    RefreshRuleList(next);
}

void RegexWorkbenchDialog::MoveRuleUp()
{
    SaveCurrentRule();
    if (m_CurrentRuleRow <= 0 || m_CurrentRuleRow >= m_Recipe.rules.size()) {
        return;
    }
    m_Recipe.rules.swapItemsAt(m_CurrentRuleRow, m_CurrentRuleRow - 1);
    RefreshRuleList(m_CurrentRuleRow - 1);
}

void RegexWorkbenchDialog::MoveRuleDown()
{
    SaveCurrentRule();
    if (m_CurrentRuleRow < 0 || m_CurrentRuleRow + 1 >= m_Recipe.rules.size()) {
        return;
    }
    m_Recipe.rules.swapItemsAt(m_CurrentRuleRow, m_CurrentRuleRow + 1);
    RefreshRuleList(m_CurrentRuleRow + 1);
}

void RegexWorkbenchDialog::RuleSelectionChanged(int row)
{
    if (row == m_CurrentRuleRow) {
        return;
    }
    SaveCurrentRule();
    m_CurrentRuleRow = row;
    LoadCurrentRule();
}

void RegexWorkbenchDialog::RuleEnabledChanged()
{
    if (m_CurrentRuleRow >= 0 && m_CurrentRuleRow < m_RuleList->count()) {
        m_RuleList->item(m_CurrentRuleRow)->setCheckState(
            m_RuleEnabled->isChecked() ? Qt::Checked : Qt::Unchecked);
    }
}

void RegexWorkbenchDialog::UpdateRuleControlState()
{
    const SecondaryMode mode = static_cast<SecondaryMode>(
        m_SecondaryMode->currentData().toInt());
    m_SecondaryPattern->setEnabled(mode != SecondaryMode::None);
    const bool captureOnly = m_CaptureOnly->isChecked();
    m_ReplacePattern->setEnabled(!captureOnly && !m_Busy);
    m_Recursive->setEnabled(!captureOnly && !m_Busy);
    m_VariableExpansion->setEnabled(!captureOnly && !m_Busy);
    if (captureOnly) {
        m_Recursive->setChecked(false);
        m_VariableExpansion->setChecked(false);
    }
    const bool recursive = !captureOnly && m_Recursive->isChecked();
    m_MaxIterations->setEnabled(recursive);
    m_AllowEmpty->setEnabled(recursive);
    if (!recursive) {
        m_AllowEmpty->setChecked(false);
    }
}

void RegexWorkbenchDialog::LoadRecipeIntoUi(const RegexRecipe& recipe,
                                            const QString& path)
{
    m_Recipe = recipe;
    m_RecipePath = path;
    if (!path.isEmpty()) {
        m_LastRecipePath = path;
    }
    m_RecipeName->setText(recipe.name);
    m_VariableScope->setCurrentIndex(FindData(
        m_VariableScope, static_cast<int>(recipe.variableScope)));
    m_WritePolicy->setCurrentIndex(FindData(
        m_WritePolicy, static_cast<int>(recipe.writePolicy)));
    m_ReportTable->setRowCount(0);
    m_VariableTable->setRowCount(0);
    RefreshRuleList(recipe.rules.isEmpty() ? -1 : 0);
}

void RegexWorkbenchDialog::RefreshRuleList(int selectedRow)
{
    const QSignalBlocker blocker(m_RuleList);
    m_RuleList->clear();
    for (const RegexWorkbenchRule& rule : std::as_const(m_Recipe.rules)) {
        auto* item = new QListWidgetItem(rule.name, m_RuleList);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(rule.enabled ? Qt::Checked : Qt::Unchecked);
    }
    m_CurrentRuleRow = -1;
    if (selectedRow >= 0 && selectedRow < m_RuleList->count()) {
        m_RuleList->setCurrentRow(selectedRow);
        m_CurrentRuleRow = selectedRow;
    }
    LoadCurrentRule();
}

void RegexWorkbenchDialog::SaveCurrentRule()
{
    if (m_CurrentRuleRow < 0 || m_CurrentRuleRow >= m_Recipe.rules.size()) {
        return;
    }
    RegexWorkbenchRule& rule = m_Recipe.rules[m_CurrentRuleRow];
    rule.name = m_RuleName->text().trimmed();
    rule.secondaryMode = static_cast<SecondaryMode>(
        m_SecondaryMode->currentData().toInt());
    rule.secondaryPattern = rule.secondaryMode == SecondaryMode::None
                                ? QString()
                                : m_SecondaryPattern->toPlainText();
    rule.find = m_FindPattern->toPlainText();
    rule.replace = m_ReplacePattern->toPlainText();
    rule.recursive = m_Recursive->isChecked();
    rule.maxIterations = m_MaxIterations->value();
    rule.allowEmpty = m_AllowEmpty->isChecked();
    rule.captureOnly = m_CaptureOnly->isChecked();
    rule.variableExpansionEnabled = !rule.captureOnly &&
                                    m_VariableExpansion->isChecked();
    rule.autoIngestNamedCaptures = m_AutoIngest->isChecked();
    rule.captureToVar = CaptureNames(m_CaptureNames->text());
    rule.enabled = m_RuleEnabled->isChecked();
    if (QListWidgetItem* item = m_RuleList->item(m_CurrentRuleRow)) {
        item->setText(rule.name);
        item->setCheckState(rule.enabled ? Qt::Checked : Qt::Unchecked);
    }
}

void RegexWorkbenchDialog::LoadCurrentRule()
{
    const bool available = m_CurrentRuleRow >= 0 &&
                           m_CurrentRuleRow < m_Recipe.rules.size();
    m_RuleEnabled->setEnabled(available);
    m_RuleName->setEnabled(available);
    m_SecondaryMode->setEnabled(available);
    m_FindPattern->setEnabled(available);
    m_ReplacePattern->setEnabled(available);
    m_Recursive->setEnabled(available);
    m_AllowEmpty->setEnabled(available);
    m_CaptureOnly->setEnabled(available);
    m_VariableExpansion->setEnabled(available);
    m_AutoIngest->setEnabled(available);
    m_CaptureNames->setEnabled(available);
    m_DuplicateRuleButton->setEnabled(available);
    m_RemoveRuleButton->setEnabled(available && m_Recipe.rules.size() > 1);
    m_UpRuleButton->setEnabled(available && m_CurrentRuleRow > 0);
    m_DownRuleButton->setEnabled(
        available && m_CurrentRuleRow + 1 < m_Recipe.rules.size());
    if (!available) {
        m_RuleName->clear();
        m_SecondaryPattern->clear();
        m_FindPattern->clear();
        m_ReplacePattern->clear();
        m_CaptureNames->clear();
        return;
    }
    const RegexWorkbenchRule& rule = m_Recipe.rules.at(m_CurrentRuleRow);
    const QSignalBlocker enabledBlocker(m_RuleEnabled);
    const QSignalBlocker captureOnlyBlocker(m_CaptureOnly);
    const QSignalBlocker recursiveBlocker(m_Recursive);
    m_RuleEnabled->setChecked(rule.enabled);
    m_RuleName->setText(rule.name);
    m_SecondaryMode->setCurrentIndex(FindData(
        m_SecondaryMode, static_cast<int>(rule.secondaryMode)));
    m_SecondaryPattern->setPlainText(rule.secondaryPattern);
    m_FindPattern->setPlainText(rule.find);
    m_ReplacePattern->setPlainText(rule.replace);
    m_CaptureOnly->setChecked(rule.captureOnly);
    m_Recursive->setChecked(rule.recursive);
    m_MaxIterations->setValue(rule.maxIterations);
    m_AllowEmpty->setChecked(rule.allowEmpty);
    m_VariableExpansion->setChecked(rule.variableExpansionEnabled);
    m_AutoIngest->setChecked(rule.autoIngestNamedCaptures);
    m_CaptureNames->setText(rule.captureToVar.join(QStringLiteral(", ")));
    UpdateRuleControlState();
}

bool RegexWorkbenchDialog::RecipeFromUi(RegexRecipe& recipe, QString* error)
{
    SaveCurrentRule();
    m_Recipe.name = m_RecipeName->text().trimmed();
    m_Recipe.variableScope = static_cast<VariableScope>(
        m_VariableScope->currentData().toInt());
    m_Recipe.writePolicy = static_cast<WritePolicy>(
        m_WritePolicy->currentData().toInt());
    if (!RegexRecipeStore::Validate(m_Recipe, error)) {
        return false;
    }
    recipe = m_Recipe;
    return true;
}

QStringList RegexWorkbenchDialog::SelectedTargetPaths() const
{
    const TargetScope scope = static_cast<TargetScope>(
        m_TargetScope->currentData().toInt());
    switch (scope) {
        case TargetScope::CurrentFile:
            return m_Targets.currentPath.isEmpty()
                       ? QStringList() : QStringList{m_Targets.currentPath};
        case TargetScope::SelectedFiles:
            return m_Targets.selectedPaths;
        case TargetScope::AllHtml:
            return m_Targets.htmlPaths;
        case TargetScope::AllCss:
            return m_Targets.cssPaths;
        case TargetScope::AllSpecial:
            return m_Targets.specialPaths;
        case TargetScope::AllText:
            return m_Targets.allTextPaths;
    }
    return QStringList();
}

void RegexWorkbenchDialog::StartDryRun()
{
    StartRun(RunMode::DryRun);
}

void RegexWorkbenchDialog::StartApply()
{
    StartRun(RunMode::Apply);
}

void RegexWorkbenchDialog::StartRun(RunMode mode)
{
    if (m_Busy) {
        return;
    }
    if (!HasCurrentBook()) {
        QMessageBox::warning(
            this, tr("Advanced Regex Workbench"),
            tr("This workbench belongs to a book that is no longer open. "
               "Close it and reopen the workbench for the current book."));
        return;
    }
    RefreshTargets();
    RegexRecipe recipe;
    QString error;
    if (!RecipeFromUi(recipe, &error)) {
        QMessageBox::warning(this, tr("Advanced Regex Workbench"), error);
        return;
    }
    const QStringList paths = SelectedTargetPaths();
    if (paths.isEmpty()) {
        QMessageBox::warning(this, tr("Advanced Regex Workbench"),
                             tr("The selected scope contains no text resources."));
        return;
    }
    if (mode == RunMode::Apply &&
        QMessageBox::question(
            this, tr("Advanced Regex Workbench"),
            tr("Apply this recipe to %1 text resource(s)? A fresh snapshot will be "
               "created; if text changes are produced, a recovery checkpoint will be "
               "created before they are written.")
                .arg(paths.size()),
            QMessageBox::Apply | QMessageBox::Cancel,
            QMessageBox::Cancel) != QMessageBox::Apply) {
        return;
    }
    const QHash<QString, TextResource*> resources = LiveResources();
    if (!SearchBatchCoordinator::CaptureSnapshot(
            m_MainWindow, paths, resources, m_RunSnapshot, &error)) {
        QMessageBox::warning(this, tr("Advanced Regex Workbench"), error);
        return;
    }

    const SearchVariableStore initialStore = m_Store;
    const SearchBatchCoordinator::Snapshot snapshot = m_RunSnapshot;
    m_CancelFlag = std::make_shared<std::atomic_bool>(false);
    const std::shared_ptr<std::atomic_bool> cancel = m_CancelFlag;
    const QPointer<RegexWorkbenchDialog> dialog(this);
    m_RunMode = mode;
    m_LastResult = RegexWorkbenchBatchResult();
    m_ReportTable->setRowCount(0);
    SetBusy(true);
    SetStatus(mode == RunMode::Apply
                  ? tr("Staging a fresh Apply run in memory...")
                  : tr("Running Dry-Run in memory..."));
    m_Watcher->setFuture(QtConcurrent::run(
        [recipe, paths, snapshot, initialStore, cancel, dialog]() mutable {
            RegexWorkbenchBatchOptions options;
            options.isCancelled = [cancel]() {
                return cancel->load(std::memory_order_relaxed);
            };
            options.progressCallback = [dialog](int completed, int total) {
                if (!dialog) {
                    return;
                }
                QMetaObject::invokeMethod(
                    dialog.data(),
                    [dialog, completed, total]() {
                        if (!dialog || !dialog->m_Busy) {
                            return;
                        }
                        dialog->m_Progress->setRange(0, std::max(1, total));
                        dialog->m_Progress->setValue(completed);
                    },
                    Qt::QueuedConnection);
            };
            return RegexWorkbenchBatchRunner::Run(
                recipe, paths, snapshot.originalTexts, snapshot.mediaTypes,
                initialStore, options);
        }));
}

void RegexWorkbenchDialog::CancelRun()
{
    if (m_Busy && m_CancelFlag) {
        m_CancelFlag->store(true, std::memory_order_relaxed);
        m_CancelButton->setEnabled(false);
        SetStatus(tr("Cancelling after the current bounded regex operation..."));
    }
}

void RegexWorkbenchDialog::RunFinished()
{
    m_LastResult = m_Watcher->result();
    const RunMode completedMode = m_RunMode;
    m_RunMode = RunMode::None;
    SetBusy(false);

    if (m_CloseWhenIdle || !HasCurrentBook()) {
        m_RunSnapshot = SearchBatchCoordinator::Snapshot();
        if (m_CloseWhenIdle) {
            close();
        } else {
            SetStatus(tr("The book changed while the recipe was running; the "
                         "staged result was discarded."), true);
        }
        return;
    }

    if (!m_LastResult.staged.success) {
        if (m_LastResult.staged.cancelled) {
            SetStatus(tr("Run cancelled. No book text or variables were changed."));
        } else {
            SetStatus(m_LastResult.report.fatalMessage.isEmpty()
                          ? m_LastResult.staged.error
                          : m_LastResult.report.fatalMessage,
                      true);
            QMessageBox::warning(this, tr("Advanced Regex Workbench"),
                                 m_Status->text());
        }
        return;
    }

    if (completedMode == RunMode::Apply) {
        SetStatus(m_LastResult.report.changedResourceCount > 0
                      ? tr("Creating the recovery checkpoint and committing staged changes...")
                      : tr("Publishing captured variables without changing document text..."));
        const SearchBatch::Result commit = RegexWorkbenchBatchCommitter::Commit(
            m_MainWindow, LiveResources(), m_RunSnapshot, m_LastResult,
            m_Store);
        if (!commit.success) {
            SetStatus(commit.error, true);
            QMessageBox::warning(this, tr("Advanced Regex Workbench"), commit.error);
            return;
        }
        PopulateReport(m_LastResult, true);
        PopulateVariables(m_Store.snapshot());
        if (m_LastResult.report.changedResourceCount == 0) {
            SetStatus(tr("Apply complete: %1 match(es), %2 replacement(s); no "
                         "document text was changed.")
                          .arg(m_LastResult.report.totalMatches)
                          .arg(m_LastResult.report.totalReplacements));
        } else {
            SetStatus(tr("Applied %1 replacement(s) from %2 match(es) to %3 resource(s). "
                         "Each file can be undone separately; use the recovery checkpoint "
                         "to restore the entire batch.")
                          .arg(m_LastResult.report.totalReplacements)
                          .arg(m_LastResult.report.totalMatches)
                          .arg(m_LastResult.report.changedResourceCount));
        }
        return;
    }

    PopulateReport(m_LastResult, false);
    PopulateVariables(m_LastResult.finalStore);
    SetStatus(tr("Dry-Run complete: %1 match(es), %2 replacement(s), %3 changed resource(s). "
                 "The book and session variables were not modified.")
                  .arg(m_LastResult.report.totalMatches)
                  .arg(m_LastResult.report.totalReplacements)
                  .arg(m_LastResult.report.changedResourceCount));
}

void RegexWorkbenchDialog::SetBusy(bool busy)
{
    m_Busy = busy;
    m_NewButton->setEnabled(!busy);
    m_OpenButton->setEnabled(!busy);
    m_SaveButton->setEnabled(!busy);
    m_ImportButton->setEnabled(!busy);
    m_RecipeName->setEnabled(!busy);
    m_RuleList->setEnabled(!busy);
    m_AddRuleButton->setEnabled(!busy);
    m_DuplicateRuleButton->setEnabled(!busy && m_CurrentRuleRow >= 0);
    m_RemoveRuleButton->setEnabled(!busy && m_Recipe.rules.size() > 1);
    m_UpRuleButton->setEnabled(!busy && m_CurrentRuleRow > 0);
    m_DownRuleButton->setEnabled(
        !busy && m_CurrentRuleRow >= 0 &&
        m_CurrentRuleRow + 1 < m_Recipe.rules.size());
    m_RuleEnabled->setEnabled(!busy);
    m_RuleName->setEnabled(!busy);
    m_SecondaryMode->setEnabled(!busy);
    m_SecondaryPattern->setEnabled(
        !busy && static_cast<SecondaryMode>(m_SecondaryMode->currentData().toInt()) !=
                     SecondaryMode::None);
    m_FindPattern->setEnabled(!busy);
    m_CaptureOnly->setEnabled(!busy);
    const bool captureOnly = m_CaptureOnly->isChecked();
    m_ReplacePattern->setEnabled(!busy && !captureOnly);
    m_Recursive->setEnabled(!busy && !captureOnly);
    m_MaxIterations->setEnabled(!busy && !captureOnly && m_Recursive->isChecked());
    m_AllowEmpty->setEnabled(!busy && !captureOnly && m_Recursive->isChecked());
    m_VariableExpansion->setEnabled(!busy && !captureOnly);
    m_AutoIngest->setEnabled(!busy);
    m_CaptureNames->setEnabled(!busy);
    m_TargetScope->setEnabled(!busy);
    m_VariableScope->setEnabled(!busy);
    m_WritePolicy->setEnabled(!busy);
    m_DryRunButton->setEnabled(!busy);
    m_ApplyButton->setEnabled(!busy);
    m_ClearVariablesButton->setEnabled(!busy);
    m_CancelButton->setEnabled(busy);
    m_Progress->setVisible(busy);
    m_Progress->setRange(0, busy ? 0 : 1);
    if (QPushButton* close = m_ButtonBox->button(QDialogButtonBox::Close)) {
        close->setEnabled(!busy);
    }
}

void RegexWorkbenchDialog::PopulateReport(const RegexWorkbenchBatchResult& result,
                                          bool applied)
{
    m_ReportApplied = applied;
    m_ReportTable->setRowCount(result.report.rows.size());
    for (int row = 0; row < result.report.rows.size(); ++row) {
        const RegexWorkbenchReportRow& report = result.report.rows.at(row);
        auto* rule = new QTableWidgetItem(report.ruleName);
        const bool exact = applied
                               ? report.exactNavigationAvailable &&
                                     report.coordinateSpace == CoordinateSpace::Final
                               : report.exactSnapshotNavigationAvailable;
        const int navigationLine = applied ? report.lineHint
                                           : report.snapshotLineHint;
        const int navigationStart = applied ? report.matchStart
                                            : report.snapshotMatchStart;
        const int navigationEnd = applied ? report.matchEnd
                                          : report.snapshotMatchEnd;
        rule->setData(Qt::UserRole, report.bookpath);
        rule->setData(Qt::UserRole + 1, navigationLine);
        rule->setData(Qt::UserRole + 2, navigationStart);
        rule->setData(Qt::UserRole + 3, exact);
        rule->setData(Qt::UserRole + 4, exact ? navigationEnd : -1);
        m_ReportTable->setItem(row, 0, rule);
        m_ReportTable->setItem(row, 1, new QTableWidgetItem(report.bookpath));
        m_ReportTable->setItem(
            row, 2, new QTableWidgetItem(navigationLine > 0
                                             ? QString::number(navigationLine)
                                             : QStringLiteral("-")));
        m_ReportTable->setItem(row, 3, new QTableWidgetItem(report.beforeSnippet));
        m_ReportTable->setItem(row, 4, new QTableWidgetItem(report.afterSnippet));
        m_ReportTable->setItem(row, 5, new QTableWidgetItem(
            QString::number(report.iterationNumber)));
        m_ReportTable->setItem(row, 6, new QTableWidgetItem(
            report.variableNames.join(QStringLiteral(", "))));
        if (exact) {
            rule->setToolTip(
                tr("Double-click to open the resource and highlight this match."));
        } else if (navigationLine > 0) {
            rule->setToolTip(
                tr("The exact range no longer maps to the document; double-click "
                   "to open the nearest corresponding line."));
        } else {
            rule->setToolTip(
                tr("Double-click to open the resource; no exact position is available."));
        }
    }
    if (result.report.rowsTruncated) {
        m_ReportTable->setToolTip(
            tr("The report omitted %1 additional row(s); totals remain exact.")
                .arg(result.report.omittedRowCount));
    } else {
        m_ReportTable->setToolTip(QString());
    }
}

void RegexWorkbenchDialog::PopulateVariables(
    const SearchVariableStore::Snapshot& snapshot)
{
    m_VariableTable->setRowCount(0);
    AddVariableRows(m_VariableTable, tr("Batch"), QString(), snapshot.batchFrame);
    AddVariableRows(m_VariableTable, tr("Session"), QString(), snapshot.sessionFrame);
    QStringList resources = snapshot.resourceFrames.keys();
    resources.sort();
    for (const QString& resource : resources) {
        AddVariableRows(m_VariableTable, tr("Resource"), resource,
                        snapshot.resourceFrames.value(resource));
    }
}

void RegexWorkbenchDialog::OpenResultRow(int row, int)
{
    QTableWidgetItem* item = m_ReportTable->item(row, 0);
    if (!item) {
        return;
    }
    const QString bookpath = item->data(Qt::UserRole).toString();
    TextResource* resource = m_ResourceGuards.value(bookpath).data();
    if (!HasCurrentBook() || !resource ||
        resource->GetRelativePath() != bookpath) {
        SetStatus(tr("This result refers to a resource that is no longer available."),
                  true);
        return;
    }
    bool exact = item->data(Qt::UserRole + 3).toBool();
    if (exact) {
        QString expected = m_RunSnapshot.originalTexts.value(bookpath);
        if (m_ReportApplied && m_LastResult.staged.changedTexts.contains(bookpath)) {
            expected = m_LastResult.staged.changedTexts.value(bookpath);
        }
        QReadLocker locker(&resource->GetLock());
        if (resource->GetText() != expected) {
            exact = false;
        }
    }
    if (!exact && item->data(Qt::UserRole + 3).toBool()) {
        SetStatus(tr("The resource changed after this report was created; opening "
                     "the nearest recorded line without highlighting."));
    }
    emit OpenFileRequest(bookpath,
                         item->data(Qt::UserRole + 1).toInt(),
                         item->data(Qt::UserRole + 2).toInt(),
                         exact ? item->data(Qt::UserRole + 4).toInt() : -1);
}

void RegexWorkbenchDialog::ClearVariables()
{
    m_Store.clear();
    PopulateVariables(m_Store.snapshot());
    SetStatus(tr("Session variables cleared."));
}

void RegexWorkbenchDialog::SetStatus(const QString& message, bool error)
{
    m_Status->setText(message);
    m_Status->setStyleSheet(error ? QStringLiteral("color: #b00020;") : QString());
}
