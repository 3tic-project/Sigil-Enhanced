/************************************************************************
 **
 **  Copyright (C) 2020-2026 Kevin B. Hendricks
 **
 **  This file is part of Sigil.
 **
*************************************************************************/

#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QRawFont>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QUrl>
#include <QVBoxLayout>
#include <QWebEngineView>

#include "FontPreview/FontGlyphCoverage.h"
#include "FontPreview/FontPreviewHtml.h"
#include "FontPreview/FontPreviewSamples.h"
#include "Misc/Utility.h"
#include "Misc/WebProfileMgr.h"
#include "ViewEditors/SimplePage.h"
#include "Widgets/FontView.h"

namespace {

QString CssWeight(int weight)
{
    if (weight < QFont::ExtraLight) return QStringLiteral("100");
    if (weight < QFont::Light) return QStringLiteral("200");
    if (weight < QFont::Normal) return QStringLiteral("300");
    if (weight < QFont::Medium) return QStringLiteral("400");
    if (weight < QFont::DemiBold) return QStringLiteral("500");
    if (weight < QFont::Bold) return QStringLiteral("600");
    if (weight < QFont::ExtraBold) return QStringLiteral("700");
    if (weight < QFont::Black) return QStringLiteral("800");
    return QStringLiteral("900");
}

QString WeightName(int weight)
{
    if (weight < QFont::ExtraLight) return QStringLiteral("Thin");
    if (weight < QFont::Light) return QStringLiteral("ExtraLight");
    if (weight < QFont::Normal) return QStringLiteral("Light");
    if (weight < QFont::Medium) return QStringLiteral("Normal");
    if (weight < QFont::DemiBold) return QStringLiteral("Medium");
    if (weight < QFont::Bold) return QStringLiteral("DemiBold");
    if (weight < QFont::ExtraBold) return QStringLiteral("Bold");
    if (weight < QFont::Black) return QStringLiteral("ExtraBold");
    return QStringLiteral("Black");
}

}



FontView::FontView(QWidget *parent)
    : QWidget(parent),
      m_languageLabel(new QLabel(this)),
      m_language(new QComboBox(this)),
      m_coverage(new QLabel(this)),
      m_WebView(new QWebEngineView(this)),
      m_layout(new QVBoxLayout(this))
{
    m_languageLabel->setText(tr("Preview language"));
    m_language->addItem(tr("Auto: %1").arg(ProfileName(FontPreviewProfile::Generic)),
                        int(FontPreviewChoice::Auto));
    m_language->addItem(tr("Simplified Chinese"), int(FontPreviewChoice::SimplifiedChinese));
    m_language->addItem(tr("Traditional Chinese"), int(FontPreviewChoice::TraditionalChinese));
    m_language->addItem(tr("Chinese (Simplified/Traditional)"), int(FontPreviewChoice::ChineseMixed));
    m_language->addItem(tr("Japanese"), int(FontPreviewChoice::Japanese));
    m_language->addItem(tr("English"), int(FontPreviewChoice::English));
    m_language->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_language->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    m_coverage->setWordWrap(false);
    m_coverage->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_coverage->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    auto *toolbar = new QHBoxLayout();
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->addWidget(m_languageLabel);
    toolbar->addWidget(m_language);
    toolbar->addSpacing(16);
    toolbar->addWidget(m_coverage);
    toolbar->addStretch(1);

    QWebEngineProfile *profile = WebProfileMgr::instance().GetOneTimeProfile();
    m_WebView->setPage(new SimplePage(profile, m_WebView));
    m_WebView->setContextMenuPolicy(Qt::NoContextMenu);
    m_WebView->setFocusPolicy(Qt::NoFocus);
    m_WebView->setAcceptDrops(false);
    m_WebView->setUrl(QUrl(QStringLiteral("about:blank")));
    m_WebView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_layout->setContentsMargins(6, 4, 6, 4);
    m_layout->setSpacing(4);
    m_layout->addLayout(toolbar);
    m_layout->addWidget(m_WebView, 1);
    connect(m_language, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &FontView::LanguageChanged);
}

FontView::~FontView()
{
    WebProfileMgr::ReleaseEngineView(m_WebView);
}

