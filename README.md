# Yinamp

[![Windows Debug](https://github.com/XalbaR/Yinamp/actions/workflows/windows-debug.yml/badge.svg)](https://github.com/XalbaR/Yinamp/actions/workflows/windows-debug.yml)

Yinamp is a **native Windows media player written in C++23**, combining a Winamp Classic-inspired interface with local audio/video playback and native YouTube streaming and downloads.
Built directly on Win32, Direct2D/DirectWrite and Windows Media Foundation, it implements its own YouTube resolution, HTTP streaming, native MP4 remuxing, audio DSP and Classic UI components without yt-dlp, FFmpeg, VLC/mpv or embedded browser/WebView playback.

## What is Yinamp?

Yinamp is a Winamp Classic-inspired player for local media and supported YouTube sources, with separate Main, Equalizer, Playlist and Video windows sharing one playback pipeline and playlist.
This C++ media player pairs the appearance of a retro Windows media player with native YouTube playback and real audio processing.

The Classic appearance is backed by native window interaction and bitmap-atlas rendering.
Yinamp develops its own YouTube resolution, HTTP streaming, MP4 remuxing, audio DSP and spectrum analysis around Windows' media and graphics APIs.
Media Foundation supplies demuxing, decoding, playback scheduling and audio/video output.

As an FFmpeg-free media player and yt-dlp-free YouTube player, Yinamp implements the supported playback and acquisition workflows in native C++ and Windows APIs.
It is not an Electron app, WebView wrapper, browser player or frontend for VLC/mpv.

## Why Yinamp is different

| Area | Yinamp's approach | Avoids |
| --- | --- | --- |
| Playback | Windows Media Foundation with a Yinamp-managed playback topology | VLC / mpv |
| YouTube extraction | Native C++ / WinHTTP resolution inside Yinamp | yt-dlp / browser automation |
| Online playback | Custom seekable HTTP Range `IMFByteStream` with bounded chunk fetching | Embedded browser / WebView |
| Downloads | Native MP4 remuxing of supported adaptive H.264/AAC streams without transcoding | FFmpeg |
| Equalizer | Custom Media Foundation Transform processing decoded PCM | External DSP engine |
| Spectrum | FFT analysis of real playback PCM | External visualization engine |
| Classic UI | Win32 + Direct2D bitmap-atlas rendering and native window interaction | Electron / WebView |

### Native playback engine

Local audio and video run through Windows Media Foundation rather than an external playback engine.
Yinamp builds and controls the playback topology: selected audio and video share a Media Session and presentation clock, with Windows' audio renderer and Enhanced Video Renderer providing output.
The same controller serves the Main window, Playlist and Media Library.

### Native YouTube extraction

As a YouTube media player, Yinamp uses its own native YouTube extraction path: WinHTTP reads supported watch-page and InnerTube responses to resolve video metadata and select reachable direct media URLs.
A separate native resolver enumerates public playlists, preserves source order and skips recognized unavailable entries.
Neither path launches yt-dlp, browser automation or an embedded web player.
Direct-format availability matters: authenticated content, signature JavaScript deciphering and live manifests are outside the current scope.

### Native online streaming

Online/Stream provides seekable online playback by resolving fresh media URLs and exposing them to Media Foundation through a custom, seekable `IMFByteStream`.
HTTP Range requests fetch at most **256 KiB per request/cache chunk**; response ranges, lengths and byte counts are checked before use.
This lets playback read and seek remote media through bounded chunks without saving a full media file.
Progressive MP4 uses one source; adaptive audio and video use separate sources aggregated under one playback clock.
A server that ignores Range requests fails this path rather than triggering a full-response download.

### Native download and remux

The YouTube download workflow in Download/Local mode acquires supported streams and prepares reusable local media before playback.
For adaptive sources, Yinamp's MP4 code combines compressed H.264 video and AAC audio samples into `final.mp4`.
**Remuxing** changes the container organization without decoding or re-encoding the audio/video; it uses no FFmpeg and performs no transcoding.
Progressive MP4 is validated directly, and both paths also extract the AAC track into `audio.m4a`.

### Real audio processing

The 10-band equalizer and preamp process decoded float PCM inside a custom Media Foundation Transform (MFT).
Ten peaking filters apply the requested gains, with short crossfades when settings change and a live bypass that retains the slider values.
The FFT spectrum analyzer reads playback PCM through a clock-scheduled sample tap and computes a Hann-windowed FFT for its 19 frequency bands.
It follows the audio data rather than a preset animation, without microphone or loopback capture.
If Media Foundation cannot negotiate a compatible processing format, playback can fall back to EQ only, spectrum only or plain playback.

### Native Classic interface

Main, Equalizer, Playlist and Video are separate native Windows windows.
Direct2D draws bitmap-atlas slices for the Classic controls and frames; DirectWrite handles Unicode text and font fallback.
Main/EQ/Playlist support docking, shade/compact modes and integer 1x/2x scaling; Video remains a separate resizable window.
Shared popup/context menus have Yinamp's own Classic-style Win32 GDI renderer, with keyboard navigation and submenus.
This rendering and interaction system implements the Classic appearance directly, without a CSS or WebView skin.

See [Architecture](docs/ARCHITECTURE.md) and [YouTube](docs/YOUTUBE.md) for the deeper pipeline details.

## Highlights

### Playback

- Local audio/video playback through Windows components. MP3, WAV, FLAC, OGG and M4A are recognized by folder import; actual decoding depends on the codecs available to Media Foundation.
- Play/pause/stop, seeking on seekable sources, previous/next, playlist repeat and shuffle.
- Separate Video Window with resize/minimize/maximize; hiding it leaves playback running.
- Read-only Track Info for known source, duration, mode/state and available audio/video format details.

### YouTube

- Single-video URL import, public YouTube playlist import and UTF-8 TXT batch import with deduplication and result counts.
- YouTube downloads in Download/Local mode with native acquisition/remuxing and reuse of validated completed media.
- Online/Stream mode with fresh direct URLs, seekable HTTP Range streaming and no media files written to disk.
- Supported SDR H.264/AAC MP4 selection, preferring adaptive sources up to 1080p/60 fps with progressive fallback when available.

### Audio

- 10-band equalizer and preamp with -12 to +12 dB gains, live bypass and smoothed setting changes.
- FFT spectrum analyzer driven by real playback PCM, normally after EQ and before renderer volume/balance.
- Volume and stereo balance controls; processing availability depends on playback-format negotiation.

### Classic interface

- Bundled MIT-licensed default Classic skin; no separate skin setup required.
- Dockable Main/EQ/Playlist windows, shade/compact modes and 1x/2x Double Size.
- Bitmap controls, Unicode playlist text and custom Classic popup/context menus.
- System tray restore and shared root menu; closing Main exits the application.

### Playlist and library

- Native playlist editor with selection, row reordering, sorting, duplicate cleanup and custom display names.
- M3U/M3U8/PLS import/export and UTF-8 HTML playlist export, with Unicode titles and paths.
- Local media, YouTube URLs, public playlists and TXT batches feed the same playlist; ordered sources and names persist across sessions.
- Searchable YouTube Media Library with author/channel grouping, sorting, Play/Add to Playlist and display-name rename/restore.

## Playlist and Media Library

The Playlist is the shared playback queue for local files and YouTube entries.
It supports file selection and nonrecursive folder import, list-file exchange, URL imports and HTML export as well as native editing controls.
Session persistence restores sources, order and custom names; playback starts stopped, without restoring the previous playhead or selection.

The Media Library brings together **validated completed YouTube downloads and YouTube entries in the current playlist**, including online entries.
It deduplicates by video ID, shows Local/Online location and available source metadata, and supports title/author search and author/channel groups.
Play and Add to Playlist reuse the existing queue and playback controller; rename/restore keeps display names consistent with the playlist and stored metadata.
The Library is a focused view of Yinamp's YouTube media, with no general local-file database or PC-wide scan.

See the [User guide](docs/USER_GUIDE.md) for playlist editing, imports, Library actions and Track Info.

## Native architecture

**UI and rendering:** Win32 owns the windows and input lifecycle.
Direct2D/DirectWrite and the skin renderers draw the Classic surfaces, while the docking and popup-menu layers implement their interaction.

**Playback and DSP:** the Main window owns the playback controller, which creates Media Foundation sources, sessions and topologies for local or online media.
Decoded PCM can pass through Yinamp's EQ transform and spectrum tap before audio output; video is presented in the Video Window.

**YouTube resolution and transfer:** native WinHTTP clients, metadata/playlist resolvers and format selection prepare supported sources.
Cancellable workers handle resolution and serial downloads; online playback instead reads remote bytes through the custom Range stream.
Native MP4 parsing/remuxing prepares downloaded media without an external codec toolchain.

**Storage, playlist and library:** MediaStore validates completed files and publishes completion metadata after the media files.
The playlist model owns entry identities, ordering and names; session storage restores durable state, and Library reads combine stored downloads with current YouTube entries.
Worker results are checked against entry identity/source generation before the UI applies them, keeping cancelled or replaced work from updating the wrong source.

For ownership, threading, topology and persistence contracts, see [Architecture](docs/ARCHITECTURE.md) and [Storage](docs/STORAGE.md).

## Requirements

Windows 10 22H2 or Windows 11, x64, with Media Foundation. Windows N editions need the Media Feature Pack. Local codec support depends on Windows. Building requires Visual Studio 2022 C++ tools, MSVC v143 with C++23 support, a Windows SDK and CMake 3.25+.

## Quick start

Build from source using [Getting started](docs/GETTING_STARTED.md), then run `build/windows-x64-app/Debug/Yinamp.exe`. Eject opens local media; Playlist **ADD URL** accepts a YouTube video or public playlist. **EQ**, **PL** and **V** toggle auxiliary windows. There is no public binary release yet.

Data is written beneath `%LOCALAPPDATA%/Yinamp`; downloads go to the Windows Music known folder under `YINAMP/<videoId>`. Playlist sessions and exports may contain personal paths and viewing information. See [Storage](docs/STORAGE.md).

## YouTube playback

**Download/Local** downloads supported media, remuxes it natively and reuses local storage. **Online/Stream** resolves fresh media URLs and streams without saving media files. Select the mode in Options; it affects the next source opened. Online mode still stores metadata and playlist state.

Supported direct MP4 H.264/AAC formats are required. Service changes, restricted content or unavailable direct URLs may prevent playback. There is no authenticated playback, signature JavaScript deciphering, live manifest support or transcoding. See [YouTube](docs/YOUTUBE.md).

For a single video or public playlist, use **ADD URL**. A watch URL containing both a video ID and playlist ID imports only the video; use the playlist's `/playlist?list=...` URL to import its membership.
For batches, use **ADD FILE** with one UTF-8 `.txt` file containing one video URL per non-empty line.
Public playlist re-import appends missing videos while preserving local order and names; it does not synchronize remote removals or reordering.

## Current scope

- Windows-native playback with the bundled Classic atlases; no general `.wsz` loader, skin browser, installer or updater.
- Video playback without fullscreen, subtitles or an audio/video track picker.
- YouTube-focused Library; no general local-media scan or database, and no radio/general HTTP URL playback.
- Direct H.264/AAC MP4 YouTube sources only: no authenticated/private-content path, signature decipherer, live/SABR manifests, WebM/AV1/VP9/Opus path or quality selector.

See [Features and limitations](docs/FEATURES.md) for the complete supported scope.

## Build from source

From the repository root in Developer PowerShell for Visual Studio 2022:

```powershell
cmake --preset windows-x64-app
cmake --build --preset debug-app-x64
```

Presets select x64 and Debug, with C++23 and `/W4 /WX`. No package manager is required; linked platform libraries come from the Windows SDK. The bundled skin is available to the application and full offline test suite. See [Building](docs/BUILDING.md).

For validation or CI, build and run the complete suite in its separate Debug tree:

```powershell
cmake --preset windows-x64-tests
cmake --build --preset debug-tests-x64
ctest --preset debug-tests-x64 --no-tests=error
```

Daily app builds use `BUILD_TESTING=OFF`; validation uses `BUILD_TESTING=ON`. Both trees use Visual Studio 2022 x64 and Debug only.

## Documentation

[Documentation index](docs/README.md) · [Getting started](docs/GETTING_STARTED.md) · [User guide](docs/USER_GUIDE.md) · [Architecture](docs/ARCHITECTURE.md) · [Testing](docs/TESTING.md)

[YouTube](docs/YOUTUBE.md) · [Storage](docs/STORAGE.md) · [Features and limitations](docs/FEATURES.md) · [Building](docs/BUILDING.md)

Manual live-service and device checks complement offline tests.

## Contributing

Read [CONTRIBUTING.md](CONTRIBUTING.md). Use Debug, preserve `/W4 /WX`, and validate with the full offline suite. Report vulnerabilities through [SECURITY.md](SECURITY.md).

## License

Yinamp source code and original application icon are licensed under the [MIT License](LICENSE), copyright XalbaR. The bundled default Classic skin is separately MIT licensed. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for asset and platform notices.

## Disclaimer

Yinamp is an independent, unofficial project, not affiliated with or endorsed by Winamp or YouTube/Google. Trademarks belong to their respective owners. Use media in accordance with applicable rights and platform terms.
