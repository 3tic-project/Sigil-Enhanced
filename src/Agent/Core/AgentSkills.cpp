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
#include <QJsonArray>
#include <QJsonDocument>
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

QString scalarValue(const QString &text, QString *error)
{
    const QString value = text.trimmed();
    if (value.startsWith(QLatin1Char('"'))) {
        const QJsonDocument document = QJsonDocument::fromJson(
            (QLatin1Char('[') + value + QLatin1Char(']')).toUtf8());
        if (document.isArray() && document.array().size() == 1
            && document.array().first().isString()) {
            return document.array().first().toString();
        }
        *error = QStringLiteral("Invalid quoted scalar");
        return QString();
    }
    if (value.startsWith(QLatin1Char('\''))) {
        if (value.size() < 2 || !value.endsWith(QLatin1Char('\''))) {
            *error = QStringLiteral("Invalid quoted scalar");
            return QString();
        }
        QString unquoted = value.mid(1, value.size() - 2);
        unquoted.replace(QStringLiteral("''"), QStringLiteral("'"));
        return unquoted;
    }
    return value;
}

QStringList listValue(const QString &text, bool split_words, QString *error)
{
    QString value = text.trimmed();
    QStringList entries;
    if (value.startsWith(QLatin1Char('['))) {
        if (!value.endsWith(QLatin1Char(']'))) {
            *error = QStringLiteral("Unterminated flow list");
            return entries;
        }
        value = value.mid(1, value.size() - 2);
        QString entry;
        QChar quote;
        bool escaped = false;
        for (const QChar character : value) {
            if (quote.isNull() && character == QLatin1Char(',')) {
                entries.append(entry);
                entry.clear();
                continue;
            }
            entry += character;
            if (escaped) {
                escaped = false;
            } else if (quote == QLatin1Char('"') && character == QLatin1Char('\\')) {
                escaped = true;
            } else if (character == quote) {
                quote = QChar();
            } else if (quote.isNull()
                       && (character == QLatin1Char('"') || character == QLatin1Char('\''))) {
                quote = character;
            }
        }
        if (!quote.isNull()) *error = QStringLiteral("Unterminated list quote");
        if (!entry.trimmed().isEmpty()) entries.append(entry);
    } else {
        entries = value.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }
    QStringList result;
    for (QString entry : entries) {
        entry = entry.trimmed();
        if (entry.startsWith(QStringLiteral("- "))) entry = entry.mid(2);
        entry = scalarValue(entry, error);
        const QStringList items = split_words
            ? entry.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts)
            : QStringList { entry };
        for (const QString &item : items) {
            if (!item.isEmpty() && !result.contains(item)) result.append(item);
        }
    }
    return result;
}

bool matchesPhrase(const QString &text, const QString &phrase)
{
    if (phrase.isEmpty()) return false;
    QString pattern = QRegularExpression::escape(phrase);
    const auto is_ascii_word = [](QChar character) {
        return character.unicode() < 128
            && (character.isLetterOrNumber() || character == QLatin1Char('_')
                || character == QLatin1Char('-'));
    };
    if (is_ascii_word(phrase.front())) pattern.prepend(QStringLiteral("(?<![A-Za-z0-9_-])"));
    if (is_ascii_word(phrase.back())) pattern.append(QStringLiteral("(?![A-Za-z0-9_-])"));
    return QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption)
        .match(text).hasMatch();
}

QString matchedTrigger(const AgentSkill &skill, const QString &text)
{
    if (!skill.autoTrigger) return QString();
    QStringList triggers = skill.triggers;
    if (triggers.isEmpty()) {
        if (skill.name == QLatin1String("ln-template-typeset")) {
            triggers = QStringList { QStringLiteral("排版"), QStringLiteral("模板"),
                QStringLiteral("typeset"), QStringLiteral("轻小说"), QStringLiteral("輕小說"),
                QStringLiteral("文稿"), QStringLiteral("manuscript"), QStringLiteral("ln-template") };
        } else if (skill.name == QLatin1String("book-structure")) {
            triggers = QStringList { QStringLiteral("拆分"), QStringLiteral("合并"),
                QStringLiteral("合併"), QStringLiteral("spine"), QStringLiteral("目录"),
                QStringLiteral("目錄"), QStringLiteral("split"), QStringLiteral("merge"),
                QStringLiteral("regex"), QStringLiteral("正则"), QStringLiteral("正則") };
        } else if (skill.name == QLatin1String("paragraph-normalization")) {
            triggers = QStringList { QStringLiteral("伪段落"), QStringLiteral("偽段落"),
                QStringLiteral("段落结构"), QStringLiteral("段落結構"),
                QStringLiteral("paragraph normalization"), QStringLiteral("paragraph structure"),
                QStringLiteral("div") };
        }
    }
    for (const QString &trigger : triggers) {
        if (matchesPhrase(text, trigger)) return trigger;
    }
    return QString();
}

} // namespace