void FontView::ShowFont(const QString &path, const QStringList &bookLanguages)
{
    const bool bookChanged = bookLanguages != m_bookLanguages;
    m_path = path;
    m_bookLanguages = bookLanguages;
    if (bookChanged) {
        m_choice = FontPreviewChoice::Auto;
        const QSignalBlocker blocker(m_language);
        m_language->setCurrentIndex(0);
    }
    Rebuild();
}

void FontView::ReloadViewer()
{
    ShowFont(m_path, m_bookLanguages);
}

void FontView::LanguageChanged(int index)
{
    m_choice = FontPreviewChoice(m_language->itemData(index).toInt());
    Rebuild();
}

QString FontView::ProfileName(FontPreviewProfile profile) const
{
    switch (profile) {
        case FontPreviewProfile::SimplifiedChinese: return tr("Simplified Chinese");
        case FontPreviewProfile::TraditionalChinese: return tr("Traditional Chinese");
        case FontPreviewProfile::ChineseMixed: return tr("Chinese (Simplified/Traditional)");
        case FontPreviewProfile::Japanese: return tr("Japanese");
        case FontPreviewProfile::English: return tr("English");
        case FontPreviewProfile::Generic: return tr("Generic");
    }
    return tr("Generic");
}

void FontView::UpdateAutoLabel(FontPreviewProfile resolved)
{
    const QSignalBlocker blocker(m_language);
    m_language->setItemText(0, tr("Auto: %1").arg(ProfileName(resolved)));
    m_language->updateGeometry();
}

void FontView::Rebuild()
{
    if (m_path.isEmpty()) {
        return;
    }
    QFileInfo info(m_path);
    QRawFont rawFont(m_path, 16.0);
    const FontPreviewProfile resolved = ResolveFontPreviewProfile(
        m_bookLanguages, m_choice, rawFont.supportedWritingSystems());
    UpdateAutoLabel(resolved);

    const FontPreviewSample &sample = FontPreviewSampleFor(resolved);
    const GlyphCoverageResult coverage = AnalyzeGlyphCoverage(rawFont, FontPreviewSpecimenText(sample));
    if (coverage.total == 0) {
        m_coverage->setText(tr("Coverage: 0 / 0"));
    } else {
        const double percent = 100.0 * coverage.supported / coverage.total;
        QString text = tr("Coverage: %1 / %2 (%3%)")
                           .arg(coverage.supported)
                           .arg(coverage.total)
                           .arg(QString::number(percent, 'f', 1));
        if (coverage.missing.isEmpty()) {
            text += QStringLiteral("  ");
            text += tr("No missing glyphs");
        } else {
            text += QStringLiteral("  ");
            text += tr("Missing: %1").arg(coverage.missing.size());
        }
        m_coverage->setText(text);
    }

    QString description = PreferredFontFamilyName(rawFont);
    const QString weightName = WeightName(rawFont.weight());
    if (!description.isEmpty()) {
        if (!description.contains(weightName) && !weightName.isEmpty()) {
            description += QLatin1Char(' ');
            description += weightName;
        }
    }
    QString styleName = QStringLiteral("normal");
    if (rawFont.style() == QFont::StyleItalic) {
        styleName = QStringLiteral("italic");
        if (!description.isEmpty()) description += QStringLiteral(" Italic");
    } else if (rawFont.style() == QFont::StyleOblique) {
        styleName = QStringLiteral("oblique");
        if (!description.isEmpty()) description += QStringLiteral(" Oblique");
    }
    if (description.isEmpty()) {
        description = tr("No reliable font data");
    }

    FontPreviewDocument document;
    document.fontUrl = QString::fromUtf8(QUrl::fromLocalFile(m_path).toEncoded());
    document.fontWeight = CssWeight(rawFont.weight());
    document.fontStyle = styleName;
    document.description = description;
    document.fileName = info.fileName();
    document.fileBytes = info.size();
    document.sample = sample;
    document.coverage = coverage;
    document.missingTooltip = tr("Not present in this font");
    document.omitFullyMissingLines = resolved == FontPreviewProfile::Generic;
    QString html = BuildFontPreviewHtml(document);
    if (Utility::IsDarkMode()) {
        html = Utility::AddDarkCSS(html);
    }
    m_WebView->page()->profile()->clearHttpCache();
    m_WebView->page()->setBackgroundColor(Utility::WebViewBackgroundColor());
    m_WebView->setHtml(html, QUrl::fromLocalFile(m_path));
}
