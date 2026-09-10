/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/Tools/DivParagraphTools.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>

#include <QCryptographicHash>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

#include "Agent/Core/AgentCancellation.h"
#include "Agent/Execution/IBookWorkspace.h"
#include "Agent/Tools/IAgentTool.h"
#include "Agent/Tools/ToolRegistry.h"
#include "BuiltinPlugins/BookLiveParagraphNormalizer.h"
#include "BuiltinPlugins/DivParagraphNormalizationPlan.h"
#include "BuiltinPlugins/DivParagraphStylesheetResolver.h"

namespace SigilAgent
{

namespace
{

using Normalizer = BuiltinPlugins::BookLiveParagraphNormalizer;
using NormalizationPlan = BuiltinPlugins::DivParagraphNormalizationPlan;

constexpr int kMaxReportedRanges = 128;
constexpr int kMaxReportedCssDependencies = 128;
constexpr int kMaxDiffExcerpt = 4096;

class LambdaTool final : public IAgentTool
{
public:
    using Function = std::function<ToolResult(const QJsonObject &)>;

    LambdaTool(AgentToolDescriptor descriptor, Function function) :
        m_descriptor(std::move(descriptor)),
        m_function(std::move(function))
    {
    }

    AgentToolDescriptor descriptor() const override { return m_descriptor; }
    ToolResult execute(const QJsonObject &arguments) override { return m_function(arguments); }

private:
    AgentToolDescriptor m_descriptor;
    Function m_function;
};

struct ResourceInfo {
    QString id;
    QString path;
    QString kind;
    QString mediaType;
    quint64 revision = 0;
};

struct InputBatch {
    bool ok = false;
    QString code;
    QString message;
    QVector<NormalizationPlan::Input> inputs;
    QHash<QString, QString> paths;
    QStringList resourceIds;
};

struct StoredAnalysis {
    QString id;
    quint64 bookRevision = 0;
    Normalizer::Options options;
    NormalizationPlan::Result result;
    QHash<QString, QString> paths;
};

struct StoredPlan {
    QString id;
    QString digest;
    QString analysisId;
    quint64 bookRevision = 0;
    Normalizer::Options options;
    NormalizationPlan::Result result;
    QHash<QString, QString> paths;
    QStringList resourceIds;
};

QString framedDigest(const QStringList &values)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const QString &value : values) {
        const QByteArray bytes = value.toUtf8();
        hash.addData(QByteArray::number(bytes.size()));
        hash.addData(QByteArrayLiteral(":"));
        hash.addData(bytes);
    }
    return QString::fromLatin1(hash.result().toHex());
}

QString statusName(NormalizationPlan::Status status)
{
    switch (status) {
    case NormalizationPlan::Status::Apply:
        return QStringLiteral("apply");
    case NormalizationPlan::Status::Review:
        return QStringLiteral("review");
    case NormalizationPlan::Status::Skip:
        return QStringLiteral("skip");
    case NormalizationPlan::Status::Error:
        return QStringLiteral("error");
    }
    return QStringLiteral("error");
}

QString candidateKindName(Normalizer::CandidateKind kind)
{
    switch (kind) {
    case Normalizer::CandidateKind::Paragraph:
        return QStringLiteral("paragraph");
    case Normalizer::CandidateKind::SpacerBr:
        return QStringLiteral("spacer_br");
    case Normalizer::CandidateKind::SceneBreak:
        return QStringLiteral("scene_break");
    case Normalizer::CandidateKind::ImageWrapper:
        return QStringLiteral("image_wrapper");
    case Normalizer::CandidateKind::SingleBlockWrapper:
        return QStringLiteral("single_block_wrapper");
    }
    return QStringLiteral("unknown");
}

QJsonObject epubcheckNotRun()
{
    return QJsonObject {
        { QStringLiteral("status"), QStringLiteral("not_run") },
        { QStringLiteral("message"),
          QStringLiteral("Full EPUBCheck was not run; only local XHTML/CSS analysis and structural invariants were evaluated.") }
    };
}

QJsonArray rangesJson(const QVector<Normalizer::SourceRange> &ranges, bool *truncated)
{
    QJsonArray array;
    const int count = qMin(ranges.size(), kMaxReportedRanges);
    for (int index = 0; index < count; ++index) {
        const Normalizer::SourceRange &range = ranges.at(index);
        array.append(QJsonObject {
            { QStringLiteral("start"), range.start },
            { QStringLiteral("end"), range.end },
            { QStringLiteral("kind"), candidateKindName(range.kind) }
        });
    }
    if (truncated) {
        *truncated = ranges.size() > count;
    }
    return array;
}