AgentSkill parseSkillMarkdown(const QString &markdown, const QString &source_path)
{
    AgentSkill skill;
    skill.sourcePath = source_path;
    QString normalized = markdown;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    if (normalized.startsWith(QChar(0xfeff))) normalized.remove(0, 1);
    const QStringList lines = normalized.split(QLatin1Char('\n'));
    QMap<QString, QString> fields;
    int body_start = 0;
    if (lines.first() == QLatin1String("---")) {
        int end = 1;
        while (end < lines.size() && lines.at(end) != QLatin1String("---")) ++end;
        if (end == lines.size()) {
            skill.metadataError = QStringLiteral("Unterminated frontmatter");
            body_start = lines.size();
        } else {
            body_start = end + 1;
            QString current_field;
            bool block = false;
            static const QRegularExpression field_pattern(
                QStringLiteral("^([A-Za-z_][A-Za-z0-9_.-]*):\\s*(.*)$"));
            for (int index = 1; index < end; ++index) {
                const QString &raw_line = lines.at(index);
                const QString line = raw_line.trimmed();
                if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
                if (raw_line.contains(QLatin1Char('\t'))) {
                    skill.metadataError = QStringLiteral("Tabs are not supported in frontmatter");
                    continue;
                }
                if (raw_line.startsWith(QLatin1Char(' '))) {
                    if (current_field == QLatin1String("metadata")) {
                        const QRegularExpressionMatch match = field_pattern.match(line);
                        const QString key = match.captured(1);
                        if (!match.hasMatch() || skill.metadata.contains(key)) {
                            skill.metadataError = QStringLiteral("Invalid or duplicate metadata field");
                        } else {
                            skill.metadata.insert(key, scalarValue(match.captured(2), &skill.metadataError));
                        }
                    } else if (block && !current_field.isEmpty()) {
                        fields[current_field] += QLatin1Char('\n') + line;
                    } else {
                        skill.metadataError = QStringLiteral("Unexpected indented field");
                    }
                    continue;
                }
                const QRegularExpressionMatch match = field_pattern.match(line);
                current_field = match.captured(1);
                if (!match.hasMatch() || fields.contains(current_field)) {
                    skill.metadataError = QStringLiteral("Invalid or duplicate frontmatter field");
                    current_field.clear();
                    block = false;
                    continue;
                }
                const QString value = match.captured(2).trimmed();
                block = value.isEmpty() || value == QLatin1String(">")
                    || value == QLatin1String("|") || value == QLatin1String(">-")
                    || value == QLatin1String("|-");
                fields.insert(current_field, block ? QString() : value);
            }
        }
    }
    skill.body = lines.mid(body_start).join(QLatin1Char('\n')).trimmed();
    skill.name = scalarValue(fields.value(QStringLiteral("name")), &skill.metadataError);
    skill.description = scalarValue(fields.value(QStringLiteral("description")), &skill.metadataError).simplified();
    skill.version = scalarValue(fields.value(QStringLiteral("version"),
        skill.metadata.value(QStringLiteral("sigil.skill-version"), QStringLiteral("1"))),
        &skill.metadataError);
    if (fields.contains(QStringLiteral("version"))
        && skill.metadata.contains(QStringLiteral("sigil.skill-version"))
        && skill.version != skill.metadata.value(QStringLiteral("sigil.skill-version"))) {
        skill.metadataError = QStringLiteral("Conflicting skill versions");
    }
    skill.toolNames = listValue(fields.value(QStringLiteral("allowed-tools")), true, &skill.metadataError);
    skill.allowedTools = skill.toolNames.join(QLatin1Char(' '));
    skill.requiredCapabilities = listValue(fields.value(QStringLiteral("requires_capabilities")), true, &skill.metadataError);
    skill.triggers = listValue(fields.value(QStringLiteral("triggers")), false, &skill.metadataError);
    const QString requires_book = scalarValue(fields.value(QStringLiteral("requires_book"),
        skill.metadata.value(QStringLiteral("sigil.requires-book"), QStringLiteral("false"))),
        &skill.metadataError);
    const QString auto_trigger = scalarValue(fields.value(QStringLiteral("auto_trigger"), QStringLiteral("true")),
        &skill.metadataError);
    if ((requires_book != QLatin1String("true") && requires_book != QLatin1String("false"))
        || (auto_trigger != QLatin1String("true") && auto_trigger != QLatin1String("false"))) {
        skill.metadataError = QStringLiteral("Skill flags must be true or false");
    }
    skill.requiresBook = requires_book == QLatin1String("true");
    skill.autoTrigger = auto_trigger == QLatin1String("true");
    static const QRegularExpression tool_pattern(QStringLiteral("^[A-Za-z0-9_]+(?:[.-][A-Za-z0-9_]+)*$"));
    for (const QString &tool : skill.toolNames + skill.requiredCapabilities) {
        if (!tool_pattern.match(tool).hasMatch()) {
            skill.metadataError = QStringLiteral("Invalid capability name: %1").arg(tool);
        }
    }
    if (skill.name.isEmpty()) {
        skill.name = QFileInfo(source_path).dir().dirName();
        if (skill.name.isEmpty()) skill.name = QStringLiteral("unnamed-skill");
    }
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
        fallback.requiredCapabilities = QStringList {
            QStringLiteral("manuscript.parse"), QStringLiteral("content.typeset_from_manuscript") };
        fallback.requiresBook = true;
        skills.prepend(fallback);
    }
    return skills;
}

