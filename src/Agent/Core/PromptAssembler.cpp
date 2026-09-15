/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/Core/PromptAssembler.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include "Agent/Core/AgentSkills.h"
#include "Agent/Model/HistoryAssembler.h"
#include "Agent/Security/PermissionPolicy.h"

namespace SigilAgent
{

namespace
{

constexpr int kMaxAttachedSelectionLength = 4096;
constexpr int kMaxAttachedResourceCount = 60;
constexpr int kMaxAutomaticSessionTasks = 8;
constexpr int kMaxAutomaticSessionMemoryEntries = 8;
constexpr int kMaxAutomaticSessionValueLength = 512;

struct AttachedSelection {
    QString resourceId;
    int start = 0;
    int end = 0;
};

bool parseAttachedSelection(const QString &handle, AttachedSelection *selection)
{
    static const QRegularExpression pattern(
        QStringLiteral("^(.+):(\\d+)-(\\d+)$"));
    const QRegularExpressionMatch match = pattern.match(handle);
    if (!match.hasMatch()) return false;
    bool start_ok = false;
    bool end_ok = false;
    const int start = match.captured(2).toInt(&start_ok);
    const int end = match.captured(3).toInt(&end_ok);
    if (!start_ok || !end_ok || end <= start) return false;
    if (selection) {
        selection->resourceId = match.captured(1);
        selection->start = start;
        selection->end = end;
    }
    return true;
}

} // namespace

QString PromptAssembler::systemPrompt(AgentMode mode, int remaining_tool_calls) const
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
        "- An attached selection is identified by resource:start-end in UTF-16 code units. Its exact bounded excerpt is included in context; use resource.read_fragment if it was truncated.\n"
        "- The book map and attached samples are already in context. For greetings or high-level questions, answer from that. Call extra read tools only for a fact you do not already have.\n"
        "- book.resources, book.spine, book.toc, style.stylesheets, font.inventory, book.validate, book.check, checkpoint.list, session.tasks, and keyless session.recall are paginated. When has_more=true, use next_offset to continue; never treat the first page as the complete inventory, diagnostic, checkpoint, or session-state result.\n"
        "- resource.patch_fragment locates text by expected_text copied from read_fragment.text (a complete tag, text node, or whole line). Do not invent character offsets. If the substring appears more than once, pass start_line from read_fragment.lines. Do not put line numbers inside expected_text.\n"
        "- A patch must not cut through a markup tag.\n"
        "- Mutations must go through transaction.begin → staged edits → transaction.preview → transaction.commit, except the native paragraph and TOC workflows below.\n"
        "- Native DIV paragraph normalization is paragraphs.analyze → paragraphs.plan → paragraphs.apply → transaction.preview → transaction.commit. Do not call transaction.begin before paragraphs.apply: apply revalidates the reviewed plan and opens its own exclusive staged transaction; it does not change the live Book.\n"
        "- Native TOC reparenting is toc.inspect_hierarchy → toc.plan_transform → toc.apply_transform → transaction.preview → transaction.commit. Do not call transaction.begin before toc.apply_transform: apply revalidates the reviewed plan and opens its own exclusive staged transaction. Promote/demote only changes Nav/NCX hierarchy; never edit XHTML h1-h6 headings to satisfy a TOC hierarchy request.\n"
        "- To add a new HTML/CSS file use resource.copy or resource.create. To remove a file use resource.delete. To rename or move a file use resource.rename (filename or full EPUB path). To reorder reading order use spine.set or spine.sort. To attach CSS use style.link.\n"
        "- Long text already in the book (dropped TXT/HTML) must stay there: content.wrap_plain, content.replace_body (source_resource_id), content.split/merge. Never paste chapter bodies through patch_fragment or replace_text. Never tell the user to paste into Book View.\n"
        "- Batch markup: content.wrap and content.replace_regex (patterns supplied by the user or inferred, never assume a fixed novel format).\n"
        "- Insert an already-imported image with image.insert. Check broken/unused images with book.check.\n"
        "- toc.generate builds a TOC from heading regex. metadata.update/_remove edits Dublin Core fields.\n"
        "- python.run executes a Live Python v2 snippet with `plugin` bound (`plugin.book`, `plugin.editor`). It is not a plugin package and not a ZIP snapshot. Commit or rollback the Agent transaction first. Prefer typed tools; use Python for logic typed tools cannot express.\n"
        "- Keep a plan with session.task_add / session.task_update and session.remember for constraints (heading regex, class names) across turns.\n"
        "- After transaction.commit, report the exact resource_outcomes success/failure counts and transaction_state. If the scope is unavailable, say so; never infer resource success from applied_changes.\n"
        "- If commit returns BOOK_REVISION_CONFLICT, re-read and replan. Do not retry the same expected revision.\n"
        "- If a patch returns PATCH_SPLITS_MARKUP, PATCH_TEXT_NOT_FOUND, or PATCH_TEXT_AMBIGUOUS, re-read and copy expected_text again. Do not retry guessed offsets.\n");
    if (remaining_tool_calls >= 0) {
        prompt += QStringLiteral(
            "- This run may make at most %1 more tool call(s). Do not return a batch larger than this remaining budget.\n")
                      .arg(remaining_tool_calls);
    }
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

