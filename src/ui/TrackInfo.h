#pragma once
#include "media/playback/PlaybackController.h"

namespace yinamp::ui {
// Copies only user-facing metadata; collection never polls or commands playback.
std::wstring TrackInformation(const media::playback::PlaybackController& playback,
                              const playlist::PlaylistModel& playlist);
std::wstring TrackInformation(const media::LibraryEntry& entry);
}
