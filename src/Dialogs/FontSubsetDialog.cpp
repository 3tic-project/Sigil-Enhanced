#include "Dialogs/FontSubsetDialog.h"

#include <algorithm>

#include <QCheckBox>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace
{

QString FormatName(FontSubset::ContainerFormat format)
{
    using FontSubset::ContainerFormat;
    switch (format) {
    case ContainerFormat::SfntTrueType: return QStringLiteral("TTF");
    case ContainerFormat::SfntCff: return QStringLiteral("OTF/CFF");
    case ContainerFormat::Collection: return QStringLiteral("TTC/OTC");
    case ContainerFormat::Woff: return QStringLiteral("WOFF");
    case ContainerFormat::Woff2: return QStringLiteral("WOFF2");
    case ContainerFormat::Unknown: return QObject::tr("Unknown");
    }
    return QObject::tr("Unknown");
}

QString LicenseName(FontSubset::LicenseStatus status)
{
    using FontSubset::LicenseStatus;
    switch (status) {
    case LicenseStatus::Installable: return QObject::tr("Installable");
    case LicenseStatus::Editable: return QObject::tr("Editable");
    case LicenseStatus::PreviewAndPrint: return QObject::tr("Preview and print only");
    case LicenseStatus::Restricted: return QObject::tr("Restricted");
    case LicenseStatus::NoSubsetting: return QObject::tr("Subsetting prohibited");
    case LicenseStatus::BitmapOnly: return QObject::tr("Bitmap embedding only");
    case LicenseStatus::InvalidOrMissing: return QObject::tr("Invalid or missing");
    }
    return QObject::tr("Invalid or missing");
}

QString RiskName(FontSubset::Risk risk)
{
    using FontSubset::Risk;
    switch (risk) {
    case Risk::Collection: return QStringLiteral("collection");
    case Risk::UnsupportedContainer: return QStringLiteral("unsupported-container");
    case Risk::MissingOutline: return QStringLiteral("missing-outline");
    case Risk::SvgTable: return QStringLiteral("svg-table");
    case Risk::EbdtEblc: return QStringLiteral("ebdt-eblc");
    case Risk::GraphiteLayout: return QStringLiteral("graphite-layout");
    case Risk::AatLayout: return QStringLiteral("aat-layout");
    case Risk::ColorBitmap: return QStringLiteral("color-bitmap");
    case Risk::VariableFont: return QStringLiteral("variable-font");
    case Risk::InvalidFont: return QStringLiteral("invalid-font");
    }
    return QStringLiteral("unknown");
}

QString FormatCode(FontSubset::ContainerFormat format)
{
    using FontSubset::ContainerFormat;
    switch (format) {
    case ContainerFormat::SfntTrueType: return QStringLiteral("sfnt-truetype");
    case ContainerFormat::SfntCff: return QStringLiteral("sfnt-cff");
    case ContainerFormat::Collection: return QStringLiteral("collection");
    case ContainerFormat::Woff: return QStringLiteral("woff");
    case ContainerFormat::Woff2: return QStringLiteral("woff2");
    case ContainerFormat::Unknown: return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

QString LicenseCode(FontSubset::LicenseStatus status)
{
    using FontSubset::LicenseStatus;
    switch (status) {
    case LicenseStatus::Installable: return QStringLiteral("installable");
    case LicenseStatus::Editable: return QStringLiteral("editable");
    case LicenseStatus::PreviewAndPrint: return QStringLiteral("preview-print");
    case LicenseStatus::Restricted: return QStringLiteral("restricted");
    case LicenseStatus::NoSubsetting: return QStringLiteral("no-subsetting");
    case LicenseStatus::BitmapOnly: return QStringLiteral("bitmap-only");
    case LicenseStatus::InvalidOrMissing: return QStringLiteral("invalid-or-missing");
    }
    return QStringLiteral("invalid-or-missing");
}

QJsonArray CodepointArray(const QSet<quint32>& codepoints)
{
    QList<quint32> sorted(codepoints.cbegin(), codepoints.cend());
    std::sort(sorted.begin(), sorted.end());
    QJsonArray array;
    for (quint32 codepoint : sorted) {
        array.append(QStringLiteral("U+%1")
                         .arg(codepoint, 4, 16, QLatin1Char('0')).toUpper());
    }
    return array;
}

QJsonArray StringArray(const QStringList& strings)
{
    QJsonArray array;
    for (const QString& string : strings) {
        array.append(string);
    }
    return array;
}

}

FontSubsetDialog::FontSubsetDialog(
    const FontSubset::BookSnapshot& snapshot,
    const QSet<QString>& initiallySelected,
    QWidget* parent)
    : QDialog(parent),
      m_Snapshot(snapshot),
      m_InitiallySelected(initiallySelected),
      m_Watcher(new QFutureWatcher<FontSubset::BatchResult>(this)),
      m_DropHinting(new QCheckBox(tr("Remove font hinting"), this)),
      m_Table(new QTableWidget(this)),
      m_Status(new QLabel(this)),
      m_AnalyzeButton(new QPushButton(tr("Analyze"), this)),
      m_SaveReportButton(new QPushButton(tr("Save Report..."), this)),
      m_ApplyButton(nullptr),
      m_ButtonBox(new QDialogButtonBox(QDialogButtonBox::Close, this)),
      m_Busy(false),
      m_ResultsMatchOptions(false),
      m_AnalyzedDropHinting(false)
{
    setWindowTitle(tr("Subset Embedded Fonts"));
    setMinimumSize(900, 500);
    resize(1050, 620);

    m_AnalyzeButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    m_SaveReportButton->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    m_SaveReportButton->setEnabled(false);
    m_ApplyButton = m_ButtonBox->addButton(tr("Apply Subsets"),
                                           QDialogButtonBox::AcceptRole);
    m_ApplyButton->setIcon(style()->standardIcon(QStyle::SP_DialogApplyButton));
    m_ApplyButton->setEnabled(false);

    QHBoxLayout* controls = new QHBoxLayout;
    controls->addWidget(m_DropHinting);
    controls->addStretch();
    controls->addWidget(m_AnalyzeButton);
    controls->addWidget(m_SaveReportButton);

    m_Table->setColumnCount(7);
    m_Table->setHorizontalHeaderLabels(
        {tr("Font"), tr("Format"), tr("License"), tr("Original"),
         tr("Subset"), tr("Glyphs"), tr("Status")});
    m_Table->setAlternatingRowColors(true);
    m_Table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_Table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_Table->verticalHeader()->setVisible(false);
    m_Table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_Table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_Table->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);

    QHBoxLayout* bottom = new QHBoxLayout;
    bottom->addWidget(m_Status, 1);
    bottom->addWidget(m_ButtonBox);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addLayout(controls);
    layout->addWidget(m_Table, 1);
    layout->addLayout(bottom);

    connect(m_AnalyzeButton, &QPushButton::clicked,
            this, &FontSubsetDialog::StartAnalysis);
    connect(m_SaveReportButton, &QPushButton::clicked,
            this, &FontSubsetDialog::SaveReport);
    connect(m_ApplyButton, &QPushButton::clicked,
            this, &FontSubsetDialog::AcceptSelection);
    connect(m_ButtonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_Table, &QTableWidget::itemChanged,
            this, &FontSubsetDialog::UpdateApplyState);
    connect(m_DropHinting, &QCheckBox::toggled, this, [this]() {
        if (!m_Analysis.fonts.isEmpty() && !m_Busy) {
            m_ResultsMatchOptions = false;
            m_Status->setText(
                tr("Options changed. Analyze again before applying subsets."));
            UpdateApplyState();
        }
    });
    connect(m_Watcher, &QFutureWatcher<FontSubset::BatchResult>::finished,
            this, &FontSubsetDialog::AnalysisFinished);

    StartAnalysis();
}

FontSubsetDialog::~FontSubsetDialog() = default;

const FontSubset::BatchResult& FontSubsetDialog::Analysis() const
{
    return m_Analysis;
}

QSet<QString> FontSubsetDialog::SelectedFontIdentifiers() const
{
    QSet<QString> identifiers;
    for (int row = 0; row < m_Table->rowCount(); ++row) {
        QTableWidgetItem* item = m_Table->item(row, 0);
        if (item && item->checkState() == Qt::Checked) {
            identifiers.insert(item->data(Qt::UserRole).toString());
        }
    }
    return identifiers;
}

void FontSubsetDialog::closeEvent(QCloseEvent* event)
{
    if (m_Busy) {
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}

void FontSubsetDialog::reject()
{
    if (!m_Busy) {
        QDialog::reject();
    }
}

void FontSubsetDialog::StartAnalysis()
{
    if (m_Busy) {
        return;
    }
    FontSubset::Options options;
    options.dropHinting = m_DropHinting->isChecked();
    m_AnalyzedDropHinting = options.dropHinting;
    m_ResultsMatchOptions = false;
    SetBusy(true);
    m_Table->setRowCount(0);
    m_Status->setText(tr("Analyzing %n embedded font(s)...", "", m_Snapshot.fonts.size()));
    m_Watcher->setFuture(QtConcurrent::run(
        [snapshot = m_Snapshot, options]() {
            return FontSubset::FontSubsetController::Analyze(snapshot, options);
        }));
}

void FontSubsetDialog::AnalysisFinished()
{
    m_Analysis = m_Watcher->result();
    m_ResultsMatchOptions = true;
    PopulateResults();
    SetBusy(false);
    m_SaveReportButton->setEnabled(true);

    int eligible = 0;
    for (const FontSubset::FontAnalysis& analysis : m_Analysis.fonts) {
        if (analysis.result.success &&
            analysis.result.newSize < analysis.result.oldSize) {
            ++eligible;
        }
    }
    m_Status->setText(
        tr("Analysis complete: %1 of %2 font(s) ready; %3 codepoint(s) collected.")
            .arg(eligible)
            .arg(m_Analysis.fonts.size())
            .arg(m_Analysis.usage.codepoints.size()));
    if (!m_Analysis.warnings.isEmpty()) {
        m_Status->setToolTip(m_Analysis.warnings.join(QLatin1Char('\n')));
    } else {
        m_Status->setToolTip(QString());
    }
    UpdateApplyState();
}

void FontSubsetDialog::UpdateApplyState()
{
    m_ApplyButton->setEnabled(!m_Busy && m_ResultsMatchOptions &&
                              !SelectedFontIdentifiers().isEmpty());
}

void FontSubsetDialog::SaveReport()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save Font Subset Report"), QStringLiteral("font-subset-report.json"),
        tr("JSON files (*.json);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(CreateReportJson()) < 0 ||
        !file.commit()) {
        QMessageBox::warning(this, tr("Save Font Subset Report"),
                             tr("Could not save the report to %1: %2")
                                 .arg(path, file.errorString()));
    }
}

void FontSubsetDialog::AcceptSelection()
{
    if (!SelectedFontIdentifiers().isEmpty()) {
        accept();
    }
}

QByteArray FontSubsetDialog::CreateReportJson() const
{
    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), 1);
    root.insert(QStringLiteral("codepointCount"),
                static_cast<qint64>(m_Analysis.usage.codepoints.size()));
    root.insert(QStringLiteral("shapingSampleCount"),
                static_cast<qint64>(m_Analysis.usage.shapingSamples.size()));
    root.insert(QStringLiteral("dropHinting"), m_AnalyzedDropHinting);
    root.insert(QStringLiteral("warnings"), StringArray(m_Analysis.warnings));

    QJsonArray fonts;
    for (const FontSubset::FontAnalysis& analysis : m_Analysis.fonts) {
        const FontSubset::Result& result = analysis.result;
        QJsonObject font;
        font.insert(QStringLiteral("identifier"), analysis.font.identifier);
        font.insert(QStringLiteral("path"), analysis.font.relativePath);
        font.insert(QStringLiteral("mediaType"), analysis.font.mediaType);
        font.insert(QStringLiteral("obfuscated"),
                    !analysis.font.obfuscationAlgorithm.isEmpty());
        font.insert(QStringLiteral("success"), result.success);
        font.insert(QStringLiteral("error"), result.error);
        font.insert(QStringLiteral("format"), FormatCode(result.inspection.format));
        font.insert(QStringLiteral("license"), LicenseCode(result.inspection.license));
        font.insert(QStringLiteral("oldSize"), static_cast<qint64>(result.oldSize));
        font.insert(QStringLiteral("newSize"), static_cast<qint64>(result.newSize));
        font.insert(QStringLiteral("oldGlyphCount"),
                    static_cast<qint64>(result.oldGlyphCount));
        font.insert(QStringLiteral("newGlyphCount"),
                    static_cast<qint64>(result.newGlyphCount));
        font.insert(QStringLiteral("mappedGlyphCount"),
                    static_cast<qint64>(result.mappedGlyphCount));
        font.insert(QStringLiteral("inputCodepointCount"),
                    static_cast<qint64>(result.inputCodepoints.size()));
        font.insert(QStringLiteral("requestedCodepointCount"),
                    static_cast<qint64>(result.requestedCodepoints.size()));
        font.insert(QStringLiteral("unavailableCodepointCount"),
                    static_cast<qint64>(result.unavailableCodepoints.size()));
        font.insert(QStringLiteral("missingCodepointCount"),
                    static_cast<qint64>(result.missingCodepoints.size()));
        font.insert(QStringLiteral("requestedCodepoints"),
                    CodepointArray(result.requestedCodepoints));
        font.insert(QStringLiteral("unavailableCodepoints"),
                    CodepointArray(result.unavailableCodepoints));
        font.insert(QStringLiteral("missingCodepoints"),
                    CodepointArray(result.missingCodepoints));
        font.insert(QStringLiteral("harfbuzzVersion"), result.harfbuzzVersion);
        font.insert(QStringLiteral("warnings"), StringArray(result.warnings));
        QJsonArray risks;
        for (FontSubset::Risk risk : result.inspection.risks) {
            risks.append(RiskName(risk));
        }
        font.insert(QStringLiteral("risks"), risks);
        fonts.append(font);
    }
    root.insert(QStringLiteral("fonts"), fonts);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

