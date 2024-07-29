
#include "EmbedPython/EmbeddedPython.h"
#include <QDir>
#include <QRegExp>
#include <QFile>
#include <QTextCodec>

#include "Misc/Utility.h"
#include "sigil_exception.h"
#include "PCRE2/PCRECache.h"

//modified: Prettify xhtml,Regexp, re_sub
QString Utility::RegExpSub(const QString& regexp, const QString& alt_pattern, const QString& text, int max_count) {

    QRegExp re(regexp);
    QString new_text = "";
    int index = re.indexIn(text);
    int count = 0;
    int offset = 0;
    while (index > -1) {
        if (max_count > 0 && count == max_count) {
            break;
        }
        ++count;
        new_text += text.mid(offset, index - offset);
        offset = index + re.cap(0).size();

        QString alt_text = "";
        bool backslash = false;
        foreach(QChar ch, alt_pattern) {
            if (ch == '\\') {
                backslash = true;
                continue;
            }
            if (backslash) {
                backslash = false;
                if (48 <= ch.unicode() && 57 >= ch.unicode()) {
                    int group_num = ch.unicode() - 48;
                    if (group_num <= re.captureCount()) {
                        alt_text += re.cap(group_num);
                        continue;
                    }
                }
                alt_text.append("\\");
            }
            alt_text.append(ch);
        }
        new_text += alt_text;
        index = re.indexIn(text, offset);
    }
    new_text += text.mid(offset);
    return new_text;
}

//modified: trimmed: trim the specific chars on text's head and tail
QString Utility::trimmed(const QString& text, const QString& chars) {
    int i, j = 0;
    QChar ch;
    for (i = 0; i < text.size(); ++i) {
        ch = text.at(i);
        if (!chars.contains(ch)) break;
    }
    for (j = text.size() - 1; j >= 0; --j) {
        ch = text.at(j);
        if (!chars.contains(ch)) {
            ++j; break;
        }
    }
    return text.mid(i, j - i);
}

//modified: used by correctOPF、walk direct files
QStringList Utility::walkDirs(QString root) {
    QDir* dirinfo = new QDir(root);
    if (!dirinfo->exists()) {
        return QStringList();
    }
    QStringList fileList, dirStack;
    dirStack.append(root);
    while (!dirStack.isEmpty()) {
        QString curDir = dirStack.takeLast();
        dirinfo = new QDir(curDir);
        foreach(QString filepath, dirinfo->entryList(QDir::Files)) {
            fileList.append(dirinfo->absoluteFilePath(filepath));
        }
        foreach(QString dir, dirinfo->entryList(QDir::Dirs, QDir::Reversed)) {
            if (dir == "." || dir == "..") continue;
            dirStack.append(dirinfo->absoluteFilePath(dir));
        }
    }
    return fileList;
}

