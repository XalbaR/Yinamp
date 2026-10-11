# Getting started

## Prepare Windows

Use Windows 10 22H2 or Windows 11 x64 with Media Foundation. On Windows N, install the Media Feature Pack. Local playback depends on installed Windows codecs.

Install Visual Studio 2022 or Build Tools with Desktop development with C++, MSVC v143, a Windows SDK and CMake 3.25+. Open Developer PowerShell. Git and CMake must be available on PATH; vcpkg is not needed.

## Build the app

```powershell
git clone https://github.com/XalbaR/Yinamp.git
cd Yinamp
cmake --preset windows-x64-app
cmake --build --preset debug-app-x64
```

The MIT-licensed default Classic skin is bundled under `assets/skins/classic`; no additional download or configuration is needed. The application and skin-dependent tests use it automatically.

For validation or CI, build and run the complete suite in its separate Debug tree:

```powershell
cmake --preset windows-x64-tests
cmake --build --preset debug-tests-x64
ctest --preset debug-tests-x64 --no-tests=error
```

Daily app builds use `BUILD_TESTING=OFF`; validation uses `BUILD_TESTING=ON`. Both trees use Visual Studio 2022 x64 and Debug only.

## First playback

1. Launch `build/windows-x64-app/Debug/Yinamp.exe`.
2. Use Eject to choose local media. Play, Pause and Stop control native playback; seek requires a known duration and seekable source.
3. Toggle EQ and PL to show the Equalizer and Playlist. D selects Double Size; double-click a Classic title bar for compact mode.
4. For YouTube, choose Download/Local or Online/Stream in Options, then Playlist ADD URL. Paste one video or public playlist URL. Download mode waits for local completion; Online opens a fresh stream.
5. For multiple video URLs, use ADD FILE with one UTF-8 TXT file containing one URL per non-empty line.
6. Open Media Library from the root menu to search completed downloads and playlist YouTube entries.

Main close exits. Auxiliary close hides the window. Playback starts stopped after restart. Read [User guide](USER_GUIDE.md), [YouTube](YOUTUBE.md) and [Storage](STORAGE.md) before downloading or deleting content.

There is no approved public binary package. Do not use older packages as installation instructions. Contributor setup and PR expectations are in [CONTRIBUTING.md](../CONTRIBUTING.md).
