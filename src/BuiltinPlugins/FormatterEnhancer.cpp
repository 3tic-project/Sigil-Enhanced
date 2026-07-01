/************************************************************************
**
**  Copyright (C) 2026 3TIC-Project
**
**  This file is part of Sigil-Enhanced.
**
**  Sigil-Enhanced is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  (at your option) any later version.
**
*************************************************************************/

#include "BuiltinPlugins/FormatterEnhancer.h"

#include <QElapsedTimer>
#include <QWriteLocker>

#include "BookManipulation/Book.h"
#include "BookManipulation/CleanSource.h"
#include "BookManipulation/FolderKeeper.h"
#include "BookManipulation/XhtmlDoc.h"
#include "Misc/SettingsStoreExtend.h"
#include "Parsers/qCSSParser.h"
#include "Parsers/XhtmlFormatParser.h"
#include "ResourceObjects/CSSResource.h"
#include "ResourceObjects/HTMLResource.h"

namespace BuiltinPlugins
{

namespace
{

QString joinParserMessages(const QStringList& messages)
{
    return messages.join(QStringLiteral("; "));
}

}

FormatterEnhancer::FormatterEnhancer(Book* book)
    :
    m_Book(book)
{
}

FormatterEnhancer::Result FormatterEnhancer::format()
{
    return format(Options());
}

FormatterEnhancer::FormatResult FormatterEnhancer::formatXhtmlText(const QString& source,
                                                                   const QString& epubVersion,
                                                                   const QString& xhtmlFormatConfig)
{
    FormatResult result;

    const XhtmlDoc::WellFormedError error =
        XhtmlDoc::WellFormedErrorForSource(source, epubVersion);
    if (error.line != -1) {
        result.messages << QStringLiteral("XHTML 格式化：文件不是 well-formed XML，已跳过。%1:%2 %3")
                               .arg(error.line)
                               .arg(error.column)
                               .arg(error.message);
        return result;
    }

    XhtmlFormatParser parser(xhtmlFormatConfig);
    result.text = CleanSource::PrettifyXhtml(source, parser);
    result.ok = true;
    result.changed = result.text != source;
    return result;
}

FormatterEnhancer::FormatResult FormatterEnhancer::formatCssText(const QString& source,
                                                                 bool multipleLineFormat)
{
    FormatResult result;

    CSSParser parser;
    parser.set_level(QStringLiteral("CSS3.0"));
    QString css = source;
    parser.parse_css(css);

    const QVector<QString> parse_errors = parser.get_parse_errors();
    if (!parse_errors.isEmpty()) {
        foreach(const QString& error, parse_errors) {
            result.messages << QStringLiteral("CSS 格式化：解析错误：%1").arg(error);
        }
        return result;
    }

    result.text = parser.serialize_css(false, multipleLineFormat);
    result.ok = true;
    result.changed = result.text != source;
    return result;
}

void FormatterEnhancer::addResult(Result& result,
                                  ValidationResult::ResType type,
                                  const QString& bookpath,
                                  const QString& message) const
{
    result.validationResults << ValidationResult(type, bookpath, -1, -1, message);
    if (type == ValidationResult::ResType_Error) {
        result.errorCount++;
    }
}

FormatterEnhancer::Result FormatterEnhancer::format(const Options& options)
{
    Result result;
    QElapsedTimer timer;
    timer.start();

    if (!m_Book || !m_Book->GetFolderKeeper()) {
        addResult(result, ValidationResult::ResType_Error, QString(),
                  QStringLiteral("增强格式化：当前没有可处理的 EPUB。"));
        result.elapsedMs = timer.elapsed();
        return result;
    }

    SettingsStoreExtend settings;
    const QString xhtml_format_config = settings.getXhtmlFormat();

    if (options.formatXhtml) {
        const QList<HTMLResource*> html_resources =
            m_Book->GetFolderKeeper()->GetResourceTypeList<HTMLResource>(true);
        foreach(HTMLResource* resource, html_resources) {
            if (!resource) {
                continue;
            }

            result.xhtmlResourcesChecked++;
            resource->InitialLoad();
            const QString bookpath = resource->GetRelativePath();
            const QString source = resource->GetText();
            const FormatResult format_result =
                formatXhtmlText(source, resource->GetEpubVersion(), xhtml_format_config);

            if (!format_result.ok) {
                result.resourcesSkipped++;
                addResult(result, ValidationResult::ResType_Warn, bookpath,
                          joinParserMessages(format_result.messages));
                continue;
            }

            if (!format_result.changed) {
                addResult(result, ValidationResult::ResType_Info, bookpath,
                          QStringLiteral("增强格式化：XHTML 已符合当前格式化配置，未修改。"));
                continue;
            }

            result.resourcesChanged++;
            if (!options.dryRun) {
                result.modified = true;
            }
            addResult(result, ValidationResult::ResType_Info, bookpath,
                      options.dryRun ?
                          QStringLiteral("增强格式化：XHTML 将按当前配置重排。") :
                          QStringLiteral("增强格式化：XHTML 已按当前配置重排。"));

            if (!options.dryRun) {
                QWriteLocker locker(&resource->GetLock());
                resource->SetText(format_result.text);
            }
        }
    }

    if (options.formatCss) {
        const QList<CSSResource*> css_resources =
            m_Book->GetFolderKeeper()->GetResourceTypeList<CSSResource>(true);
        foreach(CSSResource* resource, css_resources) {
            if (!resource) {
                continue;
            }

            result.cssResourcesChecked++;
            resource->InitialLoad();
            const QString bookpath = resource->GetRelativePath();
            const QString source = resource->GetText();
            const FormatResult format_result =
                formatCssText(source, options.cssMultipleLineFormat);

            if (!format_result.ok) {
                result.resourcesSkipped++;
                addResult(result, ValidationResult::ResType_Warn, bookpath,
                          joinParserMessages(format_result.messages));
                continue;
            }

            if (!format_result.changed) {
                addResult(result, ValidationResult::ResType_Info, bookpath,
                          QStringLiteral("增强格式化：CSS 已符合当前格式化配置，未修改。"));
                continue;
            }

            result.resourcesChanged++;
            if (!options.dryRun) {
                result.modified = true;
            }
            addResult(result, ValidationResult::ResType_Info, bookpath,
                      options.dryRun ?
                          QStringLiteral("增强格式化：CSS 将按 readable profile 重排。") :
                          QStringLiteral("增强格式化：CSS 已按 readable profile 重排。"));

            if (!options.dryRun) {
                QWriteLocker locker(&resource->GetLock());
                resource->SetText(format_result.text);
            }
        }
    }

    result.elapsedMs = timer.elapsed();
    addResult(result, ValidationResult::ResType_Info, QString(),
              QStringLiteral("增强格式化：处理完成。XHTML %1 个，CSS %2 个，修改 %3 个，跳过 %4 个，用时 %5 ms。")
                  .arg(result.xhtmlResourcesChecked)
                  .arg(result.cssResourcesChecked)
                  .arg(result.resourcesChanged)
                  .arg(result.resourcesSkipped)
                  .arg(result.elapsedMs));

    return result;
}

}
