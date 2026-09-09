#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTextStream>

#include "BuiltinPlugins/DivParagraphNormalizationPlan.h"

using BuiltinPlugins::BookLiveParagraphNormalizer;
using BuiltinPlugins::DivParagraphNormalizationPlan;

namespace
{

QString largeXhtml(int resource_index, int paragraph_count)
{
    const QString payload = QStringLiteral("縦書き本文と全角空格　").repeated(6);
    QString paragraphs;
    paragraphs.reserve(paragraph_count * (payload.length() + 100));
    for (int paragraph = 0; paragraph < paragraph_count; ++paragraph) {
        paragraphs += QStringLiteral(
            "<div class=\"para\" data-resource=\"%1\" data-order=\"%2\">"
            "　%3<ruby>漢<rp>（</rp><rt>かん</rt><rp>）</rp></ruby>。</div>\n")
            .arg(resource_index)
            .arg(paragraph)
            .arg(payload);
    }
    return QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE html>\n"
        "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head>"
        "<title>Performance</title><style>div, p { margin: 0; padding: 0; }</style>"
        "</head><body><div class=\"main\"><div class=\"content\">") +
        paragraphs + QStringLiteral("</div></div></body></html>");
}

int fail(const QString& message)
{
    QTextStream(stderr) << message << '\n';
    return 1;
}

}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    constexpr int resource_count = 200;
    constexpr int paragraphs_per_resource = 330;
    QVector<DivParagraphNormalizationPlan::Input> inputs;
    inputs.reserve(resource_count);
    qsizetype source_bytes = 0;
    for (int resource = 0; resource < resource_count; ++resource) {
        const QString source = largeXhtml(resource, paragraphs_per_resource);
        source_bytes += source.toUtf8().size();
        inputs << DivParagraphNormalizationPlan::Input {
            QStringLiteral("Text/performance-%1.xhtml").arg(resource),
            source,
            QStringLiteral("revision-%1").arg(resource),
            {}
        };
    }
    if (source_bytes < 20 * 1024 * 1024) {
        return fail(QStringLiteral("performance fixture is smaller than 20 MiB: %1 bytes")
                        .arg(source_bytes));
    }

    int last_progress = -1;
    QElapsedTimer timer;
    timer.start();
    const DivParagraphNormalizationPlan::Result plan =
        DivParagraphNormalizationPlan::build(
            inputs, BookLiveParagraphNormalizer::Options::conservative(),
            [&last_progress](int completed, int total) {
                if (completed < last_progress || total != resource_count) {
                    return false;
                }
                last_progress = completed;
                return true;
            });
    const qint64 elapsed_ms = timer.elapsed();

    QTextStream(stdout)
        << "DIV paragraph 20 MiB/200-file plan baseline: " << elapsed_ms
        << " ms, " << source_bytes << " input bytes, "
        << plan.conversionCount << " conversions\n";
    if (!plan.ok || plan.cancelled || plan.applyFiles != resource_count ||
        plan.conversionCount != resource_count * paragraphs_per_resource ||
        last_progress != resource_count) {
        return fail(QStringLiteral("large plan result or progress reporting is incomplete"));
    }
    if (elapsed_ms > 30000) {
        return fail(QStringLiteral("20 MiB/200-file plan exceeded 30 second budget: %1 ms")
                        .arg(elapsed_ms));
    }
    return 0;
}
