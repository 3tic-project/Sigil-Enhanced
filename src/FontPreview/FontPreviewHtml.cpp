/************************************************************************
**
**  This file is part of Sigil.
**
*************************************************************************/

#include "FontPreview/FontPreviewHtml.h"

#include <QSet>

namespace {

QString Escape(const QString &text)
{
    return text.toHtmlEscaped();
}

bool LineFullyMissing(const QString &text, const QSet<uint> &missing)
{
    bool saw = false;
    for (int i = 0; i < text.size();) {
        uint codepoint = text.at(i).unicode();
        if (text.at(i).isHighSurrogate() && i + 1 < text.size() && text.at(i + 1).isLowSurrogate()) {
            codepoint = QChar::surrogateToUcs4(text.at(i), text.at(i + 1));
            i += 2;
        } else {
            ++i;
        }
        if (codepoint <= 0xFFFF && QChar(static_cast<char16_t>(codepoint)).isSpace()) {
            continue;
        }
        saw = true;
        if (!missing.contains(codepoint)) {
            return false;
        }
    }
    return saw;
}

QString SpecimenBlock(const QString &text, int pixelSize, const QList<uint> &missing,
                      const QString &tooltip)
{
    return QStringLiteral("<p class=\"specimen\" style=\"font-size:%1px;\">%2</p>")
        .arg(pixelSize)
        .arg(MarkMissingGlyphs(text, missing, tooltip));
}

}

QString MarkMissingGlyphs(const QString &text, const QList<uint> &missing, const QString &tooltip)
{
    const QSet<uint> missingSet(missing.begin(), missing.end());
    const QString tip = Escape(tooltip);
    QString out;
    for (int i = 0; i < text.size();) {
        QString piece;
        uint codepoint = text.at(i).unicode();
        if (text.at(i).isHighSurrogate() && i + 1 < text.size() && text.at(i + 1).isLowSurrogate()) {
            codepoint = QChar::surrogateToUcs4(text.at(i), text.at(i + 1));
            piece = text.mid(i, 2);
            i += 2;
        } else {
            piece = text.mid(i, 1);
            ++i;
        }
        const QString escaped = Escape(piece);
        if (missingSet.contains(codepoint)) {
            out += QStringLiteral("<span class=\"missing-glyph\" title=\"%1\">%2</span>")
                       .arg(tip, escaped);
        } else {
            out += escaped;
        }
    }
    return out;
}

QString BuildFontPreviewHtml(const FontPreviewDocument &document)
{
    const QString tooltip = document.missingTooltip.isEmpty()
        ? QStringLiteral("Not present in this font")
        : document.missingTooltip;
    const QSet<uint> missing(document.coverage.missing.begin(), document.coverage.missing.end());
    QString body;
    body += SpecimenBlock(document.sample.headline, 32, document.coverage.missing, tooltip);
    for (int i = 0; i < document.sample.passages.size(); ++i) {
        const QString passage = document.sample.passages.at(i);
        if (document.omitFullyMissingLines && LineFullyMissing(passage, missing)) {
            continue;
        }
        body += SpecimenBlock(passage, 16, document.coverage.missing, tooltip);
        if (i < document.sample.attributions.size() && !document.sample.attributions.at(i).isEmpty()) {
            body += QStringLiteral("<p class=\"attribution\">%1</p>")
                        .arg(Escape(document.sample.attributions.at(i)));
        }
    }
    if (!document.sample.characterLine.isEmpty()
        && !(document.omitFullyMissingLines && LineFullyMissing(document.sample.characterLine, missing))) {
        body += SpecimenBlock(document.sample.characterLine, 16, document.coverage.missing, tooltip);
    }
    if (!document.sample.punctuationLine.isEmpty()
        && !(document.omitFullyMissingLines && LineFullyMissing(document.sample.punctuationLine, missing))) {
        body += SpecimenBlock(document.sample.punctuationLine, 16, document.coverage.missing, tooltip);
    }
    const int sizes[] = {32, 24, 18, 16, 14, 12};
    body += QStringLiteral("<div class=\"waterfall\">");
    for (int size : sizes) {
        body += QStringLiteral("<div class=\"fall-row\"><span class=\"fall-size\">%1</span>%2</div>")
                    .arg(size)
                    .arg(SpecimenBlock(document.sample.headline, size, document.coverage.missing, tooltip));
    }
    body += QStringLiteral("</div>");

    const QString style = QStringLiteral(
        "body { margin: 12px; }"
        ".specimen { font-family: \"SigilPreviewFace\"; white-space: pre-wrap; line-height: 1.45; margin: 0.4em 0; }"
        ".attribution, .meta, .fall-size { font-family: sans-serif; }"
        ".attribution, .meta { font-size: 12px; opacity: 0.75; }"
        ".missing-glyph { font-family: sans-serif; color: #ff8a80; text-decoration: underline dotted; }"
        ".waterfall { margin-top: 1.2em; }"
        ".fall-row { display: flex; align-items: baseline; gap: 12px; }"
        ".fall-size { width: 2em; flex: none; }"
        ".fall-row .specimen { margin: 0.15em 0; }");

    return QStringLiteral(
               "<html><head><meta charset=\"utf-8\"><style>"
               "@font-face { font-family: \"SigilPreviewFace\"; src: url(\"%1\"); font-weight: %2; font-style: %3; }"
               "%4"
               "</style></head><body>"
               "<p class=\"meta\">%5</p>"
               "<p class=\"meta\">%6 · %7 bytes</p>"
               "%8"
               "</body></html>")
        .arg(document.fontUrl,
             document.fontWeight.isEmpty() ? QStringLiteral("400") : document.fontWeight,
             document.fontStyle.isEmpty() ? QStringLiteral("normal") : document.fontStyle,
             style,
             Escape(document.description),
             Escape(document.fileName),
             QString::number(document.fileBytes),
             body);
}
