#include "platform/windows/WindowsRuntime.h"
#include <limits>
#include <objbase.h>
#include <sstream>
#include <stdexcept>

namespace yinamp::platform {
void CheckHResult(HRESULT result, std::string_view operation) {
    if (FAILED(result)) {
        std::ostringstream message;
        message << operation << " failed (HRESULT 0x" << std::hex << static_cast<unsigned long>(result) << ").";
        throw std::runtime_error(message.str());
    }
}
std::wstring ToUtf16(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    if (utf8.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::length_error("UTF-8 diagnostic is too long.");
    }
    const int length = static_cast<int>(utf8.size());
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), length, nullptr, 0);
    if (count == 0) {
        throw std::runtime_error("Invalid UTF-8 diagnostic.");
    }
    std::wstring output(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), length, output.data(), count) == 0) {
        throw std::runtime_error("UTF-8 conversion failed.");
    }
    return output;
}
ComApartment::ComApartment() {
    CheckHResult(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), "COM initialization");
}
ComApartment::~ComApartment() {
    CoUninitialize();
}
}
