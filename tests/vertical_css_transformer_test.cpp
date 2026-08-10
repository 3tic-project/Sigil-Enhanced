#include <QString>
#include <QTextStream>

#include "BuiltinPlugins/VerticalCssTransformer.h"

using BuiltinPlugins::VerticalCssTransformer;

int fail(const QString& message)
{
    QTextStream(stderr) << "vertical_css_transformer_test: " << message << '\n';
    return 1;
}

QString sampleXhtml()
{
    return QStringLiteral(
        "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>t</title></head>"
        "<body class=\"vrtl\"><p style=\"writing-mode: vertical-rl\">こんにちは</p></body></html>");
}

int runTests()
{
    // ---- buildOverrideCss ----
    {
        const QString css = VerticalCssTransformer::buildOverrideCss();
        if (!css.contains(QStringLiteral("horizontal-tb")) ||
            !css.contains(QStringLiteral("se-v2h-horizontal")) ||
            !css.contains(QStringLiteral("html.se-v2h-horizontal *"))) {
            return fail(QStringLiteral("override css content wrong"));
        }
    }

    // ---- transformXhtml profile：body-only vrtl 也必须报告变化 ----
    {
        VerticalCssTransformer::Options options;
        options.mode = VerticalCssTransformer::ConversionMode::ProfileAwareRewrite;
        const QString source = QStringLiteral(
            "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head/><body class=\"vrtl\">"
            "<p>本文</p></body></html>");
        const auto result = VerticalCssTransformer::transformXhtml(source, options, true);
        if (!result.ok || !result.changed
            || !result.text.contains(QStringLiteral("class=\"hltr\""))
            || result.text.contains(QStringLiteral("class=\"vrtl\""))) {
            return fail(QStringLiteral("body-only profile class switch was lost"));
        }
    }

    // ---- profile 判定为成对但页面无布局 class 时回退兼容覆盖 ----
    {
        VerticalCssTransformer::Options options;
        options.mode = VerticalCssTransformer::ConversionMode::ProfileAwareRewrite;
        const QString source = QStringLiteral(
            "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head/><body><p>本文</p></body></html>");
        const auto result = VerticalCssTransformer::transformXhtml(source, options, true);
        if (!result.ok || !result.changed
            || !result.text.contains(QStringLiteral("se-v2h-horizontal"))
            || !result.text.contains(QStringLiteral("writing-mode: horizontal-tb !important"))) {
            return fail(QStringLiteral("classless profile page did not fall back to override"));
        }
    }

    // ---- 兼容覆盖可逆：反向转换清除旧方向 class/style ----
    {
        VerticalCssTransformer::Options h2v;
        h2v.direction = VerticalCssTransformer::ConversionDirection::HorizontalToVertical;
        const QString source = QStringLiteral(
            "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head/><body><p>本文</p></body></html>");
        const auto vertical = VerticalCssTransformer::transformXhtml(source, h2v, false);
        VerticalCssTransformer::Options v2h;
        const auto horizontal = VerticalCssTransformer::transformXhtml(vertical.text, v2h, false);
        if (!vertical.ok || !horizontal.ok || !horizontal.changed
            || horizontal.text.contains(QStringLiteral("se-h2v-vertical"))
            || !horizontal.text.contains(QStringLiteral("se-v2h-horizontal"))) {
            return fail(QStringLiteral("opposite compatibility override was not replaced"));
        }
    }

    // ---- transformXhtml compat：加 class + 注入 style，幂等 ----
    {
        VerticalCssTransformer::Options options;
        const QString source = sampleXhtml();
        const auto first = VerticalCssTransformer::transformXhtml(source, options, false);
        if (!first.ok || !first.changed ||
            !first.text.contains(QStringLiteral("class=\"se-v2h-horizontal\"")) ||
            !first.text.contains(QStringLiteral("<style type=\"text/css\">"))) {
            return fail(QStringLiteral("compat transform did not inject override"));
        }
        const auto second = VerticalCssTransformer::transformXhtml(first.text, options, false);
        if (!second.ok || second.changed || second.text != first.text) {
            return fail(QStringLiteral("compat transform is not idempotent"));
        }
    }

    // ---- transformXhtml profile：vrtl -> hltr ----
    {
        VerticalCssTransformer::Options options;
        options.mode = VerticalCssTransformer::ConversionMode::ProfileAwareRewrite;
        const QString source = QStringLiteral(
            "<html xmlns=\"http://www.w3.org/1999/xhtml\" class=\"vrtl\"><head><title>t</title></head>"
            "<body><p>こんにちは</p></body></html>");
        const auto result = VerticalCssTransformer::transformXhtml(source, options, true);
        if (!result.ok || !result.changed ||
            !result.text.contains(QStringLiteral("class=\"hltr\"")) ||
            result.text.contains(QStringLiteral("vrtl")) ||
            result.text.contains(QStringLiteral("<style"))) {
            return fail(QStringLiteral("profile switch transform failed"));
        }
    }

    // ---- transformInlineWritingMode ----
    {
        const auto result = VerticalCssTransformer::transformInlineWritingMode(
            QStringLiteral("<html xmlns=\"http://www.w3.org/1999/xhtml\"><head/><body>"
                           "<p style=\"writing-mode: vertical-rl; margin-left: 1em\">x</p>"
                           "</body></html>"));
        if (!result.ok || !result.changed ||
            !result.text.contains(QStringLiteral("writing-mode: horizontal-tb")) ||
            !result.text.contains(QStringLiteral("margin-left: 1em"))) {
            return fail(QStringLiteral("inline writing-mode transform failed"));
        }
    }

    // ---- transformCss：writing-mode 值改写 + 清除 vertical-only ----
    {
        VerticalCssTransformer::Options options;
        const auto result = VerticalCssTransformer::transformCss(QStringLiteral(
            "body { writing-mode: vertical-rl; }\n"
            ".vrtl p { -epub-writing-mode: vertical-rl !important; text-combine-upright: all; }\n"
            "p { font-feature-settings: \"vert\" 1, \"kern\" 1; text-orientation: upright; }\n"), options);
        if (!result.ok || !result.changed ||
            !result.text.contains(QStringLiteral("writing-mode: horizontal-tb")) ||
            result.text.contains(QStringLiteral("vertical-rl")) ||
            result.text.contains(QStringLiteral("text-combine-upright")) ||
            result.text.contains(QStringLiteral("text-orientation")) ||
            result.text.contains(QStringLiteral("vert")) ||
            !result.text.contains(QStringLiteral("\"kern\" 1")) ||
            !result.text.contains(QStringLiteral("!important"))) {
            return fail(QStringLiteral("transformCss rewrite failed:\n%1").arg(result.text));
        }
    }

    // ---- transformCss：不触碰未知物理 margin ----
    {
        VerticalCssTransformer::Options options;
        const auto result = VerticalCssTransformer::transformCss(QStringLiteral(
            "body { margin-left: 2em; margin-top: 1em; }\n"), options);
        if (!result.ok || result.changed || result.text != QStringLiteral(
                "body { margin-left: 2em; margin-top: 1em; }\n")) {
            return fail(QStringLiteral("unknown physical margin must not be rewritten"));
        }
    }

    // ---- transformCss：replaced element 尺寸保护 ----
    {
        VerticalCssTransformer::Options options;
        const auto result = VerticalCssTransformer::transformCss(QStringLiteral(
            "img { width: 200px; height: 300px; }\n"), options);
        if (!result.ok || result.changed) {
            return fail(QStringLiteral("img width/height must not be swapped"));
        }
    }

    // ---- transformCss：解析失败不写回 ----
    {
        VerticalCssTransformer::Options options;
        const auto result = VerticalCssTransformer::transformCss(QStringLiteral(
            "body { writing-mode: vertical-rl; }\np { color: red; @#$% }\n"), options);
        if (result.ok) {
            return fail(QStringLiteral("malformed css must not be written back"));
        }
    }

    // ---- transformOpfProgression：rtl -> ltr ----
    {
        const auto result = VerticalCssTransformer::transformOpfProgression(
            QStringLiteral("<package><spine page-progression-direction=\"rtl\"/></package>"), true);
        if (!result.ok || !result.changed ||
            result.text != QStringLiteral("<package><spine page-progression-direction=\"ltr\"/></package>")) {
            return fail(QStringLiteral("opf progression rtl->ltr failed: %1").arg(result.text));
        }
    }

    // ---- transformOpfProgression：缺省补写（自闭合标签） ----
    {
        const auto result = VerticalCssTransformer::transformOpfProgression(
            QStringLiteral("<package><spine/></package>"), true);
        if (!result.ok || !result.changed ||
            result.text != QStringLiteral("<package><spine page-progression-direction=\"ltr\"/></package>")) {
            return fail(QStringLiteral("opf progression add-on-self-closing failed: %1").arg(result.text));
        }
    }

    // ---- transformOpfProgression：已 ltr 时无变化 ----
    {
        const auto result = VerticalCssTransformer::transformOpfProgression(
            QStringLiteral("<package><spine page-progression-direction=\"ltr\"/></package>"), true);
        if (!result.ok || result.changed) {
            return fail(QStringLiteral("opf progression already-ltr should be a no-op"));
        }
    }

    // ================ 反向：横排 → 竖排（H2V） ================

    // ---- buildOverrideCss(H2V) ----
    {
        const QString css = VerticalCssTransformer::buildOverrideCss(
            VerticalCssTransformer::ConversionDirection::HorizontalToVertical);
        if (!css.contains(QStringLiteral("vertical-rl")) ||
            !css.contains(QStringLiteral("se-h2v-vertical"))) {
            return fail(QStringLiteral("h2v override css content wrong"));
        }
    }

    // ---- transformInlineWritingMode(H2V) ----
    {
        const auto result = VerticalCssTransformer::transformInlineWritingMode(
            QStringLiteral("<html xmlns=\"http://www.w3.org/1999/xhtml\"><head/><body>"
                           "<p style=\"writing-mode: horizontal-tb\">x</p></body></html>"),
            VerticalCssTransformer::ConversionDirection::HorizontalToVertical);
        if (!result.ok || !result.changed ||
            !result.text.contains(QStringLiteral("writing-mode: vertical-rl"))) {
            return fail(QStringLiteral("h2v inline writing-mode transform failed"));
        }
    }

    // ---- transformCss(H2V)：horizontal-tb -> vertical-rl，且不清除纵排特性 ----
    {
        VerticalCssTransformer::Options options;
        options.direction = VerticalCssTransformer::ConversionDirection::HorizontalToVertical;
        const auto result = VerticalCssTransformer::transformCss(QStringLiteral(
            "body { writing-mode: horizontal-tb; }\n"
            "p { text-combine-upright: all; }\n"), options);
        if (!result.ok || !result.changed ||
            !result.text.contains(QStringLiteral("writing-mode: vertical-rl")) ||
            result.text.contains(QStringLiteral("horizontal-tb")) ||
            !result.text.contains(QStringLiteral("text-combine-upright: all"))) {
            return fail(QStringLiteral("h2v transformCss failed:\n%1").arg(result.text));
        }
    }

    // ---- transformCss(H2V)：默认保留子流横排 ----
    {
        VerticalCssTransformer::Options options;
        options.direction = VerticalCssTransformer::ConversionDirection::HorizontalToVertical;
        const auto result = VerticalCssTransformer::transformCss(QStringLiteral(
            "body { writing-mode: horizontal-tb; }\n"
            "table { writing-mode: horizontal-tb; }\n"), options);
        if (!result.ok || !result.changed
            || !result.text.contains(QStringLiteral("body { writing-mode: vertical-rl; }"))
            || !result.text.contains(QStringLiteral("table { writing-mode: horizontal-tb; }"))) {
            return fail(QStringLiteral("h2v horizontal subflow preservation failed:\n%1")
                            .arg(result.text));
        }
    }

    // ---- transformXhtml(H2V)：root inline 改写但保留子流 inline ----
    {
        VerticalCssTransformer::Options options;
        options.direction = VerticalCssTransformer::ConversionDirection::HorizontalToVertical;
        const auto result = VerticalCssTransformer::transformXhtml(QStringLiteral(
            "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head/>"
            "<body style=\"writing-mode: horizontal-tb\"><table "
            "style=\"writing-mode: horizontal-tb\"><tr><td>x</td></tr></table>"
            "</body></html>"), options, false);
        if (!result.ok || !result.changed
            || !result.text.contains(QStringLiteral("style=\"writing-mode: vertical-rl\""))
            || !result.text.contains(QStringLiteral("style=\"writing-mode: horizontal-tb\""))) {
            return fail(QStringLiteral("h2v inline subflow preservation failed"));
        }
    }

    // ---- transformXhtml(H2V compat)：加类 + 注入纵向 override ----
    {
        VerticalCssTransformer::Options options;
        options.direction = VerticalCssTransformer::ConversionDirection::HorizontalToVertical;
        const QString source = QStringLiteral(
            "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>t</title></head>"
            "<body><p>こんにちは</p></body></html>");
        const auto result = VerticalCssTransformer::transformXhtml(source, options, false);
        if (!result.ok || !result.changed ||
            !result.text.contains(QStringLiteral("class=\"se-h2v-vertical\"")) ||
            !result.text.contains(QStringLiteral("writing-mode: vertical-rl !important"))) {
            return fail(QStringLiteral("h2v compat transform failed"));
        }
        const auto second = VerticalCssTransformer::transformXhtml(result.text, options, false);
        if (!second.ok || second.changed || second.text != result.text) {
            return fail(QStringLiteral("h2v compat transform is not idempotent"));
        }
    }

    // ---- switchLayoutClass(H2V)：hltr -> vrtl ----
    {
        const auto result = VerticalCssTransformer::switchLayoutClass(
            QStringLiteral("<html xmlns=\"http://www.w3.org/1999/xhtml\" class=\"hltr\"><head/><body>"
                           "<p>x</p></body></html>"),
            VerticalCssTransformer::ConversionDirection::HorizontalToVertical);
        if (!result.ok || !result.changed ||
            !result.text.contains(QStringLiteral("class=\"vrtl\""))) {
            return fail(QStringLiteral("h2v layout class switch failed"));
        }
    }

    // ---- transformOpfProgression：ltr -> rtl ----
    {
        const auto result = VerticalCssTransformer::transformOpfProgression(
            QStringLiteral("<package><spine page-progression-direction=\"ltr\"/></package>"), false);
        if (!result.ok || !result.changed ||
            result.text != QStringLiteral("<package><spine page-progression-direction=\"rtl\"/></package>")) {
            return fail(QStringLiteral("opf progression ltr->rtl failed"));
        }
    }

    return 0;
}

int main()
{
    return runTests();
}
