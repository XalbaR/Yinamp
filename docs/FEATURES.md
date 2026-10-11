# Features and limitations

| Area | Supported behavior | Boundaries |
| --- | --- | --- |
| Platform | Win32 x64, Unicode, DPI-aware native windows | Windows/MSVC only; Media Foundation required. |
| Interface | Main/EQ/Playlist docking, full/compact modes, 1x/2x, shared custom menus | Fixed loose atlas set; no `.wsz`, skin browser or Preferences dialog. |
| Playback | Local audio/video, pause/stop/resume, gated seeking, previous/next, repeat/shuffle | Windows codecs determine local support; no radio/general HTTP input. |
| Audio | Ten-band EQ/preamp, live bypass, real PCM spectrum, volume/balance | Negotiation may omit processing; AUTO does not apply per-track presets; no disk EQ presets. |
| Video | EVR surface, resize/minimize/maximize, V toggle | No fullscreen, subtitles or stream/track picker; separate from Classic docking. |
| YouTube | Direct H.264/AAC MP4, Download/Local and Online/Stream, public playlists, TXT lists | No authenticated/private path, signature decipherer, live manifests, WebM/AV1/VP9/Opus or quality selector. |
| Playlists | Local files, nonrecursive folders, URL import, selection/reorder/sort, M3U/M3U8/PLS/HTML | ADD URL LIST uses the single URL dialog; TXT uses ADD FILE; no external file drag/drop. |
| Media Library | Completed downloads plus playlist YouTube entries, search/author/sort, naming and shared playback | No PC-wide scan, database, favorites, smart playlists or per-entry download deletion. |
| Persistence | Sources/order/names, Main origin, playback mode, metadata | No playhead/current/selection restore; scale/EQ/volume/layout/shuffle/repeat are session-only. |
| Shell | Original Yinamp icon, tray restore/root menu, Explorer restart handling | Main close exits; no close/minimize-to-tray preference. |
| Storage reset | Confirmed category reset through Ctrl then N/C/T | Deletes selected dedicated data; no undo or automatic media eviction. |
| Distribution | Debug source build and offline tests | MIT source and bundled default skin; no public binary release, updater or installer. |

Main titles clip rather than scroll. The full Main timer saturates at 99:59 without limiting playback duration. Repeat wraps the playlist rather than offering a distinct repeat-one mode. Track Info is read-only and does not fetch network data. EQ curves describe requested settings rather than measured acoustic response.

See [User guide](USER_GUIDE.md), [YouTube](YOUTUBE.md) and [Architecture](ARCHITECTURE.md) for detailed behavior.
