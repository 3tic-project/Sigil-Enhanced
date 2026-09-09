/************************************************************************
**
**  Copyright (C) 2026 Sigil Enhanced contributors
**
*************************************************************************/

#pragma once
#ifndef DIVPARAGRAPHNORMALIZATIONPREVIEWDIALOG_H
#define DIVPARAGRAPHNORMALIZATIONPREVIEWDIALOG_H

#include <QDialog>
#include <QSet>

#include "BuiltinPlugins/DivParagraphNormalizationPlan.h"

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;
class QTextBrowser;

class DivParagraphNormalizationPreviewDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit DivParagraphNormalizationPreviewDialog(
        const BuiltinPlugins::DivParagraphNormalizationPlan::Result& plan,
        bool analysisOnly = false,
        QWidget* parent = nullptr);

    QSet<QString> SelectedResourceIds() const;

private slots:
    void UpdateSelectionState(QTableWidgetItem* item = nullptr);
    void ShowEntry(int currentRow, int currentColumn, int previousRow, int previousColumn);

private:
    const BuiltinPlugins::DivParagraphNormalizationPlan::Result m_Plan;
    QLabel* m_Summary = nullptr;
    QTableWidget* m_Table = nullptr;
    QPlainTextEdit* m_BeforeSource = nullptr;
    QPlainTextEdit* m_AfterSource = nullptr;
    QTextBrowser* m_BeforePreview = nullptr;
    QTextBrowser* m_AfterPreview = nullptr;
    QPushButton* m_Apply = nullptr;
    bool m_AnalysisOnly = false;
};

#endif
