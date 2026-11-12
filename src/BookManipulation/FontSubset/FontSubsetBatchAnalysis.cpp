#include "BookManipulation/FontSubset/FontSubsetController.h"

#include <QObject>

#include "BookManipulation/FontSubset/FontFaceAssigner.h"
#include "BookManipulation/FontSubset/FontInspector.h"
#include "BookManipulation/FontSubset/HarfBuzzSubsetEngine.h"

namespace FontSubset
{

BatchResult FontSubsetController::Analyze(const BookSnapshot& snapshot,
                                          const Options& options)
{
    BatchResult batch;
    batch.warnings = snapshot.warnings;
    const FontFaceUsage assigned =
        FontFaceAssigner().Assign(snapshot.textSources, snapshot.fonts);
    batch.usage.codepoints = assigned.codepoints;
    batch.usage.shapingSamples = assigned.shapingSamples;
    batch.usage.warnings = assigned.warnings;
    batch.warnings.append(assigned.warnings);

    HarfBuzzSubsetEngine engine;
    FontInspector inspector;
    for (const FontSnapshot& font : snapshot.fonts) {
        FontAnalysis analysis;
        analysis.font = font;
        const FontTextAssignment text = assigned.fonts.value(font.identifier);
        if (text.codepoints.isEmpty()) {
            analysis.result.inspection = inspector.Inspect(font.bytes);
            analysis.result.oldSize = font.bytes.size();
            analysis.result.oldGlyphCount = analysis.result.inspection.glyphCount;
            analysis.result.error = analysis.result.inspection.canSubset
                ? QObject::tr("No text is assigned to this font.")
                : analysis.result.inspection.blockingReason;
            batch.fonts.append(analysis);
            continue;
        }
        Options fontOptions = options;
        fontOptions.shapingSamples = text.shapingSamples;
        analysis.result = engine.Subset(font.bytes, text.codepoints, fontOptions);
        batch.fonts.append(analysis);
    }
    return batch;
}

}
