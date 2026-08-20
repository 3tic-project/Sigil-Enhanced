/************************************************************************
**
**  Copyright (C) 2026 Sigil-Ebook Contributors
**
**  This file is part of Sigil.
**
*************************************************************************/

#include "ViewEditors/PreviewMetricsJavascript.h"

namespace
{

const QString METRICS_SCRIPT = QStringLiteral(R"JS(
(() => {
    const initial = %1;
    if (!initial) return null;
    let element = initial.nodeType === Node.ELEMENT_NODE ? initial : initial.parentElement;
    if (!element) return null;
    const relevant = 'p,div,section,article,blockquote,li,h1,h2,h3,h4,h5,h6,figure,pre,table';
    element = element.closest(relevant) || element;
    const style = getComputedStyle(element);
    const rect = element.getBoundingClientRect();
    const px = value => {
        const match = /^(-?(?:\d+|\d*\.\d+))px$/.exec(String(value).trim());
        return match ? Number(match[1]) : null;
    };
    const path = node => {
        const parts = [];
        for (let current = node; current && current.nodeType === Node.ELEMENT_NODE;
             current = current.parentElement) {
            let part = current.tagName.toLowerCase();
            if (current.id) {
                part += '#' + CSS.escape(current.id);
                parts.unshift(part);
                break;
            }
            if (current.parentElement) {
                const siblings = Array.from(current.parentElement.children)
                    .filter(sibling => sibling.tagName === current.tagName);
                if (siblings.length > 1) part += `:nth-of-type(${siblings.indexOf(current) + 1})`;
            }
            parts.unshift(part);
        }
        return parts.join('>');
    };
    const lineHeight = String(style.lineHeight).trim();
    return {
        tag: element.tagName.toLowerCase(),
        path: path(element),
        style: {
            fontSizePx: px(style.fontSize),
            lineHeightPx: px(lineHeight),
            lineHeightNormal: lineHeight === 'normal',
            marginBlockStartPx: px(style.marginBlockStart),
            marginBlockEndPx: px(style.marginBlockEnd),
            paddingBlockStartPx: px(style.paddingBlockStart),
            paddingBlockEndPx: px(style.paddingBlockEnd),
            display: style.display,
            writingMode: style.writingMode
        },
        rect: { x: rect.x, y: rect.y, width: rect.width, height: rect.height }
    };
})()
)JS");

const QString BODY_ORIGIN_SCRIPT = QStringLiteral(R"JS(
(() => {
    if (!document.body) return null;
    const style = getComputedStyle(document.body);
    const rect = document.body.getBoundingClientRect();
    const number = value => Number.parseFloat(value) || 0;
    return {
        origin: rect.top + window.scrollY + number(style.borderTopWidth) + number(style.paddingTop),
        writingMode: style.writingMode
    };
})()
)JS");

}

QString PreviewMetricsJavascript::metricsForSelector(const QString &selector)
{
    return METRICS_SCRIPT.arg(selector);
}

QString PreviewMetricsJavascript::bodyContentOrigin()
{
    return BODY_ORIGIN_SCRIPT;
}
