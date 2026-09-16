#include "EmbedPython/EmbeddedPython.h" // Python must precede Qt's slots macro.

#include <QApplication>
#include <QDir>
#include <QHash>
#include <QRegularExpression>
#include <QTimer>

#include <cstdlib>
#include <iostream>
#include <stdexcept>

#include "BookManipulation/Book.h"
#include "BookManipulation/FolderKeeper.h"
#include "BuiltinPlugins/CmoaParagraphNormalizer.h"
#include "BuiltinPlugins/DivParagraphNormalizationPlan.h"
#include "BuiltinPlugins/DivParagraphStylesheetResolver.h"
#include "Importers/ImportEPUB.h"
#include "Misc/SettingsStore.h"
#include "ResourceObjects/CSSResource.h"
#include "ResourceObjects/HTMLResource.h"

using BuiltinPlugins::CmoaParagraphNormalizer;
using BuiltinPlugins::DivParagraphNormalizationPlan;
using BuiltinPlugins::DivParagraphStylesheetResolver;

namespace
{

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

int countTag(const QString& source, const QString& name)
{
    const QRegularExpression expression(
        QStringLiteral("<%1(?:\\s|>)").arg(QRegularExpression::escape(name)),
        QRegularExpression::CaseInsensitiveOption);
    int count = 0;
    auto matches = expression.globalMatch(source);
    while (matches.hasNext()) {
        matches.next();
        ++count;
    }
    return count;
}

struct Counts {
    int div = 0;
    int paragraph = 0;
    int h1 = 0;
    int ruby = 0;
    int rt = 0;
    int br = 0;

    void add(const QString& source)
    {
        div += countTag(source, QStringLiteral("div"));
        paragraph += countTag(source, QStringLiteral("p"));
        h1 += countTag(source, QStringLiteral("h1"));
        ruby += countTag(source, QStringLiteral("ruby"));
        rt += countTag(source, QStringLiteral("rt"));
        br += countTag(source, QStringLiteral("br"));
    }
};

}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    try {
        require(argc == 3, "Expected source root and Cmoa EPUB path");
        const QString root = QString::fromLocal8Bit(argv[1]);
        auto& python = EmbeddedPython::instance();
        python.addToPythonSysPath(qEnvironmentVariable("SIGIL_TEST_PYTHON_ROOT"));
        python.addToPythonSysPath(root + "/src/Resource_Files/plugin_launchers/python");
        python.addToPythonSysPath(root + "/src/Resource_Files/python3lib");

        SettingsStore settings;
        settings.setCleanOn(0);
        const QString input = QString::fromLocal8Bit(argv[2]);
        ImportEPUB importer(input);
        const auto book = importer.GetBook();
        require(book && book->GetFolderKeeper(), "Cmoa EPUB did not import");
        require(!book->IsModified(),
                "Read-only Cmoa import or missing-nav diagnosis marked the book modified");

        QHash<QString, QString> css_by_path;
        for (CSSResource* css :
             book->GetFolderKeeper()->GetResourceTypeList<CSSResource>(true)) {
            css->InitialLoad();
            css_by_path.insert(QDir::cleanPath(css->GetRelativePath()), css->GetText());
        }

        QVector<DivParagraphNormalizationPlan::Input> inputs;
        Counts before;
        const QList<HTMLResource*> html_resources =
            book->GetFolderKeeper()->GetResourceTypeList<HTMLResource>(true);
        require(html_resources.count() == 23, "Unexpected Cmoa XHTML file count");
        for (HTMLResource* html : html_resources) {
            html->InitialLoad();
            const QString path = QDir::cleanPath(html->GetRelativePath());
            const QString source = html->GetText();
            before.add(source);
            inputs << DivParagraphNormalizationPlan::Input {
                path,
                source,
                DivParagraphNormalizationPlan::hashText(source),
                DivParagraphStylesheetResolver::resolve(source, path, css_by_path)
            };
        }

        require(before.div == 1211 && before.paragraph == 2 && before.h1 == 12 &&
                    before.ruby == 250 && before.rt == 250 && before.br == 86,
                "Cmoa attachment no longer matches the PRD D01/D04/D05 fingerprint");

        const auto options = CmoaParagraphNormalizer::defaultOptions();
        const auto plan = DivParagraphNormalizationPlan::buildCmoa(inputs, options);
        require(plan.ok && plan.applyFiles == 12 && plan.reviewFiles == 5 &&
                    plan.errorFiles == 0 && plan.conversionCount == 958,
                "Cmoa attachment did not produce twelve safe body-file plans");
        require(plan.conversionCount > 0 && plan.conversionCount < before.div,
                "Cmoa plan converted no paragraphs or attempted every DIV");

        QHash<QString, QString> outputs;
        for (const auto& entry : plan.entries) {
            if (entry.status == DivParagraphNormalizationPlan::Status::Apply) {
                outputs.insert(entry.resourceId, entry.output);
            }
        }
        Counts after;
        QVector<DivParagraphNormalizationPlan::Input> normalized_inputs = inputs;
        for (auto& input_entry : normalized_inputs) {
            if (outputs.contains(input_entry.resourceId)) {
                input_entry.text = outputs.value(input_entry.resourceId);
                input_entry.baseRevision =
                    DivParagraphNormalizationPlan::hashText(input_entry.text);
            }
            after.add(input_entry.text);
        }
        require(after.h1 == before.h1 && after.ruby == before.ruby &&
                    after.rt == before.rt && after.br == before.br,
                "Cmoa plan changed heading, Ruby, rt, or BR counts");
        require(before.div - after.div == plan.conversionCount &&
                    after.paragraph - before.paragraph == plan.conversionCount,
                "Cmoa plan changed more than the selected DIV/P tag names");

        const auto second = DivParagraphNormalizationPlan::buildCmoa(
            normalized_inputs, options);
        require(second.ok && second.applyFiles == 0 && second.conversionCount == 0,
                "Cmoa attachment did not become idempotent after the planned conversion");
        require(!book->IsModified(), "Read-only Cmoa planning modified the live book");

        std::cout << "Cmoa EPUB acceptance passed: "
                  << plan.applyFiles << " files, "
                  << plan.conversionCount << " DIV-to-P patches, "
                  << plan.reviewFiles << " review files\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
