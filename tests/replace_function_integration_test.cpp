#include "EmbedPython/EmbeddedPython.h"

#include <cstdlib>
#include <iostream>

#include <QApplication>
#include <QFile>

#include "EmbedPython/PyObjectPtr.h"
#include "EmbedPython/PythonRoutines.h"
#include "Misc/SearchOperations.h"
#include "Misc/Utility.h"
#include "PCRE2/PCRECache.h"
#include "PCRE2/SPCRE.h"
#include "sigil_constants.h"

namespace {

int failures = 0;

void Check(bool condition, const QString &message)
{
    if (!condition) {
        std::cerr << message.toStdString() << std::endl;
        ++failures;
    }
}

const QStringList NAMES {
    "uppercase", "lowercase", "capitalize", "titlecase", "swapcase",
    "uppercase_ignore_tags", "lowercase_ignore_tags", "capitalize_ignore_tags",
    "titlecase_ignore_tags", "swapcase_ignore_tags", "not_defined",
};

const QString TEXT = QString::fromUtf8(
    "<p class=\"Keep\">hello &amp; WORLD</p>\n<p>the lord of the rings: a tale</p>\n"
    "\xF0\x9F\x98\x80<em>x\xC3\x9F</em> \xCE\xA3.\n<b>\xC7\x85ungla</b> &nbsp; the end &#x1F600;\n");

// Patterns that Python re and PCRE2 (UTF, MULTILINE) match identically on TEXT.
const QStringList SHARED {
    "<p[^>]*>([^<]*)</p>", "(x)?(\xC3\x9F)", "<em>(x)(\xC3\x9F)</em>", "(lord) (of) (the)",
    "(?:(a)|(the)) ([a-z]+)", "^<p>(.*)</p>$", "((l)(o))rd", "\xCE\xA3", "(\xF0\x9F\x98\x80)(<em>)",
    "<[^>]+>", "([a-z]+)", "(?i)(HELLO)", "<b>(.)(ungla)</b>", "&[a-z]+;", "(t)(h)(e)?",
};

// Patterns where the engines differ; the C++ path keeps PCRE2's matches.
const QStringList PCRE_ONLY { "the \\Kend", "\\Q&amp;\\E" };

}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (argc != 3) return EXIT_FAILURE;
    const QString root = QString::fromLocal8Bit(argv[1]);
    auto &python = EmbeddedPython::instance();
    python.addToPythonSysPath(qEnvironmentVariable("SIGIL_TEST_PYTHON_ROOT"));
    python.addToPythonSysPath(root + "/src/Resource_Files/plugin_launchers/python");
    python.addToPythonSysPath(root + "/src/Resource_Files/python3lib");
    const QString json = Utility::DefinePrefsDir() + "/" + SIGIL_FUNCTION_REPLACE_JSON_FILE;
    QFile::remove(json);

    PythonRoutines routines;
    int compared = 0;
    for (const QString &name : NAMES) {
        Check(SPCRE::isBuiltinFunction(name), name + " should resolve to C++");
        const QString replacement = "\\F<" + name + ">";
        for (const QString &pattern : SHARED + PCRE_ONLY) {
            SPCRE *spcre = PCRECache::instance().getObject(pattern);
            Check(spcre->isValid(), "invalid test pattern " + pattern);
            // Preview and the chooser build one Python session per run.
            PyObjectPtr session = routines.SetupInitialFunctionSearchEnvInPython(name);
            for (const SPCRE::MatchInfo &match : spcre->getEveryMatchInfo(TEXT)) {
                const QString segment = Utility::Substring(match.offset.first, match.offset.second, TEXT);
                QString legacy;
                QString native;
                spcre->functionReplaceText("OEBPS/a.xhtml", segment, match.capture_groups_offsets, session, legacy);
                spcre->replaceText(segment, match.capture_groups_offsets, replacement, native);
                Check(native == legacy, QString("%1 %2 on '%3': C++ '%4', Python '%5'")
                      .arg(name, pattern, segment, native, legacy));
                ++compared;
            }
            QString replaced;
            int count = 0;
            std::tie(replaced, count) = SearchOperations::PerformGlobalReplace(TEXT, pattern, replacement);
            if (SHARED.contains(pattern)) {
                PyObjectPtr all = routines.SetupInitialFunctionSearchEnvInPython(name);
                const QString legacy = routines.DoFunctionSearchTextReplacementsInPython(all, pattern, "OEBPS/a.xhtml", TEXT);
                Check(replaced == legacy, QString("replace all %1 %2 differs from Python re").arg(name, pattern));
                Check(count == routines.GetCurrentReplacementCountInPython(all),
                      QString("replace all %1 %2 count differs from Python re").arg(name, pattern));
            } else {
                Check(count == spcre->getEveryMatchInfo(TEXT).size(), "PCRE2-only pattern count " + pattern);
            }
        }
    }

    // Same-name user code keeps running in Python; untouched defaults stay in C++.
    QFile file(json);
    Check(file.open(QIODevice::WriteOnly), "cannot write replace_functions.json");
    file.write(R"({"uppercase": "def replace(match, number, file_name, metadata, data):\n\treturn '[' + match.group(0) + ']'\n",
                  "lowercase": "def replace(match, number, file_name, metadata, data):\n\tif match:\n\t\treturn replace_lowercase(match, number, file_name, metadata, data)\n"})");
    file.close();
    Check(!SPCRE::isBuiltinFunction("uppercase"), "edited uppercase must stay Python");
    Check(SPCRE::isBuiltinFunction("lowercase"), "default lowercase must resolve to C++");
    Check(SPCRE::isBuiltinFunction("titlecase"), "a function missing from the file is the identity");
    SPCRE *spcre = PCRECache::instance().getObject("(?i)(HELLO)");
    const SPCRE::MatchInfo match = spcre->getFirstMatchInfo(TEXT);
    QString out;
    spcre->replaceText("hello", match.capture_groups_offsets, "\\F<uppercase>", out);
    Check(out == "[hello]", "edited uppercase did not run: " + out);
    spcre->replaceText("hello", match.capture_groups_offsets, "\\F<lowercase>", out);
    Check(out == "hello", "default lowercase changed: " + out);
    spcre->replaceText("hello", match.capture_groups_offsets, "\\F<titlecase>", out);
    Check(out == "hello", "missing titlecase is not the identity: " + out);

    std::cout << "compared " << compared << " single replacements" << std::endl;
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
