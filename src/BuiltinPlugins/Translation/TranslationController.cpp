#include "BuiltinPlugins/Translation/TranslationController.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>

namespace BuiltinPlugins::Translation {
namespace {
class Sink : public SigilAgent::ModelStreamSink {
public:
    explicit Sink(std::shared_ptr<std::atomic_bool> flag) : cancelled(std::move(flag)) {}
    void onReasoningDelta(const QString &) override {}
    void onContentDelta(const QString &) override {}
    void onToolCallsUpdated(const QList<SigilAgent::ToolCall> &) override {}
    bool isCancelled() const override { return cancelled->load(); }
    std::shared_ptr<std::atomic_bool> cancelled;
};

QStringList strings(const QJsonArray &array)
{
    QStringList result;
    for (const auto &item : array) if (item.isString()) result.append(item.toString());
    return result;
}
}

TranslationController::TranslationController(QObject *parent) : QObject(parent)
{
    m_pool.setMaxThreadCount(8);
    m_cancel = std::make_shared<std::atomic_bool>(false);
    m_completion = [](const SigilAgent::OpenAIProviderConfig &config, const SigilAgent::ModelRequest &request,
                      const std::shared_ptr<std::atomic_bool> &cancelled) {
        SigilAgent::OpenAICompatibleProvider provider(config);
        Sink sink(cancelled);
        return provider.stream(request, sink);
    };
}

TranslationController::~TranslationController()
{
    m_cancel->store(true);
    m_pool.waitForDone();
}

void TranslationController::setWorkspace(SigilAgent::IBookWorkspace *workspace)
{
    reset(); m_workspace = workspace;
}
void TranslationController::setConfigurationProvider(Configuration provider) { m_configuration = std::move(provider); }
void TranslationController::setCompletion(Completion completion) { m_completion = std::move(completion); }
void TranslationController::setFormatHandler(Format handler) { m_format = std::move(handler); }
void TranslationController::setMiscFolder(const QString &folder) { m_miscFolder = folder; }
bool TranslationController::running() const { return m_state == QLatin1String("running") || m_state == QLatin1String("paused"); }
QString TranslationController::contextId() const { return m_id; }
bool TranslationController::current() const { return m_workspace && m_workspace->bookSessionId() == m_bookSession; }
bool TranslationController::validContext(const QJsonObject &args) const
{
    return current() && !m_id.isEmpty() && (!args.contains(QStringLiteral("session_id"))
        || args.value(QStringLiteral("session_id")).toString() == m_id);
}

void TranslationController::reset()
{
    m_cancel->store(true); ++m_token;
    m_cancel = std::make_shared<std::atomic_bool>(false);
    m_id.clear(); m_bookSession.clear(); m_units.clear(); m_queue.clear(); m_resources.clear();
    m_terms = {}; m_plan = {}; m_memory.clear(); m_scanMemory.clear(); m_aggregateMemory.clear();
    m_errors.clear(); m_prepareErrors.clear(); m_resourceCounts.clear();
    m_cache.clear(); m_active = 0; m_stage = -1; m_state = QStringLiteral("idle"); m_phase.clear();
    m_requests = m_cacheHits = m_retries = m_unknownUsage = 0;
    m_inputTokens = m_outputTokens = m_cachedTokens = 0;
    m_requestBytes = 0; m_cooldownUntil = 0; m_runtimeConcurrency = 8;
    m_checkpoint = false; m_instruction.clear(); m_focusTerms.clear(); m_onlyParagraphs.clear(); m_advancePending = false; ++m_contextRevision;
    emit changed();
}

void TranslationController::recordError(const QString &message)
{
    m_errors.append(message.left(600));
    if (m_errors.size() > 50) m_errors.removeFirst();
}

TranslationController::Result TranslationController::prepare(const QJsonObject &args)
{
    if (!m_workspace) return Result::error("NO_BOOK", tr("No open book"));
    if (running()) return Result::error("TRANSLATION_RUNNING", tr("Pause/stop the current translation before preparing another range"));
    if (m_workspace->hasOpenTransaction()) return Result::error("TRANSACTION_OPEN", tr("Finish the current book transaction first"));
    const auto retainedTerms = m_terms;
    const QString retainedMemory = m_memory;
    const QJsonObject retainedPlan = m_plan;
    const bool sameBook = current();
    reset();
    if (sameBook) { m_terms = retainedTerms; m_memory = retainedMemory; m_plan = retainedPlan; }
    m_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_bookSession = m_workspace->bookSessionId();
    m_language = args.value(QStringLiteral("language")).toString(sameBook ? m_language : QStringLiteral("zh-Hans"));
    m_style = args.value(QStringLiteral("style")).toString(sameBook ? m_style : QStringLiteral("忠实通顺，保持人物口吻和日式引号")).left(2000);
    m_resources = strings(args.value(QStringLiteral("resource_ids")).toArray());
    m_wholeBook = m_resources.isEmpty();
    const auto resources = m_workspace->resources();
    QHash<QString, QJsonObject> byId;
    for (const auto &row : resources) byId.insert(row.toObject().value(QStringLiteral("resource_id")).toString(), row.toObject());
    if (m_resources.isEmpty()) {
        for (const auto &row : m_workspace->spine()) m_resources.append(row.toObject().value(QStringLiteral("resource_id")).toString());
        // EPUB navigation may be outside the spine; include it once.
        for (const auto &row : resources) {
            const auto object = row.toObject();
            const QString path = object.value(QStringLiteral("book_path")).toString();
            if (object.value(QStringLiteral("kind")).toString() == QLatin1String("xhtml")
                && (path.contains("nav", Qt::CaseInsensitive) || path.contains("toc", Qt::CaseInsensitive))) {
                m_resources.append(object.value(QStringLiteral("resource_id")).toString());
            }
        }
    }
    m_resources.removeDuplicates();
    const int selectionStart = args.value(QStringLiteral("selection_start")).toInt(-1);
    const int selectionEnd = args.value(QStringLiteral("selection_end")).toInt(-1);
    if (selectionStart >= 0 && (m_resources.size() != 1 || selectionEnd <= selectionStart)) {
        return Result::error("INVALID_RANGE", tr("Selection requires one resource and a nonempty source range"));
    }
    for (const QString &id : m_resources) {
        const auto resource = byId.value(id);
        if (resource.isEmpty()) { recordError(tr("Unknown resource: %1").arg(id)); continue; }
        if (resource.value(QStringLiteral("kind")).toString() != QLatin1String("xhtml")) continue;
        const QString source = m_workspace->resourceText(id);
        const auto parsed = extract(source);
        if (!parsed.error.isEmpty()) { recordError(resource.value("book_path").toString() + ": " + parsed.error); continue; }
        m_resourceCounts[id] = parsed.paragraphs.size();
        const int byteStart = selectionStart < 0 ? -1 : source.left(selectionStart).toUtf8().size();
        const int byteEnd = selectionEnd < 0 ? -1 : source.left(selectionEnd).toUtf8().size();
        for (int i = 0; i < parsed.paragraphs.size(); ++i) {
            const auto &paragraph = parsed.paragraphs.at(i);
            if (byteStart >= 0 && (paragraph.end <= byteStart || paragraph.start >= byteEnd)) continue;
            m_units.append({id, resource.value(QStringLiteral("book_path")).toString(), i, paragraph, false, false, {}});
        }
    }
    if (m_units.isEmpty()) { m_state = "idle"; emit changed(); return Result::error("NO_TEXT", tr("No translatable paragraphs in this range"), status()); }
    m_state = QStringLiteral("ready");
    m_prepareErrors = m_errors;
    m_plan["scope"] = m_wholeBook ? QStringLiteral("whole_book") : QStringLiteral("selection");
    m_plan["language"] = m_language; m_plan["style"] = m_style;
    emit changed();
    return Result::success(status());
}

QList<BatchText> TranslationController::texts(const QString &phase) const
{
    QList<BatchText> result;
    for (int i = 0; i < m_units.size(); ++i) {
        if (!m_onlyParagraphs.isEmpty() && !m_onlyParagraphs.contains(i)) continue;
        const auto &unit = m_units.at(i);
        if (phase == QLatin1String("translate") && !m_retranslate && !unit.paragraph.translation.isEmpty()) continue;
        if (phase == QLatin1String("review") && unit.paragraph.translation.isEmpty()) continue;
        if (phase == QLatin1String("review") && !m_focusTerms.isEmpty()) {
            bool hit = false;
            for (const auto &term : m_focusTerms) if (unit.paragraph.text.contains(term)) hit = true;
            if (!hit) continue;
        }
        result.append({i, unit.paragraph.text, unit.paragraph.translation, unit.paragraph.readings, QFileInfo(unit.path).fileName()});
    }
    return result;
}

TranslationController::Result TranslationController::run(const QJsonObject &args)
{
    if (!validContext(args)) return Result::error("NO_CONTEXT", tr("Prepare a range in the current book first"));
    if (m_units.isEmpty()) return Result::error("NO_TEXT", tr("No prepared reading paragraphs"));
    if (running()) return Result::error("TRANSLATION_RUNNING", tr("Translation already running"));
    if (!m_configuration) return Result::error("NO_MODEL", tr("Configure the existing Agent connection first"));
    const auto configuration = m_configuration();
    m_stages = strings(args.value(QStringLiteral("stages")).toArray());
    if (m_stages.isEmpty()) m_stages = {"scan", "aggregate", "translate", "review", "format"};
    const QSet<QString> allowed {"scan", "aggregate", "translate", "review", "format"};
    for (const auto &stage : m_stages) if (!allowed.contains(stage)) return Result::error("INVALID_STAGE", tr("Unknown translation stage"));
    const bool analysis = m_stages.contains("scan") || m_stages.contains("aggregate");
    const bool translation = m_stages.contains("translate") || m_stages.contains("review");
    const auto configured = [](const SigilAgent::OpenAIProviderConfig &config) {
        return !config.model.isEmpty() && !config.baseUrl.isEmpty() && !config.apiKey.isEmpty();
    };
    if ((analysis && !configured(configuration.agent)) || (translation && !configured(configuration.translator))) {
        return Result::error("NO_MODEL", tr("Configure the Agent connection, model and API key first"));
    }
    m_retranslate = args.value(QStringLiteral("retranslate")).toBool();
    m_runtimeConcurrency = qBound(1, configuration.concurrency, 8); m_cooldownUntil = 0;
    m_instruction = args.value(QStringLiteral("instruction")).toString().left(4000);
    m_focusTerms = strings(args.value(QStringLiteral("focus_terms")).toArray());
    m_onlyParagraphs.clear();
    for (const auto &index : args.value("only_paragraphs").toArray()) m_onlyParagraphs.insert(index.toInt());
    if (m_retranslate) m_cache.clear();
    m_plan[QStringLiteral("preset")] = args.value(QStringLiteral("preset")).toString(m_wholeBook ? QStringLiteral("chinese") : QStringLiteral("preserve"));
    m_plan[QStringLiteral("stages")] = QJsonArray::fromStringList(m_stages);
    m_cancel = std::make_shared<std::atomic_bool>(false); ++m_token;
    m_stage = -1; m_active = 0; m_queue.clear(); m_errors = m_prepareErrors; m_state = "running"; m_advancePending = false;
    for (auto &unit : m_units) unit.error.clear();
    const auto token = m_token;
    QTimer::singleShot(0, this, [this, token]() { if (token == m_token) dispatch(); });
    emit changed();
    return Result::success(status());
}

void TranslationController::beginPhase()
{
    if (m_state != QLatin1String("running")) return;
    if (!current()) { control({{"action", "stop"}}); return; }
    ++m_stage;
    if (m_stage >= m_stages.size()) {
        bool partial = !m_errors.isEmpty();
        for (const auto &unit : m_units) partial = partial || !unit.error.isEmpty();
        m_state = partial ? "partial" : "completed"; emit changed(); emit finished(); return;
    }
    m_phase = m_stages.at(m_stage);
    if (m_phase == QLatin1String("scan")) m_scanMemory.clear();
    if (m_phase == QLatin1String("format")) {
        auto result = format({{"preset", m_plan.value("preset")}});
        if (!result.ok) recordError(result.message);
        const auto token = m_token;
        QTimer::singleShot(0, this, [this, token]() { if (token == m_token) beginPhase(); }); return;
    }
    if (m_phase == QLatin1String("aggregate")) {
        m_aggregateMemory.clear();
        const QString summaries = m_memory + QLatin1Char('\n') + m_scanMemory.join(QLatin1Char('\n'));
        const int count = qMax(1, qMax((int(m_terms.size()) + 119) / 120, (int(summaries.size()) + 5999) / 6000));
        const int memoryChunk = (int(summaries.size()) + count - 1) / count;
        for (int i = 0; i < count; ++i) {
            Work work; work.phase = m_phase; work.token = m_token; work.contextRevision = m_contextRevision;
            for (int j = i * 120; j < qMin((i + 1) * 120, int(m_terms.size())); ++j) work.terms.append(m_terms.at(j));
            work.memory = summaries.mid(i * memoryChunk, memoryChunk);
            m_queue.append(work);
        }
    } else {
        const auto limits = m_phase == QLatin1String("scan") ? BatchLimits {10000, 15000, 512}
            : m_phase == QLatin1String("review") ? BatchLimits {6000, 9000, 96} : BatchLimits {};
        for (const auto &batch : makeBatches(texts(m_phase), limits, m_phase == QLatin1String("review"))) {
            Work work; work.phase = m_phase; work.items = batch; work.token = m_token;
            work.contextRevision = m_contextRevision; m_queue.append(work);
        }
    }
    emit changed();
    QTimer::singleShot(0, this, [this]() { dispatch(); });
}

void TranslationController::dispatch()
{
    if (m_state != QLatin1String("running")) return;
    if (!current()) { control({{"action", "stop"}}); return; }
    if (m_stage < 0) { beginPhase(); return; }
    const auto configuration = m_configuration();
    const qint64 wait = m_cooldownUntil - QDateTime::currentMSecsSinceEpoch();
    if (wait > 0) {
        const auto token = m_token;
        QTimer::singleShot(int(qMin<qint64>(wait, 5000)), this, [this, token]() { if (token == m_token) dispatch(); });
        return;
    }
    while (!m_queue.isEmpty() && m_active < qMin(m_runtimeConcurrency, qBound(1, configuration.concurrency, 8))) {
        Work work = m_queue.takeFirst();
        work.token = m_token; work.contextRevision = m_contextRevision;
        for (auto &item : work.items) item.translation = m_units.at(item.paragraph).paragraph.translation;
        const bool analysis = work.phase == QLatin1String("scan") || work.phase == QLatin1String("aggregate");
        const auto config = analysis ? configuration.agent : configuration.translator;
        QStringList neighbors;
        if (!work.items.isEmpty()) {
            const int first = work.items.first().paragraph, last = work.items.last().paragraph;
            if (first > 0 && m_units.at(first - 1).paragraph.text.size() <= 600) neighbors.append(m_units.at(first - 1).paragraph.text);
            if (last + 1 < m_units.size() && m_units.at(last + 1).paragraph.text.size() <= 600) neighbors.append(m_units.at(last + 1).paragraph.text);
        }
        auto payload = batchPayload(work.phase, work.items, analysis && work.phase == QLatin1String("aggregate") ? work.terms : m_terms,
                                    work.phase == QLatin1String("aggregate") ? work.memory : m_memory, neighbors);
        if (!m_instruction.isEmpty() && work.phase == QLatin1String("review")) payload[QStringLiteral("correction")] = m_instruction;
        SigilAgent::ModelRequest request;
        request.model = config.model; request.sessionId = m_id; request.thinking = config.thinking;
        request.reasoningEffort = config.reasoningEffort; request.reasoningProtocol = config.reasoningProtocol;
        request.includeUsage = config.requestUsage; request.timeoutMs = config.firstTokenTimeoutMs;
        const qint64 window = qMax<qint64>(4096, analysis ? configuration.agentContext : configuration.translatorContext);
        request.maxOutputTokens = int(qMin<qint64>(work.phase == QLatin1String("scan") ? 4096 : 8192, window / 3));
        request.messages = {{"system", systemPrompt(work.phase, m_language, m_style)},
                            {"user", QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact))}};
        const QByteArray wire = QJsonDocument(SigilAgent::OpenAICompatibleProvider::buildChatBody(request)).toJson(QJsonDocument::Compact);
        QJsonArray ids;
        for (const auto &item : work.items) ids.append(item.paragraph);
        const QByteArray cacheInput = m_id.toUtf8() + config.baseUrl.toUtf8()
            + QCryptographicHash::hash(config.apiKey.toUtf8(), QCryptographicHash::Sha256)
            + wire + QJsonDocument(ids).toJson(QJsonDocument::Compact);
        work.cacheKey = QCryptographicHash::hash(cacheInput, QCryptographicHash::Sha256);
        ++m_active;
        // Conservative byte-based estimate when no tokenizer is available.
        if (wire.size() > 96 * 1024 || wire.size() / 2 + request.maxOutputTokens > window) {
            retry(work, tr("Batch exceeds model context/output space; reducing batch size"), {}, true); continue;
        }
        if (const auto *cached = m_cache.object(work.cacheKey)) {
            ++m_cacheHits; accept(work, *cached); continue;
        }
        ++m_requests;
        m_requestBytes += wire.size();
        auto *watcher = new QFutureWatcher<SigilAgent::ModelTurn>(this);
        connect(watcher, &QFutureWatcher<SigilAgent::ModelTurn>::finished, this, [this, watcher, work]() {
            const auto turn = watcher->result(); watcher->deleteLater(); receive(work, turn);
        });
        const auto completion = m_completion;
        const auto cancel = m_cancel;
        watcher->setFuture(QtConcurrent::run(&m_pool, [completion, config, request, cancel]() {
            return completion(config, request, cancel);
        }));
    }
    if (m_queue.isEmpty() && m_active == 0 && m_phase == "aggregate" && m_aggregateMemory.size() > 1) {
        const auto summaries = m_aggregateMemory;
        m_aggregateMemory.clear();
        Work summary; summary.phase = "aggregate"; summary.token = m_token; summary.contextRevision = m_contextRevision;
        for (const QString &memory : summaries) {
            if (!summary.memory.isEmpty() && summary.memory.size() + memory.size() > 10000) {
                m_queue.append(summary); summary.memory.clear();
            }
            summary.memory += memory + QLatin1Char('\n');
        }
        if (!summary.memory.isEmpty() || m_queue.isEmpty()) m_queue.append(summary);
        if (m_queue.size() == 1 && m_terms.size() <= 160) m_queue.first().terms = m_terms;
        // Each response is at most 2600 characters: this reduction strictly
        // reduces the number of summaries and never discards the book's tail.
        QTimer::singleShot(0, this, [this]() { dispatch(); });
        return;
    }
    if (m_queue.isEmpty() && m_active == 0 && !m_advancePending) {
        m_advancePending = true;
        const auto token = m_token;
        QTimer::singleShot(0, this, [this, token]() {
            if (token != m_token) return;
            m_advancePending = false;
            if (m_queue.isEmpty() && m_active == 0) beginPhase();
        });
    }
    emit changed();
}

