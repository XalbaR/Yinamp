#pragma once
#include "playlist/PlaylistModel.h"
#include <windows.h>
#include "skin/SkinAssets.h"

namespace yinamp::ui {
std::optional<std::filesystem::path> PickPlaylistPath(HWND owner, bool save, bool html = false, bool urlList = false);
bool RecyclePlaylistFile(HWND owner, const std::filesystem::path& path);
bool ConfirmPlaylistDeletion(HWND owner, std::size_t local, std::size_t youtube);
void ShowPlaylistInformation(HWND owner, const playlist::Entry& entry, const skin::SkinAssets& assets,
                             core::IntegerScale scale);
}