void FontSubsetDialog::PopulateResults()
{
    m_Table->blockSignals(true);
    m_Table->setRowCount(m_Analysis.fonts.size());
    const bool restrictInitialSelection = !m_InitiallySelected.isEmpty();
    for (int row = 0; row < m_Analysis.fonts.size(); ++row) {
        const FontSubset::FontAnalysis& analysis = m_Analysis.fonts.at(row);
        const FontSubset::Result& result = analysis.result;
        const bool eligible = result.success && result.newSize < result.oldSize;

        QTableWidgetItem* name = new QTableWidgetItem(analysis.font.relativePath);
        name->setData(Qt::UserRole, analysis.font.identifier);
        name->setFlags(name->flags() | Qt::ItemIsUserCheckable);
        name->setCheckState(eligible &&
                                (!restrictInitialSelection ||
                                 m_InitiallySelected.contains(analysis.font.identifier))
                            ? Qt::Checked : Qt::Unchecked);
        if (!eligible) {
            name->setFlags(name->flags() & ~Qt::ItemIsEnabled);
        }
        m_Table->setItem(row, 0, name);
        m_Table->setItem(row, 1, new QTableWidgetItem(FormatName(result.inspection.format)));
        m_Table->setItem(row, 2, new QTableWidgetItem(LicenseName(result.inspection.license)));
        m_Table->setItem(row, 3, new QTableWidgetItem(
            QLocale().formattedDataSize(result.oldSize)));
        m_Table->setItem(row, 4, new QTableWidgetItem(
            result.success ? QLocale().formattedDataSize(result.newSize) : QStringLiteral("-")));
        m_Table->setItem(row, 5, new QTableWidgetItem(
            result.success
                ? tr("%1 to %2").arg(result.oldGlyphCount).arg(result.newGlyphCount)
                : QStringLiteral("-")));

        QString status;
        if (eligible) {
            status = tr("Ready");
        } else if (result.success) {
            status = tr("No size reduction");
        } else {
            status = result.error.isEmpty() ? result.inspection.blockingReason : result.error;
        }
        QTableWidgetItem* statusItem = new QTableWidgetItem(status);
        QStringList details = result.warnings;
        if (!result.inspection.blockingReason.isEmpty()) {
            details.prepend(result.inspection.blockingReason);
        }
        statusItem->setToolTip(details.join(QLatin1Char('\n')));
        m_Table->setItem(row, 6, statusItem);
    }
    m_Table->blockSignals(false);
}

void FontSubsetDialog::SetBusy(bool busy)
{
    m_Busy = busy;
    m_DropHinting->setEnabled(!busy);
    m_AnalyzeButton->setEnabled(!busy);
    m_SaveReportButton->setEnabled(!busy && !m_Analysis.fonts.isEmpty());
    m_ApplyButton->setEnabled(false);
    if (QPushButton* closeButton = m_ButtonBox->button(QDialogButtonBox::Close)) {
        closeButton->setEnabled(!busy);
    }
}