QJsonArray cssDependenciesJson(
    const QVector<BuiltinPlugins::DivParagraphCssAnalyzer::Dependency> &dependencies,
    bool *truncated)
{
    QJsonArray array;
    const int count = qMin(dependencies.size(), kMaxReportedCssDependencies);
    for (int index = 0; index < count; ++index) {
        const auto &dependency = dependencies.at(index);
        array.append(QJsonObject {
            { QStringLiteral("source_id"), dependency.sourceId },
            { QStringLiteral("selector"), dependency.selector },
            { QStringLiteral("reason"), dependency.reason }
        });
    }
    if (truncated) {
        *truncated = dependencies.size() > count;
    }
    return array;
}

QJsonObject analysisEntryJson(const NormalizationPlan::Entry &entry,
                              const QString &path)
{
    bool ranges_truncated = false;
    bool protected_truncated = false;
    bool css_truncated = false;
    const QJsonArray candidates = rangesJson(
        entry.analysis.candidateRanges, &ranges_truncated);
    const QJsonArray protected_ranges = rangesJson(
        entry.analysis.protectedRanges, &protected_truncated);
    const QJsonArray css = cssDependenciesJson(
        entry.analysis.cssDependencies, &css_truncated);
    return QJsonObject {
        { QStringLiteral("resource_id"), entry.resourceId },
        { QStringLiteral("book_path"), path },
        { QStringLiteral("resource_revision"), entry.baseRevision.toLongLong() },
        { QStringLiteral("status"), statusName(entry.status) },
        { QStringLiteral("classification"), Normalizer::pageKindName(entry.analysis.pageKind) },
        { QStringLiteral("safe_to_normalize"), entry.analysis.safeToNormalize },
        { QStringLiteral("message"), entry.analysis.message },
        { QStringLiteral("body_candidates"), entry.analysis.paragraphLeaves },
        { QStringLiteral("convertible_candidates"), entry.analysis.convertibleLeaves },
        { QStringLiteral("blank_line_candidates"), entry.analysis.spacerBrLeaves },
        { QStringLiteral("scene_break_candidates"), entry.analysis.sceneBreaks },
        { QStringLiteral("image_candidates"), entry.analysis.imageLeaves },
        { QStringLiteral("nested_block_candidates"), entry.analysis.wrappedBlockLeaves },
        { QStringLiteral("protected_count"), entry.analysis.protectedRanges.size() },
        { QStringLiteral("candidate_ranges"), candidates },
        { QStringLiteral("candidate_ranges_truncated"), ranges_truncated },
        { QStringLiteral("protected_ranges"), protected_ranges },
        { QStringLiteral("protected_ranges_truncated"), protected_truncated },
        { QStringLiteral("css_dependencies"), css },
        { QStringLiteral("css_dependencies_truncated"), css_truncated },
        { QStringLiteral("warnings"), QJsonArray::fromStringList(entry.analysis.warnings) },
        { QStringLiteral("before_hash"), entry.beforeHash },
        { QStringLiteral("after_hash"), entry.afterHash }
    };
}

QJsonObject diffExcerpt(const QString &before, const QString &after)
{
    int prefix = 0;
    const int common_limit = qMin(before.size(), after.size());
    while (prefix < common_limit && before.at(prefix) == after.at(prefix)) {
        ++prefix;
    }

    int before_suffix = before.size();
    int after_suffix = after.size();
    while (before_suffix > prefix && after_suffix > prefix &&
           before.at(before_suffix - 1) == after.at(after_suffix - 1)) {
        --before_suffix;
        --after_suffix;
    }

    const int context_start = qMax(0, prefix - 160);
    const int before_end = qMin(before.size(), before_suffix + 160);
    const int after_end = qMin(after.size(), after_suffix + 160);
    const int before_length = qMin(kMaxDiffExcerpt, before_end - context_start);
    const int after_length = qMin(kMaxDiffExcerpt, after_end - context_start);
    return QJsonObject {
        { QStringLiteral("change_start"), prefix },
        { QStringLiteral("before_change_end"), before_suffix },
        { QStringLiteral("after_change_end"), after_suffix },
        { QStringLiteral("excerpt_start"), context_start },
        { QStringLiteral("before"), before.mid(context_start, before_length) },
        { QStringLiteral("after"), after.mid(context_start, after_length) },
        { QStringLiteral("prefix_truncated"), context_start > 0 },
        { QStringLiteral("suffix_truncated"),
          context_start + before_length < before.size() ||
          context_start + after_length < after.size() }
    };
}

