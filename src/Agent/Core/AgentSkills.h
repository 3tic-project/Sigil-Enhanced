/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef SIGIL_AGENT_SKILLS_H
#define SIGIL_AGENT_SKILLS_H

#include <QJsonArray>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include "Agent/Execution/IBookWorkspace.h"

namespace SigilAgent
{

struct AgentSkill {
    QString name;
    QString description;
    QString body;
    QString allowedTools;
    QString sourcePath;
    QString version = QStringLiteral("1");
    QStringList toolNames;
    QStringList requiredCapabilities;
    QStringList triggers;
    QMap<QString, QString> metadata;
    bool requiresBook = false;
    bool autoTrigger = true;
    QString metadataError;
};

struct AgentSkillSelection {
    QList<AgentSkill> availableSkills;
    QString bodies;
    QJsonArray decisions;
};

AgentSkill parseSkillMarkdown(const QString &markdown, const QString &source_path = QString());
QList<AgentSkill> loadAgentSkills();
AgentSkillSelection selectAgentSkills(const QList<AgentSkill> &skills,
                                     const QString &user_text,
                                     IBookWorkspace *workspace,
                                     const QStringList &available_tools);
QString skillCatalogPrompt(const QList<AgentSkill> &skills);
QString matchedSkillBodies(const QList<AgentSkill> &skills,
                           const QString &user_text,
                           IBookWorkspace *workspace);
bool looksLikeLightNovelTemplate(IBookWorkspace *workspace);

} // namespace SigilAgent

#endif