void TranslationController::receive(Work work, SigilAgent::ModelTurn turn)
{
    if (work.token != m_token || !current()) return;
    if (turn.usage.inputTokens < 0 || turn.usage.outputTokens < 0) ++m_unknownUsage;
    if (turn.usage.inputTokens >= 0) m_inputTokens += turn.usage.inputTokens;
    if (turn.usage.outputTokens >= 0) m_outputTokens += turn.usage.outputTokens;
    if (turn.usage.cachedInputTokens >= 0) m_cachedTokens += turn.usage.cachedInputTokens;
    if (m_cancel->load()) return;
    if (!turn.error.isEmpty()) {
        const QString lower = turn.error.toLower();
        if (lower.contains("context_length") || lower.contains("maximum context") || lower.contains("too many tokens")) {
            retry(work, turn.error, {}, true); return;
        }
        static const QRegularExpression permanent(QStringLiteral("\\b(?:400|401|403|404)\\b"));
        if (permanent.match(turn.error).hasMatch()) {
            work.retries = 2; retry(work, turn.error);
            m_queue.clear(); m_state = "stopped"; m_cancel->store(true); ++m_token; m_active = 0;
            emit changed(); emit finished(); return;
        }
        retry(work, turn.error); return;
    }
    if (turn.finishReason == QLatin1String("length")) { retry(work, tr("Model output truncated"), {}, true); return; }
    auto parsed = parseBatch(work.phase, turn.content, work.items.size());
    if (!parsed.error.isEmpty()) { retry(work, parsed.error, {}, true); return; }
    if (work.contextRevision != m_contextRevision) { --m_active; m_queue.append(work); dispatch(); return; }
    accept(work, std::move(parsed));
}

