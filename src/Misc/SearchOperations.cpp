/************************************************************************
**
**  Copyright (C) 2015-2025 Kevin B. Hendricks, Stratford Ontario Canada
**  Copyright (C) 2009-2011 Strahinja Markovic  <strahinja.markovic@gmail.com>
**
**  This file is part of Sigil.
**
**  Sigil is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  (at your option) any later version.
**
**  Sigil is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with Sigil.  If not, see <http://www.gnu.org/licenses/>.
**
*************************************************************************/

#include "EmbedPython/PyObjectPtr.h"
#include <signal.h>

#include <QtCore/QtCore>
#include <QtWidgets/QApplication>
#include <QtWidgets/QProgressDialog>

#include "BookManipulation/CleanSource.h"
#include "Misc/SearchOperations.h"
#include "Misc/SettingsStore.h"
#include "Misc/Utility.h"
#include "PCRE2/PCRECache.h"
#include "Misc/HTMLSpellCheck.h"
#include "ResourceObjects/HTMLResource.h"
#include "ResourceObjects/TextResource.h"
#include "ViewEditors/Searchable.h"
#include "EmbedPython/PythonRoutines.h"
#include "sigil_constants.h"

int SearchOperations::CountInFiles(const QString &search_regex,
                                   QList<Resource *> resources,
                                   bool check_spelling)
{
    QProgressDialog progress(QObject::tr("Counting occurrences.."), 0, 0, resources.count(), Utility::GetMainWindow());
    progress.setMinimumDuration(PROGRESS_BAR_MINIMUM_DURATION);
    int progress_value = 0;
    progress.setValue(progress_value);
    // Count sequentially in order to see if occassional crashes are due to threading
    int count = 0;
    foreach(Resource * resource, resources) {
        progress.setValue(progress_value++);
        qApp->processEvents();
        count += CountInFile(search_regex, resource, check_spelling);
    }
    return count;
}


int SearchOperations::ReplaceInAllFIles(const QString &search_regex,
                                        const QString &replacement,
                                        QList<Resource *> resources)
{
    QProgressDialog progress(QObject::tr("Replacing search term..."), 0, 0, resources.count(), Utility::GetMainWindow());
    progress.setMinimumDuration(PROGRESS_BAR_MINIMUM_DURATION);
    int progress_value = 0;
    progress.setValue(progress_value);
    // Replace sequentially in order to see if occassional crashes are due to threading
    int count = 0;
    foreach(Resource * resource, resources) {
        progress.setValue(progress_value++);
        qApp->processEvents();
        count += ReplaceInFile(search_regex, replacement, resource);
    }
    return count;
}


int SearchOperations::CountInFile(const QString &search_regex,
                                  Resource *resource,
                                  bool check_spelling)
{
    // QReadLocker locker(&resource->GetLock());
    HTMLResource *html_resource = qobject_cast<HTMLResource *>(resource);

    if (html_resource) {
        return CountInHTMLFile(search_regex, html_resource, check_spelling);
    }

    TextResource *text_resource = qobject_cast<TextResource *>(resource);

    if (text_resource) {
        return CountInTextFile(search_regex, text_resource);
    }

    // We should never get here.
    return 0;
}



int SearchOperations::CountInHTMLFile(const QString &search_regex,
                                      HTMLResource *html_resource,
                                      bool check_spelling)
{
    QReadLocker locker(&html_resource->GetLock());
    // note you can not use a reference here because the text returned from
    // any text resource can come from an internal cache that can go away
    const QString text = html_resource->GetText();
    if (check_spelling) {
        return HTMLSpellCheck::CountMisspelledWords(text, 0, text.length(), search_regex);
    } else {
        return PCRECache::instance()->getObject(search_regex)->getEveryMatchInfo(text).count();
    }
}

int SearchOperations::CountInTextFile(const QString &search_regex, TextResource *text_resource)
{
    QReadLocker locker(&text_resource->GetLock());
    // note you can not use a reference here because the text returned from
    // any text resource can come from an internal cache that can go away
    const QString text = text_resource->GetText();
    return PCRECache::instance()->getObject(search_regex)->getEveryMatchInfo(text).count();
}


int SearchOperations::ReplaceInFile(const QString &search_regex,
                                    const QString &replacement,
                                    Resource *resource)
{
    // QWriteLocker locker(&resource->GetLock());
    HTMLResource *html_resource = qobject_cast<HTMLResource *>(resource);

    if (html_resource) {
        return ReplaceHTMLInFile(search_regex, replacement, html_resource);
    }

    TextResource *text_resource = qobject_cast<TextResource *>(resource);

    if (text_resource) {
        return ReplaceTextInFile(search_regex, replacement, text_resource);
    }

    // We should never get here.
    return 0;
}


