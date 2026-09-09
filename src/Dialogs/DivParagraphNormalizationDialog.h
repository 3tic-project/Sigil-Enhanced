/************************************************************************
**
**  Copyright (C) 2026 Sigil Enhanced contributors
**
*************************************************************************/

#pragma once
#ifndef DIVPARAGRAPHNORMALIZATIONDIALOG_H
#define DIVPARAGRAPHNORMALIZATIONDIALOG_H

#include <QDialog>

#include "BuiltinPlugins/BookLiveParagraphNormalizer.h"

class QCheckBox;
class QRadioButton;

class DivParagraphNormalizationDialog final : public QDialog
{
    Q_OBJECT

public:
    enum class Scope {
        CurrentFile,
        SelectedFiles,
        WholeBook
    };

    DivParagraphNormalizationDialog(
        Scope initialScope,
        bool currentFileAvailable,
        int selectedFileCount,
        const BuiltinPlugins::BookLiveParagraphNormalizer::Options& savedOptions,
        bool savedFormatSource,
        QWidget* parent = nullptr);

    Scope SelectedScope() const;
    BuiltinPlugins::BookLiveParagraphNormalizer::Options NormalizerOptions() const;
    bool FormatSource() const;

private:
    QRadioButton* m_CurrentFile = nullptr;
    QRadioButton* m_SelectedFiles = nullptr;
    QRadioButton* m_WholeBook = nullptr;
    QCheckBox* m_BlankLines = nullptr;
    QCheckBox* m_SceneBreaks = nullptr;
    QCheckBox* m_ImageWrappers = nullptr;
    QCheckBox* m_SingleBlockWrappers = nullptr;
    QCheckBox* m_FormatSource = nullptr;
};

#endif
