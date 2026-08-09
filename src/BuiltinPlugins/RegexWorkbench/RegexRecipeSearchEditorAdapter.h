/************************************************************************
**
**  Copyright (C) 2026 Sigil-Ebook contributors
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef REGEX_RECIPE_SEARCH_EDITOR_ADAPTER_H
#define REGEX_RECIPE_SEARCH_EDITOR_ADAPTER_H

#include <QString>
#include <QStringList>

#include "BuiltinPlugins/RegexWorkbench/RegexWorkbenchTypes.h"
#include "MiscEditors/SearchEditorModelPlus.h"

namespace BuiltinPlugins
{
namespace RegexWorkbench
{

struct RegexRecipeImportResult
{
    bool success = false;
    RegexWorkbenchRule rule;
    QStringList warnings;
    QString errorMessage;
};

struct RegexSearchTemplateEntry
{
    bool isGroup = false;
    QString fullName;
    QString name;
    QString prefind;
    QString find;
    QString replace;
    QString controls;
};

class RegexRecipeSearchEditorAdapter final
{
public:
    static RegexRecipeImportResult Import(const RegexSearchTemplateEntry& entry);
    static RegexRecipeImportResult Import(const SearchEditorModelPlus::searchEntry& entry);
};

}
}

#endif // REGEX_RECIPE_SEARCH_EDITOR_ADAPTER_H
