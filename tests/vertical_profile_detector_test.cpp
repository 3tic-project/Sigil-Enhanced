#include <QString>
#include <QStringList>
#include <QTextStream>

#include "BuiltinPlugins/VerticalProfileDetector.h"

using BuiltinPlugins::VerticalProfileDetector;

int fail(const QString& message)
{
    QTextStream(stderr) << "vertical_profile_detector_test: " << message << '\n';
    return 1;
}

int runTests()
{
    // ---- DPFJ/EBPAJ：成对规则 + 模板样式表 + tcy ----
    {
        const auto d = VerticalProfileDetector::detect(
            QStringList() << QStringLiteral("style-standard.css"),
            QStringList() << QStringLiteral(
                ".vrtl p { writing-mode: vertical-rl; }\n"
                ".hltr p { writing-mode: horizontal-tb; }\n"
                ".tcy { text-combine-upright: all; }\n"),
            QStringList());
        if (d.profileName != QStringLiteral("DPFJ/EBPAJ") || !d.pairedHltr || !d.canSwitchHltr) {
            return fail(QStringLiteral("DPFJ/EBPAJ detection failed: %1 (%2)")
                            .arg(d.profileName)
                            .arg(d.confidence));
        }
        if (d.profileName == QStringLiteral("DPFJ/EBPAJ") && d.confidence < 0.5) {
            return fail(QStringLiteral("DPFJ/EBPAJ confidence too low"));
        }
    }

    // ---- AozoraEpub3：生成器 metadata ----
    {
        const auto d = VerticalProfileDetector::detect(
            QStringList(),
            QStringList() << QStringLiteral("body { writing-mode: vertical-rl; }"),
            QStringList() << QStringLiteral("AozoraEpub3-JDK21 2.3.0"));
        if (d.profileName != QStringLiteral("AozoraEpub3")) {
            return fail(QStringLiteral("AozoraEpub3 detection failed: %1").arg(d.profileName));
        }
    }

    // ---- .vrtl/.hltr 分散在不同样式表时仍可识别成对模板 ----
    {
        const auto d = VerticalProfileDetector::detect(
            QStringList() << QStringLiteral("style-standard.css")
                          << QStringLiteral("style-advance.css"),
            QStringList() << QStringLiteral(".vrtl { writing-mode: vertical-rl; }")
                          << QStringLiteral(".hltr { writing-mode: horizontal-tb; }"),
            QStringList());
        if (!d.pairedHltr || !d.canSwitchHltr) {
            return fail(QStringLiteral("cross-stylesheet profile pair detection failed"));
        }
    }

    // ---- Generic：仅有纵向但未命中模板 ----
    {
        const auto d = VerticalProfileDetector::detect(
            QStringList(),
            QStringList() << QStringLiteral("body { writing-mode: vertical-rl; }"),
            QStringList());
        if (d.profileName != QStringLiteral("Generic")) {
            return fail(QStringLiteral("Generic detection failed: %1").arg(d.profileName));
        }
    }

    // ---- 无纵向信号：空 profile ----
    {
        const auto d = VerticalProfileDetector::detect(
            QStringList(),
            QStringList() << QStringLiteral("body { color: black; }"),
            QStringList());
        if (!d.profileName.isEmpty() || d.confidence != 0.0) {
            return fail(QStringLiteral("no-vertical should yield empty profile"));
        }
    }

    // ---- hasVerticalSignals ----
    if (!VerticalProfileDetector::hasVerticalSignals(
            QStringList() << QStringLiteral("body { writing-mode: vertical-rl; }"))) {
        return fail(QStringLiteral("hasVerticalSignals failed"));
    }
    if (VerticalProfileDetector::hasVerticalSignals(
            QStringList() << QStringLiteral("body { color: black; }"))) {
        return fail(QStringLiteral("hasVerticalSignals false positive"));
    }

    return 0;
}

int main()
{
    return runTests();
}