QJsonObject summaryJson(const NormalizationPlan::Result &result)
{
    return QJsonObject {
        { QStringLiteral("ready_files"), result.applyFiles },
        { QStringLiteral("review_only_files"), result.reviewFiles },
        { QStringLiteral("skipped_files"), result.skippedFiles },
        { QStringLiteral("error_files"), result.errorFiles },
        { QStringLiteral("conversion_count"), result.conversionCount },
        { QStringLiteral("protected_count"), result.protectedCount }
    };
}

Normalizer::Options optionsFromArguments(const QJsonObject &arguments)
{
    Normalizer::Options options = Normalizer::Options::conservative();
    options.convertSpacerBr = arguments.value(QStringLiteral("convert_blank_lines")).toBool(false);
    options.convertSceneBreaks = arguments.value(QStringLiteral("convert_scene_breaks")).toBool(false);
    options.convertImageWrappers = arguments.value(QStringLiteral("convert_image_wrappers")).toBool(false);
    options.convertSingleBlockWrappers =
        arguments.value(QStringLiteral("convert_nested_blocks")).toBool(false);
    return options;
}

bool isXhtml(const ResourceInfo &resource)
{
    return resource.kind == QLatin1String("xhtml") ||
           resource.mediaType == QLatin1String("application/xhtml+xml");
}

bool isCss(const ResourceInfo &resource)
{
    return resource.kind == QLatin1String("css") ||
           resource.mediaType == QLatin1String("text/css");
}

QList<ResourceInfo> resourceInfos(IBookWorkspace *workspace)
{
    QList<ResourceInfo> resources;
    for (const QJsonValue &value : workspace->resources()) {
        const QJsonObject object = value.toObject();
        ResourceInfo resource;
        resource.id = object.value(QStringLiteral("resource_id")).toString();
        resource.path = QDir::cleanPath(object.value(QStringLiteral("book_path")).toString());
        resource.kind = object.value(QStringLiteral("kind")).toString();
        resource.mediaType = object.value(QStringLiteral("media_type")).toString();
        resource.revision = static_cast<quint64>(
            object.value(QStringLiteral("revision")).toInteger());
        if (!resource.id.isEmpty()) {
            resources.append(resource);
        }
    }
    std::sort(resources.begin(), resources.end(), [](const ResourceInfo &left,
                                                      const ResourceInfo &right) {
        if (left.path != right.path) {
            return left.path < right.path;
        }
        return left.id < right.id;
    });
    return resources;
}

InputBatch buildInputs(IBookWorkspace *workspace,
                       const QJsonValue &requested_value,
                       const QStringList &fallback_ids = QStringList())
{
    InputBatch batch;
    const QList<ResourceInfo> resources = resourceInfos(workspace);
    QHash<QString, ResourceInfo> by_id;
    QHash<QString, QString> css_by_path;
    for (const ResourceInfo &resource : resources) {
        by_id.insert(resource.id, resource);
        if (isCss(resource)) {
            css_by_path.insert(resource.path, workspace->resourceText(resource.id));
        }
    }

    QStringList requested_ids;
    if (!fallback_ids.isEmpty()) {
        requested_ids = fallback_ids;
    } else if (!requested_value.isUndefined() && !requested_value.isNull()) {
        if (!requested_value.isArray()) {
            batch.code = QStringLiteral("INVALID_ARGUMENT");
            batch.message = QStringLiteral("resource_ids must be an array of XHTML resource IDs");
            return batch;
        }
        for (const QJsonValue &value : requested_value.toArray()) {
            const QString id = value.toString();
            if (id.isEmpty()) {
                batch.code = QStringLiteral("INVALID_ARGUMENT");
                batch.message = QStringLiteral("resource_ids must contain non-empty strings");
                return batch;
            }
            if (!requested_ids.contains(id)) {
                requested_ids.append(id);
            }
        }
    } else {
        for (const ResourceInfo &resource : resources) {
            if (isXhtml(resource)) {
                requested_ids.append(resource.id);
            }
        }
    }

    if (requested_ids.isEmpty()) {
        batch.code = QStringLiteral("NO_XHTML_RESOURCES");
        batch.message = QStringLiteral("The requested scope contains no XHTML resources");
        return batch;
    }

    for (const QString &id : requested_ids) {
        if (!by_id.contains(id)) {
            batch.code = QStringLiteral("RESOURCE_NOT_FOUND");
            batch.message = QStringLiteral("Unknown resource %1").arg(id);
            return batch;
        }
        const ResourceInfo resource = by_id.value(id);
        if (!isXhtml(resource)) {
            batch.code = QStringLiteral("RESOURCE_NOT_XHTML");
            batch.message = QStringLiteral("Resource %1 is not XHTML").arg(id);
            return batch;
        }
        const QString text = workspace->resourceText(id);
        NormalizationPlan::Input input;
        input.resourceId = id;
        input.text = text;
        input.baseRevision = QString::number(workspace->resourceRevision(id));
        input.stylesheets = BuiltinPlugins::DivParagraphStylesheetResolver::resolve(
            text, resource.path, css_by_path);
        batch.inputs.append(input);
        batch.paths.insert(id, resource.path);
        batch.resourceIds.append(id);
    }
    batch.ok = true;
    return batch;
}