int SearchOperations::ReplaceHTMLInFile(const QString &search_regex,
                                        const QString &replacement,
                                        HTMLResource *html_resource)
{
    // SettingsStore ss;
    QWriteLocker locker(&html_resource->GetLock());
    int count;
    QString new_text;
    QString text = html_resource->GetText();
    std::tie(new_text, count) = PerformGlobalReplace(text, search_regex, replacement);
    html_resource->SetText(new_text);
    return count;
 }


int SearchOperations::ReplaceTextInFile(const QString &search_regex,
                                        const QString &replacement,
                                        TextResource *text_resource)
{
    QWriteLocker locker(&text_resource->GetLock());
    int count;
    QString new_text;
    QString text = text_resource->GetText();
    std::tie(new_text, count) = PerformGlobalReplace(text, search_regex, replacement);
    text_resource->SetText(new_text);
    return count;
}


std::tuple<QString, int> SearchOperations::PerformGlobalReplace(const QString &text,
        const QString &search_regex,
        const QString &replacement)
{
    QString new_text = text;
    int count = 0;
    SPCRE *spcre = PCRECache::instance()->getObject(search_regex);
    QList<SPCRE::MatchInfo> match_info = spcre->getEveryMatchInfo(text);

    for (int i =  match_info.count() - 1; i >= 0; i--) {
        QString match_segement = Utility::Substring(match_info.at(i).offset.first, match_info.at(i).offset.second, new_text);
        QString replacement_text;

        if (spcre->replaceText(match_segement, match_info.at(i).capture_groups_offsets, replacement, replacement_text)) {
            new_text.replace(match_info.at(i).offset.first, match_info.at(i).offset.second - match_info.at(i).offset.first, replacement_text);
            count++;
        }
    }

    return std::make_tuple(new_text, count);
}


std::tuple<QString, int> SearchOperations::PerformHTMLSpellCheckReplace(const QString &text,
        const QString &search_regex,
        const QString &replacement)
{
    QString new_text = text;
    int count = 0;
    int offset = 0;
    SPCRE *spcre = PCRECache::instance()->getObject(search_regex);
    QList<HTMLSpellCheck::MisspelledWord> check_spelling = HTMLSpellCheck::GetMisspelledWords(text, 0, text.length(), search_regex);
    foreach(HTMLSpellCheck::MisspelledWord misspelled_word, check_spelling) {
        SPCRE::MatchInfo match_info = spcre->getFirstMatchInfo(misspelled_word.text);

        if (match_info.offset.first != -1) {
            QString replacement_text;

            if (spcre->replaceText(Utility::Substring(match_info.offset.first, match_info.offset.second, misspelled_word.text), match_info.capture_groups_offsets, replacement, replacement_text)) {
                new_text.replace(offset + misspelled_word.offset + match_info.offset.first, match_info.offset.second - match_info.offset.first, replacement_text);
                offset += replacement_text.length() - (match_info.offset.second - match_info.offset.first);
                count++;
            }
        }
    }
    return std::make_tuple(new_text, count);
}


void SearchOperations::Accumulate(int &first, const int &second)
{
    first += second;
}

int SearchOperations::FunctionReplaceInAllFiles(const QString &search_regex,
                                                const QString &function_name,
                                                QList<Resource *> resources)
{
    QProgressDialog progress(QObject::tr("Replacing search term..."), 0, 0, resources.count(), Utility::GetMainWindow());
    progress.setMinimumDuration(PROGRESS_BAR_MINIMUM_DURATION);
    int progress_value = 0;
    progress.setValue(progress_value);
    PythonRoutines pr;
    PyObjectPtr fsp = pr.SetupInitialFunctionSearchEnvInPython(function_name);

    foreach(Resource * resource, resources) {
       progress.setValue(progress_value++);
        qApp->processEvents();
        QString bookpath = resource->GetRelativePath();
        HTMLResource *html_resource = qobject_cast<HTMLResource *>(resource);
        TextResource *text_resource = qobject_cast<TextResource *>(resource);
        if (html_resource) {
            QWriteLocker locker(&html_resource->GetLock());
            QString text = html_resource->GetText();
            QString new_text = pr.DoFunctionSearchTextReplacementsInPython(fsp, search_regex, bookpath, text);
            html_resource->SetText(new_text);
        } else if (text_resource) {
            QWriteLocker locker(&text_resource->GetLock());
            QString text = text_resource->GetText();
            QString new_text = pr.DoFunctionSearchTextReplacementsInPython(fsp, search_regex, bookpath, text);
            text_resource->SetText(new_text);
        }
    }
    int count = pr.GetCurrentReplacementCountInPython(fsp);
    return count;
}