void TranslationController::accept(Work work, ParsedBatch parsed)
{
    if (work.token != m_token || !current() || m_cancel->load()) return;
    if ((work.phase == QLatin1String("translate") || work.phase == QLatin1String("review"))
        && m_workspace->hasOpenTransaction()) {
        QTimer::singleShot(100, this, [this, work, parsed]() { accept(work, parsed); }); return;
    }
    if (work.contextRevision != m_contextRevision) { --m_active; m_queue.append(work); dispatch(); return; }
    if (work.phase == QLatin1String("scan") || work.phase == QLatin1String("aggregate")) {
        m_terms = mergeTerms(m_terms, parsed.terms);
        if (work.phase == QLatin1String("scan")) {
            for (const auto &item : work.items) m_units[item.paragraph].scanned = true;
            m_scanMemory.append(parsed.memory);
        } else {
            m_aggregateMemory.append(parsed.memory);
            m_memory = parsed.memory.trimmed().left(2600);
        }
    } else {
        const auto result = apply(work, parsed);
        if (!result.ok) { retry(work, result.message); return; }
        if (work.phase == QLatin1String("review")) {
            for (int i = 0; i < work.items.size(); ++i) {
                const auto &item = work.items.at(i);
                if (!parsed.missing.contains(i) && m_units[item.paragraph].error.isEmpty()) m_units[item.paragraph].reviewed = true;
            }
        }
    }
    if (parsed.missing.isEmpty()) {
        m_cache.insert(work.cacheKey, new ParsedBatch(parsed));
        --m_active;
    } else {
        retry(work, tr("Missing/empty paragraph translations"), parsed.missing);
    }
    emit changed();
    QTimer::singleShot(0, this, [this]() { dispatch(); });
}

