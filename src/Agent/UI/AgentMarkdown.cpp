/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/UI/AgentMarkdown.h"

#include <algorithm>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFont>
#include <QList>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>

namespace SigilAgent
{

namespace
{

const QString LOCATION_HREF_PREFIX = QStringLiteral("sigil-agent://location/");

struct FragmentEdit {
    int start = 0;
    int end = 0;
    bool image = false;
    QTextCharFormat format;
};

} // namespace

QString agentLocationHref(const QString &location_id)
{
    return LOCATION_HREF_PREFIX + location_id;
}

QString agentLocationIdFromHref(const QString &href)
{
    static const QRegularExpression pattern(
        QStringLiteral("\\Asigil-agent://location/([0-9a-f]{32})\\z"));
    const QRegularExpressionMatch match = pattern.match(href);
    return match.hasMatch() ? match.captured(1) : QString();
}

AgentMarkdownRender renderAgentMarkdown(const QString &markdown, const QFont &font)
{
    AgentMarkdownRender result;
    if (markdown.size() > AGENT_MARKDOWN_RENDER_BUDGET) {
        result.overBudget = true;
        return result;
    }
    QElapsedTimer timer;
    timer.start();

    QTextDocument document;
    document.setDefaultFont(font);
    document.setMarkdown(markdown, QTextDocument::MarkdownFeatures(
        QTextDocument::MarkdownDialectGitHub | QTextDocument::MarkdownNoHTML));

    QList<FragmentEdit> edits;
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid()) continue;
            const QTextCharFormat format = fragment.charFormat();
            FragmentEdit edit;
            edit.start = fragment.position();
            edit.end = fragment.position() + fragment.length();
            edit.format = format;
            if (format.isImageFormat()) {
                edit.image = true;
                edits.append(edit);
                ++result.blockedImages;
            } else if (format.isAnchor() && !format.anchorHref().isEmpty()) {
                if (!agentLocationIdFromHref(format.anchorHref()).isEmpty()) {
                    ++result.allowedLinks;
                } else {
                    edits.append(edit);
                    ++result.blockedLinks;
                }
            }
        }
    }

    std::sort(edits.begin(), edits.end(), [](const FragmentEdit &a, const FragmentEdit &b) {
        return a.start > b.start;
    });
    for (const FragmentEdit &edit : edits) {
        QTextCursor cursor(&document);
        cursor.setPosition(edit.start);
        cursor.setPosition(edit.end, QTextCursor::KeepAnchor);
        if (edit.image) {
            QTextCharFormat plain;
            plain.setFontItalic(true);
            cursor.insertText(QCoreApplication::translate(
                "SigilAgent::AgentDock", "[image not loaded]"), plain);
            continue;
        }
        QTextCharFormat format = edit.format;
        format.setAnchor(false);
        format.setAnchorHref(QString());
        format.setAnchorNames(QStringList());
        format.clearForeground();
        format.setFontUnderline(false);
        format.setToolTip(QString());
        cursor.setCharFormat(format);
    }

    result.html = document.toHtml();
    result.rendered = true;
    result.elapsedMs = timer.elapsed();
    return result;
}

} // namespace SigilAgent