//------------ modified: FindReplacePlus -------------------

int SearchOperations::CountInFilesPlus(const QString& presearch_regex,
                                       const QString& search_regex,
                                       QList<Resource*> resources)
{
    QProgressDialog progress(QObject::tr("Counting occurrences.."), 0, 0, resources.count(), Utility::GetMainWindow());
    progress.setMinimumDuration(PROGRESS_BAR_MINIMUM_DURATION);
    int progress_value = 0;
    progress.setValue(progress_value);

    int count = 0;
    foreach(Resource * resource, resources) {
        progress.setValue(progress_value++);
        qApp->processEvents();
        count += CountInFilePlus(presearch_regex, search_regex, resource);
    }
    return count;
}

//modified: FindReplacePlus
int SearchOperations::CountInFilePlus(const QString& presearch_regex,
                                      const QString& search_regex,
                                      Resource* resource)
{
    TextResource* text_resource = qobject_cast<TextResource*>(resource);
    if (!text_resource) return 0;

    QReadLocker locker(&text_resource->GetLock());
    const QString text = text_resource->GetText();

    SPCRE* spcre = PCRECache::instance()->getObject(search_regex);
    if (presearch_regex.isEmpty()) {
        return spcre->getEveryMatchInfo(text).count();
    }

    int count = 0;
    QList<std::pair<int,int>> match_infos = Utility::GetPreSearchMatchInfos(presearch_regex, text);
    if (!match_infos.isEmpty()) {
        for (int i = 0; i < match_infos.count(); ++i) {
            std::pair<int, int> info = match_infos.at(i);
            int start = info.first,
                end = info.second;
            QString sub_text = text.mid(start, end - start);
            count += spcre->getEveryMatchInfo(sub_text).count();
        }
    }
    return count;
}

//modified: FindReplacePlus
bool SearchOperations::TestInFilePlus(const QString& presearch_regex, const QString& search_regex, Resource* resource)
{
    TextResource* text_resource = qobject_cast<TextResource*>(resource);
    if (!text_resource) return 0;

    QReadLocker locker(&text_resource->GetLock());
    const QString text = text_resource->GetText();

    SPCRE* spcre_pre = PCRECache::instance()->getObject(presearch_regex);
    SPCRE* spcre = PCRECache::instance()->getObject(search_regex);
    SPCRE::MatchInfo pre_m_info, m_info;

    if (presearch_regex.isEmpty()) {
        return spcre->getFirstMatchInfo(text).offset.first != -1;
    }

    int pre_start = 0,
        pre_end = text.length();

    bool found = false;

    while (pre_start < pre_end) {
        QString pre_text = text.mid(pre_start, pre_end - pre_start);
        pre_m_info = spcre_pre->getFirstMatchInfo(pre_text);

        if (pre_m_info.offset.first == -1) break;

        int start, end;
        if (pre_m_info.capture_groups_offsets.count() >= 2) {
            std::pair<int, int> g_offset = pre_m_info.capture_groups_offsets.at(1);
            start = pre_m_info.offset.first + g_offset.first;
            end = pre_m_info.offset.first + g_offset.second;
        }
        else {
            start = pre_m_info.offset.first;
            end = pre_m_info.offset.second;
        }
        QString sub_text = pre_text.mid(start, end - start);
        m_info = spcre->getFirstMatchInfo(sub_text);
        if (m_info.offset.first != -1) {
            found = true;
            break;
        }
        pre_start += end;
    }
    return found;
}

//modified: FindReplacePlus
int SearchOperations::ReplaceInAllFIlesPlus(const QString& presearch_regex,
                                            const QString& search_regex,
                                            const QString& replacement,
                                            QList<Resource*> resources)
{
    QProgressDialog progress(QObject::tr("Replacing search term..."), 0, 0, resources.count(), Utility::GetMainWindow());
    progress.setMinimumDuration(PROGRESS_BAR_MINIMUM_DURATION);
    int progress_value = 0;
    progress.setValue(progress_value);
    // Replace sequentially in order to see if occassional crashes are due to threading
    int count = 0;
    foreach(Resource * resource, resources) {
        progress.setValue(progress_value++);
        qApp->processEvents();
        count += ReplaceInFilePlus(presearch_regex, search_regex, replacement, resource);
    }
    return count;
}