void TranslationController::retry(Work work, const QString &message, const QList<int> &missing, bool reduce)
{
    --m_active;
    QList<BatchText> pending;
    if (missing.isEmpty()) pending = work.items;
    else for (int i : missing) pending.append(work.items.at(i));
    if (work.retries < 2 && m_state != QLatin1String("stopped")) {
        ++m_retries; ++work.retries; work.items = pending;
        if (message.contains("429")) {
            m_runtimeConcurrency = qMax(1, m_runtimeConcurrency / 2);
            m_cooldownUntil = QDateTime::currentMSecsSinceEpoch() + 1000 * work.retries;
        }
        if (reduce && pending.size() > 1 && missing.isEmpty()) {
            Work second = work;
            work.items = pending.mid(0, pending.size() / 2);
            second.items = pending.mid(pending.size() / 2);
            m_queue.prepend(second);
        } else if (reduce && work.phase == "aggregate" && work.terms.size() > 1) {
            const auto terms = work.terms;
            Work second = work;
            work.terms = {}; second.terms = {};
            for (int i = 0; i < terms.size(); ++i) {
                if (i < terms.size() / 2) work.terms.append(terms.at(i)); else second.terms.append(terms.at(i));
            }
            m_queue.prepend(second);
        }
        m_queue.prepend(work);
    } else {
        recordError(work.phase + QStringLiteral(": ") + message);
        for (const auto &item : pending) { m_units[item.paragraph].error = message; m_units[item.paragraph].reviewed = false; }
    }
    emit changed();
    QTimer::singleShot(work.retries > 0 ? 250 * work.retries : 0, this, [this]() { dispatch(); });
}

