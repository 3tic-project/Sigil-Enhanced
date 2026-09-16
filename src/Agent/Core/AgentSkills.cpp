/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/Core/AgentSkills.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

#include "Agent/Typeset/TypesetEngine.h"

namespace SigilAgent
{

namespace
{

QString readUtf8File(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QString();
    return QString::fromUtf8(file.readAll());
}

QStringList skillSearchRoots()
{
    QStringList roots;
    roots << QStringLiteral(":/agent/skills");
    if (qApp) {
        const QDir app(QCoreApplication::applicationDirPath());
        roots << app.filePath(QStringLiteral("agent/skills"));
        roots << app.filePath(QStringLiteral("../Resources/agent/skills"));
        roots << app.filePath(QStringLiteral("../share/sigil/agent/skills"));
    }
    roots << QDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath())
                 .filePath(QStringLiteral("../Skills"));
    const QString data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!data.isEmpty()) roots << (data + QStringLiteral("/agent/skills"));
    return roots;
}

QList<AgentSkill> loadFromDirectory(const QString &root)
{
    QList<AgentSkill> skills;
    QDir dir(root);
    if (!dir.exists()) return skills;
    const QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    if (entries.isEmpty()) {
        const QString direct = dir.filePath(QStringLiteral("ln-template-typeset/SKILL.md"));
        const QString markdown = readUtf8File(direct);
        if (!markdown.isEmpty()) skills.append(parseSkillMarkdown(markdown, direct));
        return skills;
    }
    for (const QFileInfo &info : entries) {
        const QString path = info.absoluteFilePath() + QStringLiteral("/SKILL.md");
        const QString markdown = readUtf8File(path);
        if (markdown.isEmpty()) continue;
        skills.append(parseSkillMarkdown(markdown, path));
    }
    return skills;
}

bool alreadyHas(const QList<AgentSkill> &skills, const QString &name)
{
    for (const AgentSkill &skill : skills) {
        if (skill.name == name) return true;
    }
    return false;
}

} // namespace

AgentSkill parseSkillMarkdown(const QString &markdown, const QString &source_path)
{
    AgentSkill skill;
    skill.sourcePath = source_path;
    QString body = markdown;
    if (markdown.startsWith(QStringLiteral("---"))) {
        const int end = markdown.indexOf(QStringLiteral("\n---"), 3);
        if (end > 0) {
            const QString front = markdown.mid(4, end - 3);
            body = markdown.mid(end + 4).trimmed();
            for (const QString &raw_line : front.split(QLatin1Char('\n'))) {
                const QString line = raw_line.trimmed();
                if (line.startsWith(QStringLiteral("name:"))) {
                    skill.name = line.mid(5).trimmed();
                } else if (line.startsWith(QStringLiteral("description:"))) {
                    QString value = line.mid(12).trimmed();
                    if (value == QLatin1String(">") || value == QLatin1String("|")) value.clear();
                    skill.description = value;
                } else if (line.startsWith(QStringLiteral("allowed-tools:"))) {
                    skill.allowedTools = line.mid(14).trimmed();
                    if (skill.allowedTools == QLatin1String(">")) skill.allowedTools.clear();
                } else if (line.startsWith(QLatin1Char(' ')) && skill.description.endsWith(QLatin1Char(' '))) {
                    skill.description += line.trimmed() + QLatin1Char(' ');
                } else if (line.startsWith(QLatin1Char(' ')) && !skill.description.isEmpty()
                           && skill.allowedTools.isEmpty() && !skill.description.contains(QLatin1Char('.'))) {
                    skill.description += line.trimmed() + QLatin1Char(' ');
                } else if (!line.contains(QLatin1Char(':')) && !skill.description.isEmpty()
                           && skill.body.isEmpty()) {
                    skill.description += QLatin1Char(' ') + line;
                }
            }
        }
    }
    skill.body = body.trimmed();
    if (skill.name.isEmpty()) {
        skill.name = QFileInfo(source_path).dir().dirName();
        if (skill.name.isEmpty()) skill.name = QStringLiteral("unnamed-skill");
    }
    skill.description = skill.description.simplified();
    return skill;
}

