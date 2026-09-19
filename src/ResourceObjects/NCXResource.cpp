/************************************************************************
**
**  Copyright (C) 2016-2020 Kevin B. Hendricks, Stratford, Ontario, Canada
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

#include <QtCore/QBuffer>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QTextStream>
#include <QtXml/QDomDocument>

#include "BookManipulation/CleanSource.h"
#include "Exporters/NCXWriter.h"
#include "ResourceObjects/NCXResource.h"
#include "Misc/SettingsStore.h"
#include "Misc/Utility.h"
#include "sigil_constants.h"

static const QString TEMPLATE_TEXT =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<!DOCTYPE ncx PUBLIC \"-//NISO//DTD ncx 2005-1//EN\"\n"
    "   \"http://www.daisy.org/z3986/2005/ncx-2005-1.dtd\">\n"
    "<ncx xmlns=\"http://www.daisy.org/z3986/2005/ncx/\" version=\"2005-1\">\n"
    "  <head>\n"
    "    <meta name=\"dtb:uid\" content=\"ID_UNKNOWN\" />\n"
    "    <meta name=\"dtb:depth\" content=\"0\" />\n"
    "    <meta name=\"dtb:totalPageCount\" content=\"0\" />\n"
    "    <meta name=\"dtb:maxPageNumber\" content=\"0\" />\n"
    "  </head>\n"
    "<docTitle>\n"
    "  <text>Unknown</text>\n"
    "</docTitle>\n"
    "<navMap>\n"
    "<navPoint id=\"navPoint-1\" playOrder=\"1\">\n"
    "  <navLabel>\n"
    "    <text>%1</text>\n"
    "  </navLabel>\n"
    "  <content src=\"%2\" />\n"
    "</navPoint>\n"
    "</navMap>\n"
    "</ncx>";

static const QRegularExpression NAV_MAP_PATTERN(
    QStringLiteral("<(?:[A-Za-z_][\\w.-]*:)?navMap\\b[^>]*>.*?"
                   "</(?:[A-Za-z_][\\w.-]*:)?navMap\\s*>"),
    QRegularExpression::CaseInsensitiveOption
        | QRegularExpression::DotMatchesEverythingOption);

namespace {

bool HasLocalName(const QDomElement &element, const QString &name)
{
    return element.localName() == name || element.tagName() == name
        || element.tagName().endsWith(QLatin1Char(':') + name);
}

QDomElement FindElement(const QDomNode &parent, const QString &name)
{
    for (QDomNode child = parent.firstChild(); !child.isNull();
         child = child.nextSibling()) {
        if (!child.isElement()) continue;
        const QDomElement element = child.toElement();
        if (HasLocalName(element, name)) return element;
        const QDomElement nested = FindElement(element, name);
        if (!nested.isNull()) return nested;
    }
    return QDomElement();
}

void CollectNavPoints(const QDomNode &parent, const QList<TocNodeId> &preorder,
                      int &position, QHash<TocNodeId, QDomElement> &points)
{
    for (QDomNode child = parent.firstChild(); !child.isNull();
         child = child.nextSibling()) {
        if (!child.isElement()) continue;
        const QDomElement element = child.toElement();
        if (!HasLocalName(element, QStringLiteral("navPoint"))) continue;
        if (position >= preorder.size()) return;
        const TocNodeId id = preorder.at(position++);
        points.insert(id, element);
        CollectNavPoints(element, preorder, position, points);
    }
}

QString NavPointLabel(const QDomElement &point)
{
    const QDomElement label = FindElement(point, QStringLiteral("navLabel"));
    return FindElement(label, QStringLiteral("text")).text().simplified();
}

QString NavPointBookTarget(const QDomElement &point, const NCXResource *resource)
{
    const QDomElement content = FindElement(point, QStringLiteral("content"));
    const QString href = content.attribute(QStringLiteral("src"));
    if (href.contains(QLatin1Char(':'))) return href;
    const QStringList pieces = href.split(QLatin1Char('#'), Qt::KeepEmptyParts);
    const QString path = pieces.value(0);
    const QString fragment = pieces.size() > 1 ? pieces.at(1) : QString();
    QString target;
    if (path == QLatin1String("./") || path.isEmpty()) {
        target = Utility::URLEncodePath(resource->GetRelativePath());
    } else {
        target = Utility::URLEncodePath(Utility::buildBookPath(
            Utility::URLDecodePath(path), resource->GetFolder()));
    }
    if (!fragment.isEmpty()) target += QLatin1Char('#') + fragment;
    return target;
}

void AppendNavPointHierarchy(const TocEditTree &tree, TocNodeId id,
                             QDomNode parent,
                             const QHash<TocNodeId, QDomElement> &points)
{
    QDomElement point = points.value(id);
    parent.appendChild(point);
    for (TocNodeId child : tree.nodes.value(id).children) {
        AppendNavPointHierarchy(tree, child, point, points);
    }
}

}


// under xhtml5 / epub3 no doctype is allowed for the ncx
static const QString TEMPLATE3_TEXT =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<ncx xmlns=\"http://www.daisy.org/z3986/2005/ncx/\" version=\"2005-1\">\n"
    "  <head>\n"
    "    <meta name=\"dtb:uid\" content=\"ID_UNKNOWN\" />\n"
    "    <meta name=\"dtb:depth\" content=\"0\" />\n"
    "    <meta name=\"dtb:totalPageCount\" content=\"0\" />\n"
    "    <meta name=\"dtb:maxPageNumber\" content=\"0\" />\n"
    "  </head>\n"
    "<docTitle>\n"
    "   <text>Unknown</text>\n"
    "</docTitle>\n"
    "<navMap>\n"
    "<navPoint id=\"navPoint-1\" playOrder=\"1\">\n"
    "  <navLabel>\n"
    "    <text>%1</text>\n"
    "  </navLabel>\n"
    "  <content src=\"%2\" />\n"
    "</navPoint>\n"
    "</navMap>\n"
    "</ncx>";


NCXResource::NCXResource(const QString &mainfolder, 
                         const QString &fullfilepath, 
                         const QString & version, 
                         QObject *parent)
    : XMLResource(mainfolder, fullfilepath, parent)
{
    if (!QFileInfo::exists(fullfilepath)) {
        FillWithDefaultText(version, "OEBPS/Text");
    }
}

// a rename of the ncx should only need updating in the opf
// which should happen automagically via signals and slots here
bool NCXResource::RenameTo(const QString &new_filename, bool in_bulk)
{
    Q_UNUSED(in_bulk);
    bool successful = Resource::RenameTo(new_filename);
    return successful;
}


// a move of the ncx should need updating in the ncx and opf
// which should happen automagically via signals and slots here
bool NCXResource::MoveTo(const QString &newbookpath, bool in_bulk)
{
    Q_UNUSED(in_bulk);
    bool successful = Resource::MoveTo(newbookpath);
    return successful;
}


Resource::ResourceType NCXResource::Type() const
{
    return Resource::NCXResourceType;
}


void NCXResource::SetMainID(const QString &main_id)
{
    SetText(GetText().replace("ID_UNKNOWN", main_id));
}


bool NCXResource::GenerateNCXFromBookContents(const Book *book)
{
    bool is_changed = false;
    QByteArray raw_ncx;
    QBuffer buffer(&raw_ncx);
    buffer.open(QIODevice::WriteOnly);
    NCXWriter ncx(book, buffer);
    ncx.WriteXMLFromHeadings();
    buffer.close();
    QString new_text = CleanSource::ProcessXML(QString::fromUtf8(raw_ncx.constData(), raw_ncx.size()),"application/x-dtbncx+xml");
    QString existing_text = GetText();

    // Only update the resource if have changed. Note that this is_changed trick will not
    // work after first loading an EPUB, because the metadata elements have their attributes
    // in swapped in a different order from the xhtml processing. 
    if (new_text != existing_text) {
        SetText(new_text);
        is_changed = true;
    }

    return is_changed;
}


void NCXResource::GenerateNCXFromTOCContents(const Book *book, TOCModel *toc_model)
{
    GenerateNCXFromTOCEntries(book, toc_model->GetRootTOCEntry());
}

void NCXResource::GenerateNCXFromTOCEntries(const Book *book, TOCModel::TOCEntry toc_root_entry)
{
    QByteArray raw_ncx;
    QBuffer buffer(&raw_ncx);
    buffer.open(QIODevice::WriteOnly);
    NCXWriter ncx(book, buffer, toc_root_entry);
    ncx.WriteXML();
    buffer.close();
    const QString generated = CleanSource::ProcessXML(
        QString::fromUtf8(raw_ncx.constData(), raw_ncx.size()),
        "application/x-dtbncx+xml");
    QString updated = GetText();
    const QRegularExpressionMatch existingMap = NAV_MAP_PATTERN.match(updated);
    const QRegularExpressionMatch generatedMap = NAV_MAP_PATTERN.match(generated);
    if (existingMap.hasMatch() && generatedMap.hasMatch()) {
        updated.replace(existingMap.capturedStart(), existingMap.capturedLength(),
                        generatedMap.captured());
    } else {
        updated = generated;
    }
    if (updated != GetText()) SetText(updated);
}

bool NCXResource::ReparentNCX(const TocEditTree &before,
                              const TocEditTree &after,
                              bool undoable)
{
    if (!TocTreeTransform::Validate(before) || !TocTreeTransform::Validate(after)
            || before.rootId != after.rootId
            || before.nodes.size() != after.nodes.size()) return false;
    for (auto it = before.nodes.cbegin(); it != before.nodes.cend(); ++it) {
        if (!after.nodes.contains(it.key())
                || it.value().label != after.nodes.value(it.key()).label
                || it.value().target != after.nodes.value(it.key()).target) {
            return false;
        }
    }

    const QString source = GetText();
    QDomDocument document;
    if (!document.setContent(source, true)) return false;
    QDomElement navMap = FindElement(document, QStringLiteral("navMap"));
    if (navMap.isNull()) return false;

    const QList<TocNodeId> preorder = TocTreeTransform::PreorderIds(before);
    QHash<TocNodeId, QDomElement> points;
    int position = 0;
    CollectNavPoints(navMap, preorder, position, points);
    if (position != preorder.size() || points.size() != preorder.size()) {
        return false;
    }
    for (TocNodeId id : preorder) {
        const QDomElement point = points.value(id);
        const TocEditNode node = before.nodes.value(id);
        if (NavPointLabel(point) != node.label
                || NavPointBookTarget(point, this) != node.target) return false;
    }
    if (TocTreeTransform::Equal(before, after)) return true;

    for (TocNodeId id : preorder) {
        QDomElement point = points.value(id);
        point.parentNode().removeChild(point);
    }
    for (TocNodeId id : after.nodes.value(after.rootId).children) {
        AppendNavPointHierarchy(after, id, navMap, points);
    }
    const QList<TocNodeId> afterPreorder = TocTreeTransform::PreorderIds(after);
    for (int index = 0; index < afterPreorder.size(); ++index) {
        points.value(afterPreorder.at(index)).setAttribute(
            QStringLiteral("playOrder"), QString::number(index + 1));
    }

    QString rewrittenMap;
    QTextStream stream(&rewrittenMap);
    navMap.save(stream, 2);
    const QRegularExpressionMatch match = NAV_MAP_PATTERN.match(source);
    if (!match.hasMatch() || rewrittenMap.isEmpty()) return false;
    QString updated = source;
    updated.replace(match.capturedStart(), match.capturedLength(), rewrittenMap);
    if (updated != source) {
        if (undoable) SetTextAsUndoableEdit(updated);
        else SetText(updated);
    }
    return true;
}


void NCXResource::FillWithDefaultText(const QString &version, const QString &default_text_folder)
{
    QString first_section_bookpath = FIRST_SECTION_NAME;
    if (!default_text_folder.isEmpty()) first_section_bookpath = default_text_folder + "/" + FIRST_SECTION_NAME;
    FillWithDefaultTextToBookPath(version, first_section_bookpath);
    SaveToDisk();
}


void NCXResource::FillWithDefaultTextToBookPath(const QString &version, const QString &start_bookpath)
{
    QString epubversion = version;
    if (epubversion.isEmpty()) {
        SettingsStore ss;
        epubversion = ss.defaultVersion();
    }
    QString ncxbookpath = GetRelativePath();
    QString texthref = Utility::URLEncodePath(Utility::buildRelativePath(ncxbookpath, start_bookpath));
    if (epubversion.startsWith('2')) {
        SetText(TEMPLATE_TEXT.arg(tr("Start")).arg(texthref));
    } else {
        SetText(TEMPLATE3_TEXT.arg(tr("Start")).arg(texthref));
    }
    // Make sure the file exists on disk.
    // Among many reasons, this also solves the problem
    // with the Book Browser not displaying an icon for this resource.
    SaveToDisk();
}
