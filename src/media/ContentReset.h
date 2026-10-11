#pragma once
#include <array>
#include <filesystem>
#include <optional>
#include <vector>

namespace yinamp::media {
class ContentReset final {
  public:
    using Selection = std::array<bool, 4>;
    ContentReset(std::optional<std::filesystem::path> sessionRoot = {},
                 std::optional<std::filesystem::path> mediaRoot = {});
    const std::vector<std::filesystem::path>& Targets() const noexcept {
        return targets_;
    }
    void Validate(Selection selected = {true, true, true, true}) const;
    void Delete(Selection selected = {true, true, true, true}) const;

  private:
    std::vector<std::filesystem::path> targets_;
};
class ContentResetChord final {
  public:
    bool InProgress() const noexcept {
        return progress_ != 0;
    }
    bool Update(bool control, unsigned key, bool repeat = false) noexcept {
        if (!control) {
            progress_ = 0;
            return false;
        }
        if (repeat)
            return false;
        if (key == 'N') {
            progress_ = 1;
        } else if (key == 'C' && progress_ == 1) {
            progress_ = 2;
        } else if (key == 'T' && progress_ == 2) {
            progress_ = 0;
            return true;
        } else {
            progress_ = 0;
        }
        return false;
    }

  private:
    unsigned progress_{};
};
}
