#pragma once

#include <stdexcept>

namespace yinamp::core {
struct Point {
    int x;
    int y;
};
struct Size {
    int width;
    int height;
};
struct Rect {
    int x;
    int y;
    int width;
    int height;
    [[nodiscard]] constexpr bool Contains(Point point) const noexcept {
        return point.x >= x && point.y >= y && point.x - x < width && point.y - y < height;
    }
};

class IntegerScale final {
  public:
    explicit constexpr IntegerScale(int value) : value_(value) {
        if (value < 1 || value > 3) {
            throw std::invalid_argument("Display scale must be 1, 2 or 3.");
        }
    }
    [[nodiscard]] constexpr int Value() const noexcept {
        return value_;
    }
    [[nodiscard]] constexpr Size ToPhysical(Size size) const noexcept {
        return {size.width * value_, size.height * value_};
    }
    [[nodiscard]] constexpr Point ToLogical(Point point) const noexcept {
        // Negatif istemci koordinatları sıfıra yuvarlanmamalı; kenar dışındaki fareyi dışarıda tutuyoruz.
        const auto divide = [this](int value) { return value / value_ - (value % value_ < 0 ? 1 : 0); };
        return {divide(point.x), divide(point.y)};
    }

  private:
    int value_;
};
}