TranslationController::Result TranslationController::commitTexts(const QHash<QString, QString> &texts, const QString &label)
{
    if (!current()) return Result::error("BOOK_CHANGED", tr("Current book changed"));
    if (m_workspace->hasOpenTransaction()) return Result::error("TRANSACTION_OPEN", tr("Another book transaction is active"));
    if (!m_checkpoint) {
        auto checkpoint = m_workspace->createCheckpoint(tr("Before bilingual translation"));
        if (!checkpoint.ok) return checkpoint;
        m_checkpoint = true;
    }
    auto result = m_workspace->beginTransaction(label);
    if (!result.ok) return result;
    for (auto it = texts.begin(); it != texts.end(); ++it) {
        result = m_workspace->replaceText(it.key(), it.value(), m_workspace->resourceRevision(it.key()));
        if (!result.ok) { m_workspace->rollbackTransaction(); return result; }
    }
    result = m_workspace->commitTransaction(m_workspace->revision());
    if (!result.ok) m_workspace->rollbackTransaction();
    if (result.ok) emit bookModified();
    return result;
}

TranslationController::Result TranslationController::apply(const Work &work, const ParsedBatch &parsed)
{
    QHash<QString, QList<Replacement>> byResource;
    QHash<QString, QList<int>> globals;
    QHash<QString, TextResult> observed;
    QSet<int> valid;
    for (const auto &item : work.items) {
        auto &unit = m_units[item.paragraph];
        if (!observed.contains(unit.resource)) observed.insert(unit.resource, extract(m_workspace->resourceText(unit.resource)));
        const auto &document = observed[unit.resource];
        if (!document.error.isEmpty() || document.paragraphs.size() != m_resourceCounts.value(unit.resource)
            || document.paragraphs.at(unit.index).sourceKey != unit.paragraph.sourceKey
            || document.paragraphs.at(unit.index).translation != item.translation) {
            unit.error = tr("Source or translation changed; preserving your edit");
            unit.reviewed = false;
        } else { valid.insert(item.paragraph); unit.error.clear(); }
    }
    for (auto it = parsed.texts.begin(); it != parsed.texts.end(); ++it) {
        const auto &item = work.items.at(it.key());
        if (!valid.contains(item.paragraph)) continue;
        const auto &unit = m_units.at(item.paragraph);
        byResource[unit.resource].append({unit.index, unit.paragraph.sourceKey, item.translation, it.value()});
        globals[unit.resource].append(item.paragraph);
    }
    if (byResource.isEmpty()) return Result::success({});
    QHash<QString, QString> texts;
    QSet<int> successful;
    for (auto it = byResource.begin(); it != byResource.end(); ++it) {
        const QString currentText = m_workspace->resourceText(it.key());
        const auto &currentParsed = observed[it.key()];
        if (!currentParsed.error.isEmpty() || currentParsed.paragraphs.size() != m_resourceCounts.value(it.key())) {
            for (int global : globals.value(it.key())) m_units[global].error = tr("Source structure changed; prepare this resource again");
            continue;
        }
        const auto result = writePairs(currentText, it.value(), m_language, &currentParsed);
        if (!result.error.isEmpty()) return Result::error("TRANSLATION_WRITE_FAILED", result.error);
        for (int global : globals.value(it.key())) {
            if (result.applied.contains(m_units.at(global).index)) successful.insert(global);
            else m_units[global].error = tr("Source or translation changed; preserving your edit");
        }
        if (!result.applied.isEmpty()) texts.insert(it.key(), result.text);
    }
    if (!texts.isEmpty()) {
        const auto result = commitTexts(texts, tr("Batch bilingual translation"));
        if (!result.ok) return result;
        for (auto it = parsed.texts.begin(); it != parsed.texts.end(); ++it) {
            const int global = work.items.at(it.key()).paragraph;
            if (!successful.contains(global)) continue;
            auto &unit = m_units[global];
            unit.paragraph.translation = normalizedTranslation(unit.paragraph.text, it.value());
            if (work.phase == QLatin1String("translate")) unit.reviewed = false;
            unit.error.clear();
        }
    }
    return Result::success({{"applied", successful.size()}}, !texts.isEmpty());
}