QString PromptAssembler::contextBlock(IBookWorkspace *workspace, const QStringList &handles,
                                      const AgentSession *session) const
{
    if (!workspace) return QString();
    const bool include_book_structure = handles.isEmpty()
        || handles.contains(QStringLiteral("book"));
    QString block = QStringLiteral("Current book identity:\n");
    block += QString::fromUtf8(QJsonDocument(workspace->summary()).toJson(QJsonDocument::Compact));
    block += QLatin1Char('\n');
    if (include_book_structure) {
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
            if (stem == QLatin1String("cover")
                || stem.startsWith(QLatin1String("illus"))) continue;
            const QString id = value.toObject().value(QStringLiteral("resource_id")).toString();
            const BookOpResult fragment = workspace->readFragment(id, 0, 400);
            if (!fragment.ok) continue;
            const QString sample = fragment.data.value(QStringLiteral("text")).toString();
            if (sample.contains(QLatin1String("<svg"))
                && sample.contains(QLatin1String("image"))) continue;
            block += QStringLiteral("\nSample %1:\n%2\n").arg(id, sample);
            ++sampled;
        }
    }
    if (!handles.isEmpty()) {
        block += QStringLiteral("\nUser-attached handles:\n");
        int attached_resources = 0;
        int omitted_resources = 0;
        for (const QString &handle : handles) {
            if (handle == QLatin1String("book")) {
                block += QStringLiteral("- book (structure shown above)\n");
                continue;
            }
            AttachedSelection selection;
            if (parseAttachedSelection(handle, &selection)) {
                const int requested = selection.end - selection.start;
                const int length = qMin(requested, kMaxAttachedSelectionLength);
                block += QStringLiteral("- selection %1 [%2,%3) UTF-16\n")
                             .arg(selection.resourceId)
                             .arg(selection.start)
                             .arg(selection.end);
                const BookOpResult fragment = workspace->readFragment(
                    selection.resourceId, selection.start, length);
                if (fragment.ok) {
                    block += fragment.data.value(QStringLiteral("text")).toString();
                    block += QLatin1Char('\n');
                } else {
                    block += QStringLiteral("[selection unavailable: %1]\n")
                                 .arg(fragment.code);
                }
                if (requested > length) {
                    block += QStringLiteral(
                        "[selection truncated after %1 UTF-16 code units; read the remaining range with resource.read_fragment]\n")
                                 .arg(length);
                }
                continue;
            }
            if (attached_resources >= kMaxAttachedResourceCount) {
                ++omitted_resources;
                continue;
            }
            ++attached_resources;
            block += QStringLiteral("- resource %1\n").arg(handle);
            const BookOpResult fragment = workspace->readFragment(handle, 0, 400);
            if (fragment.ok) {
                block += fragment.data.value(QStringLiteral("text")).toString();
                block += QLatin1Char('\n');
            }
        }
        if (omitted_resources > 0) {
            block += QStringLiteral(
                "- %1 additional selected resource(s) omitted from automatic context; read them with resource.read_fragment\n")
                         .arg(omitted_resources);
        }
    }
    if (session) {
        const QJsonArray tasks = session->tasks();
        if (!tasks.isEmpty()) {
            const int start = qMax(0, tasks.size() - kMaxAutomaticSessionTasks);
            QJsonArray recent_tasks;
            for (int index = start; index < tasks.size(); ++index) {
                QJsonObject task = tasks.at(index).toObject();
                const QString note = task.value(QStringLiteral("note")).toString();
                if (note.size() > kMaxAutomaticSessionValueLength) {
                    task.insert(QStringLiteral("note"),
                                note.left(kMaxAutomaticSessionValueLength));
                    task.insert(QStringLiteral("note_length"), note.size());
                    task.insert(QStringLiteral("note_truncated"), true);
                }
                recent_tasks.append(task);
            }
            block += QStringLiteral("\nSession tasks (most recent %1 of %2):\n")
                         .arg(recent_tasks.size()).arg(tasks.size());
            block += QString::fromUtf8(
                QJsonDocument(recent_tasks).toJson(QJsonDocument::Compact));
            block += QLatin1Char('\n');
            if (start > 0) {
                block += QStringLiteral(
                    "%1 earlier task(s) omitted from automatic context; read session.tasks pages for the complete checklist.\n")
                             .arg(start);
            }
        }
        const QJsonObject memory = session->memory();
        if (!memory.isEmpty()) {
            const QStringList keys = session->memoryKeys();
            const int start = qMax(
                0, keys.size() - kMaxAutomaticSessionMemoryEntries);
            QJsonObject recent_memory;
            QJsonObject truncated_memory_lengths;
            for (int index = start; index < keys.size(); ++index) {
                const QString &key = keys.at(index);
                const QJsonValue value = memory.value(key);
                if (value.isString()
                    && value.toString().size()
                        > kMaxAutomaticSessionValueLength) {
                    recent_memory.insert(
                        key, value.toString().left(
                            kMaxAutomaticSessionValueLength));
                    truncated_memory_lengths.insert(key, value.toString().size());
                } else if (!value.isString()) {
                    const QByteArray serialized = QJsonDocument(
                        QJsonArray { value }).toJson(QJsonDocument::Compact);
                    if (serialized.size() > kMaxAutomaticSessionValueLength) {
                        recent_memory.insert(
                            key, QStringLiteral(
                                "[structured value omitted; use session.recall key]"));
                        truncated_memory_lengths.insert(key, serialized.size());
                    } else {
                        recent_memory.insert(key, value);
                    }
                } else {
                    recent_memory.insert(key, value);
                }
            }
            block += QStringLiteral("\nSession memory (most recent %1 of %2):\n")
                         .arg(recent_memory.size()).arg(keys.size());
            block += QString::fromUtf8(
                QJsonDocument(recent_memory).toJson(QJsonDocument::Compact));
            block += QLatin1Char('\n');
            if (!truncated_memory_lengths.isEmpty()) {
                block += QStringLiteral(
                    "Truncated memory value sizes (read exact values with session.recall key): ");
                block += QString::fromUtf8(QJsonDocument(truncated_memory_lengths)
                                               .toJson(QJsonDocument::Compact));
                block += QLatin1Char('\n');
            }
            if (start > 0) {
                block += QStringLiteral(
                    "%1 earlier memory note(s) omitted from automatic context; read session.recall pages for the complete memory.\n")
                             .arg(start);
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
                                    const QStringList &handles,
                                    int history_previous_turn_budget_bytes,
                                    const PermissionPolicy *permission_policy,
                                    int remaining_tool_calls) const
{
    ModelRequest request;
    request.model = model;
    request.thinking = thinking;
    request.reasoningEffort = reasoning_effort;
    request.stream = true;
    PermissionPolicy default_policy;
    const PermissionPolicy *effective_policy = permission_policy
        ? permission_policy : &default_policy;
    QJsonArray hidden_tools;
    const QJsonArray all_tool_schemas = tools.openaiToolSchemas();
    request.tools = tools.openaiToolSchemas(
        [effective_policy, mode, &hidden_tools](
            const AgentToolDescriptor &descriptor) {
            const bool exposed = effective_policy->evaluate(mode, descriptor)
                != PermissionAction::Deny;
            if (!exposed) hidden_tools.append(descriptor.name);
            return exposed;
        });
    const int all_schema_bytes = QJsonDocument(all_tool_schemas)
        .toJson(QJsonDocument::Compact).size();
    const int exposed_schema_bytes = QJsonDocument(request.tools)
        .toJson(QJsonDocument::Compact).size();
    request.toolContext = QJsonObject {
        { QStringLiteral("mode"), modeName(mode) },
        { QStringLiteral("policy_applied"), true },
        { QStringLiteral("total_tool_count"), all_tool_schemas.size() },
        { QStringLiteral("exposed_tool_count"), request.tools.size() },
        { QStringLiteral("hidden_tool_count"), hidden_tools.size() },
        { QStringLiteral("hidden_tools"), hidden_tools },
        { QStringLiteral("unfiltered_schema_bytes"), all_schema_bytes },
        { QStringLiteral("exposed_schema_bytes"), exposed_schema_bytes },
        { QStringLiteral("saved_schema_bytes"),
          qMax(0, all_schema_bytes - exposed_schema_bytes) }
    };

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
    system.content = systemPrompt(mode, remaining_tool_calls);
    system.content += QLatin1Char('\n');
    system.content += skillCatalogPrompt(skills);
    system.content += matchedSkillBodies(skills, last_user, workspace);
    request.messages.append(system);

    ChatMessage context;
    context.role = QStringLiteral("user");
    context.content = contextBlock(workspace, handles, &session);
    request.messages.append(context);

    HistoryAssembler assembler;
    const bool include_tools = !request.tools.isEmpty();
    HistoryAssemblyStats history_stats;
    request.messages += assembler.assemble(
        session.events(), include_tools, history_previous_turn_budget_bytes,
        &history_stats);
    request.historyContext = history_stats.toJson();
    return request;
}

} // namespace SigilAgent
