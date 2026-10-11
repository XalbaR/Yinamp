#pragma once
#include <Windows.h>
#include <functional>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace yinamp::ui {
struct ClassicMenuItem {
    enum class Type { Command, Separator, Submenu };
    Type type{Type::Command};
    std::wstring label;
    std::optional<UINT> command;
    bool checked{};
    bool enabled{true};
    std::vector<ClassicMenuItem> children;
    static ClassicMenuItem Item(std::wstring label, UINT command, bool checked = false, bool enabled = true);
    static ClassicMenuItem Separator();
    static ClassicMenuItem Submenu(std::wstring label, std::vector<ClassicMenuItem> children);
};
using ClassicMenuItems = std::vector<ClassicMenuItem>;

// One session owns a root HWND and its submenu HWNDs. No HMENU/theme rendering.
class ClassicPopupMenu final {
  public:
    using Opened = std::function<void(HWND)>;
    struct Layout { SIZE size{}; std::vector<RECT> rows; };
    struct View { const ClassicMenuItems* items{}; int selected{-1}; UINT dpi{}; Layout layout; };
    static UINT Track(const ClassicMenuItems& items, POINT origin, HWND owner, const Opened& opened = {});
    static void Cancel(HWND owner) noexcept;
    // Inspect the actual popup and render into a DIB for deterministic native tests.
    static std::optional<View> Inspect(HWND popup);
    static Layout Measure(const ClassicMenuItems& items, UINT dpi);
    static void Draw(HDC dc, const ClassicMenuItems& items, const Layout& layout, UINT dpi, int selected);
    static RECT Place(SIZE size, POINT origin, RECT workArea, std::optional<RECT> parent = {});
  private:
    struct Panel;
    ClassicPopupMenu(HWND owner);
    ~ClassicPopupMenu();
    UINT Run(const ClassicMenuItems& items, POINT origin, const Opened& opened);
    void AddPanel(const ClassicMenuItems& items, POINT origin, std::optional<RECT> parent = {});
    void Trim(std::size_t count);
    void Select(std::size_t depth, int index, bool keyboard);
    void OpenChild(std::size_t depth, bool keyboard);
    void Key(WPARAM key);
    void Mouse(POINT screen, UINT message);
    void Finish(UINT command = 0) noexcept;
    static LRESULT CALLBACK Dispatch(HWND window, UINT message, WPARAM wp, LPARAM lp) noexcept;
    HWND owner_{};
    HWND capture_{};
    HWND savedFocus_{};
    HWND menuFocus_{};
    HWND savedForeground_{};
    HINSTANCE instance_{};
    std::wstring className_;
    ATOM classAtom_{};
    bool done_{};
    bool foregroundAcquired_{};
    UINT command_{};
    std::exception_ptr failure_;
    std::vector<std::unique_ptr<Panel>> panels_;
};
}
