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
            || horizontal.text.contains(QStringLiteral("se-v2h-horizontal"))
            || horizontal.text.contains(QStringLiteral("writing-mode: horizontal-tb !important"))) {
            return fail(QStringLiteral("opposite compatibility override was not restored cleanly"));
        }
    }

    // ---- profile class 切换记录来源，反向转换恢复原 class ----
    {
        VerticalCssTransformer::Options v2h;
        v2h.mode = VerticalCssTransformer::ConversionMode::ProfileAwareRewrite;
        const QString source = QStringLiteral(
            "<html xmlns=\"http://www.w3.org/1999/xhtml\" class=\"vrtl\">"
            "<head/><body><p>本文</p></body></html>");
        const auto horizontal = VerticalCssTransformer::transformXhtml(source, v2h, true);
        VerticalCssTransformer::Options h2v = v2h;
        h2v.direction = VerticalCssTransformer::ConversionDirection::HorizontalToVertical;
        h2v.mode = VerticalCssTransformer::ConversionMode::CompatibilityOverride;
        const auto restored = VerticalCssTransformer::transformXhtml(horizontal.text, h2v, false);
        if (!horizontal.ok || !restored.ok
            || !horizontal.text.contains(QStringLiteral("se-v2h-converted"))
            || !restored.text.contains(QStringLiteral("class=\"vrtl\""))
            || restored.text.contains(QStringLiteral("se-v2h-converted"))
            || restored.text.contains(QStringLiteral("se-h2v-converted"))) {
            return fail(QStringLiteral("profile conversion provenance did not restore original class"));
        }
    }

    // ---- compatibility 来源必须按来源恢复，不受当前结构化选项影响 ----
    {
        const QString source = QStringLiteral(
            "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head/><body><p>本文</p>"
            "</body></html>");
        VerticalCssTransformer::Options h2v;
        h2v.direction = VerticalCssTransformer::ConversionDirection::HorizontalToVertical;
        const auto vertical = VerticalCssTransformer::transformXhtml(source, h2v, false);
        VerticalCssTransformer::Options v2h;
        v2h.mode = VerticalCssTransformer::ConversionMode::ProfileAwareRewrite;
        const auto restored = VerticalCssTransformer::transformXhtml(vertical.text, v2h, true);
        if (!vertical.ok || !restored.ok
            || restored.text.contains(QStringLiteral("se-h2v-vertical"))
            || restored.text.contains(QStringLiteral("se-v2h-horizontal"))
            || restored.text.contains(QStringLiteral("se-v2h-converted"))) {
            return fail(QStringLiteral("compatibility provenance was changed by current mode"));
        }
    }

    // ---- transformXhtml compat：加 class + 注入 style，幂等 ----
    {
        VerticalCssTransformer::Options options;
        const QString source = sampleXhtml();
        const auto first = VerticalCssTransformer::transformXhtml(source, options, false);
        if (!first.ok || !first.changed ||
            !first.text.contains(QStringLiteral("class=\"se-v2h-horizontal\"")) ||
            !first.text.contains(QStringLiteral("data-sigil-enhanced-layout-override=\"se-v2h-horizontal\""))) {
            return fail(QStringLiteral("compat transform did not inject override"));
        }
        const auto second = VerticalCssTransformer::transformXhtml(first.text, options, false);
        if (!second.ok || second.changed || second.text != first.text) {
            return fail(QStringLiteral("compat transform is not idempotent"));
        }
    }

    // ---- 自动整页转换不改写 inline style，确保局部子流可无损往返 ----
    {
        const QString source = QStringLiteral(
            "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head/><body "
            "style=\"writing-mode: vertical-rl\"><p "
            "style=\"writing-mode: vertical-rl; margin-left: 1em; \">x</p>"
            "<table style=\"writing-mode: horizontal-tb\"><tr><td>y</td></tr></table>"
            "</body></html>");
        VerticalCssTransformer::Options v2h;
        const auto horizontal = VerticalCssTransformer::transformXhtml(source, v2h, false);
        VerticalCssTransformer::Options h2v;
        h2v.direction = VerticalCssTransformer::ConversionDirection::HorizontalToVertical;
        const auto restored = VerticalCssTransformer::transformXhtml(horizontal.text, h2v, false);
        if (!horizontal.ok || !restored.ok
            || !horizontal.text.contains(QStringLiteral(
                "style=\"writing-mode: vertical-rl; margin-left: 1em; \""))
            || !restored.text.contains(QStringLiteral(
                "style=\"writing-mode: horizontal-tb\""))
            || restored.text.contains(QStringLiteral("se-v2h-horizontal"))
            || restored.text.contains(QStringLiteral("se-h2v-vertical"))) {
            return fail(QStringLiteral("automatic round trip rewrote inline subflows"));
        }
    }

    // ---- 只删除插件精确注入的 style，不删除恰好提到类名的用户 CSS ----
    {
        const QString source = QStringLiteral(
            "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head>"
            "<style>.note::before { content: 'se-h2v-vertical'; }</style>"
            "</head><body><p>本文</p></body></html>");
        VerticalCssTransformer::Options h2v;
        h2v.direction = VerticalCssTransformer::ConversionDirection::HorizontalToVertical;
        const auto vertical = VerticalCssTransformer::transformXhtml(source, h2v, false);
        VerticalCssTransformer::Options v2h;
        const auto restored = VerticalCssTransformer::transformXhtml(vertical.text, v2h, false);
        if (!vertical.ok || !restored.ok
            || !restored.text.contains(QStringLiteral(".note::before"))
            || !restored.text.contains(QStringLiteral("content: 'se-h2v-vertical'"))) {
            return fail(QStringLiteral("reverse conversion removed user-authored style"));
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
            !result.text.contains(QStringLiteral("class=\"hltr se-v2h-converted\"")) ||
            result.text.contains(QStringLiteral("vrtl")) ||
            result.text.contains(QStringLiteral("<style")) ||
            !result.text.contains(QStringLiteral("se-v2h-converted"))) {
            return fail(QStringLiteral("profile switch transform failed"));
        }
    }

    // ---- transformInlineWritingMode ----
    {
        const QString source = QStringLiteral(
            "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head/><body>"
            "<p style=\"writing-mode: vertical-rl; margin-left: 1em; \" >x</p>"
            "<div style=\"width: 9.8em; width: fit-content; \" >y</div>"
            "</body></html>");
        const auto result = VerticalCssTransformer::transformInlineWritingMode(
            source);
        if (!result.ok || !result.changed ||
            !result.text.contains(QStringLiteral("writing-mode: horizontal-tb")) ||
            !result.text.contains(QStringLiteral("margin-left: 1em; ")) ||
            !result.text.contains(QStringLiteral("width: 9.8em; width: fit-content; "))) {
            return fail(QStringLiteral("inline writing-mode transform failed"));
        }
    }

    // ---- 双向循环不得累积无关 style 空白 ----
    {
        const QString source = QStringLiteral(
            "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head/><body>"
            "<div style=\"width: 9.8em; width: fit-content; \" >x</div>"
            "</body></html>");
        VerticalCssTransformer::Options h2v;
        h2v.direction = VerticalCssTransformer::ConversionDirection::HorizontalToVertical;
        VerticalCssTransformer::Options v2h;
        const auto vertical1 = VerticalCssTransformer::transformXhtml(source, h2v, false);
        const auto horizontal1 = VerticalCssTransformer::transformXhtml(vertical1.text, v2h, false);
        const auto vertical2 = VerticalCssTransformer::transformXhtml(horizontal1.text, h2v, false);
        const auto horizontal2 = VerticalCssTransformer::transformXhtml(vertical2.text, v2h, false);
        if (!vertical1.ok || !horizontal1.ok || !vertical2.ok || !horizontal2.ok
            || horizontal1.text != horizontal2.text
            || !horizontal2.text.contains(
                QStringLiteral("style=\"width: 9.8em; width: fit-content; \""))) {
            return fail(QStringLiteral("direction round-trip accumulated inline style drift"));
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

    // ---- transformCss：规范化 !important，只移除已启用的 vert/vrt2 ----
    {
        VerticalCssTransformer::Options options;
        const auto result = VerticalCssTransformer::transformCss(QStringLiteral(
            "body { writing-mode: vertical-rl ! IMPORTANT; }\n"
            "p { font-feature-settings: 'vert' 0, \"vrt2\" off, 'kern' 1, "
            "\"vert\" on !IMPORTANT; }\n"), options);
        if (!result.ok || !result.changed
            || !result.text.contains(QStringLiteral("writing-mode: horizontal-tb !important"))
            || !result.text.contains(QStringLiteral("'vert' 0"))
            || !result.text.contains(QStringLiteral("\"vrt2\" off"))
            || !result.text.contains(QStringLiteral("'kern' 1 !important"),
                                     Qt::CaseInsensitive)
            || result.text.contains(QStringLiteral("\"vert\" on"))) {
            return fail(QStringLiteral("important/font feature rewrite failed:\n%1").arg(result.text));
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

    // ---- transformOpfProgression：rtl -> ltr，并可精确恢复 ----
    {
        const QString source = QStringLiteral(
            "<package><spine page-progression-direction=\"rtl\"/></package>");
        const auto result = VerticalCssTransformer::transformOpfProgression(source, true);
        const auto repeated = VerticalCssTransformer::transformOpfProgression(result.text, true);
        const auto restored = VerticalCssTransformer::transformOpfProgression(result.text, false);
        if (!result.ok || !result.changed
            || !result.text.contains(QStringLiteral("original=rtl applied=ltr"))
            || !result.text.contains(QStringLiteral("page-progression-direction=\"ltr\""))
            || !repeated.ok || repeated.changed || repeated.text != result.text
            || !restored.ok || !restored.changed || restored.text != source) {
            return fail(QStringLiteral("opf progression rtl round trip failed: %1").arg(restored.text));
        }
    }

    // ---- transformOpfProgression：缺省补写后恢复为缺省 ----
    {
        const QString source = QStringLiteral("<package><spine/></package>");
        const auto result = VerticalCssTransformer::transformOpfProgression(source, true);
        const auto restored = VerticalCssTransformer::transformOpfProgression(result.text, false);
        if (!result.ok || !result.changed
            || !result.text.contains(QStringLiteral("original=absent applied=ltr"))
            || !restored.ok || restored.text != source) {
            return fail(QStringLiteral("opf absent progression round trip failed: %1").arg(restored.text));
        }
    }

    // ---- default 翻页方向可精确恢复；人工改动不得被覆盖 ----
    {
        const QString source = QStringLiteral(
            "<package><spine page-progression-direction='default'/></package>");
        const auto result = VerticalCssTransformer::transformOpfProgression(source, true);
        const auto restored = VerticalCssTransformer::transformOpfProgression(result.text, false);
        QString manually_changed = result.text;
        manually_changed.replace(QStringLiteral("page-progression-direction='ltr'"),
                                 QStringLiteral("page-progression-direction='rtl'"));
        const auto protected_result = VerticalCssTransformer::transformOpfProgression(
            manually_changed, false);
        if (!result.ok || !restored.ok || restored.text != source
            || protected_result.ok || protected_result.text != manually_changed) {
            return fail(QStringLiteral("default progression/manual edit protection failed"));
        }
    }

    // ---- 已是目标值也记录来源，反向转换恢复原值 ----
    {
        const auto result = VerticalCssTransformer::transformOpfProgression(
            QStringLiteral("<package><spine page-progression-direction=\"ltr\"/></package>"), true);
        if (!result.ok || !result.changed
            || !result.text.contains(QStringLiteral("original=ltr applied=ltr"))) {
            return fail(QStringLiteral("opf original target progression was not tracked"));
        }
    }

    // ---- 注释中的伪 spine 不得被修改 ----
    {
        const QString source = QStringLiteral(
            "<package><!-- <spine page-progression-direction=\"rtl\"/> -->"
            "<opf:spine xmlns:opf=\"urn:oebps\"/></package>");
        const auto result = VerticalCssTransformer::transformOpfProgression(source, true);
        if (!result.ok || !result.changed
            || !result.text.contains(QStringLiteral(
                "<!-- <spine page-progression-direction=\"rtl\"/> -->"))
            || !result.text.contains(QStringLiteral(
                "<opf:spine xmlns:opf=\"urn:oebps\" page-progression-direction=\"ltr\"/>"))) {
            return fail(QStringLiteral("OPF scanner modified a commented spine"));
        }
    }

    // ---- DOCTYPE 内部子集中的伪 spine 不得被修改 ----
    {
        const QString source = QStringLiteral(
            "<!DOCTYPE package [<!ENTITY fake '<spine page-progression-direction=\"rtl\"/>'>]>")
            + QStringLiteral("<package><spine/></package>");
        const auto result = VerticalCssTransformer::transformOpfProgression(source, true);
        if (!result.ok || !result.changed
            || !result.text.contains(QStringLiteral(
                "<!ENTITY fake '<spine page-progression-direction=\"rtl\"/>'>"))
            || !result.text.contains(QStringLiteral(
                "<spine page-progression-direction=\"ltr\"/>"))) {
            return fail(QStringLiteral("OPF scanner modified a DOCTYPE entity"));
        }
    }

    // ================ 反向：横排 → 竖排（H2V） ================

    // ---- 带前缀 XHTML 注入同前缀 style，保持 XHTML 命名空间 ----
    {
        const QString source = QStringLiteral(
            "<x:html xmlns:x=\"http://www.w3.org/1999/xhtml\"><x:head/>"
            "<x:body><x:p>本文</x:p></x:body></x:html>");
        VerticalCssTransformer::Options options;
        const auto result = VerticalCssTransformer::transformXhtml(source, options, false);
        if (!result.ok || !result.changed
            || !result.text.contains(QStringLiteral("<x:style"))
            || result.text.contains(QStringLiteral("<style"))) {
            return fail(QStringLiteral("prefixed XHTML override namespace was lost"));
        }
    }

    // ---- profile 反向转换遇到人工 class 改动必须拒绝覆盖 ----
    {
        VerticalCssTransformer::Options v2h;
        v2h.mode = VerticalCssTransformer::ConversionMode::ProfileAwareRewrite;
        const auto converted = VerticalCssTransformer::transformXhtml(QStringLiteral(
            "<html xmlns=\"http://www.w3.org/1999/xhtml\" class=\"vrtl\"><head/>"
            "<body><p>本文</p></body></html>"), v2h, true);
        QString manually_changed = converted.text;
        manually_changed.replace(QStringLiteral("hltr"), QStringLiteral("manual-layout"));
        VerticalCssTransformer::Options h2v = v2h;
        h2v.direction = VerticalCssTransformer::ConversionDirection::HorizontalToVertical;
        const auto protected_result = VerticalCssTransformer::transformXhtml(
            manually_changed, h2v, true);
        if (!converted.ok || protected_result.ok
            || protected_result.text != manually_changed) {
            return fail(QStringLiteral("profile manual class edit was overwritten"));
        }
    }

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

    // ---- transformXhtml(H2V)：保留所有 inline，由可逆 override 控制根流 ----
    {
        VerticalCssTransformer::Options options;
        options.direction = VerticalCssTransformer::ConversionDirection::HorizontalToVertical;
        const auto result = VerticalCssTransformer::transformXhtml(QStringLiteral(
            "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head/>"
            "<body style=\"writing-mode: horizontal-tb\"><table "
            "style=\"writing-mode: horizontal-tb\"><tr><td>x</td></tr></table>"
            "</body></html>"), options, false);
        if (!result.ok || !result.changed
            || result.text.count(QStringLiteral("style=\"writing-mode: horizontal-tb\"")) != 2) {
            return fail(QStringLiteral("h2v automatic conversion rewrote inline style"));
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
        if (!result.ok || !result.changed
            || !result.text.contains(QStringLiteral("original=ltr applied=rtl"))
            || !result.text.contains(QStringLiteral("page-progression-direction=\"rtl\""))) {
            return fail(QStringLiteral("opf progression ltr->rtl failed"));
        }
    }

    return 0;
}

int main()
{
    return runTests();
}
