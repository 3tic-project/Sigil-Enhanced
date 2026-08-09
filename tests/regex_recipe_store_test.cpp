#include <cstdlib>
#include <iostream>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "BuiltinPlugins/RegexWorkbench/RegexRecipeSearchEditorAdapter.h"
#include "BuiltinPlugins/RegexWorkbench/RegexRecipeStore.h"
#include "Misc/Utility.h"

namespace
{
QString gPreferencesDirectory;
}

QString Utility::DefinePrefsDir()
{
    return gPreferencesDirectory;
}

namespace
{

using BuiltinPlugins::RegexWorkbench::RegexRecipe;
using BuiltinPlugins::RegexWorkbench::RegexRecipeLimits;
using BuiltinPlugins::RegexWorkbench::RegexRecipeSearchEditorAdapter;
using BuiltinPlugins::RegexWorkbench::RegexRecipeStore;
using BuiltinPlugins::RegexWorkbench::RegexSearchTemplateEntry;
using BuiltinPlugins::RegexWorkbench::RegexWorkbenchRule;
using BuiltinPlugins::RegexWorkbench::SecondaryMode;
using BuiltinPlugins::RegexWorkbench::VariableScope;
using BuiltinPlugins::RegexWorkbench::WritePolicy;

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

RegexRecipe CompleteRecipe()
{
    RegexRecipe recipe;
    recipe.name = QStringLiteral("正文标点清洗");
    recipe.variableScope = VariableScope::Session;
    recipe.writePolicy = WritePolicy::Append;
    RegexWorkbenchRule rule;
    rule.id = QStringLiteral("r1");
    rule.name = QStringLiteral("collapse");
    rule.find = QStringLiteral("，+");
    rule.replace = QStringLiteral("${var:tail}，");
    rule.secondaryMode = SecondaryMode::PreSearch;
    rule.secondaryPattern = QStringLiteral("<p>([\\s\\S]*?)</p>");
    rule.recursive = true;
    rule.maxIterations = 16;
    rule.allowEmpty = true;
    rule.variableExpansionEnabled = true;
    rule.autoIngestNamedCaptures = true;
    rule.captureToVar = {QStringLiteral("tail")};
    rule.enabled = false;
    recipe.rules.append(rule);
    return recipe;
}

void TestRoundTripPreservesSchemaFields()
{
    const RegexRecipe source = CompleteRecipe();
    QString error;
    const QByteArray json = RegexRecipeStore::Serialize(source, &error);
    Require(!json.isEmpty() && error.isEmpty(),
            "valid recipes must serialize");

    RegexRecipe loaded;
    Require(RegexRecipeStore::Deserialize(json, loaded, &error),
            "serialized recipes must deserialize");
    Require(loaded.name == source.name && loaded.variableScope == VariableScope::Session &&
                loaded.writePolicy == WritePolicy::Append && loaded.rules.size() == 1,
            "recipe root fields did not survive round-trip");
    const RegexWorkbenchRule& rule = loaded.rules.first();
    Require(rule.id == QStringLiteral("r1") &&
                rule.secondaryMode == SecondaryMode::PreSearch &&
                rule.secondaryPattern == QStringLiteral("<p>([\\s\\S]*?)</p>") &&
                rule.recursive && rule.maxIterations == 16 && rule.allowEmpty &&
                rule.variableExpansionEnabled && rule.autoIngestNamedCaptures &&
                rule.captureToVar == QStringList{QStringLiteral("tail")} && !rule.enabled,
            "recipe rule fields did not survive round-trip");
}

QJsonObject MinimalRoot()
{
    QJsonObject rule;
    rule.insert(QStringLiteral("id"), QStringLiteral("r1"));
    rule.insert(QStringLiteral("name"), QStringLiteral("rule"));
    rule.insert(QStringLiteral("find"), QStringLiteral("x"));
    rule.insert(QStringLiteral("replace"), QStringLiteral("y"));
    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("sigil.regexWorkbench.recipe"));
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("name"), QStringLiteral("minimal"));
    root.insert(QStringLiteral("variableScope"), QStringLiteral("Batch"));
    root.insert(QStringLiteral("writePolicy"), QStringLiteral("LastWins"));
    root.insert(QStringLiteral("rules"), QJsonArray{rule});
    return root;
}

bool LoadObject(const QJsonObject& root, QString* error = nullptr)
{
    RegexRecipe recipe;
    return RegexRecipeStore::Deserialize(
        QJsonDocument(root).toJson(QJsonDocument::Compact), recipe, error);
}

void TestDefaultsAndStrictSchema()
{
    RegexRecipe parsed;
    QString error;
    Require(RegexRecipeStore::Deserialize(
                QJsonDocument(MinimalRoot()).toJson(), parsed, &error) &&
                parsed.rules.first().secondaryMode == SecondaryMode::None &&
                parsed.rules.first().maxIterations == 32 &&
                !parsed.rules.first().variableExpansionEnabled &&
                parsed.rules.first().enabled,
            "omitted optional rule fields must use safe defaults");

    QJsonObject unknown = MinimalRoot();
    unknown.insert(QStringLiteral("skipIf"), true);
    Require(!LoadObject(unknown), "unknown root fields must be rejected");

    QJsonObject noVersion = MinimalRoot();
    noVersion.remove(QStringLiteral("version"));
    Require(!LoadObject(noVersion), "missing schema versions must be rejected");

    QJsonObject wrongVersion = MinimalRoot();
    wrongVersion.insert(QStringLiteral("version"), 2);
    Require(!LoadObject(wrongVersion), "future schema versions must fail closed");
}