NormalizationPlan::Result selectedResult(const NormalizationPlan::Result &source,
                                         const QSet<QString> &selected)
{
    NormalizationPlan::Result result;
    result.ok = source.ok;
    result.ruleVersion = source.ruleVersion;
    result.presetId = source.presetId;
    for (const NormalizationPlan::Entry &entry : source.entries) {
        if (!selected.contains(entry.resourceId)) {
            continue;
        }
        result.entries.append(entry);
        switch (entry.status) {
        case NormalizationPlan::Status::Apply:
            ++result.applyFiles;
            result.conversionCount += entry.analysis.convertibleLeaves;
            result.protectedCount += entry.analysis.protectedRanges.size();
            break;
        case NormalizationPlan::Status::Review:
            ++result.reviewFiles;
            break;
        case NormalizationPlan::Status::Skip:
            ++result.skippedFiles;
            break;
        case NormalizationPlan::Status::Error:
            ++result.errorFiles;
            break;
        }
    }
    NormalizationPlan::refreshIdentity(result);
    return result;
}

QStringList pathConflicts(const QHash<QString, QString> &expected,
                          const QHash<QString, QString> &current,
                          const QStringList &resource_ids)
{
    QStringList conflicts;
    for (const QString &id : resource_ids) {
        if (expected.value(id) != current.value(id)) {
            conflicts.append(QStringLiteral("%1: resource path changed").arg(id));
        }
    }
    return conflicts;
}

class DivParagraphToolService
{
public:
    DivParagraphToolService(IBookWorkspace *workspace, AgentCancellation *cancellation) :
        m_workspace(workspace),
        m_cancellation(cancellation)
    {
    }

    ToolResult analyze(const QJsonObject &arguments)
    {
        if (m_workspace->hasOpenTransaction()) {
            return ToolResult::failure(
                QStringLiteral("TRANSACTION_OPEN"),
                QStringLiteral("Preview, commit, or roll back the open transaction before paragraph analysis"));
        }
        if (isCancelled()) {
            return ToolResult::cancelled();
        }
        const InputBatch batch = buildInputs(
            m_workspace, arguments.value(QStringLiteral("resource_ids")));
        if (!batch.ok) {
            return ToolResult::failure(batch.code, batch.message);
        }
        const Normalizer::Options options = optionsFromArguments(arguments);
        const NormalizationPlan::Result result = NormalizationPlan::build(
            batch.inputs, options,
            [this](int, int) { return !isCancelled(); });
        if (result.cancelled) {
            return ToolResult::cancelled();
        }

        QStringList identity {
            QStringLiteral("sigil-agent-div-analysis-v1"),
            QString::number(m_workspace->revision()),
            result.planId
        };
        for (const QString &id : batch.resourceIds) {
            identity.append(id);
            identity.append(batch.paths.value(id));
        }
        StoredAnalysis stored;
        stored.id = framedDigest(identity);
        stored.bookRevision = m_workspace->revision();
        stored.options = options;
        stored.result = result;
        stored.paths = batch.paths;
        m_analysis = stored;
        m_plan.reset();

        QJsonArray files;
        for (const NormalizationPlan::Entry &entry : result.entries) {
            files.append(analysisEntryJson(entry, batch.paths.value(entry.resourceId)));
        }
        return ToolResult::success(QJsonObject {
            { QStringLiteral("analysis_id"), stored.id },
            { QStringLiteral("book_revision"), static_cast<qint64>(stored.bookRevision) },
            { QStringLiteral("rule_version"), result.ruleVersion },
            { QStringLiteral("preset_id"), result.presetId },
            { QStringLiteral("summary"), summaryJson(result) },
            { QStringLiteral("files"), files },
            { QStringLiteral("full_epubcheck"), epubcheckNotRun() },
            { QStringLiteral("applied_to_book"), false }
        }, false, true);
    }

