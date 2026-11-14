#include "Agent/Tools/TranslationTools.h"

#include <QMetaObject>
#include <QPointer>
#include <QThread>
#include "Agent/Tools/ToolRegistry.h"
#include "BuiltinPlugins/Translation/TranslationController.h"

namespace SigilAgent {
namespace {
using Controller = BuiltinPlugins::Translation::TranslationController;
class TranslationTool final : public IAgentTool {
public:
    AgentToolDescriptor info;
    QPointer<Controller> controller;
    AgentToolDescriptor descriptor() const override { return info; }
    ToolResult execute(const QJsonObject &arguments) override
    {
        if (!controller) return ToolResult::failure("NO_BOOK", "Translation window is closed");
        const auto properties = info.inputSchema.value("properties").toObject();
        for (auto it = arguments.begin(); it != arguments.end(); ++it) {
            const auto property = properties.value(it.key()).toObject();
            const QString type = property.value("type").toString();
            const auto value = it.value();
            if (!properties.contains(it.key()) || (type == "string" && !value.isString())
                || (type == "array" && !value.isArray()) || (type == "boolean" && !value.isBool())
                || (type == "integer" && (!value.isDouble() || value.toDouble() != value.toInt()))) {
                return ToolResult::failure("INVALID_ARGUMENT", "Invalid translation parameter: " + it.key());
            }
            if (property.contains("enum") && !property.value("enum").toArray().contains(value))
                return ToolResult::failure("INVALID_ARGUMENT", "Unknown translation option: " + it.key());
            if (type == "array" && property.value("items").toObject().value("type").toString() == "string") {
                for (const auto &entry : value.toArray()) if (!entry.isString()) return ToolResult::failure("INVALID_ARGUMENT", "Expected string array");
            }
        }
        Controller::Result result;
        auto call = [&]() {
            if (info.name == "translation.prepare") result = controller->prepare(arguments);
            else if (info.name == "translation.run") result = controller->run(arguments);
            else if (info.name == "translation.status") result = Controller::Result::success(controller->status());
            else if (info.name == "translation.control") result = controller->control(arguments);
            else if (info.name == "translation.context") result = controller->context(arguments);
            else if (info.name == "translation.revise") result = controller->revise(arguments);
            else if (info.name == "translation.format") result = controller->format(arguments);
            else result = controller->saveGlossary(arguments);
        };
        if (QThread::currentThread() == controller->thread()) call();
        else QMetaObject::invokeMethod(controller, call, Qt::BlockingQueuedConnection);
        return result.ok ? ToolResult::success(result.data, result.applied, result.previewOnly)
                         : ToolResult::failure(result.code, result.message, result.data);
    }
};
QJsonObject string() { return {{"type", "string"}}; }
QJsonObject array() { return {{"type", "array"}, {"items", string()}}; }
QJsonObject enumeration(const QJsonArray &values) { return {{"type", "string"}, {"enum", values}}; }
}

void registerTranslationTools(ToolRegistry *registry, Controller *controller)
{
    auto add = [&](const QString &name, const QString &description, bool writes, QJsonObject properties) {
        auto tool = std::make_unique<TranslationTool>();
        tool->controller = controller;
        properties["session_id"] = string();
        tool->info = {name, description, QJsonObject {{"type", "object"}, {"properties", properties}, {"additionalProperties", false}},
            writes ? ToolRisk::Bulk : ToolRisk::Read, writes, false};
        registry->add(std::move(tool));
    };
    add("translation.prepare", "Prepare complete reading paragraphs from the live book, including unsaved text. Default whole spine; pass resource_ids for files and selection_start/end (UTF-16) for one file selection. No remote requests. Returns window-only session_id and counts. Preserve/reuse an existing context instead of repeatedly preparing.", false,
        {{"resource_ids", array()}, {"language", string()}, {"style", string()},
         {"selection_start", QJsonObject {{"type", "integer"}}}, {"selection_end", QJsonObject {{"type", "integer"}}}});
    add("translation.run", "Start window-local native batch workflow: scan full scope, aggregate glossary/memory, translate directly into current Book as bilingual pairs, review, format. Returns immediately; host batches and applies all paragraphs. No per-sentence patches or HTML. stages may select a subset (sample: translate only). Chinese format is default only for whole book. Does not save EPUB.", true,
        {{"stages", array()}, {"preset", enumeration({"chinese", "preserve"})}, {"retranslate", QJsonObject {{"type", "boolean"}}}});
    add("translation.status", "Read bounded native translation progress, requests/usage, local cache hits and failed locations. No model calls. Do not busy-poll; the panel displays live progress.", false, {});
    add("translation.control", "Pause, resume, stop or retry missing translations in this window. Stop rejects late model writes and keeps current Book edits. No restart persistence.", true,
        {{"action", enumeration({"pause", "resume", "stop", "retry_failed"})}});
    add("translation.context", "Read/update temporary plan, memory or glossary. value is a plan object, memory string, or glossary array (source/target/aliases/note/locked; Orion src/dst accepted). Read glossary pages with offset/limit. No remote calls or disk persistence.", false,
        {{"kind", enumeration({"plan", "memory", "glossary"})}, {"action", enumeration({"read", "update"})},
         {"value", QJsonObject {}}, {"offset", QJsonObject {{"type", "integer"}}}, {"limit", QJsonObject {{"type", "integer"}}}});
    add("translation.revise", "Batch-correct existing bilingual translations using instruction and/or corrected terms. Terms limit review to source paragraphs that contain them. Updates existing translation nodes; never perform one patch per sentence. Stop the current run first.", true,
        {{"instruction", string()}, {"terms", QJsonObject {{"type", "array"}}}});
    add("translation.format", "Apply the native Chinese reading preset to the prepared scope (horizontal/LTR for whole-book scope, responsive images). Preserve preset retains original layout. Reuses Sigil converters; no model calls or approval workflow.", true,
        {{"preset", enumeration({"chinese", "preserve"})}});
    add("translation.save_glossary", "Save completed temporary terms as JSON in the current book's actual Misc folder, using native resource creation. Unique filename by default; resource_id explicitly updates an existing Misc JSON. No keys or request logs; EPUB remains unsaved until normal Save.", true,
        {{"filename", string()}, {"resource_id", string()}});
}
} // namespace SigilAgent
