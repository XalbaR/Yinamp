#include "media/ContentReset.h"
#include <shlobj.h>
#include <stdexcept>
#include <windows.h>

namespace yinamp::media {
namespace {
std::filesystem::path KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR value{};
    const auto result = SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &value);
    struct Free {
        PWSTR value;
        ~Free() {
            CoTaskMemFree(value);
        }
    } free{value};
    if (FAILED(result))
        throw std::runtime_error("Content reset folder unavailable");
    return value;
}
DWORD Attributes(const std::filesystem::path& path) {
    const auto value = GetFileAttributesW(path.c_str());
    if (value == INVALID_FILE_ATTRIBUTES) {
        const auto error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
            throw std::runtime_error("Cannot inspect content reset target");
    }
    return value;
}
void CheckPath(const std::filesystem::path& path) {
    if (!path.is_absolute() || path.lexically_normal() != path || path == path.root_path())
        throw std::runtime_error("Unsafe content reset target");
    auto prefix = path.root_path();
    for (const auto& part : path.relative_path()) {
        if (part.native().find(L':') != std::wstring::npos)
            throw std::runtime_error("Unsafe content reset component");
        prefix /= part;
        const auto attributes = Attributes(prefix);
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("Content reset refuses reparse points");
    }
}
void InspectTree(const std::filesystem::path& path) {
    CheckPath(path);
    const auto attributes = Attributes(path);
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY))
        return;
    for (const auto& child : std::filesystem::directory_iterator(path))
        InspectTree(child.path());
}
void DeleteTree(const std::filesystem::path& path) {
    CheckPath(path);
    const auto attributes = Attributes(path);
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return;
    if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
        for (const auto& child : std::filesystem::directory_iterator(path))
            DeleteTree(child.path());
    }
    // Never use recursive removal against unchecked targets or follow junctions.
    if (!std::filesystem::remove(path))
        throw std::runtime_error("Cannot remove content reset target");
}
}
ContentReset::ContentReset(std::optional<std::filesystem::path> sessionRoot,
                           std::optional<std::filesystem::path> mediaRoot) {
    const auto session = sessionRoot ? *sessionRoot : KnownFolder(FOLDERID_LocalAppData) / L"Yinamp";
    const auto media = mediaRoot ? *mediaRoot : KnownFolder(FOLDERID_Music) / L"YINAMP";
    if (_wcsicmp(session.filename().c_str(), L"Yinamp") != 0 || _wcsicmp(media.filename().c_str(), L"YINAMP") != 0)
        throw std::runtime_error("Content reset requires dedicated Yinamp directories");
    CheckPath(session);
    CheckPath(media);
    targets_ = {session / L"session.json", session / L"youtube-info" / L"playlists",
                session / L"youtube-info" / L"videos", media};
}
void ContentReset::Validate(Selection selected) const {
    for (std::size_t i = 0; i < targets_.size(); ++i)
        if (selected[i]) InspectTree(targets_[i]);
}
void ContentReset::Delete(Selection selected) const {
    Validate(selected); // Preflight every selected target before removing any files.
    for (std::size_t i = 0; i < targets_.size(); ++i)
        if (selected[i]) DeleteTree(targets_[i]);
}
}
