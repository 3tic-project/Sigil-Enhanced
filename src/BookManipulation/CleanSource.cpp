/************************************************************************
**
**  Copyright (C) 2015-2021 Kevin B. Hendricks Stratford, ON, Canada 
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

#include "EmbedPython/EmbeddedPython.h"

#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QWriteLocker>
#include <QtWidgets/QApplication>
#include <QtWidgets/QProgressDialog>
#include <QRegularExpression>
#include <QRegularExpressionMatch>

#include "BookManipulation/CleanSource.h"
#include "BookManipulation/XhtmlDoc.h"
#include "Parsers/GumboInterface.h"
#include "Misc/SettingsStore.h"
#include "sigil_constants.h"
#include "sigil_exception.h"
#include "Misc/Utility.h"
#include <utility>
// ----------- modified: Prettify xhtml -------------
#include "Parsers/TagLister.h"
#include "Parsers/CSSInfo.h"
//---------------------------------------------------

static const QString HEAD_END = "</\\s*head\\s*>";
const QString SVG_NAMESPACE_PREFIX = "<\\s*[^>]*(xmlns\\s*:\\s*svg\\s*=\\s*(?:\"|')[^\"']+(?:\"|'))[^>]*>";

static const QStringList NUMERIC_NBSP = QStringList() << "&#160;" << "&#xa0;" << "&#x00a0;";


// Performs general cleaning (and improving)
// of provided book XHTML source code
QString CleanSource::Mend(const QString &source, const QString &version)
{
    SettingsStore settings;
    QString newsource = PreprocessSpecialCases(source);
    GumboInterface gp = GumboInterface(newsource, version);
    newsource = gp.repair();
    newsource = CharToEntity(newsource, version);
    newsource = PrettifyDOCTYPEHeader(newsource);
    return newsource;
}

// Mend and Prettify XHTML
QString CleanSource::MendPrettify(const QString& source, const QString& version)
{
    QString newsource = PreprocessSpecialCases(source);
    GumboInterface gi = GumboInterface(newsource, version);
    newsource = gi.prettyprint();
    newsource = CharToEntity(newsource, version);
    newsource = PrettifyDOCTYPEHeader(newsource);
    return newsource;
}

//------------------------ modified: Prettify xhtml ---------------------------------
QString CleanSource::XhtmlPrettify(const QString& source, XhtmlFormatParser& xfparser)
{
    QString newsource = PrettifyXhtml(source, xfparser); // modified: PrettifyXhtml
    return newsource;
}
//-----------------------------------------------------------------------------------



// Repair XML if needed and PrettyPrint using BeautifulSoup4
QString CleanSource::XMLPrettyPrintBS4(const QString &source, const QString mtype)
{
    int rv = 0;
    QString error_traceback;
    QList<QVariant> args;
    args.append(QVariant(source));
    args.append(QVariant(mtype));
    EmbeddedPython * epython  = EmbeddedPython::instance();

    QVariant res = epython->runInPython( QString("xmlprocessor"),
                                         QString("repairXML"),
                                         args,
                                         &rv,
                                         error_traceback);    
    if (rv != 0) {
        Utility::DisplayStdWarningDialog(QString("error in xmlprocessor repairXML: ") + QString::number(rv), 
                                         error_traceback);
        // an error happened, return unchanged original
        return QString(source);
    }
    return res.toString();
}

// convert the source to valid XHTML
QString CleanSource::ToValidXHTML(const QString &source, const QString &version)
{
    QString newsource = source;
    if (!XhtmlDoc::IsDataWellFormed(source)) {
        newsource = Mend(source, version);
    }
    return newsource;
}

XhtmlDoc::WellFormedError CleanSource::WellFormedXMLCheck(const QString &source, const QString mtype)
{
    XhtmlDoc::WellFormedError error; 
    int rv = 0;
    QString error_traceback;
    QList<QVariant> args;
    args.append(QVariant(source));
    args.append(QVariant(mtype));
    EmbeddedPython * epython  = EmbeddedPython::instance();

    QVariant res = epython->runInPython( QString("xmlprocessor"),
                                         QString("WellFormedXMLCheck"),
                                         args,
                                         &rv,
                                         error_traceback);    
    if (rv != 0) {
        Utility::DisplayStdWarningDialog(QString("error in xmlprocessor WellFormedXMLCheck: ") + QString::number(rv), 
                                         error_traceback);
        // an error happened during check, return well-formed as true
        return error;
    }
    QStringList errors = res.toStringList();
    error.line = errors.at(0).toInt();
    error.column = errors.at(1).toInt();
    error.message = errors.at(2);
    return error;
}

bool CleanSource::IsWellFormedXML(const QString &source, const QString mtype)
{
    int rv = 0;
    QString error_traceback;
    QList<QVariant> args;
    args.append(QVariant(source));
    args.append(QVariant(mtype));
    EmbeddedPython * epython  = EmbeddedPython::instance();

    QVariant res = epython->runInPython( QString("xmlprocessor"),
                                         QString("IsWellFormedXML"),
                                         args,
                                         &rv,
                                         error_traceback);    
    if (rv != 0) {
        Utility::DisplayStdWarningDialog(QString("error in xmlprocessor IsWellFormedXML: ") + QString::number(rv), 
                                         error_traceback);
        // an error happened during check return well-formed as true
        return true;
    }
    return res.toBool();
}

QString CleanSource::ProcessXML(const QString &source, const QString mtype)
{
    return XMLPrettyPrintBS4(source, mtype);
}

QString CleanSource::RemoveMetaCharset(const QString &source)
{
    int head_end = source.indexOf(QRegularExpression(HEAD_END));
    if (head_end == -1) {
        return source;
    }
    QString head = Utility::Substring(0, head_end, source);

    QRegularExpression metacharset("<meta[^>]+charset[^>]+>");
    QRegularExpressionMatch metacharset_match = metacharset.match(head);
    if (!metacharset_match.hasMatch()) {
        return source;
    }
    int meta_start = metacharset_match.capturedStart();

    head.remove(meta_start, metacharset_match.capturedLength());
    return head + Utility::Substring(head_end, source.length(), source);
}


// neither svg nor math tags need a namespace prefix defined
// especially as epub3 now includes them into the html5 spec
// So we need to remove the svg prefix from the tags before
// processing them with gumbo
QString CleanSource::PreprocessSpecialCases(const QString &source)
{
    QString newsource = source;
    // remove prefix from root tag and add unprefixed svg namespace to it
    QRegularExpression root_svg_tag_with_prefix("<\\s*svg\\s*:\\s*svg");
    QString root_svg_embeddedNS = "<svg xmlns=\"http://www.w3.org/2000/svg\"";
    newsource.replace(root_svg_tag_with_prefix, root_svg_embeddedNS);
    // search for any prefixed svg namespace in that root tag and remove it
    QRegularExpression svg_nsprefix(SVG_NAMESPACE_PREFIX);
    QRegularExpressionMatch mo = svg_nsprefix.match(newsource);
    if (mo.hasMatch()) {
        newsource.replace(mo.capturedStart(1), mo.capturedLength(1), "");
    } 
    // now strip the prefix from all child starting tags
    QRegularExpression starting_child_svg_tag_with_prefix("<\\s*svg\\s*:");
    QString starting_child_tag_no_prefix = "<";
    newsource.replace(starting_child_svg_tag_with_prefix, starting_child_tag_no_prefix);
    // do the same for any child ending tags
    QRegularExpression ending_child_svg_tag_with_prefix("<\\s*/\\s*svg\\s*:");
    QString ending_child_tag_no_prefix = "</";
    newsource.replace(ending_child_svg_tag_with_prefix, ending_child_tag_no_prefix);
    return newsource;
}