AgentSkillSelection selectAgentSkills(const QList<AgentSkill> &skills,
                                     const QString &user_text,
                                     IBookWorkspace *workspace,
                                     const QStringList &available_tools)
{
    AgentSkillSelection selection;
    bool explicit_selection = false;
    for (const AgentSkill &skill : skills) {
        if (matchesPhrase(user_text, skill.name)) explicit_selection = true;
    }
    QStringList seen_names;
    for (const AgentSkill &skill : skills) {
        QJsonObject decision {
            { QStringLiteral("name"), skill.name },
            { QStringLiteral("version"), skill.version },
            { QStringLiteral("source"), skill.sourcePath }
        };
        QString unavailable_reason;
        if (seen_names.contains(skill.name)) {
            unavailable_reason = QStringLiteral("duplicate_name");
        } else if (!skill.metadataError.isEmpty()) {
            unavailable_reason = QStringLiteral("invalid_metadata");
            decision.insert(QStringLiteral("error"), skill.metadataError);
        } else if (skill.version != QLatin1String("1")) {
            unavailable_reason = QStringLiteral("unsupported_version");
        } else if (skill.requiresBook && !workspace) {
            unavailable_reason = QStringLiteral("book_required");
        }
        seen_names.append(skill.name);
        QStringList required = skill.requiredCapabilities + skill.toolNames;
        required.removeDuplicates();
        QJsonArray missing;
        for (const QString &tool : required) {
            if (!available_tools.contains(tool)) missing.append(tool);
        }
        if (unavailable_reason.isEmpty() && !missing.isEmpty()) {
            unavailable_reason = QStringLiteral("capabilities_unavailable");
        }
        if (!missing.isEmpty()) decision.insert(QStringLiteral("missing_capabilities"), missing);
        if (!unavailable_reason.isEmpty()) {
            decision.insert(QStringLiteral("state"), QStringLiteral("unavailable"));
            decision.insert(QStringLiteral("reason"), unavailable_reason);
        } else {
            selection.availableSkills.append(skill);
            const bool named = matchesPhrase(user_text, skill.name);
            const QString trigger = explicit_selection ? QString() : matchedTrigger(skill, user_text);
            if (named || !trigger.isEmpty()) {
                selection.bodies += QStringLiteral("\n# Skill: %1\n%2\n").arg(skill.name, skill.body);
                decision.insert(QStringLiteral("state"), QStringLiteral("selected"));
                decision.insert(QStringLiteral("reason"), named
                    ? QStringLiteral("explicit_name") : QStringLiteral("trigger"));
                if (!trigger.isEmpty()) decision.insert(QStringLiteral("trigger"), trigger);
            } else {
                decision.insert(QStringLiteral("state"), QStringLiteral("available"));
                decision.insert(QStringLiteral("reason"), explicit_selection
                    ? QStringLiteral("explicit_selection") : QStringLiteral("no_match"));
            }
        }
        selection.decisions.append(decision);
    }
    return selection;
}

QString skillCatalogPrompt(const QList<AgentSkill> &skills)
{
    if (skills.isEmpty()) return QString();
    QString block = QStringLiteral(
        "Available skills (declarative; no scripts). Metadata does not grant tool permissions. "
        "Use when the description matches:\n");
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
    QString block;
    for (const AgentSkill &skill : skills) {
        if (matchesPhrase(user_text, skill.name) || !matchedTrigger(skill, user_text).isEmpty()) {
            block += QStringLiteral("\n# Skill: %1\n%2\n").arg(skill.name, skill.body);
        }
    }
    return block;
}

} // namespace SigilAgent