//modified: getMtypeFromExt
QString Utility::ExtToMTypeMap(QString& ext)
{
    QHash<QString, QString> ExtToMType;
    // default to using the preferred media-types ffrom the epub 3.2 spec
    // https://www.w3.org/publishing/epub3/epub-spec.html#sec-cmt-supported
    ExtToMType["bm"] = "image/bmp";
    ExtToMType["bmp"] = "image/bmp";
    ExtToMType["css"] = "text/css";
    ExtToMType["epub"] = "application/epub+zip";
    ExtToMType["gif"] = "image/gif";
    ExtToMType["htm"] = "application/xhtml+xml";
    ExtToMType["html"] = "application/xhtml+xml";
    ExtToMType["jpeg"] = "image/jpeg";
    ExtToMType["jpg"] = "image/jpeg";
    ExtToMType["js"] = "application/javascript";
    ExtToMType["m4a"] = "audio/mp4";
    ExtToMType["m4v"] = "video/mp4";
    ExtToMType["mp3"] = "audio/mpeg";
    ExtToMType["mp4"] = "video/mp4";
    ExtToMType["ncx"] = "application/x-dtbncx+xml";
    ExtToMType["oga"] = "audio/ogg";
    ExtToMType["ogg"] = "audio/ogg";
    ExtToMType["ogv"] = "video/ogg";
    ExtToMType["opf"] = "application/oebps-package+xml";
    ExtToMType["opus"] = "audio/opus";
    ExtToMType["otf"] = "font/otf";
    ExtToMType["pls"] = "application/pls+xml";
    ExtToMType["png"] = "image/png";
    ExtToMType["smil"] = "application/smil+xml";
    ExtToMType["svg"] = "image/svg+xml";
    ExtToMType["tif"] = "image/tiff";
    ExtToMType["tiff"] = "image/tiff";
    ExtToMType["ttc"] = "font/collection";
    ExtToMType["ttf"] = "font/ttf";
    ExtToMType["ttml"] = "application/ttml+xml";
    ExtToMType["txt"] = "text/plain";
    ExtToMType["vtt"] = "text/vtt";
    ExtToMType["webm"] = "video/webm";
    ExtToMType["webp"] = "image/webp";
    ExtToMType["woff"] = "font/woff";
    ExtToMType["woff2"] = "font/woff2";
    ExtToMType["xhtml"] = "application/xhtml+xml";
    ExtToMType["xml"] = "application/oebps-page-map+xml";
    ExtToMType["xpgt"] = "application/vnd.adobe-page-template+xml";

    if (!ExtToMType.contains(ext)) {
        return "";
    }
    return ExtToMType[ext];
}

//modified：StringTrimmedIndex
/*
 * This function is used to obtain the indexes of non whitespace characters
 * at the begining and the end of a string. It returnsa pair of int indexes.
 */
Utility::TrimmedIndex Utility::StringTrimmedIndex(const QString& text) {

    if (text.size() == 0) {
        return { 0,0 };
    }
    /**
     * s_index : Starting position of the non whitespace characters at front end of the string.
     * e_index : Cutoff position of the non whitespace characters at back end of the string.
     * If the return values s_index and e_index are both 0, it means the string is empt.
     * If s_index is equal to e_index when returned, it means the string is filled with pure
     * whitespace characters.
     */
    int s_index = 0, e_index = text.size();
    for (int i = 0; i < text.size(); i++) {
        // 0x20 WhiteSpace     0x9 Tab\t
        if (text[i] == QChar(0x20) || text[i] == QChar(0x9)) {
            ++s_index;
            continue;
        }
        break;
    }
    for (int i = text.size(); i >= s_index; i--) {
        if (i == s_index) {
            e_index = s_index;
            break;
        }
        if (text[i - 1] == QChar(0x20) || text[i - 1] == QChar(0x9)) {
            --e_index;
            continue;
        }
        break;
    }
    return { s_index, e_index };
}

//modified: importing txt
QString Utility::ReadUnicodeTextFile_M(const QString& fullfilepath)
{
    QFile file(fullfilepath);

    // Check if we can open the file
    if (!file.open(QFile::ReadOnly)) {
        std::string msg = fullfilepath.toStdString() + ": " + file.errorString().toStdString();
        throw(CannotOpenFile(msg));
    }

    int rv = 0;
    QString error_traceback;
    QList<QVariant> args;
    args.append(QVariant(fullfilepath));
    EmbeddedPython* epython = EmbeddedPython::instance();

    QVariant res = epython->runInPython(QString("importtxt"),
                                        QString("read_unicode"),
                                        args,
                                        &rv,
                                        error_traceback);
    if (rv != 0) {
        Utility::DisplayStdWarningDialog(QString("error in importtxt read_unicode: ") + QString::number(rv),
            error_traceback);
        // an error happened - make no changes
        return QString();
    }
    return ConvertLineEndingsAndNormalize(res.toString());
}
//----------------------------------------------------------------------------------

