/************************************************************************
**
**  Copyright (C) 2026 Sigil Enhanced contributors
**
**  This file is part of Sigil.
**
*************************************************************************/

#pragma once

#include <memory>

#include <QString>

#include "ChineseConversion/ChineseConversionProfile.h"

namespace opencc {
class SimpleConverter;
}

class OpenCCConverter final
{
public:
    OpenCCConverter(const ChineseConversionProfile& profile,
                    const QString& dataDirectory);
    ~OpenCCConverter();

    OpenCCConverter(const OpenCCConverter&) = delete;
    OpenCCConverter& operator=(const OpenCCConverter&) = delete;

    bool IsValid() const;
    QString ErrorString() const;
    QString Convert(const QString& input, QString *error = nullptr) const;

private:
    std::unique_ptr<opencc::SimpleConverter> m_Converter;
    QString m_Error;
};