QList<AgentSkill> loadAgentSkills()
{
    QList<AgentSkill> skills;
    const QString bundled = readUtf8File(QStringLiteral(":/agent/skills/ln-template-typeset/SKILL.md"));
    if (!bundled.isEmpty()) {
        skills.append(parseSkillMarkdown(
            bundled, QStringLiteral(":/agent/skills/ln-template-typeset/SKILL.md")));
    }
    for (const QString &root : skillSearchRoots()) {
        const QList<AgentSkill> loaded = loadFromDirectory(root);
        for (const AgentSkill &skill : loaded) {
            if (alreadyHas(skills, skill.name)) continue;
            skills.append(skill);
        }
        if (root.contains(QLatin1String(":/agent/skills")) && QFile::exists(
                QStringLiteral(":/agent/skills/ln-template-typeset/SKILL.md"))) {
            continue;
        }
    }
    if (!alreadyHas(skills, QStringLiteral("ln-template-typeset"))) {
        AgentSkill fallback;
        fallback.name = QStringLiteral("ln-template-typeset");
        fallback.description = QStringLiteral(
            "Fill the open light-novel template EPUB from a dropped TXT manuscript and images.");
        fallback.body = QStringLiteral(
            "Call manuscript.parse then content.typeset_from_manuscript. "
            "Never paste chapter bodies. Never tell the user to paste in Book View.");
        skills.prepend(fallback);
    }
    return skills;
}

QString skillCatalogPrompt(const QList<AgentSkill> &skills)
{
    if (skills.isEmpty()) return QString();
    QString block = QStringLiteral("Available skills (declarative; no scripts). Use when the description matches:\n");
    for (const AgentSkill &skill : skills) {
        block += QStringLiteral("- %1: %2\n").arg(skill.name, skill.description);
    }
    return block;
}

bool looksLikeLightNovelTemplate(IBookWorkspace *workspace)
{
    if (!workspace) return false;
    const TemplateMap map = detectTemplateMap(workspace);
    return map.detected;
}

QString matchedSkillBodies(const QList<AgentSkill> &skills,
                           const QString &user_text,
                           IBookWorkspace *workspace)
{
    Q_UNUSED(workspace);
    const QString lowered = user_text.toLower();
    const bool keyword = lowered.contains(QStringLiteral("排版"))
        || lowered.contains(QStringLiteral("模板"))
        || lowered.contains(QStringLiteral("typeset"))
        || lowered.contains(QStringLiteral("轻小说"))
        || lowered.contains(QStringLiteral("輕小說"))
        || lowered.contains(QStringLiteral("文稿"))
        || lowered.contains(QStringLiteral("manuscript"))
        || lowered.contains(QLatin1String("ln-template"));
    const bool structure = lowered.contains(QStringLiteral("拆分"))
        || lowered.contains(QStringLiteral("合并"))
        || lowered.contains(QStringLiteral("合併"))
        || lowered.contains(QStringLiteral("spine"))
        || lowered.contains(QStringLiteral("目录"))
        || lowered.contains(QStringLiteral("目錄"))
        || lowered.contains(QStringLiteral("split"))
        || lowered.contains(QStringLiteral("merge"))
        || lowered.contains(QStringLiteral("regex"))
        || lowered.contains(QStringLiteral("正则"))
        || lowered.contains(QStringLiteral("正則"));
    static const QRegularExpression div_word(
        QStringLiteral("\\bdiv\\b"), QRegularExpression::CaseInsensitiveOption);
    const bool paragraphs = lowered.contains(QStringLiteral("伪段落"))
        || lowered.contains(QStringLiteral("偽段落"))
        || lowered.contains(QStringLiteral("段落结构"))
        || lowered.contains(QStringLiteral("段落結構"))
        || lowered.contains(QStringLiteral("paragraph normal"))
        || lowered.contains(QStringLiteral("paragraph structure"))
        || div_word.match(lowered).hasMatch();
    QString block;
    for (const AgentSkill &skill : skills) {
        const bool named = lowered.contains(skill.name.toLower());
        const bool typeset_skill = skill.name == QLatin1String("ln-template-typeset");
        const bool structure_skill = skill.name == QLatin1String("book-structure");
        const bool paragraph_skill = skill.name == QLatin1String("paragraph-normalization");
        if (named || (typeset_skill && keyword) || (structure_skill && structure)
            || (paragraph_skill && paragraphs)) {
            block += QStringLiteral("\n# Skill: %1\n%2\n").arg(skill.name, skill.body);
        }
    }
    return block;
}

} // namespace SigilAgent
