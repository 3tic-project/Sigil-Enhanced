#include <cstdlib>
#include <iostream>

#include <QHash>
#include <QString>

#include "Misc/Utility.h"
#include "PCRE2/PCREReplaceTextBuilder.h"

SPCRE::SPCRE(const QString& pattern) :
    m_valid(true),
    m_errpos(-1),
    m_pattern(pattern),
    m_re(nullptr),
    m_matchdata(nullptr),
    m_captureSubpatternCount(2),
    m_mcontext(nullptr)
{
#ifndef PCRE_NO_JIT
    m_jitstack = nullptr;
#endif
}

SPCRE::~SPCRE() = default;

bool SPCRE::isValid()
{
    return m_valid;
}

int SPCRE::getCaptureStringNumber(const QString& name)
{
    return name == QStringLiteral("word") ? 1 : -1;
}

QString Utility::Substring(int startIndex, int endIndex, const QString& string)
{
    return string.mid(startIndex, endIndex - startIndex);
}

QString Utility::FullWidthChars2HalfWidthChars(const QString& text)
{
    return text;
}

QString Utility::HalfWidthChars2FullWidthChars(const QString& text)
{
    return text;
}

namespace
{

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

bool Build(const QString& replacement,
           QString& output,
           const ReplacementVariableResolver& resolver = ReplacementVariableResolver())
{
    SPCRE regex(QStringLiteral("(?<word>abc)"));
    PCREReplaceTextBuilder builder;
    return builder.BuildReplacementText(regex, QStringLiteral("abc"),
                                        {{0, 3}, {0, 3}}, replacement,
                                        output, resolver);
}

ReplacementVariableResolver MapResolver(const QHash<QString, QString>& values)
{
    return [values](const QString& name, QString& value) {
        const auto found = values.constFind(name);
        if (found == values.constEnd()) {
            return false;
        }
        value = found.value();
        return true;
    };
}

void TestClassicBehaviorWithoutResolver()
{
    QString output;
    Require(Build(QStringLiteral("${var:x}"), output) &&
                output == QStringLiteral("${var:x}"),
            "resolver-disabled variable syntax must remain literal");
    Require(Build(QStringLiteral("\\v{foo}"), output) &&
                output == QString(QChar(0x0b)) + QStringLiteral("{foo}"),
            "classic \\v{foo} vertical-tab behavior must remain unchanged");
}

void TestExplicitVariableExpansion()
{
    QString output;
    const ReplacementVariableResolver resolver =
        MapResolver({{QStringLiteral("x"), QStringLiteral("bar")}});
    Require(Build(QStringLiteral("pre-${var:x}-post"), output, resolver) &&
                output == QStringLiteral("pre-bar-post"),
            "explicit variable syntax did not resolve");
    Require(Build(QStringLiteral("${name}"), output, resolver) &&
                output == QStringLiteral("${name}"),
            "un-namespaced dollar braces must remain literal");
    Require(Build(QStringLiteral("\\v${var:x}"), output, resolver) &&
                output == QString(QChar(0x0b)) + QStringLiteral("bar"),
            "enabling variables must not alter classic \\v behavior");
}

void TestUndefinedAndLiteralResolverValues()
{
    QString output;
    Require(!Build(QStringLiteral("${var:missing}"), output, MapResolver({})),
            "undefined variables must fail replacement expansion");
    Require(Build(QStringLiteral("${var:x}"), output,
                  MapResolver({{QStringLiteral("x"), QStringLiteral("\\g{1}")}})) &&
                output == QStringLiteral("\\g{1}"),
            "resolved values must be injected literally without backreference re-parsing");
}

void TestBackreferenceAndVariableComposition()
{
    QString output;
    Require(Build(QStringLiteral("\\g{word}-${var:x}"), output,
                  MapResolver({{QStringLiteral("x"), QStringLiteral("tail")}})) &&
                output == QStringLiteral("abc-tail"),
            "match-local backreferences and store variables must compose in order");
}

}

int main()
{
    TestClassicBehaviorWithoutResolver();
    TestExplicitVariableExpansion();
    TestUndefinedAndLiteralResolverValues();
    TestBackreferenceAndVariableComposition();
    std::cout << "regex replacement resolver tests passed\n";
    return 0;
}
