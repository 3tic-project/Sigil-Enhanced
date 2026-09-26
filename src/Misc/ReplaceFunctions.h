#pragma once

#include <string>
#include <utility>

#include <QList>
#include <QString>

// The ten default \F<> replace functions, frozen from the legacy Python
// functionrep/titlecase/fr_utils modules (CPython 3.11, Unicode 14).
namespace ReplaceFunctions {

enum class Builtin {
    Identity,
    Uppercase, Lowercase, Capitalize, Titlecase, Swapcase,
    UppercaseIgnoreTags, LowercaseIgnoreTags, CapitalizeIgnoreTags, TitlecaseIgnoreTags, SwapcaseIgnoreTags,
};

// Looks the function up the way functionsearch.getFunctionSearchEnv does.
// Returns false when it is user Python code; otherwise sets *builtin.
bool Resolve(const QString &functionName, const QString &jsonPath, Builtin *builtin);

// text is the matched segment; groups are UTF-16 offsets into it, starting
// with the whole match. Unmatched groups carry negative offsets.
QString Apply(Builtin builtin, const QString &text, const QList<std::pair<int, int>> &groups);

std::u32string Upper(const std::u32string &text);
std::u32string Lower(const std::u32string &text);
std::u32string Swapcase(const std::u32string &text);
std::u32string Capitalize(const std::u32string &text);
std::u32string Titlecase(const std::u32string &text);
std::u32string Unescape(const std::u32string &text);

// CPython 3.11 str.isspace and the re \w / \d classes.
bool IsPythonSpace(char32_t value);
bool IsPythonWord(char32_t value);
bool IsPythonDigit(char32_t value);

}
