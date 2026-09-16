/************************************************************************
**
**  Copyright (C) 2026 Sigil Enhanced contributors
**
*************************************************************************/

#include "Dialogs/DivParagraphNormalizationDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

DivParagraphNormalizationDialog::DivParagraphNormalizationDialog(
    Scope initialScope,
    bool currentFileAvailable,
    int selectedFileCount,
    const BuiltinPlugins::BookLiveParagraphNormalizer::Options& savedOptions,
    bool savedFormatSource,
    QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Normalize Cmoa DIV Paragraphs"));
    setMinimumWidth(560);

    auto* root = new QVBoxLayout(this);
    auto* explanation = new QLabel(
        tr("Analyze Cmoa/EBPAJ XHTML first, then preview a revision-bound plan. Only "
           "DIV leaves covered by the verified Cmoa CSS profile are converted automatically."),
        this);
    explanation->setWordWrap(true);
    root->addWidget(explanation);

    auto* scopeGroup = new QGroupBox(tr("Scope"), this);
    auto* scopeLayout = new QVBoxLayout(scopeGroup);
    m_CurrentFile = new QRadioButton(tr("Current XHTML file"), scopeGroup);
    m_SelectedFiles = new QRadioButton(
        tr("Selected XHTML files (%1)").arg(selectedFileCount), scopeGroup);
    m_WholeBook = new QRadioButton(tr("All XHTML files in the book"), scopeGroup);
    m_CurrentFile->setObjectName(QStringLiteral("divParagraphCurrentFile"));
    m_SelectedFiles->setObjectName(QStringLiteral("divParagraphSelectedFiles"));
    m_WholeBook->setObjectName(QStringLiteral("divParagraphWholeBook"));
    m_CurrentFile->setEnabled(currentFileAvailable);
    m_SelectedFiles->setEnabled(selectedFileCount > 0);
    scopeLayout->addWidget(m_CurrentFile);
    scopeLayout->addWidget(m_SelectedFiles);
    scopeLayout->addWidget(m_WholeBook);
    root->addWidget(scopeGroup);

    if (initialScope == Scope::CurrentFile && currentFileAvailable) {
        m_CurrentFile->setChecked(true);
    } else if (initialScope != Scope::WholeBook && selectedFileCount > 0) {
        m_SelectedFiles->setChecked(true);
    } else {
        m_WholeBook->setChecked(true);
    }

    auto* categoryGroup = new QGroupBox(tr("Conversion categories"), this);
    auto* categoryLayout = new QVBoxLayout(categoryGroup);
    auto* bodyParagraphs = new QCheckBox(tr("Body-text paragraph DIVs"), categoryGroup);
    bodyParagraphs->setChecked(true);
    bodyParagraphs->setEnabled(false);
    bodyParagraphs->setToolTip(tr("The conservative content-model check is always enabled."));
    m_BlankLines = new QCheckBox(tr("Blank-line DIVs containing only BR"), categoryGroup);
    m_SceneBreaks = new QCheckBox(tr("Scene-separator DIVs"), categoryGroup);
    m_ImageWrappers = new QCheckBox(tr("Image-only DIV wrappers"), categoryGroup);
    m_SingleBlockWrappers = new QCheckBox(
        tr("Single nested visual blocks (DIV becomes P; inner DIV becomes SPAN)"),
        categoryGroup);
    m_BlankLines->setObjectName(QStringLiteral("divParagraphBlankLines"));
    m_SceneBreaks->setObjectName(QStringLiteral("divParagraphSceneBreaks"));
    m_ImageWrappers->setObjectName(QStringLiteral("divParagraphImageWrappers"));
    m_SingleBlockWrappers->setObjectName(QStringLiteral("divParagraphSingleBlockWrappers"));
    m_BlankLines->setChecked(savedOptions.convertSpacerBr);
    m_SceneBreaks->setChecked(savedOptions.convertSceneBreaks);
    m_ImageWrappers->setChecked(savedOptions.convertImageWrappers);
    m_SingleBlockWrappers->setChecked(savedOptions.convertSingleBlockWrappers);
    categoryLayout->addWidget(bodyParagraphs);
    categoryLayout->addWidget(m_BlankLines);
    categoryLayout->addWidget(m_SceneBreaks);
    categoryLayout->addWidget(m_ImageWrappers);
    categoryLayout->addWidget(m_SingleBlockWrappers);
    root->addWidget(categoryGroup);

    m_FormatSource = new QCheckBox(tr("Format XHTML source after conversion"), this);
    m_FormatSource->setObjectName(QStringLiteral("divParagraphFormatSource"));
    m_FormatSource->setChecked(savedFormatSource);
    m_FormatSource->setToolTip(
        tr("Off by default. When off, only selected start/end tag names are patched."));
    root->addWidget(m_FormatSource);

    auto* safety = new QLabel(
        tr("Tag-dependent CSS, scripts, fixed-layout indicators, lists, tables, SVG, "
           "MathML, and unknown mixed blocks remain review-only. No force-all mode is provided."),
        this);
    safety->setWordWrap(true);
    root->addWidget(safety);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Cancel | QDialogButtonBox::Ok, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Analyze and Preview"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
}

DivParagraphNormalizationDialog::Scope
DivParagraphNormalizationDialog::SelectedScope() const
{
    if (m_CurrentFile->isChecked()) {
        return Scope::CurrentFile;
    }
    if (m_SelectedFiles->isChecked()) {
        return Scope::SelectedFiles;
    }
    return Scope::WholeBook;
}

BuiltinPlugins::BookLiveParagraphNormalizer::Options
DivParagraphNormalizationDialog::NormalizerOptions() const
{
    BuiltinPlugins::BookLiveParagraphNormalizer::Options options =
        BuiltinPlugins::BookLiveParagraphNormalizer::Options::conservative();
    options.convertSpacerBr = m_BlankLines->isChecked();
    options.convertSceneBreaks = m_SceneBreaks->isChecked();
    options.convertImageWrappers = m_ImageWrappers->isChecked();
    options.convertSingleBlockWrappers = m_SingleBlockWrappers->isChecked();
    return options;
}

bool DivParagraphNormalizationDialog::FormatSource() const
{
    return m_FormatSource->isChecked();
}
