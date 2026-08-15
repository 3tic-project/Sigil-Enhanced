#include <cstdio>

#include "Misc/ImagePreviewPolicy.h"

namespace
{

bool expect(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        return false;
    }
    return true;
}

} // namespace

int main()
{
    const bool ok =
        expect(ImagePreviewPolicy::hoverDelayMs(700, false) == 700,
               "platform hover delay was not preserved") &&
        expect(ImagePreviewPolicy::hoverDelayMs(0, false) == 400,
               "invalid platform delay did not use fallback") &&
        expect(ImagePreviewPolicy::hoverDelayMs(150, true) == 300,
               "macOS arm64 delay was not raised to its safe minimum") &&
        expect(ImagePreviewPolicy::hoverDelayMs(700, true) == 700,
               "macOS arm64 delay unexpectedly shortened a platform delay") &&
        expect(ImagePreviewPolicy::normalizedMaximumSide(20) == 50,
               "preview size was not clamped to the minimum") &&
        expect(ImagePreviewPolicy::normalizedMaximumSide(300) == 300,
               "valid preview size was not preserved") &&
        expect(ImagePreviewPolicy::normalizedMaximumSide(900) == 600,
               "preview size was not clamped to the maximum");
    if (ok) {
        std::fprintf(stdout, "All image preview policy tests passed.\n");
    }
    return ok ? 0 : 1;
}