// Be careful to make sure that we do not mess up epub3 <!DOCTYPE html> here
QString CleanSource::PrettifyDOCTYPEHeader(const QString &source)
{
    QString newsource = source;
    const int SAFE_LENGTH = 200;
    QRegularExpression doctype_invalid("<!DOCTYPE html PUBLIC \"W3C");
    int index = newsource.indexOf(doctype_invalid);

    if (index > 0 && index < SAFE_LENGTH) {
        newsource.insert(index + 23, "-//");
    }

    QRegularExpression doctype_missing_newline("\\?><!DOCTYPE");
    index = source.indexOf(doctype_missing_newline);

    if (index > 0 && index < SAFE_LENGTH) {
        newsource.insert(index + 2, "\n");
        QRegularExpression html_missing_newline("\"><html ");
        index = newsource.indexOf(html_missing_newline);

        if (index > 0 && index < SAFE_LENGTH) {
            newsource.insert(index + 2, "\n\n");
        }

        bool is_ncx = false;
        QRegularExpression ncx_missing_newline("\"><ncx ");
        index = newsource.indexOf(ncx_missing_newline);

        if (index > 0 && index < SAFE_LENGTH) {
            is_ncx = true;
            newsource.insert(index + 2, "\n");
        }

        QRegularExpression doctype_http_missing_newline("//EN\" \"http://");
        index = newsource.indexOf(doctype_http_missing_newline);

        if (index > 0 && index < SAFE_LENGTH) {
            newsource.insert(index + 5, is_ncx ? "\n" : "\n ");
        }
    }

    return newsource;
}


