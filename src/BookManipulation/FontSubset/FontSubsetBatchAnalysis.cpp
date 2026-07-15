#include "BookManipulation/FontSubset/FontSubsetController.h"

#include "BookManipulation/FontSubset/HarfBuzzSubsetEngine.h"

namespace FontSubset
{

BatchResult FontSubsetController::Analyze(const BookSnapshot& snapshot,
                                          const Options& options)
{
    BatchResult batch;
    batch.warnings = snapshot.warnings;
    GlobalFontUsageCollector collector;
    batch.usage = collector.Collect(snapshot.textSources);
    batch.warnings.append(batch.usage.warnings);

    HarfBuzzSubsetEngine engine;
    Options effectiveOptions = options;
    effectiveOptions.shapingSamples = batch.usage.shapingSamples;
    for (const FontSnapshot& font : snapshot.fonts) {
        FontAnalysis analysis;
        analysis.font = font;
        analysis.result = engine.Subset(font.bytes, batch.usage.codepoints,
                                        effectiveOptions);
        batch.fonts.append(analysis);
    }
    return batch;
}

}
