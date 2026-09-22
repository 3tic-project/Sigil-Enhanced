/************************************************************************
**
**  Copyright (C) 2015-2026 Kevin B. Hendricks, Stratford Ontario Canada
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

#include <QFileInfo>
#include <QImage>
#include "Misc/AtomicFileWrite.h"
#include "Misc/Utility.h"
#include "ResourceObjects/ImageResource.h"

ImageResource::ImageResource(const QString &mainfolder, const QString &fullfilepath,
                             QObject *parent)
    :
    Resource(mainfolder, fullfilepath, parent)
{
}


Resource::ResourceType ImageResource::Type() const
{
    return Resource::ImageResourceType;
}

bool ImageResource::LoadFromDisk()
{
    emit ResourceUpdatedOnDisk();
    return true;
}

bool ImageResource::isContentModified() const
{
    return m_ContentModified;
}

void ImageResource::setContentModified(bool modified)
{
    m_ContentModified = modified;
}

bool ImageResource::hasPendingPayload() const
{
    return !m_PendingPayload.isEmpty();
}

void ImageResource::setPendingPayload(const QByteArray &bytes)
{
    m_PendingPayload = bytes;
}

void ImageResource::clearPendingPayload()
{
    m_PendingPayload.clear();
}

bool ImageResource::writePendingPayloadTo(const QString &destination, QString *error) const
{
    if (m_PendingPayload.isEmpty()) {
        if (error) {
            *error = tr("No image payload is waiting to be written.");
        }
        return false;
    }
    return AtomicFile::WriteBytesReplacing(destination, m_PendingPayload, error);
}

void ImageResource::SaveToDisk(bool book_wide_save)
{
    if (!m_PendingPayload.isEmpty()) {
        QString error;
        if (AtomicFile::WriteBytesReplacing(GetFullPath(), m_PendingPayload, &error)) {
            const QFileInfo saved(GetFullPath());
            if (saved.size() == static_cast<qint64>(m_PendingPayload.size())) {
                m_PendingPayload.clear();
            }
        }
    }
    Resource::SaveToDisk(book_wide_save);
}

QString ImageResource::GetDescription() const
{
    const QString path = GetFullPath();
    // Drop the file mapping before this function does anything else.
    const QImage img = QImage(path).copy();
    QString colors_shades = img.isGrayscale() ? tr("shades") : tr("colors");
    QString grayscale_color = img.isGrayscale() ? tr("Grayscale") : tr("Color");
    QString colorsInfo = "";
    if (img.depth() == 32) {
        colorsInfo = QString(" %1bpp").arg(img.bitPlaneCount());
    } else if (img.depth() > 0) {
        colorsInfo = QString(" %1bpp (%2 %3)").arg(img.bitPlaneCount()).arg(img.colorCount()).arg(colors_shades);
    }
    QString description = QString("(%1px × %2px) %3%4").arg(img.width()).arg(img.height()).arg(grayscale_color).arg(colorsInfo);
    return description;
}
