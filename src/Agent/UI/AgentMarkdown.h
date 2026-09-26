/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef SIGIL_AGENT_MARKDOWN_H
#define SIGIL_AGENT_MARKDOWN_H

#include <QString>

class QFont;

namespace SigilAgent
{

// Messages above this many UTF-16 units stay plain text so one answer cannot stall the GUI.
constexpr int AGENT_MARKDOWN_RENDER_BUDGET = 64 * 1024;

struct AgentMarkdownRender {
    bool rendered = false;
    bool overBudget = false;
    QString html;
    int allowedLinks = 0;
    int blockedLinks = 0;
    int blockedImages = 0;
    qint64 elapsedMs = 0;
};

QString agentLocationHref(const QString &location_id);
QString agentLocationIdFromHref(const QString &href);

// Parses GitHub-flavoured Markdown without raw HTML, then removes every image
// and every link that is not a host-issued sigil-agent location.
AgentMarkdownRender renderAgentMarkdown(const QString &markdown, const QFont &font);

} // namespace SigilAgent

#endif
