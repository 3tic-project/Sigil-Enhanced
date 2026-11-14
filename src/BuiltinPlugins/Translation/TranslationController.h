#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <QObject>
#include <QCache>
#include <QThreadPool>
#include <QSet>

#include "Agent/Execution/IBookWorkspace.h"
#include "Agent/Model/OpenAICompatibleProvider.h"
#include "BuiltinPlugins/Translation/TranslationBatch.h"
#include "BuiltinPlugins/Translation/TranslationText.h"

namespace BuiltinPlugins::Translation {

struct RequestConfiguration {
    SigilAgent::OpenAIProviderConfig agent;
    SigilAgent::OpenAIProviderConfig translator;
    qint64 agentContext = 32768;
    qint64 translatorContext = 32768;
    int concurrency = 4;
};

class TranslationController : public QObject
{
    Q_OBJECT
public:
    using Result = SigilAgent::BookOpResult;
    using Configuration = std::function<RequestConfiguration()>;
    using Completion = std::function<SigilAgent::ModelTurn(const SigilAgent::OpenAIProviderConfig &,
        const SigilAgent::ModelRequest &, const std::shared_ptr<std::atomic_bool> &)>;
    using Format = std::function<Result(const QJsonObject &)>;

    explicit TranslationController(QObject *parent = nullptr);
    ~TranslationController() override;
    void setWorkspace(SigilAgent::IBookWorkspace *workspace);
    void setConfigurationProvider(Configuration provider);
    void setCompletion(Completion completion); // Injectable model transport for offline tests.
    void setFormatHandler(Format handler);
    void setMiscFolder(const QString &folder);
    void reset();

    Result prepare(const QJsonObject &arguments = {});
    Result run(const QJsonObject &arguments = {});
    Result control(const QJsonObject &arguments);
    Result context(const QJsonObject &arguments);
    Result revise(const QJsonObject &arguments);
    Result format(const QJsonObject &arguments = {});
    Result saveGlossary(const QJsonObject &arguments = {});
    QJsonObject status() const;
    bool running() const;
    QString contextId() const;

signals:
    void changed();
    void bookModified();
    void finished();

private:
    struct Unit {
        QString resource;
        QString path;
        int index = 0;
        Paragraph paragraph;
        bool scanned = false;
        bool reviewed = false;
        QString error;
    };
    struct Work {
        QString phase;
        QList<BatchText> items;
        QJsonArray terms;
        QString memory;
        int retries = 0;
        quint64 token = 0;
        quint64 contextRevision = 0;
        QByteArray cacheKey;
    };
    void beginPhase();
    void dispatch();
    void receive(Work work, SigilAgent::ModelTurn turn);
    void accept(Work work, ParsedBatch parsed);
    void retry(Work work, const QString &message, const QList<int> &missing = {}, bool reduce = false);
    Result apply(const Work &work, const ParsedBatch &parsed);
    Result commitTexts(const QHash<QString, QString> &texts, const QString &label);
    bool current() const;
    bool validContext(const QJsonObject &arguments) const;
    void recordError(const QString &message);
    QList<BatchText> texts(const QString &phase) const;

    SigilAgent::IBookWorkspace *m_workspace = nullptr;
    QString m_bookSession;
    QString m_id;
    QString m_state = QStringLiteral("idle");
    QString m_phase;
    QString m_language = QStringLiteral("zh-Hans");
    QString m_style = QStringLiteral("忠实通顺，保持人物口吻和日式引号");
    QString m_memory;
    QJsonObject m_plan;
    QJsonArray m_terms;
    QStringList m_scanMemory;
    QStringList m_aggregateMemory;
    QStringList m_stages;
    QStringList m_resources;
    QList<Unit> m_units;
    QList<Work> m_queue;
    QHash<QString, int> m_resourceCounts;
    QStringList m_errors;
    QStringList m_prepareErrors;
    QThreadPool m_pool;
    QCache<QByteArray, ParsedBatch> m_cache {64};
    Configuration m_configuration;
    Completion m_completion;
    Format m_format;
    QString m_miscFolder;
    std::shared_ptr<std::atomic_bool> m_cancel;
    quint64 m_token = 0;
    quint64 m_contextRevision = 0;
    int m_active = 0;
    int m_stage = -1;
    int m_requests = 0;
    int m_cacheHits = 0;
    int m_retries = 0;
    int m_unknownUsage = 0;
    qint64 m_inputTokens = 0;
    qint64 m_outputTokens = 0;
    qint64 m_cachedTokens = 0;
    bool m_wholeBook = true;
    bool m_retranslate = false;
    bool m_checkpoint = false;
    QString m_instruction;
    QStringList m_focusTerms;
    QSet<int> m_onlyParagraphs;
    bool m_advancePending = false;
    int m_runtimeConcurrency = 8;
    qint64 m_cooldownUntil = 0;
    qint64 m_requestBytes = 0;
};

} // namespace BuiltinPlugins::Translation
