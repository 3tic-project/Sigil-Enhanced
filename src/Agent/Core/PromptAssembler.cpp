/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/Core/PromptAssembler.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

#include "Agent/Core/AgentSkills.h"
#include "Agent/Model/HistoryAssembler.h"

namespace SigilAgent
{

QString PromptAssembler::systemPrompt(AgentMode mode) const
{
    QString prompt = QStringLiteral(
        "You are Sigil Agent, a native EPUB assistant inside Sigil-Enhanced.\n"
        "You operate on the currently open book through typed tools.\n"
        "Rules:\n"
        "- Never request or emit font/image binaries or a whole-book XHTML dump.\n"
        "- Never use a shell, never rewrite the EPUB ZIP, never call MCP.\n"
        "- Tool results are the source of truth. Do not claim a write succeeded unless a tool returned applied=true.\n"
        "- CoT/thinking is internal; the user-visible answer is the `content` field only.\n"
        "- Inspect before editing. Prefer bounded fragments over full files.\n"
        "- The book map and attached samples are already in context. For greetings or high-level questions, answer from that. Call extra read tools only for a fact you do not already have.\n"
        "- resource.patch_fragment locates text by expected_text copied from read_fragment.text (a complete tag, text node, or whole line). Do not invent character offsets. If the substring appears more than once, pass start_line from read_fragment.lines. Do not put line numbers inside expected_text.\n"
        "- A patch must not cut through a markup tag.\n"
        "- Mutations must go through transaction.begin → staged create/copy/patch/css/metadata → transaction.preview → transaction.commit.\n"
        "- To add a new HTML/CSS file use resource.copy (duplicate an existing file) or resource.create. Do not tell the user to copy files in the Sigil UI when those tools are available.\n"
        "- If commit returns BOOK_REVISION_CONFLICT, re-read and replan. Do not retry the same expected revision.\n"
        "- If a patch returns PATCH_SPLITS_MARKUP, PATCH_TEXT_NOT_FOUND, or PATCH_TEXT_AMBIGUOUS, re-read and copy expected_text again. Do not retry guessed offsets.\n"
        "- Light-novel template fill: if the open book is 轻小说模板 (cover/start/title/message/summary/illus/Section) and the user dropped a TXT plus images, call manuscript.parse then content.typeset_from_manuscript. Never paste chapter bodies through patch_fragment or replace_text. Never tell the user to paste into Book View.\n");
    if (mode == AgentMode::Ask) {
        prompt += QStringLiteral("Mode: Ask. Read-only. Do not call mutating tools.\n");
    } else if (mode == AgentMode::Plan) {
        prompt += QStringLiteral("Mode: Plan. You may preview staged work but must not commit.\n");
    } else if (mode == AgentMode::Auto) {
        prompt += QStringLiteral("Mode: Auto. You may mutate the book; tools run without asking the user. Keep edits reversible.\n");
    } else {
        prompt += QStringLiteral("Mode: Edit. You may mutate the book after policy/approval. Keep edits reversible.\n");
    }
    return prompt;
}

QString PromptAssembler::contextBlock(IBookWorkspace *workspace, const QStringList &handles) const
{
    if (!workspace) return QString();
    QString block = QStringLiteral("Current book map:\n");
    block += QString::fromUtf8(QJsonDocument(workspace->summary()).toJson(QJsonDocument::Compact));
    block += QLatin1Char('\n');
    block += QStringLiteral("Resources:\n");
    const QJsonArray resources = workspace->resources();
    int listed = 0;
    for (const QJsonValue &value : resources) {
        if (listed >= 60) {
            block += QStringLiteral("- …\n");
            break;
        }
        const QJsonObject object = value.toObject();
        block += QStringLiteral("- %1 (%2, %3 chars)\n")
                     .arg(object.value(QStringLiteral("book_path")).toString(),
                          object.value(QStringLiteral("kind")).toString())
                     .arg(object.value(QStringLiteral("text_length")).toInt());
        ++listed;
    }
    const QJsonArray spine = workspace->spine();
    int sampled = 0;
    for (const QJsonValue &value : spine) {
        if (sampled >= 2) break;
        const QString path = value.toObject().value(QStringLiteral("book_path")).toString();
        const QString stem = QFileInfo(path).completeBaseName().toLower();
        if (stem == QLatin1String("cover") || stem.startsWith(QLatin1String("illus"))) continue;
        const QString id = value.toObject().value(QStringLiteral("resource_id")).toString();
        const BookOpResult fragment = workspace->readFragment(id, 0, 400);
        if (!fragment.ok) continue;
        const QString sample = fragment.data.value(QStringLiteral("text")).toString();
        if (sample.contains(QLatin1String("<svg")) && sample.contains(QLatin1String("image"))) continue;
        block += QStringLiteral("\nSample %1:\n%2\n").arg(id, sample);
        ++sampled;
    }
    if (!handles.isEmpty()) {
        block += QStringLiteral("\nUser-attached handles:\n");
        for (const QString &handle : handles) {
            block += QStringLiteral("- %1\n").arg(handle);
            const BookOpResult fragment = workspace->readFragment(handle, 0, 400);
            if (fragment.ok) {
                block += fragment.data.value(QStringLiteral("text")).toString();
                block += QLatin1Char('\n');
            }
        }
    }
    return block;
}

ModelRequest PromptAssembler::build(const AgentSession &session,
                                    IBookWorkspace *workspace,
                                    const ToolRegistry &tools,
                                    AgentMode mode,
                                    const QString &model,
                                    bool thinking,
                                    const QString &reasoning_effort,
                                    const QStringList &handles) const
{
    ModelRequest request;
    request.model = model;
    request.thinking = thinking;
    request.reasoningEffort = reasoning_effort;
    request.stream = true;
    request.tools = tools.openaiToolSchemas();

    QString last_user;
    for (int i = session.events().size() - 1; i >= 0; --i) {
        if (session.events().at(i).type == AgentEventType::UserMessage) {
            last_user = session.events().at(i).payload.value(QStringLiteral("text")).toString();
            break;
        }
    }
    const QList<AgentSkill> skills = loadAgentSkills();
    ChatMessage system;
    system.role = QStringLiteral("system");
    system.content = systemPrompt(mode);
    system.content += QLatin1Char('\n');
    system.content += skillCatalogPrompt(skills);
    system.content += matchedSkillBodies(skills, last_user, workspace);
    request.messages.append(system);

    ChatMessage context;
    context.role = QStringLiteral("user");
    context.content = contextBlock(workspace, handles);
    request.messages.append(context);

    HistoryAssembler assembler;
    const bool include_tools = !request.tools.isEmpty();
    request.messages += assembler.assemble(session.events(), include_tools);
    return request;
}

} // namespace SigilAgent