    ToolResult plan(const QJsonObject &arguments)
    {
        if (!m_analysis) {
            return ToolResult::failure(
                QStringLiteral("ANALYSIS_NOT_FOUND"),
                QStringLiteral("Run paragraphs.analyze before creating a plan"));
        }
        if (arguments.value(QStringLiteral("analysis_id")).toString() != m_analysis->id) {
            return ToolResult::failure(
                QStringLiteral("ANALYSIS_BINDING_MISMATCH"),
                QStringLiteral("The analysis ID is not the current book-session analysis"));
        }
        if (m_workspace->hasOpenTransaction()) {
            return ToolResult::failure(
                QStringLiteral("TRANSACTION_OPEN"),
                QStringLiteral("Preview, commit, or roll back the open transaction before creating a paragraph plan"));
        }
        if (isCancelled()) {
            return ToolResult::cancelled();
        }

        QSet<QString> selected;
        const QJsonValue requested = arguments.value(QStringLiteral("resource_ids"));
        if (!requested.isUndefined() && !requested.isNull()) {
            if (!requested.isArray()) {
                return ToolResult::failure(
                    QStringLiteral("INVALID_ARGUMENT"),
                    QStringLiteral("resource_ids must be an array"));
            }
            for (const QJsonValue &value : requested.toArray()) {
                if (!value.isString() || value.toString().isEmpty()) {
                    return ToolResult::failure(
                        QStringLiteral("INVALID_ARGUMENT"),
                        QStringLiteral("resource_ids must contain non-empty strings"));
                }
                selected.insert(value.toString());
            }
        } else {
            for (const NormalizationPlan::Entry &entry : m_analysis->result.entries) {
                if (entry.status == NormalizationPlan::Status::Apply) {
                    selected.insert(entry.resourceId);
                }
            }
        }
        if (selected.isEmpty()) {
            return ToolResult::failure(
                QStringLiteral("NO_SAFE_SELECTION"),
                QStringLiteral("No auto-safe paragraph resources were selected"));
        }

        QStringList selected_ids;
        for (const NormalizationPlan::Entry &entry : m_analysis->result.entries) {
            if (!selected.contains(entry.resourceId)) {
                continue;
            }
            if (entry.status != NormalizationPlan::Status::Apply) {
                return ToolResult::failure(
                    QStringLiteral("PLAN_SELECTION_UNSAFE"),
                    QStringLiteral("Resource %1 is not auto-safe and cannot be added to this plan")
                        .arg(entry.resourceId));
            }
            selected_ids.append(entry.resourceId);
        }
        if (selected_ids.size() != selected.size()) {
            return ToolResult::failure(
                QStringLiteral("RESOURCE_NOT_IN_ANALYSIS"),
                QStringLiteral("A selected resource was not part of the bound analysis"));
        }

        const NormalizationPlan::Result selected_analysis = selectedResult(
            m_analysis->result, selected);
        const InputBatch current = buildInputs(
            m_workspace, QJsonValue(), selected_ids);
        if (!current.ok) {
            return ToolResult::failure(current.code, current.message);
        }
        QStringList conflicts = pathConflicts(
            m_analysis->paths, current.paths, selected_ids);
        conflicts.append(NormalizationPlan::revisionConflicts(
            selected_analysis, current.inputs));
        if (m_workspace->revision() != m_analysis->bookRevision) {
            conflicts.prepend(QStringLiteral("book revision changed"));
        }
        if (!conflicts.isEmpty()) {
            return ToolResult::failure(
                QStringLiteral("ANALYSIS_STALE"),
                QStringLiteral("Book content changed after paragraph analysis"),
                QJsonObject { { QStringLiteral("conflicts"), QJsonArray::fromStringList(conflicts) } });
        }

        const NormalizationPlan::Result current_plan = NormalizationPlan::build(
            current.inputs, m_analysis->options,
            [this](int, int) { return !isCancelled(); });
        if (current_plan.cancelled) {
            return ToolResult::cancelled();
        }
        if (!current_plan.ok || current_plan.applyFiles != selected_ids.size() ||
            current_plan.planId != selected_analysis.planId) {
            return ToolResult::failure(
                QStringLiteral("ANALYSIS_STALE"),
                QStringLiteral("The selected paragraph output no longer matches the analyzed result"));
        }

        StoredPlan stored;
        stored.analysisId = m_analysis->id;
        stored.bookRevision = m_analysis->bookRevision;
        stored.options = m_analysis->options;
        stored.result = current_plan;
        stored.paths = current.paths;
        stored.resourceIds = selected_ids;
        stored.id = framedDigest({
            QStringLiteral("sigil-agent-div-plan-v1"),
            stored.analysisId,
            QString::number(stored.bookRevision),
            current_plan.planId
        });
        QStringList digest_parts {
            QStringLiteral("sigil-agent-div-plan-digest-v1"),
            stored.id,
            QString::number(stored.bookRevision),
            current_plan.ruleVersion,
            current_plan.presetId
        };
        for (const NormalizationPlan::Entry &entry : current_plan.entries) {
            digest_parts.append(entry.resourceId);
            digest_parts.append(current.paths.value(entry.resourceId));
            digest_parts.append(entry.baseRevision);
            digest_parts.append(entry.beforeHash);
            digest_parts.append(entry.cssHash);
            digest_parts.append(entry.afterHash);
        }
        stored.digest = framedDigest(digest_parts);
        m_plan = stored;

        QJsonArray changes;
        for (const NormalizationPlan::Entry &entry : current_plan.entries) {
            changes.append(QJsonObject {
                { QStringLiteral("resource_id"), entry.resourceId },
                { QStringLiteral("book_path"), current.paths.value(entry.resourceId) },
                { QStringLiteral("resource_revision"), entry.baseRevision.toLongLong() },
                { QStringLiteral("before_hash"), entry.beforeHash },
                { QStringLiteral("after_hash"), entry.afterHash },
                { QStringLiteral("css_hash"), entry.cssHash },
                { QStringLiteral("conversion_count"), entry.analysis.convertibleLeaves },
                { QStringLiteral("protected_count"), entry.analysis.protectedRanges.size() },
                { QStringLiteral("source_diff"), diffExcerpt(entry.source, entry.output) }
            });
        }
        return ToolResult::success(QJsonObject {
            { QStringLiteral("plan_id"), stored.id },
            { QStringLiteral("plan_digest"), stored.digest },
            { QStringLiteral("analysis_id"), stored.analysisId },
            { QStringLiteral("book_revision"), static_cast<qint64>(stored.bookRevision) },
            { QStringLiteral("operation"), QStringLiteral("normalize_div_paragraphs") },
            { QStringLiteral("changes_css"), false },
            { QStringLiteral("changes_opf"), false },
            { QStringLiteral("adds_resources"), false },
            { QStringLiteral("summary"), summaryJson(current_plan) },
            { QStringLiteral("changes"), changes },
            { QStringLiteral("local_validation"), QStringLiteral("passed") },
            { QStringLiteral("full_epubcheck"), epubcheckNotRun() },
            { QStringLiteral("applied_to_book"), false }
        }, false, true);
    }

