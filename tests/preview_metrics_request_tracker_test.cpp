#include "ViewEditors/PreviewMetricsRequestTracker.h"

#include <iostream>

namespace
{

bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << std::endl;
    }
    return condition;
}

}

int main()
{
    bool okay = true;
    PreviewMetricsRequestTracker tracker;

    tracker.begin(11, PreviewMetricsRequestPurpose::Calibration, QStringLiteral("preview:A"));
    tracker.begin(12, PreviewMetricsRequestPurpose::Status, QStringLiteral("caret:B"));

    const auto status = tracker.take(12);
    okay &= expect(status.has_value()
                       && status->purpose == PreviewMetricsRequestPurpose::Status
                       && status->elementKey == QStringLiteral("caret:B"),
                   "status result must retain its own request purpose and element identity");
    okay &= expect(tracker.hasActive(PreviewMetricsRequestPurpose::Calibration),
                   "a status result must not consume a pending calibration");

    const auto calibration = tracker.take(11);
    okay &= expect(calibration.has_value()
                       && calibration->purpose == PreviewMetricsRequestPurpose::Calibration
                       && calibration->elementKey == QStringLiteral("preview:A"),
                   "calibration must only consume its matching request token");

    tracker.begin(20, PreviewMetricsRequestPurpose::Status, QStringLiteral("caret:old"));
    tracker.begin(21, PreviewMetricsRequestPurpose::Status, QStringLiteral("caret:new"));
    okay &= expect(!tracker.take(20).has_value(),
                   "a newer request of the same purpose must supersede the older result");
    const auto newest = tracker.take(21);
    okay &= expect(newest.has_value() && newest->elementKey == QStringLiteral("caret:new"),
                   "the latest same-purpose request must remain active");

    tracker.begin(25, PreviewMetricsRequestPurpose::Inspection, QStringLiteral("preview:old"));
    tracker.cancel(PreviewMetricsRequestPurpose::Inspection);
    okay &= expect(!tracker.take(25).has_value(),
                   "leaving a transient inspection context must discard its pending result");

    tracker.begin(30, PreviewMetricsRequestPurpose::Settings, QStringLiteral("caret:C"));
    tracker.begin(31, PreviewMetricsRequestPurpose::Inspection, QStringLiteral("preview:D"));
    tracker.clear();
    okay &= expect(!tracker.take(30).has_value() && !tracker.take(31).has_value(),
                   "page invalidation must discard every outstanding request purpose");

    return okay ? 0 : 1;
}