//modified: FindReplacePlus
QList<std::pair<int, int>> Utility::GetPreSearchMatchInfos(const QString& presearch_regex, const QString& text)
{
    if (presearch_regex.isEmpty()) return QList<std::pair<int, int>>();

    SPCRE* spcre_pre = PCRECache::instance()->getObject(presearch_regex);
    SPCRE::MatchInfo pre_m_info;
    QList<std::pair<int, int>> match_infos;

    int pre_start = 0,
        pre_end = text.length();

    while (pre_start < pre_end) {
        QString pre_text = text.mid(pre_start, pre_end - pre_start);
        pre_m_info = spcre_pre->getFirstMatchInfo(pre_text);

        if (pre_m_info.offset.first == -1) break;

        int _start,_end;
        int match_start = pre_start,
            match_end = pre_start;
        if (pre_m_info.capture_groups_offsets.count() >= 2) {
            std::pair<int, int> g_offset = pre_m_info.capture_groups_offsets.at(1);
            _start = pre_m_info.offset.first + g_offset.first;
            _end = pre_m_info.offset.first + g_offset.second;
        }
        else {
            _start = pre_m_info.offset.first;
            _end = pre_m_info.offset.second;
        }
        match_start += _start;
        match_end += _end;
        pre_start = match_end;
        match_infos << std::pair<int, int>{match_start, match_end};
    }
    return match_infos;
}

//modified: FindReplacePlus
QList<Utility::MatchInfo> Utility::GetSearchInfoWithPreSearch(const QString& presearch, const QString& search, const QString& text)
{
    if (search.isEmpty()) return QList<Utility::MatchInfo>();

    if (presearch.isEmpty()) {
        SPCRE* spcre = PCRECache::instance()->getObject(search);
        QList<Utility::MatchInfo> match_infos;
        Utility::MatchInfo alt_info;
        foreach(SPCRE::MatchInfo info, spcre->getEveryMatchInfo(text)) {
            alt_info.offset = info.offset;
            alt_info.capture_groups_offsets = info.capture_groups_offsets;
            match_infos << alt_info;
        }
        return match_infos;
    }
    else {
        QList<std::pair<int, int>> pre_offset_infos = Utility::GetPreSearchMatchInfos(presearch, text);
        if (pre_offset_infos.isEmpty()) return QList<Utility::MatchInfo>();

        SPCRE* spcre = PCRECache::instance()->getObject(search);
        QList<Utility::MatchInfo> match_infos;
        for (int i = 0; i < pre_offset_infos.count(); i++) {
            std::pair<int, int> offset_info = pre_offset_infos.at(i);
            int sub_start = offset_info.first,
                sub_end = offset_info.second;
            QString sub_text = text.mid(sub_start, sub_end - sub_start);
            Utility::MatchInfo sub_info;
            foreach(SPCRE::MatchInfo info, spcre->getEveryMatchInfo(sub_text)) {
                sub_info.offset.first = info.offset.first + sub_start;
                sub_info.offset.second = info.offset.second + sub_start;
                sub_info.capture_groups_offsets = info.capture_groups_offsets;
                match_infos << sub_info;
            }
        }
        return match_infos;
    }
}

//modified: PCREReplace
QString Utility::HalfWidthChars2FullWidthChars(const QString& text)
{
    if (text.isEmpty()) return QString();
    QString converted_text;
    for (int i = 0; i < text.length(); i++) {
        QChar c = text.at(i);
        if (c.unicode() == 0x20) { // space
            converted_text += QChar(0x3000);
        }
        else if (c.unicode() < 0x7F) { // ascii
            converted_text += QChar(c.unicode() + 0xFEE0);
        }
        else {
            converted_text += c;
        }
    }
    return converted_text;
}

//modified: PCREReplace
QString Utility::FullWidthChars2HalfWidthChars(const QString& text)
{
    if (text.isEmpty()) return QString();
    QString converted_text;
    for (int i = 0; i < text.length(); i++) {
        QChar c = text.at(i);
        if (c.unicode() >= 0xFF01 && c.unicode() <= 0xFF5D) {
            converted_text += QChar(c.unicode() - 0xFEE0);
        }
        else if (c.unicode() == 0x3000) {
            converted_text += QChar(0x20);
        }
        else {
            converted_text += c;
        }
    }
    return converted_text;
}
