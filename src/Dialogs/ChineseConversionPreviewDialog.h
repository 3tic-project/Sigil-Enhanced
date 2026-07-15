/************************************************************************
**
**  Copyright (C) 2026 Sigil Enhanced contributors
**
*************************************************************************/

#pragma once

#include <QDialog>
#include <QList>
#include <QSet>

#include "ChineseConversion/ChineseTextConversionPlan.h"

class QLabel;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;

struct ChineseConversionPreviewResource {
    QString resourcePath;
    QList<ChineseTextChange> changes;
};

class ChineseConversionPreviewDialog final : public QDialog
{
    Q_OBJECT

public:
    ChineseConversionPreviewDialog(const QString& resourcePath,
                                   const QList<ChineseTextChange>& changes,
                                   int skippedJapaneseSegments,
                                   int skippedProtectedSegments,
                                   QWidget *parent = nullptr);
    ChineseConversionPreviewDialog(const QList<ChineseConversionPreviewResource>& resources,
                                   int skippedJapaneseSegments,
                                   int skippedProtectedSegments,
                                   QWidget *parent = nullptr);

    QSet<int> EnabledChanges() const;

private slots:
    void UpdateSelectionState(QTableWidgetItem *item = nullptr);

private:
    QLabel *m_Summary = nullptr;
    QTableWidget *m_Table = nullptr;
    QPushButton *m_Apply = nullptr;
    int m_SkippedJapaneseSegments = 0;
    int m_SkippedProtectedSegments = 0;
};
