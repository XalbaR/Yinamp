#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace yinamp::media::native::json {

struct Value;
using Object = std::map<std::string, Value, std::less<>>;
using Array = std::vector<Value>;

struct Value {
    using Storage = std::variant<std::nullptr_t, bool, double, std::string, Array, Object>;
    Storage data;

    [[nodiscard]] const Object* AsObject() const;
    [[nodiscard]] const Array* AsArray() const;
    [[nodiscard]] const std::string* AsString() const;
    [[nodiscard]] std::optional<double> AsNumber() const;
    [[nodiscard]] const Value* Get(std::string_view key) const;
};

Value Parse(std::string_view text);
std::string_view ObjectAt(std::string_view text, std::size_t start);
std::string Escape(std::string_view value);

}
