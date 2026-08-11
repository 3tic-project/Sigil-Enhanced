#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QTextStream>

#include "BuiltinPlugins/VerticalCssTransformer.h"
#include "BuiltinPlugins/VerticalLayoutAnalyzer.h"

using BuiltinPlugins::VerticalCssTransformer;
using BuiltinPlugins::VerticalLayoutAnalyzer;

namespace
{

QString readFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

bool writeFile(const QString& path, const QString& text)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(text.toUtf8()) >= 0;
}

QString modeName(VerticalLayoutAnalyzer::WritingMode mode)
{
    switch (mode) {
    case VerticalLayoutAnalyzer::WritingMode::Horizontal:
        return QStringLiteral("horizontal");
    case VerticalLayoutAnalyzer::WritingMode::Vertical:
        return QStringLiteral("vertical");
    case VerticalLayoutAnalyzer::WritingMode::Mixed:
        return QStringLiteral("mixed");
    case VerticalLayoutAnalyzer::WritingMode::Unknown:
        return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

VerticalCssTransformer::ConversionDirection parseDirection(const QString& value)
{
    return value == QStringLiteral("h2v")
        ? VerticalCssTransformer::ConversionDirection::HorizontalToVertical
        : VerticalCssTransformer::ConversionDirection::VerticalToHorizontal;
}

}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() < 2) {
        return 2;
    }

    if (args.at(1) == QStringLiteral("analyze") && args.size() == 5) {
        const auto xhtml = VerticalLayoutAnalyzer::analyzeXhtml(readFile(args.at(2)));
        const auto css = VerticalLayoutAnalyzer::analyzeCss(readFile(args.at(3)));
        const bool profile = args.at(4) == QStringLiteral("1");
        QJsonObject object;
        object.insert(QStringLiteral("ok"), xhtml.ok && css.ok);
        object.insert(QStringLiteral("mode"), modeName(
            VerticalLayoutAnalyzer::effectiveWritingMode(css, xhtml)));
        object.insert(QStringLiteral("risk"),
                      VerticalLayoutAnalyzer::combinedRiskScore(css, xhtml, profile));
        object.insert(QStringLiteral("fixed"), xhtml.fixedViewport);
        object.insert(QStringLiteral("nav"), xhtml.isNavDocument);
        object.insert(QStringLiteral("image_only"), xhtml.hasImage && xhtml.visibleTextLength <= 2);
        object.insert(QStringLiteral("script_layout"),
                      xhtml.hasScript && xhtml.absolutePositionCount > 0);
        object.insert(QStringLiteral("svg_text"), xhtml.hasSvgText);
        object.insert(QStringLiteral("visible"), xhtml.visibleTextLength);
        object.insert(QStringLiteral("vrtl"), xhtml.htmlHasVrtlClass || xhtml.bodyHasVrtlClass);
        object.insert(QStringLiteral("hltr"), xhtml.htmlHasHltrClass || xhtml.bodyHasHltrClass);
        object.insert(QStringLiteral("generated_v2h"),
                      xhtml.hasV2hOverrideClass || xhtml.hasV2hConversionMarker);
        object.insert(QStringLiteral("generated_h2v"),
                      xhtml.hasH2vOverrideClass || xhtml.hasH2vConversionMarker);
        QTextStream(stdout) << QJsonDocument(object).toJson(QJsonDocument::Compact) << '\n';
        return 0;
    }

    if (args.at(1) == QStringLiteral("xhtml") && args.size() == 7) {
        VerticalCssTransformer::Options options;
        options.direction = parseDirection(args.at(4));
        options.mode = args.at(5) == QStringLiteral("profile")
            ? VerticalCssTransformer::ConversionMode::ProfileAwareRewrite
            : VerticalCssTransformer::ConversionMode::CompatibilityOverride;
        const bool switch_class = args.at(6) == QStringLiteral("1");
        const auto result = VerticalCssTransformer::transformXhtml(
            readFile(args.at(2)), options, switch_class);
        if (!result.ok || !writeFile(args.at(3), result.text)) {
            return 3;
        }
        QTextStream(stdout) << (result.changed ? "changed" : "unchanged") << '\n';
        return 0;
    }

    if (args.at(1) == QStringLiteral("css") && args.size() == 6) {
        VerticalCssTransformer::Options options;
        options.direction = parseDirection(args.at(4));
        options.mode = args.at(5) == QStringLiteral("profile")
            ? VerticalCssTransformer::ConversionMode::ProfileAwareRewrite
            : VerticalCssTransformer::ConversionMode::CompatibilityOverride;
        const auto result = VerticalCssTransformer::transformCss(readFile(args.at(2)), options);
        if (!result.ok || !writeFile(args.at(3), result.text)) {
            return 3;
        }
        QTextStream(stdout) << (result.changed ? "changed" : "unchanged") << '\n';
        return 0;
    }

    if (args.at(1) == QStringLiteral("opf") && args.size() == 5) {
        const bool to_ltr = args.at(4) == QStringLiteral("v2h");
        const auto result = VerticalCssTransformer::transformOpfProgression(
            readFile(args.at(2)), to_ltr);
        if (!result.ok || !writeFile(args.at(3), result.text)) {
            return 3;
        }
        QTextStream(stdout) << (result.changed ? "changed" : "unchanged") << '\n';
        return 0;
    }

    return 2;
}
