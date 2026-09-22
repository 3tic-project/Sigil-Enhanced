/************************************************************************
**
**  Copyright (C) 2015-2019 Kevin B. Hendricks, Stratford Ontario Canada
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

#pragma once
#ifndef IMAGERESOURCE_H
#define IMAGERESOURCE_H

#include <QByteArray>

#include "ResourceObjects/Resource.h"

class ImageResource : public Resource
{
    Q_OBJECT

public:

    /**
     * Constructor.
     *
     * @param fullfilepath The full path to the file that this
     *                     resource is representing.
     * @param parent The object's parent.
     */
    ImageResource(const QString &mainfolder, const QString &fullfilepath,
                  QObject *parent = NULL);

    QString GetDescription() const;
    // inherited
    ResourceType Type() const override;

    bool LoadFromDisk() override;

    bool isContentModified() const;
    void setContentModified(bool modified);

    // Bytes for an edit that could not replace the extracted file because
    // another handle still has it open. Kept after the book is marked clean
    // so the next full EPUB export can still package the edit. Cleared only
    // once those bytes are on the extracted file itself.
    bool hasPendingPayload() const;
    void setPendingPayload(const QByteArray &bytes);
    void clearPendingPayload();
    bool writePendingPayloadTo(const QString &destination, QString *error = nullptr) const;

    void SaveToDisk(bool book_wide_save = false) override;

private:
    bool m_ContentModified = false;
    QByteArray m_PendingPayload;
};

#endif // IMAGERESOURCE_H