TranslationController::Result TranslationController::control(const QJsonObject &args)
{
    const QString action = args.value(QStringLiteral("action")).toString();
    if (action == QLatin1String("stop")) {
        m_cancel->store(true); ++m_token; m_queue.clear(); m_active = 0; m_state = "stopped";
        emit changed(); return Result::success(status());
    }
    if (!validContext(args)) return Result::error("NO_CONTEXT", tr("No translation in this book"));
    if (action == QLatin1String("pause") && running()) { m_state = "paused"; emit changed(); return Result::success(status()); }
    if (action == QLatin1String("resume") && m_state == QLatin1String("paused")) {
        m_state = "running"; emit changed(); QTimer::singleShot(0, this, [this]() { dispatch(); }); return Result::success(status());
    }
    if (action == QLatin1String("retry_failed") && !running()) {
        QJsonArray selected;
        for (int i = 0; i < m_units.size(); ++i) if (!m_units.at(i).error.isEmpty() || m_units.at(i).paragraph.translation.isEmpty()) selected.append(i);
        if (selected.isEmpty()) return Result::success(status());
        return run({{"stages", QJsonArray {"translate", "review"}}, {"only_paragraphs", selected}});
    }
    return Result::error("INVALID_ACTION", tr("Unknown/unavailable translation action"));
}

