#pragma once
#include <Windows.h>

namespace yinamp::platform {
inline constexpr wchar_t ApplicationUserModelId[] = L"Yinamp.Yinamp";
void InitializeApplicationIdentity();

// Owned resource handles; never destroy icons supplied by another window/class.
class ApplicationIcons final {
  public:
    ApplicationIcons() = default;
    ~ApplicationIcons();
    ApplicationIcons(const ApplicationIcons&) = delete;
    ApplicationIcons& operator=(const ApplicationIcons&) = delete;
    void Load(HINSTANCE instance, UINT dpi);
    [[nodiscard]] HICON Big() const noexcept { return big_; }
    [[nodiscard]] HICON Small() const noexcept { return small_; }
  private:
    HICON big_{};
    HICON small_{};
};
}
