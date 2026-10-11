#pragma once
#include <filesystem>
#include <stdexcept>
#include <windows.h>
#include <objbase.h>

class TemporaryMediaRoot final {
  public:
    TemporaryMediaRoot() {
        GUID guid{};
        if (FAILED(CoCreateGuid(&guid)))
            throw std::runtime_error("Temporary media root identity unavailable");
        wchar_t text[40]{};
        StringFromGUID2(guid, text, 40);
        parent_ = std::filesystem::temp_directory_path();
        path = parent_ / (std::wstring(L"Yinamp-media-test-") + text);
        if (!std::filesystem::create_directory(path))
            throw std::runtime_error("Temporary media root creation failed");
    }
    ~TemporaryMediaRoot() {
        // Only delete this absolute, uniquely created immediate child of the temp folder.
        if (path.is_absolute() && path.parent_path() == parent_ &&
            path.filename().wstring().starts_with(L"Yinamp-media-test-")) {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    }
    TemporaryMediaRoot(const TemporaryMediaRoot&) = delete;
    TemporaryMediaRoot& operator=(const TemporaryMediaRoot&) = delete;
    std::filesystem::path path;
  private:
    std::filesystem::path parent_;
};