QString CleanSource::CharToEntity(const QString &source, const QString &version)
{
    SettingsStore settings;
    QString new_source = source;
    QList<std::pair <ushort, QString>> codenames = settings.preserveEntityCodeNames();
    std::pair <ushort, QString> epair;
    bool has_numeric_nbsp = false;
    foreach(epair, codenames) {
        QString codename = epair.second.toLower();
        if (NUMERIC_NBSP.contains(codename)) {
            has_numeric_nbsp = true;
        } 
    }
    // now intelligently handle the replacements
    foreach(epair, codenames) {
        QString codename = epair.second.toLower();
        if (version.startsWith("2")) {
            new_source.replace(QChar(epair.first), codename);
        } else if (version.startsWith("3")) {
            // only use numeric entities in epub3
            if (codename.startsWith("&#")) { 
                new_source.replace(QChar(epair.first), codename);
            } else if ((codename == "&nbsp;") && !has_numeric_nbsp) {
                new_source.replace(QChar(epair.first), "&#160;");
            }
        }
    }
    return new_source;
}


bool CleanSource::ReformatAll(QList <HTMLResource *> resources, QString(clean_func)(const QString &source, const QString &version))
{
    QProgressDialog progress(QObject::tr("Cleaning..."), 0, 0, resources.count(), Utility::GetMainWindow());
    progress.setMinimumDuration(PROGRESS_BAR_MINIMUM_DURATION);
    int progress_value = 0;
    progress.setValue(progress_value);
    bool book_modified = false;

    foreach(HTMLResource * resource, resources) {
        progress.setValue(progress_value++);
        qApp->processEvents();
        QWriteLocker locker(&resource->GetLock());
        QString source = resource->GetText();
        QString version = resource->GetEpubVersion();
        QString newsource = clean_func(source, version);
        if (newsource != source) {
            book_modified = true;
            resource->SetText(newsource);
        }
    }
    return book_modified;
}