    ToolResult apply(const QJsonObject &arguments)
    {
        if (!m_plan) {
            return ToolResult::failure(
                QStringLiteral("PLAN_NOT_FOUND"),
                QStringLiteral("Create a paragraph plan in this book session before applying it"));
        }
        const QString plan_id = arguments.value(QStringLiteral("plan_id")).toString();
        const QString plan_digest = arguments.value(QStringLiteral("plan_digest")).toString();
        const quint64 expected_revision = static_cast<quint64>(
            arguments.value(QStringLiteral("expected_book_revision")).toInteger());
        if (plan_id != m_plan->id || plan_digest != m_plan->digest ||
            expected_revision != m_plan->bookRevision) {
            return ToolResult::failure(
                QStringLiteral("PLAN_BINDING_MISMATCH"),
                QStringLiteral("plan_id, plan_digest, and expected_book_revision must match the reviewed plan"));
        }
        if (m_workspace->hasOpenTransaction()) {
            return ToolResult::failure(
                QStringLiteral("TRANSACTION_OPEN"),
                QStringLiteral("The reviewed paragraph plan requires an exclusive new transaction"));
        }
        if (m_workspace->revision() != m_plan->bookRevision) {
            return ToolResult::failure(
                QStringLiteral("BOOK_REVISION_CONFLICT"),
                QStringLiteral("The book revision changed after paragraph planning"));
        }
        if (isCancelled()) {
            return ToolResult::cancelled();
        }

        const InputBatch current = buildInputs(
            m_workspace, QJsonValue(), m_plan->resourceIds);
        if (!current.ok) {
            return ToolResult::failure(current.code, current.message);
        }
        QStringList conflicts = pathConflicts(
            m_plan->paths, current.paths, m_plan->resourceIds);
        conflicts.append(NormalizationPlan::revisionConflicts(
            m_plan->result, current.inputs));
        if (!conflicts.isEmpty()) {
            return ToolResult::failure(
                QStringLiteral("PLAN_STALE"),
                QStringLiteral("XHTML, CSS, or resource identity changed after paragraph planning"),
                QJsonObject { { QStringLiteral("conflicts"), QJsonArray::fromStringList(conflicts) } });
        }
        const NormalizationPlan::Result rebuilt = NormalizationPlan::build(
            current.inputs, m_plan->options,
            [this](int, int) { return !isCancelled(); });
        if (rebuilt.cancelled || isCancelled()) {
            return ToolResult::cancelled();
        }
        if (!rebuilt.ok || rebuilt.planId != m_plan->result.planId ||
            rebuilt.applyFiles != m_plan->resourceIds.size()) {
            return ToolResult::failure(
                QStringLiteral("PLAN_STALE"),
                QStringLiteral("Revalidated paragraph output differs from the reviewed plan"));
        }

        const BookOpResult begun = m_workspace->beginTransaction(
            QStringLiteral("Normalize DIV paragraphs (%1)").arg(m_plan->id.left(12)));
        if (!begun.ok) {
            return ToolResult::failure(begun.code, begun.message, begun.data);
        }
        for (const NormalizationPlan::Entry &entry : rebuilt.entries) {
            if (isCancelled()) {
                return rollbackFailure(ToolResult::cancelled());
            }
            const BookOpResult staged = m_workspace->replaceText(
                entry.resourceId, entry.output,
                static_cast<quint64>(entry.baseRevision.toULongLong()));
            if (!staged.ok) {
                return rollbackFailure(ToolResult::failure(
                    staged.code, staged.message, staged.data));
            }
        }

        QJsonObject data = begun.data;
        data.insert(QStringLiteral("plan_id"), m_plan->id);
        data.insert(QStringLiteral("plan_digest"), m_plan->digest);
        data.insert(QStringLiteral("book_revision"), static_cast<qint64>(m_plan->bookRevision));
        data.insert(QStringLiteral("staged_files"), rebuilt.applyFiles);
        data.insert(QStringLiteral("staged_conversions"), rebuilt.conversionCount);
        data.insert(QStringLiteral("requires_transaction_preview"), true);
        data.insert(QStringLiteral("requires_transaction_commit"), true);
        data.insert(QStringLiteral("applied_to_book"), false);
        data.insert(QStringLiteral("save_status"), QStringLiteral("not_applied"));
        data.insert(QStringLiteral("local_validation"), QStringLiteral("passed"));
        data.insert(QStringLiteral("full_epubcheck"), epubcheckNotRun());
        return ToolResult::success(data, false, true);
    }

private:
    bool isCancelled() const
    {
        return m_cancellation && m_cancellation->isCancelled();
    }

