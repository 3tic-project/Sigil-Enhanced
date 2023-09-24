
#include "EmbedPython/EmbeddedPython.h"
#include <QDir>
#include <QRegExp>
#include <QFile>
#include <QTextCodec>

#include "Misc/Utility.h"
#include "sigil_exception.h"


// --------------- modified: Prettify xhtml,Regexp, re_sub ---------------------------
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
//-------------------------------------------------------------------------

//---------modified: trimmed: trim the specific chars on text's head and tail----------
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
//----------------------------------------------------------------------------

//------------modified: used by correctOPF、walk direct files----------------------------------
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
//--------------------------------------------------------------------------------------------

//---------modified: getMtypeFromExt----------------------------------------------------------
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
//--------------------------------------------------------------------------------------------

//------------------------modified：StringTrimmedIndex-----------------------------------
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
//----------------------------------------------------------------------------------------

//------------------------------ modified: importing txt ---------------------------
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
    return ConvertLineEndings(res.toString());
}
//----------------------------------------------------------------------------------