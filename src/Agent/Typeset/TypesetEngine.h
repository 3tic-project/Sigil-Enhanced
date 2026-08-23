/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef SIGIL_AGENT_TYPESET_ENGINE_H
#define SIGIL_AGENT_TYPESET_ENGINE_H

#include <QJsonObject>
#include <QList>
#include <QString>

#include "Agent/Execution/IBookWorkspace.h"
#include "Agent/Typeset/ManuscriptParser.h"

namespace SigilAgent
{

struct TypesetOptions {
    QString manuscriptId;
    bool retireSource = true;
    bool updateMetadata = true;
};

struct TemplatePage {
    QString resourceId;
    QString bookPath;
    QString role;
    int number = 0;
};

struct TemplateMap {
    bool detected = false;
    TemplatePage cover;
    TemplatePage start;
    TemplatePage title;
    TemplatePage credits;
    TemplatePage synopsis;
    TemplatePage contents;
    QList<TemplatePage> illustrations;
    QList<TemplatePage> chapters;
};

QString templatePageRole(const QString &book_path);
TemplateMap detectTemplateMap(IBookWorkspace *workspace);
QString findManuscriptResourceId(IBookWorkspace *workspace, const QString &explicit_id = QString());
QJsonObject parseManuscriptInBook(IBookWorkspace *workspace, const QString &manuscript_id = QString());
BookOpResult fillTemplateSection(IBookWorkspace *workspace,
                                 const QString &resource_id,
                                 const QString &role,
                                 const QString &manuscript_id,
                                 int chapter_index,
                                 const QString &image_name);
BookOpResult typesetFromManuscript(IBookWorkspace *workspace, const TypesetOptions &options);

} // namespace SigilAgent

#endif