    ToolResult rollbackFailure(const ToolResult &failure)
    {
        const BookOpResult rolled_back = m_workspace->rollbackTransaction();
        if (!rolled_back.ok) {
            return ToolResult::failure(
                QStringLiteral("STAGING_ROLLBACK_FAILED"),
                QStringLiteral("Paragraph staging failed and the transaction could not be rolled back: %1")
                    .arg(rolled_back.message),
                QJsonObject {
                    { QStringLiteral("original_code"), failure.code },
                    { QStringLiteral("original_message"), failure.message }
                });
        }
        if (failure.code == QLatin1String("CANCELLED")) {
            return failure;
        }
        return ToolResult::failure(
            QStringLiteral("STAGING_ROLLED_BACK"),
            QStringLiteral("Paragraph staging failed; the exclusive transaction was rolled back: %1")
                .arg(failure.message),
            QJsonObject { { QStringLiteral("original_code"), failure.code } });
    }

    IBookWorkspace *m_workspace = nullptr;
    AgentCancellation *m_cancellation = nullptr;
    std::optional<StoredAnalysis> m_analysis;
    std::optional<StoredPlan> m_plan;
};

void addTool(ToolRegistry *registry,
             const QString &name,
             const QString &description,
             ToolRisk risk,
             bool mutates,
             bool preview,
             const QJsonObject &schema,
             LambdaTool::Function function)
{
    AgentToolDescriptor descriptor;
    descriptor.name = name;
    descriptor.description = description;
    descriptor.risk = risk;
    descriptor.mutatesBook = mutates;
    descriptor.supportsPreview = preview;
    descriptor.inputSchema = schema;
    registry->add(std::make_unique<LambdaTool>(descriptor, std::move(function)));
}

