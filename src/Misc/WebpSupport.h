/************************************************************************
**
**  This file is part of Sigil.
**
*************************************************************************/

#pragma once
#ifndef WEBPSUPPORT_H
#define WEBPSUPPORT_H

#include <QByteArray>
#include <QImage>
#include <QString>

/**
 * Helpers for WebP files that some Windows Qt builds refuse to open
 * (extended VP8X + alpha, or Photoshop-private RIFF chunks).
 */
bool IsWebpPayload(const QByteArray &data);
QByteArray SanitizeWebpPayload(const QByteArray &data);
QImage LoadRasterImage(const QString &path, QString *error_out = nullptr);

#endif
