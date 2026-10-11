#pragma once
#include "playlist/PlaylistModel.h"
#include "skin/PlaylistLayout.h"

namespace yinamp::ui {
enum class PlaylistKey {
    Up,
    Down,
    PageUp,
    PageDown,
    Home,
    End
};
class PlaylistInteraction final {
  public:
    playlist::PlaylistModel model;
    core::Size size{skin::PlaylistSize};
    bool active{true};
    bool shaded{};
    int scroll{};
    std::optional<int> focus, anchor;
    std::optional<skin::PlaylistControl> pressed, menu;
    std::optional<int> menuHover;
    bool dragging{};
    [[nodiscard]] int MaximumScroll() const {
        return std::max(0, static_cast<int>(model.entries.size()) - skin::PlaylistRows(size));
    }
    void Scroll(int delta) {
        scroll = static_cast<int>(
            std::clamp(static_cast<long long>(scroll) + delta, 0LL, static_cast<long long>(MaximumScroll())));
    }
    void Resize(core::Size requested) {
        size = skin::ClampPlaylistSize(requested);
        Scroll(0);
        Cancel();
    }
    [[nodiscard]] std::optional<int> RowAt(core::Point point) const {
        if (shaded || !skin::PlaylistList(size).Contains(point))
            return {};
        const int row = (point.y - 20) / skin::PlaylistRowHeight;
        const int index = scroll + row;
        if (row >= skin::PlaylistRows(size) || index >= static_cast<int>(model.entries.size()))
            return {};
        return index;
    }
    void Select(int index, bool control, bool shift) {
        if (index < 0 || index >= static_cast<int>(model.entries.size()))
            return;
        if (shift) {
            const int start = anchor.value_or(focus.value_or(index));
            if (!anchor)
                anchor = start;
            if (!control)
                model.SelectAll(false);
            for (int i = std::min(start, index); i <= std::max(start, index); ++i)
                model.entries[static_cast<std::size_t>(i)].selected = true;
        } else {
            if (!control)
                model.SelectAll(false);
            auto& entry = model.entries[static_cast<std::size_t>(index)];
            entry.selected = control ? !entry.selected : true;
            anchor = index;
        }
        focus = index;
    }
    void EnsureVisible() {
        if (!focus)
            return;
        if (*focus < scroll)
            scroll = *focus;
        if (*focus >= scroll + skin::PlaylistRows(size))
            scroll = *focus - skin::PlaylistRows(size) + 1;
        Scroll(0);
    }
    void Navigate(PlaylistKey key, bool control, bool shift) {
        if (model.entries.empty())
            return;
        const int last = static_cast<int>(model.entries.size()) - 1;
        int index = focus.value_or(key == PlaylistKey::Up ? last : 0);
        switch (key) {
        case PlaylistKey::Up:
            if (focus)
                --index;
            break;
        case PlaylistKey::Down:
            if (focus)
                ++index;
            break;
        case PlaylistKey::PageUp:
            index -= skin::PlaylistRows(size);
            break;
        case PlaylistKey::PageDown:
            index += skin::PlaylistRows(size);
            break;
        case PlaylistKey::Home:
            index = 0;
            break;
        case PlaylistKey::End:
            index = last;
            break;
        }
        index = std::clamp(index, 0, last);
        if (control && !shift)
            focus = index;
        else
            Select(index, control, shift);
        EnsureVisible();
    }
    void Changed() {
        anchor.reset();
        if (model.entries.empty())
            focus.reset();
        else if (focus)
            focus = std::min(*focus, static_cast<int>(model.entries.size()) - 1);
        Scroll(0);
    }
    [[nodiscard]] std::optional<std::uint64_t> FocusId() const {
        if (!focus || *focus < 0 || *focus >= static_cast<int>(model.entries.size())) return {};
        return model.entries[static_cast<std::size_t>(*focus)].id;
    }
    void ChangedKeepingFocus(std::optional<std::uint64_t> id) {
        const auto found = std::ranges::find_if(model.entries, [id](const auto& entry) { return id == entry.id; });
        if (found != model.entries.end()) focus = static_cast<int>(found - model.entries.begin());
        Changed();
        EnsureVisible();
    }
    void Delete(bool crop = false) {
        const auto id = FocusId();
        model.RemoveSelected(crop);
        ChangedKeepingFocus(id);
    }
    void MoveSelected(int delta) {
        const auto first = std::ranges::find_if(model.entries, [](const auto& entry) { return entry.selected; });
        if (first == model.entries.end()) return;
        const auto last = std::find_if(model.entries.rbegin(), model.entries.rend(), [](const auto& entry) { return entry.selected; });
        const int count = static_cast<int>(model.entries.size());
        delta = std::clamp(delta, -static_cast<int>(first - model.entries.begin()), static_cast<int>(last - model.entries.rbegin()));
        const auto focusId = focus ? std::optional{model.entries[static_cast<std::size_t>(*focus)].id} : std::nullopt;
        const auto anchorId = anchor ? std::optional{model.entries[static_cast<std::size_t>(*anchor)].id} : std::nullopt;
        // Seçili satırlar komşu boş yuvalarla yer değiştirir; kimlik ve etkin satır korunur.
        for (int step = 0; step < std::abs(delta); ++step) {
            if (delta > 0) {
                for (int i = count - 2; i >= 0; --i)
                    if (model.entries[static_cast<std::size_t>(i)].selected)
                        std::swap(model.entries[static_cast<std::size_t>(i)], model.entries[static_cast<std::size_t>(i + 1)]);
            } else {
                for (int i = 1; i < count; ++i)
                    if (model.entries[static_cast<std::size_t>(i)].selected)
                        std::swap(model.entries[static_cast<std::size_t>(i)], model.entries[static_cast<std::size_t>(i - 1)]);
            }
        }
        if (delta != 0) model.NotifyDurableChange();
        for (int i = 0; i < count; ++i) {
            const auto id = model.entries[static_cast<std::size_t>(i)].id;
            if (focusId == id) focus = i;
            if (anchorId == id) anchor = i;
        }
    }
    [[nodiscard]] bool Down(core::Point point, bool control = false, bool shift = false) {
        if (menu) {
            Move(point);
            if (menuHover) {
                menuClick_ = true;
                return true;
            }
            Cancel();
        }
        if (auto button = skin::HitTestPlaylistButton(size, point)) {
            if (shaded && *button != skin::PlaylistControl::Close && *button != skin::PlaylistControl::Shade)
                return false;
            captured_ = pressed = button;
            if (static_cast<int>(*button) < 5) {
                menu = button;
                menuHover.reset();
                openedAt_ = point;
            }
            return true;
        }
        if (!shaded && skin::PlaylistScrollTrack(size).Contains(point)) {
            const auto thumb = skin::PlaylistThumb(size, scroll, MaximumScroll());
            if (thumb.Contains(point)) {
                dragging = true;
                grab_ = point.y - thumb.y;
                return true;
            }
            Scroll(point.y < thumb.y ? -skin::PlaylistRows(size) : skin::PlaylistRows(size));
        }
        if (const auto row = RowAt(point)) {
            if (control || shift || !model.entries[static_cast<std::size_t>(*row)].selected)
                Select(*row, control, shift);
            if (!control && !shift) {
                trackDragY_ = point.y;
                trackClick_ = row;
                trackMoved_ = false;
                return true;
            }
        } else if (!shaded && skin::PlaylistList(size).Contains(point) && !control && !shift)
            model.SelectAll(false);
        return false;
    }
    void Move(core::Point point) {
        if (trackDragY_ && RowAt(point)) {
            const int delta = (point.y - *trackDragY_) / skin::PlaylistRowHeight;
            if (delta != 0) {
                trackMoved_ = true;
                MoveSelected(delta);
                *trackDragY_ += delta * skin::PlaylistRowHeight;
            }
        }
        if (dragging)
            scroll = skin::PlaylistScrollAt(size, point.y - 20 - grab_, MaximumScroll());
        if (menu) {
            menuHover.reset();
            for (int row = 0; row < skin::PlaylistMenuCount(*menu); ++row)
                if (skin::PlaylistMenuItem(size, *menu, row).Contains(point))
                    menuHover = row;
        } else if (captured_)
            pressed = skin::HitTestPlaylistButton(size, point) == captured_ ? captured_ : std::nullopt;
    }
    struct Action {
        skin::PlaylistControl control;
        std::optional<int> row;
    };
    [[nodiscard]] std::optional<Action> Up(core::Point point) {
        Move(point);
        if (trackClick_ && !trackMoved_)
            Select(*trackClick_, false, false);
        if (menu) {
            const bool moved = std::abs(point.x - openedAt_.x) > 2 || std::abs(point.y - openedAt_.y) > 2;
            if (menuHover && (menuClick_ || moved)) {
                const Action action{*menu, menuHover};
                Cancel();
                return action;
            }
            if (menuClick_ || moved) {
                Cancel();
                return {};
            }
            captured_.reset();
            pressed.reset();
            return {};
        }
        const auto clicked = pressed;
        Cancel();
        if (clicked)
            return Action{*clicked, {}};
        return {};
    }
    void Cancel() {
        captured_.reset();
        pressed.reset();
        dragging = false;
        trackDragY_.reset();
        trackClick_.reset();
        trackMoved_ = false;
        menu.reset();
        menuHover.reset();
        menuClick_ = false;
    }
    [[nodiscard]] bool Captured() const {
        return dragging || trackDragY_.has_value() || captured_.has_value() || menuClick_;
    }

  private:
    std::optional<skin::PlaylistControl> captured_;
    core::Point openedAt_{};
    int grab_{};
    bool menuClick_{};
    std::optional<int> trackDragY_;
    std::optional<int> trackClick_;
    bool trackMoved_{};
};
} // namespace yinamp::ui