QJsonObject paragraphScopeSchema(bool require_analysis = false)
{
    QJsonObject properties {
        { QStringLiteral("resource_ids"), QJsonObject {
            { QStringLiteral("type"), QStringLiteral("array") },
            { QStringLiteral("items"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } }
        } }
    };
    QJsonArray required;
    if (require_analysis) {
        properties.insert(QStringLiteral("analysis_id"),
                          QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } });
        required.append(QStringLiteral("analysis_id"));
    } else {
        properties.insert(QStringLiteral("convert_blank_lines"),
                          QJsonObject { { QStringLiteral("type"), QStringLiteral("boolean") } });
        properties.insert(QStringLiteral("convert_scene_breaks"),
                          QJsonObject { { QStringLiteral("type"), QStringLiteral("boolean") } });
        properties.insert(QStringLiteral("convert_image_wrappers"),
                          QJsonObject { { QStringLiteral("type"), QStringLiteral("boolean") } });
        properties.insert(QStringLiteral("convert_nested_blocks"),
                          QJsonObject { { QStringLiteral("type"), QStringLiteral("boolean") } });
    }
    QJsonObject schema {
        { QStringLiteral("type"), QStringLiteral("object") },
        { QStringLiteral("properties"), properties }
    };
    if (!required.isEmpty()) {
        schema.insert(QStringLiteral("required"), required);
    }
    return schema;
}

} // namespace

void registerDivParagraphTools(ToolRegistry *registry,
                               IBookWorkspace *workspace,
                               AgentCancellation *cancellation)
{
    if (!registry || !workspace) {
        return;
    }
    auto service = std::make_shared<DivParagraphToolService>(workspace, cancellation);

    addTool(
        registry,
        QStringLiteral("paragraphs.analyze"),
        QStringLiteral("Analyze selected XHTML resource IDs (or all XHTML when omitted) with Sigil's native conservative DIV-paragraph and CSS-risk engine. Returns bounded source ranges and diagnostics; never changes the Book."),
        ToolRisk::Read, false, true, paragraphScopeSchema(),
        [service](const QJsonObject &arguments) { return service->analyze(arguments); });

    addTool(
        registry,
        QStringLiteral("paragraphs.plan"),
        QStringLiteral("Create a revision-bound native DIV-paragraph plan from the current analysis. Only auto-safe resources may be selected. Returns hashes and bounded source diffs; never changes the Book."),
        ToolRisk::Read, false, true, paragraphScopeSchema(true),
        [service](const QJsonObject &arguments) { return service->plan(arguments); });

    addTool(
        registry,
        QStringLiteral("paragraphs.apply"),
        QStringLiteral("Revalidate and stage exactly the reviewed native DIV-paragraph plan in an exclusive transaction. Requires matching plan_id, plan_digest, and book revision. The live Book remains unchanged until transaction.preview and transaction.commit."),
        ToolRisk::Bulk, true, true,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("plan_id"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("plan_digest"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("expected_book_revision"), QJsonObject { { QStringLiteral("type"), QStringLiteral("integer") } } }
            } },
            { QStringLiteral("required"), QJsonArray {
                QStringLiteral("plan_id"),
                QStringLiteral("plan_digest"),
                QStringLiteral("expected_book_revision")
            } }
        },
        [service](const QJsonObject &arguments) { return service->apply(arguments); });
}

} // namespace SigilAgent
