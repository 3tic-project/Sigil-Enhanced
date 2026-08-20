#include "ViewEditors/PreviewLayoutMetrics.h"
#include "ViewEditors/PreviewMetricsJavascript.h"

#include <QApplication>
#include <QEventLoop>
#include <QDebug>
#include <QTimer>
#include <QVariant>
#include <QVariantMap>
#include <QWebEnginePage>
#include <QWebEngineScript>

#include <cmath>
#include <iostream>

namespace
{

bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << std::endl;
    }
    return condition;
}

bool near(qreal actual, qreal expected)
{
    return std::abs(actual - expected) < 0.01;
}

bool loadHtml(QWebEnginePage &page, const QString &html)
{
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    bool loaded = false;
    QObject::connect(&page, &QWebEnginePage::loadFinished, &loop, [&](bool okay) {
        loaded = okay;
        loop.quit();
    });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(10000);
    page.setHtml(html);
    loop.exec();
    return loaded;
}

QVariant evaluate(QWebEnginePage &page, const QString &javascript, bool *completed)
{
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QVariant result;
    *completed = false;
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(10000);
    page.runJavaScript(javascript, QWebEngineScript::ApplicationWorld,
        [&](const QVariant &value) {
            result = value;
            *completed = true;
            loop.quit();
        });
    loop.exec();
    return result;
}

}

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QWebEnginePage page;
    const QString html = QStringLiteral(R"HTML(
<!doctype html>
<html><head><style>
html, body { margin: 0; }
body { border-top: 2px solid transparent; padding-top: 12px; }
#sample {
  display: block;
  font-size: 16px;
  line-height: 24px;
  margin-block-start: 4px;
  margin-block-end: 8px;
  padding-block-start: 2px;
  padding-block-end: 6px;
}
#normal { font-size: 18px; line-height: normal; margin: 0; padding: 0; }
</style></head><body>
<p id="sample">Measured paragraph</p>
<p id="normal">Normal line height</p>
</body></html>
)HTML");

    bool okay = expect(loadHtml(page, html), "WebEngine fixture must load");
    bool completed = false;
    const QVariant measuredResult = evaluate(
        page,
        PreviewMetricsJavascript::metricsForSelector(
            QStringLiteral("document.querySelector('#sample')")),
        &completed);
    okay &= expect(completed, "computed metrics JavaScript must complete asynchronously");
    const PreviewLayoutMetrics measured = PreviewLayoutMetrics::fromVariant(measuredResult);
    if (!measured.valid) {
        qDebug() << "metrics JavaScript result:" << measuredResult;
    }
    okay &= expect(measured.valid && measured.tagName == QStringLiteral("p"),
                   "WebEngine must return a valid current block element");
    okay &= expect(near(measured.fontSizePx, 16.0)
                       && measured.hasLineHeightPx && near(measured.lineHeightPx, 24.0),
                   "font-size and line-height must match getComputedStyle");
    okay &= expect(near(measured.marginBlockStartPx, 4.0)
                       && near(measured.marginBlockEndPx, 8.0)
                       && near(measured.paddingBlockStartPx, 2.0)
                       && near(measured.paddingBlockEndPx, 6.0),
                   "logical margin and padding must match getComputedStyle");
    okay &= expect(measured.writingMode == QStringLiteral("horizontal-tb")
                       && measured.display == QStringLiteral("block"),
                   "writing mode and display must come from computed style");

    const QVariant normalResult = evaluate(
        page,
        PreviewMetricsJavascript::metricsForSelector(
            QStringLiteral("document.querySelector('#normal')")),
        &completed);
    const PreviewLayoutMetrics normal = PreviewLayoutMetrics::fromVariant(normalResult);
    okay &= expect(completed && normal.valid && normal.lineHeightNormal
                       && !normal.hasLineHeightPx,
                   "WebEngine line-height normal must remain non-numeric");

    const QVariantMap origin = evaluate(
        page, PreviewMetricsJavascript::bodyContentOrigin(), &completed).toMap();
    bool originOkay = false;
    const qreal originCssPx = origin.value(QStringLiteral("origin")).toDouble(&originOkay);
    okay &= expect(completed && originOkay && near(originCssPx, 14.0),
                   "body content origin must include its border and padding");

    return okay ? 0 : 1;
}
