#include <cstdlib>
#include <iostream>

#define PCRE2_CODE_UNIT_WIDTH 16
#include <pcre2.h>

#include <QString>
#include <QStringList>

#include "PCRE2/CaptureNameTable.h"

namespace
{

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

pcre2_code_16* Compile(const QString& pattern, uint32_t options = PCRE2_UTF | PCRE2_MULTILINE)
{
    int errorCode = 0;
    PCRE2_SIZE errorOffset = 0;
    return pcre2_compile_16(pattern.utf16(), pattern.size(), options,
                            &errorCode, &errorOffset, nullptr);
}

void TestCaptureNameEnumeration()
{
    pcre2_code_16* unnamed = Compile(QStringLiteral("(a)(b)"));
    Require(unnamed != nullptr && PCRE2Helpers::CaptureNames(unnamed).isEmpty(),
            "unnamed captures must not appear in capture-name metadata");
    pcre2_code_free_16(unnamed);

    pcre2_code_16* named = Compile(QStringLiteral("(?<zeta>a)(?<alpha>b)(c)"));
    Require(named != nullptr &&
                PCRE2Helpers::CaptureNames(named) ==
                    QStringList({QStringLiteral("alpha"), QStringLiteral("zeta")}),
            "named captures must be read from the PCRE2 name table in stable order");
    pcre2_code_free_16(named);

    pcre2_code_16* pythonNamed = Compile(QStringLiteral("(?P<author>[^<]+)"));
    Require(pythonNamed != nullptr &&
                PCRE2Helpers::CaptureNames(pythonNamed) ==
                    QStringList({QStringLiteral("author")}),
            "Python-style named captures must appear in capture-name metadata");
    pcre2_code_free_16(pythonNamed);

    Require(PCRE2Helpers::CaptureNames(nullptr).isEmpty(),
            "null compiled patterns must yield no capture names");
}

void TestDuplicateNamesRemainRejectedByDefault()
{
    pcre2_code_16* duplicate = Compile(QStringLiteral("(?<same>a)(?<same>b)"));
    Require(duplicate == nullptr,
            "workbench patterns must reject duplicate capture names without PCRE2_DUPNAMES");
}

}

int main()
{
    TestCaptureNameEnumeration();
    TestDuplicateNamesRemainRejectedByDefault();
    std::cout << "regex capture name tests passed\n";
    return 0;
}
