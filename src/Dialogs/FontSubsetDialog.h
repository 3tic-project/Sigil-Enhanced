#pragma once

#include <QDialog>
#include <QSet>

#include "BookManipulation/FontSubset/FontSubsetController.h"

class QCheckBox;
class QCloseEvent;
class QDialogButtonBox;
class QLabel;
class QPushButton;
class QTableWidget;

template<typename T> class QFutureWatcher;

class FontSubsetDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FontSubsetDialog(const FontSubset::BookSnapshot& snapshot,
                              const QSet<QString>& initiallySelected,
                              QWidget* parent = nullptr);
    ~FontSubsetDialog() override;

    const FontSubset::BatchResult& Analysis() const;
    QSet<QString> SelectedFontIdentifiers() const;

protected:
    void closeEvent(QCloseEvent* event) override;

public slots:
    void reject() override;

private slots:
    void StartAnalysis();
    void AnalysisFinished();
    void UpdateApplyState();
    void SaveReport();
    void AcceptSelection();

private:
    QByteArray CreateReportJson() const;
    void PopulateResults();
    void SetBusy(bool busy);

    FontSubset::BookSnapshot m_Snapshot;
    FontSubset::BatchResult m_Analysis;
    QSet<QString> m_InitiallySelected;
    QFutureWatcher<FontSubset::BatchResult>* m_Watcher;
    QCheckBox* m_DropHinting;
    QTableWidget* m_Table;
    QLabel* m_Status;
    QPushButton* m_AnalyzeButton;
    QPushButton* m_SaveReportButton;
    QPushButton* m_ApplyButton;
    QDialogButtonBox* m_ButtonBox;
    bool m_Busy;
    bool m_ResultsMatchOptions;
    bool m_AnalyzedDropHinting;
};