void TestSemanticAndResourceLimits()
{
    RegexRecipe duplicate = CompleteRecipe();
    duplicate.rules.append(duplicate.rules.first());
    Require(!RegexRecipeStore::Validate(duplicate),
            "duplicate rule ids must be rejected");

    RegexRecipe inconsistent = CompleteRecipe();
    inconsistent.rules[0].secondaryMode = SecondaryMode::None;
    Require(!RegexRecipeStore::Validate(inconsistent),
            "secondaryPattern must be empty in None mode");

    RegexRecipe nonRecursiveEmpty = CompleteRecipe();
    nonRecursiveEmpty.rules[0].recursive = false;
    Require(!RegexRecipeStore::Validate(nonRecursiveEmpty),
            "allowEmpty must not be enabled for non-recursive rules");

    RegexRecipe function = CompleteRecipe();
    function.rules[0].replace = QStringLiteral(" \\F<danger> ");
    Require(!RegexRecipeStore::Validate(function),
            "Python function replacements must not enter recipes");

    RegexRecipeLimits tiny;
    tiny.maxFileBytes = 32;
    Require(RegexRecipeStore::Serialize(CompleteRecipe(), nullptr, tiny).isEmpty(),
            "serialized recipe size must be bounded");
    RegexRecipe ignored;
    Require(!RegexRecipeStore::Deserialize(QByteArray(33, 'x'), ignored, nullptr, tiny),
            "input recipe size must be checked before parsing");
}

void TestAtomicFileRoundTripAndDefaultDirectory()
{
    QTemporaryDir temporary;
    Require(temporary.isValid(), "temporary recipe directory setup failed");
    gPreferencesDirectory = temporary.path();
    Require(RegexRecipeStore::DefaultDirectory() ==
                temporary.path() + QStringLiteral("/regex_workbench"),
            "default recipe directory must live below DefinePrefsDir");

    const QString path = RegexRecipeStore::DefaultDirectory() +
                         QStringLiteral("/nested/test.json");
    QString error;
    Require(RegexRecipeStore::SaveFile(path, CompleteRecipe(), &error) &&
                QFile::exists(path),
            "recipe save must create directories and atomically publish the file");
    RegexRecipe loaded;
    Require(RegexRecipeStore::LoadFile(path, loaded, &error) &&
                loaded.name == CompleteRecipe().name,
            "saved recipe files must load successfully");
}

void TestSearchEditorImportMappingAndWarnings()
{
    RegexSearchTemplateEntry entry;
    entry.name = QStringLiteral("legacy");
    entry.prefind = QStringLiteral("<p>(.*?)</p>");
    entry.find = QStringLiteral("x+");
    entry.replace = QStringLiteral("x");
    entry.controls = QStringLiteral("PS AH DN");
    const auto imported = RegexRecipeSearchEditorAdapter::Import(entry);
    Require(imported.success && !imported.rule.id.isEmpty() &&
                imported.rule.secondaryMode == SecondaryMode::PreSearch &&
                imported.rule.secondaryPattern == entry.prefind &&
                !imported.rule.recursive && !imported.rule.variableExpansionEnabled &&
                !imported.warnings.isEmpty(),
            "PS templates must import as safe-default PreSearch rules with scope warnings");

    entry.controls = QStringLiteral("RX");
    const auto ignoredPrefind = RegexRecipeSearchEditorAdapter::Import(entry);
    Require(ignoredPrefind.success &&
                ignoredPrefind.rule.secondaryMode == SecondaryMode::None &&
                !ignoredPrefind.warnings.isEmpty(),
            "prefind without an exact PS token must be ignored with a warning");

    entry.controls = QStringLiteral("RPS");
    const auto substring = RegexRecipeSearchEditorAdapter::Import(entry);
    Require(substring.success && substring.rule.secondaryMode == SecondaryMode::None,
            "PS import detection must use control tokens rather than substrings");

    entry.controls = QStringLiteral("NL AH");
    Require(!RegexRecipeSearchEditorAdapter::Import(entry).success,
            "normal-mode templates must not silently change semantics on import");

    entry.controls = QStringLiteral("RX");
    entry.replace = QStringLiteral("\\F<legacy>");
    Require(!RegexRecipeSearchEditorAdapter::Import(entry).success,
            "Python function templates must be skipped during import");

    SearchEditorModelPlus::searchEntry modelEntry{};
    modelEntry.name = QStringLiteral("model entry");
    modelEntry.prefind = QStringLiteral("<div>(.*?)</div>");
    modelEntry.find = QStringLiteral("z+");
    modelEntry.replace = QStringLiteral("z");
    modelEntry.controls = QStringLiteral("PS CF");
    const auto modelImport = RegexRecipeSearchEditorAdapter::Import(modelEntry);
    Require(modelImport.success &&
                modelImport.rule.secondaryMode == SecondaryMode::PreSearch &&
                !modelImport.warnings.isEmpty(),
            "SearchEditorModelPlus entries must use the same import contract");
}

}

int main()
{
    TestRoundTripPreservesSchemaFields();
    TestDefaultsAndStrictSchema();
    TestSemanticAndResourceLimits();
    TestAtomicFileRoundTripAndDefaultDirectory();
    TestSearchEditorImportMappingAndWarnings();
    std::cout << "regex recipe store tests passed\n";
    return 0;
}
