#include "platform/windows/shell/ApplicationIdentity.h"
#include "platform/windows/WindowsRuntime.h"
#include "app/resource.h"
#include <shobjidl.h>
#include <stdexcept>
#include <utility>

namespace yinamp::platform {
void InitializeApplicationIdentity() {
    CheckHResult(SetCurrentProcessExplicitAppUserModelID(ApplicationUserModelId), "Application identity initialization");
}
ApplicationIcons::~ApplicationIcons() {
    if (big_) DestroyIcon(big_);
    if (small_) DestroyIcon(small_);
}
void ApplicationIcons::Load(HINSTANCE instance, UINT dpi) {
    const auto load = [instance, dpi](int width, int height) {
        return static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_YINAMP), IMAGE_ICON,
            GetSystemMetricsForDpi(width, dpi), GetSystemMetricsForDpi(height, dpi), 0));
    };
    HICON big = load(SM_CXICON, SM_CYICON);
    HICON smallIcon = load(SM_CXSMICON, SM_CYSMICON);
    if (!big || !smallIcon) {
        if (big) DestroyIcon(big);
        if (smallIcon) DestroyIcon(smallIcon);
        throw std::runtime_error("Yinamp application icon resource unavailable.");
    }
    // Keep both previous handles valid until both new frames have loaded.
    if (const auto old = std::exchange(big_, big)) DestroyIcon(old);
    if (const auto old = std::exchange(small_, smallIcon)) DestroyIcon(old);
}
}
