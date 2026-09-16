/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef SIGIL_AGENT_PLAN_COMPARISON_DIALOG_H
#define SIGIL_AGENT_PLAN_COMPARISON_DIALOG_H

#include <QDialog>
#include <QString>

namespace SigilAgent
{

struct AgentPlanComparisonContent {
    QString windowTitle;
    QString summary;
    QString beforeTitle;
    QString afterTitle;
    QString beforeAccessibleName;
    QString afterAccessibleName;
    QString beforeText;
    QString afterText;
    QString truncationNotice;
    bool prefixTruncated = false;
    bool suffixTruncated = false;
};

class AgentPlanComparisonDialog : public QDialog
{
public:
    explicit AgentPlanComparisonDialog(const AgentPlanComparisonContent &content,
                                       QWidget *parent = nullptr);
};

} // namespace SigilAgent

#endif
