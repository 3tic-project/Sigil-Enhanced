#include <cstdlib>
#include <iostream>

#include "Misc/StagedTextValidator.h"

namespace
{

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

void TestXmlMediaTypesAndCssBypass()
{
    Require(SearchBatch::StagedTextValidator::RequiresXmlValidation(
                QStringLiteral("application/xhtml+xml; charset=utf-8")) &&
                SearchBatch::StagedTextValidator::RequiresXmlValidation(
                    QStringLiteral("IMAGE/SVG+XML")) &&
                SearchBatch::StagedTextValidator::RequiresXmlValidation(
                    QStringLiteral("application/oebps-package+xml")) &&
                SearchBatch::StagedTextValidator::RequiresXmlValidation(
                    QStringLiteral("application/x-dtbncx+xml")) &&
                SearchBatch::StagedTextValidator::RequiresXmlValidation(
                    QStringLiteral("text/xml")) &&
                !SearchBatch::StagedTextValidator::RequiresXmlValidation(
                    QStringLiteral("text/css")),
            "XML media-type classification is incorrect");

    const QHash<QString, QString> texts = {
        {QStringLiteral("Text/a.xhtml"), QStringLiteral("<html><body/></html>")},
        {QStringLiteral("content.opf"), QStringLiteral("<package/>")},
        {QStringLiteral("toc.ncx"), QStringLiteral("<ncx/>")},
        {QStringLiteral("Styles/a.css"), QStringLiteral("body { broken")}
    };
    const QHash<QString, QString> mediaTypes = {
        {QStringLiteral("Text/a.xhtml"), QStringLiteral("application/xhtml+xml")},
        {QStringLiteral("content.opf"),
         QStringLiteral("application/oebps-package+xml")},
        {QStringLiteral("toc.ncx"),
         QStringLiteral("application/x-dtbncx+xml")},
        {QStringLiteral("Styles/a.css"), QStringLiteral("text/css")}
    };
    const auto result = SearchBatch::StagedTextValidator::Validate(texts, mediaTypes);
    Require(result.success && result.validatedResourceCount == 3 &&
                result.issueCount == 0,
            "well-formed XHTML, OPF, and NCX must pass while CSS remains untouched");
}

void TestMalformedXmlAndMissingMetadataFailClosed()
{
    QHash<QString, QString> texts;
    texts.insert(QStringLiteral("Text/a.xhtml"),
                 QStringLiteral("<html>\n<body></html>"));
    texts.insert(QStringLiteral("Text/unknown.xhtml"), QStringLiteral("<html/>") );
    QHash<QString, QString> mediaTypes;
    mediaTypes.insert(QStringLiteral("Text/a.xhtml"),
                      QStringLiteral("application/xhtml+xml"));
    const auto result = SearchBatch::StagedTextValidator::Validate(texts, mediaTypes);
    Require(!result.success && result.issueCount == 2 && result.issues.size() == 2 &&
                result.issues.first().resourcePath == QStringLiteral("Text/a.xhtml") &&
                result.issues.first().line >= 1 &&
                result.error.contains(QStringLiteral("Text/a.xhtml")),
            "malformed XML and missing media metadata must both fail closed");
}

void TestIssueCapAndCancellation()
{
    QHash<QString, QString> texts;
    QHash<QString, QString> mediaTypes;
    for (int index = 0; index < 5; ++index) {
        const QString path = QStringLiteral("Text/%1.xhtml").arg(index);
        texts.insert(path, QStringLiteral("<broken>"));
        mediaTypes.insert(path, QStringLiteral("application/xhtml+xml"));
    }
    SearchBatch::StagedValidationOptions capped;
    capped.maxIssues = 2;
    const auto result = SearchBatch::StagedTextValidator::Validate(
        texts, mediaTypes, capped);
    Require(!result.success && result.issueCount == 5 && result.issues.size() == 2 &&
                result.issuesTruncated,
            "validation must keep exact issue counts while bounding details");

    SearchBatch::StagedValidationOptions cancelled;
    cancelled.isCancelled = []() { return true; };
    const auto cancelResult = SearchBatch::StagedTextValidator::Validate(
        texts, mediaTypes, cancelled);
    Require(!cancelResult.success && cancelResult.cancelled &&
                cancelResult.issueCount == 0,
            "validation cancellation must stop before parsing staged resources");
}

}

int main()
{
    TestXmlMediaTypesAndCssBypass();
    TestMalformedXmlAndMissingMetadataFailClosed();
    TestIssueCapAndCancellation();
    std::cout << "staged text validator tests passed\n";
    return 0;
}