//modified: FindReplacePlus
int SearchOperations::ReplaceInFilePlus(const QString& presearch_regex, const QString& search_regex, const QString& replacement, Resource* resource)
{
    int count;
    QString new_text;

    HTMLResource* html_resource = qobject_cast<HTMLResource*>(resource);
    if (html_resource) {
        QWriteLocker locker(&html_resource->GetLock());
        QString text = html_resource->GetText();
        std::tie(new_text, count) = PerformGlobalReplacePlus(presearch_regex, search_regex, replacement, text);
        html_resource->SetText(new_text);
        return count;
    }

    TextResource* text_resource = qobject_cast<TextResource*>(resource);
    if (text_resource) {
        QWriteLocker locker(&text_resource->GetLock());
        QString text = text_resource->GetText();
        std::tie(new_text, count) = PerformGlobalReplacePlus(presearch_regex, search_regex, replacement, text);
        text_resource->SetText(new_text);
        return count;
    }

    // We should never get here.
    return 0;
}

//modified: FindReplacePlus
std::tuple<QString, int> SearchOperations::PerformGlobalReplacePlus(const QString& presearch_regex,
                                                                    const QString& search_regex,
                                                                    const QString& replacement,
                                                                    const QString& text)
{
    QList<Utility::MatchInfo> match_infos = Utility::GetSearchInfoWithPreSearch(presearch_regex, search_regex, text);

    SPCRE* spcre = PCRECache::instance()->getObject(search_regex);

    QString new_text;
    int count = 0;
    int head_start = 0,
        head_end = 0;
    int match_start, match_end;
    foreach(Utility::MatchInfo info, match_infos) {
        match_start = info.offset.first;
        match_end = info.offset.second;
        head_end = match_start;
        QString head_text = text.mid(head_start, head_end - head_start);
        QString match_segement = text.mid(match_start, match_end - match_start);
        QString replacement_text;
        if (spcre->replaceText(match_segement, info.capture_groups_offsets, replacement, replacement_text)) {
            ++count;
            new_text += head_text + replacement_text;
        }
        else {
            new_text += head_text + match_segement;
        }
        head_start = match_end;
    }
    new_text += text.mid(head_start);
    return std::make_tuple(new_text, count);
}

//modified: FindReplacePlus
int SearchOperations::GetSuitableStartPos(const QString& presearch_regex,
                                          const QString& search_regex,
                                          Resource* resource,
                                          int origin_pos,
                                          bool reverse_direction)
{
    TextResource* text_resource = qobject_cast<TextResource*>(resource);
    if (!text_resource) return 0;

    QReadLocker locker(&text_resource->GetLock());
    const QString text = text_resource->GetText();

    QString regex = !presearch_regex.isEmpty() ? presearch_regex : search_regex;
    if (regex.isEmpty()) return origin_pos;

    SPCRE* spcre = PCRECache::instance()->getObject(regex);
    QList<SPCRE::MatchInfo> match_infos = spcre->getEveryMatchInfo(text);

    int new_pos = -1;

    if (match_infos.isEmpty()) return origin_pos;
    if (match_infos.count() == 1) {
        return new_pos = reverse_direction ? text.length() : 0;
    }

    SPCRE::MatchInfo info,last_info;
    if (!reverse_direction) {
        if (match_infos.first().offset.first >= origin_pos)
            return new_pos = 0;
        for (int i = 0,j = 1; j < match_infos.count(); i++,j++) {
            last_info = match_infos.at(i);
            info = match_infos.at(j);
            if (info.offset.first >= origin_pos) {
                new_pos = last_info.offset.second;
                break;
            }
        }
        if (new_pos == -1) new_pos = 0;
        return new_pos;
    }
    else {
        if (match_infos.last().offset.second <= origin_pos)
            return new_pos = text.length();
        for (int i = match_infos.count() - 2,j = match_infos.count() - 1; i >=0 ; i--,j--) {
            info = match_infos.at(i);
            last_info = match_infos.at(j);
            if (info.offset.second <= origin_pos) {
                new_pos = last_info.offset.first;
                break;
            }
        }
        if (new_pos == -1) new_pos = text.length();
        return new_pos;
    }
}
