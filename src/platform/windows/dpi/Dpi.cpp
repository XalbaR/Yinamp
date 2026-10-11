#include "platform/windows/dpi/Dpi.h"
#include <Windows.h>
#include <stdexcept>

namespace yinamp::platform {
void EnableDpiAwareness() {
    // Manifest farkındalığı önceden ayarlamışsa API erişim reddi verebilir; gerçek bağlamı doğruluyoruz.
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) &&
        !AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(), DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        throw std::runtime_error("Per-monitor V2 DPI awareness could not be enabled.");
    }
}
}