TranslationController::Result TranslationController::context(const QJsonObject &args)
{
    if (!validContext(args)) return Result::error("NO_CONTEXT", tr("Prepare this book first"));
    const QString kind = args.value("kind").toString("glossary");
    if (args.value("action").toString("read") == QLatin1String("update")) {
        const auto value = args.value("value");
        if (kind == QLatin1String("glossary")) {
            QString error; const auto terms = normalizeTerms(value, &error);
            if (!error.isEmpty()) return Result::error("INVALID_GLOSSARY", error);
            QJsonArray manual = terms;
            const QJsonArray original = value.isObject() ? value.toObject().value("terms").toArray() : value.toArray();
            for (int i = 0; i < manual.size(); ++i) {
                auto term = manual.at(i).toObject();
                if (!original.at(i).toObject().contains("locked") && !term.value("target").toString().isEmpty()) term["locked"] = true;
                manual[i] = term;
            }
            m_terms = mergeTerms({}, manual);
        } else if (kind == QLatin1String("memory") && value.isString()) m_memory = value.toString().left(8000);
        else if (kind == QLatin1String("plan") && value.isObject()) {
            m_plan = value.toObject();
            if (m_plan.contains("style")) m_style = m_plan.value("style").toString().left(2000);
        } else return Result::error("INVALID_CONTEXT", tr("Use plan object, memory string or glossary array"));
        for (auto &unit : m_units) unit.reviewed = false;
        ++m_contextRevision; m_cache.clear(); emit changed();
    }
    QJsonObject result;
    if (kind == QLatin1String("glossary")) {
        const int offset = qMax(0, args.value("offset").toInt());
        const int limit = qBound(1, args.value("limit").toInt(50), 200);
        QJsonArray rows;
        for (int i = offset; i < qMin(offset + limit, int(m_terms.size())); ++i) rows.append(m_terms.at(i));
        result = {{"terms", rows}, {"total", m_terms.size()}, {"next_offset", offset + rows.size()}, {"has_more", offset + rows.size() < m_terms.size()}};
    } else if (kind == QLatin1String("memory")) result["memory"] = m_memory;
    else if (kind == QLatin1String("plan")) result["plan"] = m_plan;
    else return Result::error("INVALID_CONTEXT", tr("Unknown context kind"));
    return Result::success(result);
}

TranslationController::Result TranslationController::revise(const QJsonObject &args)
{
    if (!validContext(args)) return Result::error("NO_CONTEXT", tr("Prepare this book first"));
    if (running()) return Result::error("TRANSLATION_RUNNING", tr("Stop the current run before correcting terminology"));
    const QString instruction = args.value("instruction").toString().left(4000);
    QJsonArray focus;
    if (args.contains("terms")) {
        QString error; const auto terms = normalizeTerms(args.value("terms"), &error);
        if (!error.isEmpty()) return Result::error("INVALID_GLOSSARY", error);
        for (const auto &term : terms) {
            const auto object = term.toObject();
            const QString source = object.value("source").toString();
            focus.append(source);
            bool found = false;
            for (int i = 0; i < m_terms.size(); ++i) {
                if (m_terms.at(i).toObject().value("source").toString() == source) { m_terms[i] = object; found = true; break; }
            }
            if (!found) m_terms.append(object);
        }
        ++m_contextRevision;
    }
    m_cache.clear();
    return run({{"stages", QJsonArray {"review"}}, {"instruction", instruction}, {"focus_terms", focus}});
}

