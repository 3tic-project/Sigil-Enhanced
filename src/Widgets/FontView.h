/************************************************************************
**
**  Copyright (C) 2020 Kevin B. Hendricks, Stratford, Ontario Canada
**
**  This file is part of Sigil.
**
*************************************************************************/

#pragma once
#ifndef FONTVIEW_H
#define FONTVIEW_H

#include <QComboBox>
#include <QLabel>
#include <QString>
#include <QStringList>
#include <QWidget>

#include "FontPreview/FontPreviewLanguage.h"

class QVBoxLayout;
class QWebEngineView;

class FontView : public QWidget
{
    Q_OBJECT

 public:
    FontView(QWidget *parent = nullptr);
    ~FontView();

 public slots:
    void ShowFont(const QString &path, const QStringList &bookLanguages = QStringList());
    void ReloadViewer();

 private slots:
    void LanguageChanged(int index);

 private:
    void Rebuild();
    QString ProfileName(FontPreviewProfile profile) const;
    void UpdateAutoLabel(FontPreviewProfile resolved);

    QString m_path;
    QStringList m_bookLanguages;
    FontPreviewChoice m_choice = FontPreviewChoice::Auto;
    QLabel *m_languageLabel;
    QComboBox *m_language;
    QLabel *m_coverage;
    QWebEngineView *m_WebView;
    QVBoxLayout *m_layout;
};

#endif
