/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef SIGIL_AGENT_BOOK_EDITS_H
#define SIGIL_AGENT_BOOK_EDITS_H

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "Agent/Execution/IBookWorkspace.h"

namespace SigilAgent
{

BookOpResult stageWorkingReplace(IBookWorkspace *workspace, const QString &resource_id, const QString &text);
BookOpResult replaceBody(IBookWorkspace *workspace, const QString &resource_id,
                         const QString &inner, const QString &source_id);
BookOpResult insertHtml(IBookWorkspace *workspace, const QString &resource_id,
                        const QString &anchor, const QString &html, bool before);
BookOpResult wrapInResource(IBookWorkspace *workspace, const QString &resource_id,
                            const QString &pattern, const QString &open, const QString &close, int max_hits);
BookOpResult regexReplaceInBook(IBookWorkspace *workspace, const QString &pattern,
                                const QString &replacement, const QString &resource_id, int max_hits);
QJsonObject regexSearchInBook(IBookWorkspace *workspace, const QString &pattern,
                              const QString &resource_id, int max_hits);
BookOpResult splitResourceByHeading(IBookWorkspace *workspace, const QString &resource_id,
                                    const QString &heading_pattern);
BookOpResult mergeResources(IBookWorkspace *workspace, const QStringList &resource_ids, bool delete_sources);
BookOpResult insertImageTag(IBookWorkspace *workspace, const QString &page_id,
                            const QString &image_id, const QString &anchor, bool before,
                            const QString &klass, const QString &alt);
BookOpResult wrapPlainResource(IBookWorkspace *workspace, const QString &source_id,
                               const QString &target_id, const QJsonObject &rules);
BookOpResult generateTocFromHeadings(IBookWorkspace *workspace, const QString &heading_pattern);
QJsonObject inspectBook(IBookWorkspace *workspace);

} // namespace SigilAgent

#endif
