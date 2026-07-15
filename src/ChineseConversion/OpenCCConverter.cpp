/************************************************************************
**
**  Copyright (C) 2026 Sigil Enhanced contributors
**
**  This file is part of Sigil.
**
*************************************************************************/

#include "ChineseConversion/OpenCCConverter.h"

#include <exception>
#include <string>

#include <QDir>
#include <QFileInfo>

#include <SimpleConverter.hpp>

OpenCCConverter::OpenCCConverter(const ChineseConversionProfile& profile,
                                 const QString& dataDirectory)
{
    const QString configPath = QDir(dataDirectory).filePath(profile.ConfigFile());
    if (!QFileInfo(configPath).isFile()) {
        m_Error = QStringLiteral("OpenCC configuration does not exist: %1").arg(configPath);
        return;
    }

    try {
        m_Converter = std::make_unique<opencc::SimpleConverter>(
            configPath.toUtf8().toStdString());
    } catch (const std::exception& error) {
        m_Error = QString::fromUtf8(error.what());
    }
}

OpenCCConverter::~OpenCCConverter() = default;

bool OpenCCConverter::IsValid() const
{
    return static_cast<bool>(m_Converter);
}

QString OpenCCConverter::ErrorString() const
{
    return m_Error;
}

QString OpenCCConverter::Convert(const QString& input, QString *error) const
{
    if (error) {
        error->clear();
    }
    if (input.isEmpty()) {
        return input;
    }
    if (!m_Converter) {
        if (error) {
            *error = m_Error;
        }
        return input;
    }

    try {
        const QByteArray utf8 = input.toUtf8();
        const std::string converted = m_Converter->Convert(
            std::string(utf8.constData(), static_cast<size_t>(utf8.size())));
        return QString::fromUtf8(converted.data(),
                                 static_cast<qsizetype>(converted.size()));
    } catch (const std::exception& exception) {
        if (error) {
            *error = QString::fromUtf8(exception.what());
        }
        return input;
    }
}
