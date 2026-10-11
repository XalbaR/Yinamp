#pragma once
#include <Windows.h>
#include <string>
#include <string_view>

namespace yinamp::platform {
void CheckHResult(HRESULT result, std::string_view operation);
[[nodiscard]] std::wstring ToUtf16(std::string_view utf8);

class ComApartment final {
  public:
    ComApartment();
    ~ComApartment();
    ComApartment(const ComApartment&) = delete;
    ComApartment& operator=(const ComApartment&) = delete;
};
}
