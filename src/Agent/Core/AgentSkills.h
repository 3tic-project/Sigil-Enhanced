/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef SIGIL_AGENT_SKILLS_H
#define SIGIL_AGENT_SKILLS_H

#include <QList>
#include <QString>

#include "Agent/Execution/IBookWorkspace.h"

namespace SigilAgent
{

struct AgentSkill {
    QString name;
    QString description;
    QString body;
    QString allowedTools;
    QString sourcePath;
};

AgentSkill parseSkillMarkdown(const QString &markdown, const QString &source_path = QString());
QList<AgentSkill> loadAgentSkills();
QString skillCatalogPrompt(const QList<AgentSkill> &skills);
QString matchedSkillBodies(const QList<AgentSkill> &skills,
                           const QString &user_text,
                           IBookWorkspace *workspace);
bool looksLikeLightNovelTemplate(IBookWorkspace *workspace);

} // namespace SigilAgent

#endif