// --------------- modified: Prettify xhtml ---------------------------
bool CleanSource::ReformatAllWithParser(QList <HTMLResource*> resources, XhtmlFormatParser& xfparser)
{
    QProgressDialog progress(QObject::tr("Cleaning..."), 0, 0, resources.count(), Utility::GetMainWindow());
    progress.setMinimumDuration(PROGRESS_BAR_MINIMUM_DURATION);
    int progress_value = 0;
    progress.setValue(progress_value);
    bool book_modified = false;

    foreach(HTMLResource * resource, resources) {
        progress.setValue(progress_value++);
        qApp->processEvents();
        QWriteLocker locker(&resource->GetLock());
        QString source = resource->GetText();
        QString newsource = XhtmlPrettify(source, xfparser);
        if (newsource != source) {
            book_modified = true;
            resource->SetText(newsource);
        }
    }
    return book_modified;
}
QString CleanSource::PrettifyXhtml(const QString &source,XhtmlFormatParser &xfparser) {

    QString new_text = "";

    QStringList ascendSelectors = xfparser.getAllSelectors(XhtmlFormatParser::ASCEND);
    QStringList nodePath;

    auto isSelectorMatchNode = [&xfparser](QString& sel, QStringList& nodePath)->bool {
        QStringList segments = sel.split(" ");
        if (segments.size() > nodePath.size()) return false;
        bool isMatched = true;
        for (ushort i = 1; i <= segments.size(); ++i) {
            QString seg = segments.at(segments.size() - i);
            QString seg2 = nodePath.at(nodePath.size() - i);
            if (seg == "*") continue;
            if (seg != seg2) {
                isMatched = false;
                break;
            }
        }
        return isMatched;
    };
    ushort indentPara = xfparser.m_gobal_props.indent > 4 ? 2 : xfparser.m_gobal_props.indent < 0 ? 2 : xfparser.m_gobal_props.indent;
    ushort cssfold = xfparser.m_gobal_props.cssfold > 1 ? 0 : xfparser.m_gobal_props.cssfold < 0 ? 0 : xfparser.m_gobal_props.cssfold;

    auto calcFinalProps = [&ascendSelectors, &xfparser, &isSelectorMatchNode](QStringList& nodePath)->XhtmlFormatParser::properties {
        XhtmlFormatParser::properties finalProps;
        QString featurePath = nodePath.join(' ');

        if (xfparser.m_pathPropsCache.contains(featurePath))
            return xfparser.m_pathPropsCache[featurePath];

        foreach(QString sel, ascendSelectors) {
            if (isSelectorMatchNode(sel, nodePath)) {
                XhtmlFormatParser::properties props = xfparser.getSelectorProperties(sel);
                if (props.open_pre_br   != XhtmlFormatParser::UNDEFINED_PROP) finalProps.open_pre_br   = props.open_pre_br;
                if (props.open_post_br  != XhtmlFormatParser::UNDEFINED_PROP) finalProps.open_post_br  = props.open_post_br;
                if (props.close_pre_br  != XhtmlFormatParser::UNDEFINED_PROP) finalProps.close_pre_br  = props.close_pre_br;
                if (props.close_post_br != XhtmlFormatParser::UNDEFINED_PROP) finalProps.close_post_br = props.close_post_br;
                if (props.ind_adj       != XhtmlFormatParser::UNDEFINED_PROP) finalProps.ind_adj       = props.ind_adj;
                if (props.inner_ind_adj != XhtmlFormatParser::UNDEFINED_PROP) finalProps.inner_ind_adj = props.inner_ind_adj;
                if (props.attr_fm_resv  != XhtmlFormatParser::UNDEFINED_PROP) finalProps.attr_fm_resv  = props.attr_fm_resv;
                if (props.text_fm_resv  != XhtmlFormatParser::UNDEFINED_PROP) finalProps.text_fm_resv  = props.text_fm_resv;
            }
        }
        // Set default value
        finalProps.open_pre_br   = finalProps.open_pre_br   > 9 ? 0 : finalProps.open_pre_br   < 0 ? 0 : finalProps.open_pre_br;
        finalProps.open_post_br  = finalProps.open_post_br  > 9 ? 0 : finalProps.open_post_br  < 0 ? 0 : finalProps.open_post_br;
        finalProps.close_pre_br  = finalProps.close_pre_br  > 9 ? 0 : finalProps.close_pre_br  < 0 ? 0 : finalProps.close_pre_br;
        finalProps.close_post_br = finalProps.close_post_br > 9 ? 0 : finalProps.close_post_br < 0 ? 0 : finalProps.close_post_br;
        finalProps.ind_adj       = finalProps.ind_adj       > 9 ? 0 : finalProps.ind_adj       <-9 ? 0 : finalProps.ind_adj;
        finalProps.inner_ind_adj = finalProps.inner_ind_adj > 9 ? 0 : finalProps.inner_ind_adj <-9 ? 0 : finalProps.inner_ind_adj;
        finalProps.attr_fm_resv  = finalProps.attr_fm_resv  > 1 ? 0 : finalProps.attr_fm_resv  < 0 ? 0 : finalProps.attr_fm_resv;
        finalProps.text_fm_resv  = finalProps.text_fm_resv  > 1 ? 0 : finalProps.text_fm_resv  < 0 ? 0 : finalProps.text_fm_resv;

        xfparser.m_pathPropsCache[featurePath] = finalProps;
        return finalProps;
    };

    auto cleanOpenTagText = [](QString& opentag)->QString {
        QString new_tag = "", blank = "\n\t ", tight = "=;";
        for (unsigned int i = 0; i < opentag.size() - 1; i++) {
            QChar ch = opentag.at(i), next_ch = opentag.at(i + 1);
            if (blank.contains(ch)) {
                if (blank.contains(next_ch)) continue;
                if (tight.contains(next_ch) || tight.contains(new_tag.right(1))) continue;
            }
            new_tag.append(ch);
        }
        new_tag.append(opentag.right(1));
        return new_tag;
    };

    // tag.at(i)   represents the i-th tag.
    // ti.pos      represents the starting position of tag, with the "position line" located to left of left corner symbol"<",such as "xxxxxxx|<tag>xxxxxxxxxx", the "|" means the virtual position line.
    // ti.pos + ti.len    represents the closing position of tag, with the "position line" located to right of right corner symbol">",such as "xxxxxxx<tag>|xxxxxxxxxx".
    // ti.tname    represents the tag name.
    // ti.ttype    with value "begin" | "end" | "single" | "xmlheader" | "doctype" | "comment", the tname of type "comment" is written as "!--"
    TagLister taglist(source);
    int lvl = -1;
    TagLister::TagInfo lastTi = taglist.at(0);
    int lastPostBr = 0;
    ulong lastTagEndPos = 0;
    unsigned int ti_count = taglist.size() - 1; // The TagInfo at last of TagLister Obj is not point to any tag, we should eliminate it.
    for (unsigned int i = 0; i < ti_count; ++i) {
        TagLister::TagInfo ti = taglist.at(i);
        QString previousText = ti.pos > lastTagEndPos ? Utility::trimmed(source.mid(lastTagEndPos, ti.pos - lastTagEndPos), " \n\t") : "";
        if (ti.ttype == "begin") {
            nodePath << ti.tname;
            XhtmlFormatParser::properties props = calcFinalProps(nodePath);
            lvl += 1 + props.ind_adj;
            QString pre_br = previousText.size() > 0 ? QString(props.open_pre_br, '\n') : props.open_pre_br > lastPostBr ? QString(props.open_pre_br - lastPostBr, '\n') : "";
            QString post_br = props.open_post_br > 0 ? QString(props.open_post_br, '\n') : "";
            QString indent = props.open_pre_br + lastPostBr == 0 ? "" : indentPara * lvl > 0 ? QString(indentPara * lvl, ' ') : "";
            QString tag = source.mid(ti.pos, ti.len);
            if (!props.attr_fm_resv) {
                tag = cleanOpenTagText(tag);
            }
            if (props.text_fm_resv) {
                post_br = "";
                i = taglist.findCloseTagForOpen(i) - 1; // The next index jump to closing tag directly
            }
            new_text += previousText + pre_br + indent + tag + post_br;
            lastPostBr = post_br.size();
            lvl += props.inner_ind_adj;

        }
        else if (ti.ttype == "end") {
            XhtmlFormatParser::properties props = calcFinalProps(nodePath);
            QString pre_br = previousText.size() > 0 ? QString(props.close_pre_br, '\n') : props.close_pre_br > lastPostBr ? QString(props.close_pre_br - lastPostBr, '\n') : "";
            QString post_br = props.close_post_br > 0 ? QString(props.close_post_br, '\n') : "";
            QString tag = source.mid(ti.pos, ti.len);
            lvl -= props.inner_ind_adj;
            QString indent = props.close_pre_br + lastPostBr == 0 ? "" : indentPara * lvl > 0 ? QString(indentPara * lvl, ' ') : "";

            if (ti.tname == "style") {
                if (Utility::trimmed(previousText,"\n\t ") != "") {
                    QString indent = indentPara * lvl > 0 ? QString(indentPara * lvl, ' ') : "";
                    CSSInfo* cp = new CSSInfo(previousText);
                    QString reformatCss = '\n'+ cp->getReformattedCSSText(!cssfold) +'\n';
                    previousText = Utility::RegExpSub("\n", "\n" + indent, reformatCss);
                }
            }
            if (props.text_fm_resv) {
                previousText = source.mid(lastTagEndPos, ti.pos - lastTagEndPos);
                pre_br = "";
            }
            new_text += previousText + pre_br + indent + tag + post_br;

            nodePath.pop_back();
            lastPostBr = post_br.size();
            lvl -= 1 + props.ind_adj;
            //--lvl;
        }
        else { // ti.ttype = "single" | "xmlheader" | "doctype" | "comment"
            nodePath << ti.tname;
            XhtmlFormatParser::properties props = calcFinalProps(nodePath);
            lvl += 1 + props.ind_adj;
            //++lvl;

            QString pre_br = previousText.size() > 0 ? QString(props.open_pre_br + props.close_pre_br, '\n') : props.open_pre_br + props.close_pre_br > lastPostBr ? QString(props.open_pre_br + props.close_pre_br - lastPostBr, '\n') : "";
            QString post_br = props.open_post_br + props.close_post_br > 0 ? QString(props.open_post_br + props.close_post_br, '\n') : "";
            QString indent = props.open_pre_br + lastPostBr == 0 ? "" : indentPara * lvl > 0 ? QString(indentPara * lvl, ' ') : "";
            QString tag = source.mid(ti.pos, ti.len);
            if (!props.attr_fm_resv) {
                tag = cleanOpenTagText(tag);
            }
            new_text += previousText + pre_br + indent + tag + post_br;

            nodePath.pop_back();
            lastPostBr = post_br.size();
            lvl -= 1 + props.ind_adj;
            //--lvl;
        }
        lastTagEndPos = ti.pos + ti.len;
    } // End of for loop
    return new_text;
}
//---------------------------------------------------------------------