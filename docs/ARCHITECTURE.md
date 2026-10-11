# Architecture

## Layers and ownership

| Directory | Responsibility |
| --- | --- |
| `src/app` | Entry point, resource/manifest and skin lookup. |
| `src/core` | Geometry, logical coordinates and integer scaling. |
| `src/platform/windows` | COM, DPI, HWND lifecycle, application identity and tray. |
| `src/graphics` | WIC bitmap decoding, Direct2D drawing and DirectWrite text. |
| `src/skin` | Required atlas bounds, sprite rectangles and window renderers. |
| `src/ui` | Main/EQ/Playlist/Video/Library windows, dialogs, menus and docking. |
| `src/playlist` | Stable entry identities, operations, list formats and sessions. |
| `src/media` | Storage, jobs, metadata, reset, native extraction and playback. |

CMake builds static libraries along these boundaries and one native executable. The UI owns HWNDs on the UI thread. Main owns the playback controller; Library actions reuse it and the existing PlaylistModel rather than owning a second player or queue. Cancellable workers publish identity/generation-checked results; they do not mutate HWNDs or the playlist directly. Shutdown cancels and joins owned workers before teardown.

## Rendering and windows

WIC loads indexed/true-color BMPs into opaque pixels. Direct2D draws fixed atlas slices at integer scale with nearest-neighbor sampling, with target-loss recreation. DirectWrite supplies Unicode text and font fallback. Main ASCII labels use bitmap glyphs. Playlist frames tile/crop instead of stretching.

Main/EQ/Playlist docking uses an explicit graph in logical coordinates. Main drags its attached component; auxiliary dragging detaches at 10 logical pixels and snaps within 12. Leaving the magnetic zone rearms same-drag attachment. Shade/scale changes update offsets; capture loss cancels transient interaction. Video is separate. Only Main's normal origin is persisted.

Shared root/secondary menus use a Win32 GDI popup renderer and the existing command model. It handles keyboard/mouse navigation, delayed submenus, monitor work-area clamping and DPI separately from Double Size. Playlist ADD/REM/SEL/MISC bitmap stacks retain their own interaction path.

## Playback

Media Foundation handles demuxing, decoding, Media Session and presentation-clock scheduling. Audio goes to the Streaming Audio Renderer (SAR); video to EVR. First selected audio/video streams share one session/clock; additional streams are deselected.

Local files use the native resolver. Download mode opens validated MediaStore `final.mp4`. Online creates fresh HTTP MP4 sources: one for progressive media, or separate audio/video sources aggregated under one session/clock. Generation checks isolate source changes. Stop while Opening cancels work. Selected online failures allow one fresh resolution preserving position and paused/playing intent; Opening has a 30-second watchdog.

The HTTP byte stream implements sync/async reads, length discovery and byte seeking with bounded Range requests. It requires HTTP 206, matching Content-Range and byte counts, and rejects full-response fallback. See [YouTube](YOUTUBE.md).

## Equalizer

Decoded float32 interleaved PCM passes through a synchronous in-place IMFTransform. It supports 8–192 kHz and 1–8 channels, preserves sample timing/attributes and accepts one pending input sample.

Preamp precedes ten peaking biquads at 60, 170, 310, 600, 1000, 3000, 6000, 12000, 14000 and 16000 Hz, with -12 to +12 dB gains. Bands at or above 45% of the sample rate remain neutral. Per-channel double-precision histories use fixed storage. Coherent setting revisions crossfade old/new banks for about 20 ms. OFF bypasses and retains gains. Nonfinite values are neutralized; float headroom is retained without a limiter. Flush, discontinuity, stream/source/seek and format changes reset history.

Negotiation fallback tries EQ+spectrum, EQ alone, spectrum alone, then plain playback. EQ ON stores a request rather than proving processing availability. Gains, AUTO and the one preset slot remain session-only.

## Spectrum

Normal topology is decoder/converter → EQ → PCM tee → SAR, with a clock-scheduled sample-grabber branch feeding analysis. It analyzes playback PCM, without microphone/loopback capture, before renderer volume/balance.

Float32/PCM16 support spans 8–192 kHz and 1–8 channels. Channels average to mono; opposite phases can cancel. A bounded nonblocking callback copies recent PCM into a 2048-frame buffer; the UI snapshot computes a Hann/radix-2 FFT. Nineteen logarithmic bands use -60..0 dBFS normalization, about 35/180 ms attack/release and 0.55 normalized units/s peak decay. After 150 ms without PCM, silence handling applies. Pause/stop/seek/source changes reset or halt analysis. Muting SAR need not change the spectrum.

## Storage and media jobs

Serial download jobs resolve supported formats, stage transfers, remux compressed samples without transcoding and publish completed metadata last. Metadata resolution is a separate queue from download progress. Stale removed-row results are ignored. Playlist imports prepare all results before publication and preserve local identities. See [Storage](STORAGE.md) and [YouTube](YOUTUBE.md) for contracts and bounds.

## Skin rendering contract

Renderer rectangles are declared in SkinLayout.h, EqualizerLayout.h and PlaylistLayout.h. The bundled Classic atlases supply all required states, glyphs and tiles; dimensions are documented in the [skin README](../assets/skins/classic/README.md). Tests cover loader bounds, palettes and rendered pixels as well as interaction geometry. See [third-party notices](../THIRD_PARTY_NOTICES.md) for licensing.

Source entry points: [PlaybackSession](../src/media/playback/PlaybackSession.cpp), [DSP](../src/media/playback/EqualizerDsp.cpp), [Spectrum](../src/media/playback/AudioSpectrum.cpp), [MediaStore](../src/media/MediaStore.cpp), [SkinLoader](../src/skin/SkinLoader.cpp), [Docking](../src/ui/ClassicWindowDock.cpp).