TranslationController::Result TranslationController::format(const QJsonObject &args)
{
    if (!validContext(args)) return Result::error("NO_CONTEXT", tr("Prepare this book first"));
    if (m_active > 0) return Result::error("TRANSLATION_RUNNING", tr("Wait for in-flight batches before formatting"));
    if (!m_format) return Result::error("FORMAT_UNAVAILABLE", tr("Book format conversion is available in the Sigil window"));
    auto options = args;
    options["resource_ids"] = QJsonArray::fromStringList(m_resources);
    options["whole_book"] = m_wholeBook;
    if (!m_checkpoint) {
        const auto result = m_workspace->createCheckpoint(tr("Before translation formatting"));
        if (!result.ok) return result;
        m_checkpoint = true;
    }
    const auto result = m_format(options);
    if (result.ok) {
        QHash<QString, TextResult> parsed;
        for (const auto &id : m_resources) parsed.insert(id, extract(m_workspace->resourceText(id)));
        for (auto &unit : m_units) {
            const auto &document = parsed[unit.resource];
            if (document.error.isEmpty() && unit.index < document.paragraphs.size()
                && document.paragraphs.at(unit.index).text == unit.paragraph.text) unit.paragraph = document.paragraphs.at(unit.index);
        }
        m_cache.clear(); emit bookModified(); emit changed();
    }
    return result;
}

TranslationController::Result TranslationController::saveGlossary(const QJsonObject &args)
{
    if (!validContext(args)) return Result::error("NO_CONTEXT", tr("No glossary in this book"));
    if (m_workspace->hasOpenTransaction()) return Result::error("TRANSACTION_OPEN", tr("Finish the current book transaction first"));
    const QString text = QString::fromUtf8(QJsonDocument(QJsonObject {{"format", "sigil-translation-glossary-v1"},
        {"target_language", m_language}, {"terms", m_terms}}).toJson(QJsonDocument::Indented));
    const QString target = args.value("resource_id").toString();
    if (!target.isEmpty()) {
        bool misc = false;
        for (const auto &row : m_workspace->resources()) {
            const auto resource = row.toObject();
            if (resource.value("resource_id").toString() == target
                && resource.value("book_path").toString().startsWith(m_miscFolder + QLatin1Char('/'))
                && resource.value("book_path").toString().endsWith(".json", Qt::CaseInsensitive)) misc = true;
        }
        if (!misc) return Result::error("INVALID_RESOURCE", tr("Select a JSON glossary in this book's Misc folder"));
        return commitTexts({{target, text}}, tr("Update translation glossary"));
    }
    QString name = args.value("filename").toString("translation-glossary.json");
    if (name != QFileInfo(name).fileName() || !name.endsWith(".json", Qt::CaseInsensitive)) return Result::error("INVALID_PATH", tr("Use a JSON filename without a directory"));
    if (m_miscFolder.isEmpty()) return Result::error("NO_MISC_FOLDER", tr("The current book's Misc folder is unavailable"));
    QSet<QString> existing;
    for (const auto &row : m_workspace->resources()) existing.insert(row.toObject().value("book_path").toString().toCaseFolded());
    QString path = m_miscFolder + QLatin1Char('/') + name;
    for (int n = 2; existing.contains(path.toCaseFolded()); ++n) path = m_miscFolder + QLatin1Char('/') + QFileInfo(name).completeBaseName() + QStringLiteral("-%1.json").arg(n);
    auto result = m_workspace->beginTransaction(tr("Save translation glossary"));
    if (!result.ok) return result;
    result = m_workspace->createResource(path, QStringLiteral("text"), text, false, {});
    if (result.ok) result = m_workspace->commitTransaction(m_workspace->revision());
    if (!result.ok) m_workspace->rollbackTransaction();
    else { result.data["book_path"] = path; emit bookModified(); }
    return result;
}

QJsonObject TranslationController::status() const
{
    int scanned = 0, translated = 0, reviewed = 0, failed = 0;
    QJsonArray problems;
    for (const auto &unit : m_units) {
        scanned += unit.scanned; translated += !unit.paragraph.translation.isEmpty(); reviewed += unit.reviewed;
        if (!unit.error.isEmpty()) {
            ++failed;
            if (problems.size() < 20) problems.append(QJsonObject {{"resource_id", unit.resource}, {"book_path", unit.path},
                {"line", unit.paragraph.line}, {"message", unit.error}});
        }
    }
    return {{"session_id", m_id}, {"state", m_state}, {"phase", m_phase}, {"paragraphs", m_units.size()},
        {"scanned", scanned}, {"translated", translated}, {"reviewed", reviewed}, {"failed", failed},
        {"in_flight", m_active}, {"queued_batches", m_queue.size()}, {"terms", m_terms.size()},
        {"requests", m_requests}, {"local_cache_hits", m_cacheHits}, {"retries", m_retries},
        {"request_bytes", m_requestBytes}, {"effective_concurrency", m_runtimeConcurrency},
        {"input_tokens", m_inputTokens}, {"output_tokens", m_outputTokens}, {"cached_input_tokens", m_cachedTokens},
        {"unknown_usage_requests", m_unknownUsage}, {"problems", problems}, {"messages", QJsonArray::fromStringList(m_errors)},
        {"epub_saved_by_translation", false}};
}

} // namespace BuiltinPlugins::Translation
