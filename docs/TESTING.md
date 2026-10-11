# Testing

## Deterministic Debug suite

```powershell
cmake --preset windows-x64-tests
cmake --build --preset debug-tests-x64
ctest --preset debug-tests-x64 --no-tests=error
```

The tests tree uses `BUILD_TESTING=ON` and is separate from daily app builds in `build/windows-x64-app` (`BUILD_TESTING=OFF`). The test preset selects Debug and prints failure output. For an explicit directory use `ctest --test-dir build/windows-x64-tests -C Debug --output-on-failure`. Do not switch configurations to bypass a failure.

Standard CTest uses synthetic media, injected metadata/network runners, isolated temporary roots and loopback HTTP fixtures. Loopback tests exercise Range/status/cancellation without contacting YouTube/CDNs. Media Foundation tests generate local audio/video samples, including encoded fixtures through Windows codecs; they require Windows media components and Win32 HWND support, but no physical audio endpoint.

## Hosted deterministic coverage

The standard suite runs all 36 registered tests on GitHub-hosted Windows Server 2022.
The existing `missing_skin_startup` return-77 condition is the only expected skip.
No environment-variable bypass, test exclusion, timeout extension or failure masking is used.

Playback fixtures explicitly inject `tests/PlaybackOutput.h` through `AudioOutput`.
This uses Microsoft's real sample-grabber sink with `MF_SAMPLEGRABBERSINK_IGNORE_CLOCK=FALSE`.
It replaces only the physical audio output; Media Foundation still resolves sources, decodes,
loads topologies, aggregates adaptive A/V sources, negotiates EQ/spectrum transforms and
owns the presentation clock, state transitions, seeking, completion and shutdown.
The integration test checks decoded output sample delivery and mute values on the injected
volume service as well as the existing source, metadata, DSP and transport assertions.

Normal Yinamp never constructs this fixture. Without an explicit adapter, `PlaybackSession`
continues to create SAR and obtain its real `MR_STREAM_VOLUME_SERVICE`; EVR is retained in
both standard and interactive suites. Content reset retains the explicitly injected adapter.

Native UI tests use a thread-local PMv2 DPI context, synthetic input and a controlled HWND
lifecycle. A thread CBT hook replaces untouched OS maximum tracking sizes with explicit
8192x8192 bounds, so a 928-pixel-tall 2x Playlist need not fit the runner's physical screen.
Application-owned size constraints, docking, shade transitions and their assertions remain
intact. This does not modify monitor settings or validate physical viewport containment.
Rename/URL bitmap assertions render the same Direct2D drawing code to a 96-DPI WIC software
surface using repository atlases; screen capture and foreground visibility are not their oracle.
Modal quit assertions retrieve `WM_QUIT` through a controlled message pump rather than
assuming one `PeekMessage` call sees the low-priority synthesized quit message.

CTest labels describe behavior: `unit`, `integration`, `ui`, `playback`, and (in the real-output
configuration) `interactive`. Labels do not filter either full validation suite.

## Interactive-only coverage

Run the same complete suite against real SAR/audio endpoint output on a Windows machine
with a working audio device (or a future self-hosted interactive runner):

```powershell
cmake --preset windows-x64-interactive
cmake --build --preset debug-interactive-x64
ctest --preset debug-interactive-x64 --no-tests=error
```

This separate build tree sets `YINAMP_INTERACTIVE_VALIDATION=ON` only for test executables.
The five native playback/UI integration executables then omit the test audio adapter;
their CTest entries also carry `interactive`. Assertions are retained, not converted into
successful no-ops. A missing endpoint must fail this configuration. The Yinamp executable
uses normal SAR/EVR in either build configuration.

### Reason for separation

A hosted server does not promise an audio endpoint or an interactive user desktop. The
standard sink validates decoded sample delivery and playback contracts without claiming
that speakers played sound. Actual endpoint/driver negotiation is covered by the interactive
configuration. Audible EQ, physical A/V synchronization, monitor containment, desktop
activation across applications, taskbar/Explorer UI and physical multi-monitor/DPI transitions
still require manual device/desktop validation. Synthetic HWND tests validate application
routing/layout contracts, not those physical desktop semantics.

Sample-grabber documentation: [sink creation](https://learn.microsoft.com/en-us/windows/win32/api/mfidl/nf-mfidl-mfcreatesamplegrabbersinkactivate)
and [clock scheduling](https://learn.microsoft.com/en-us/windows/win32/medfound/mf-samplegrabbersink-ignore-clock).

## Coverage

- Geometry, atlas bounds, required-file diagnostics, Unicode and WIC/Direct2D.
- Main/EQ/Playlist interaction, docking, startup layout, menus and shell identity/tray lifecycle.
- Playlist formats, naming, persistence, batch/playlist imports and metadata caches.
- Native HTTP transport, synthetic MP4 mux/store publication and cancellation.
- Playback/controller/clock, local video/audio and injected online-source behavior.
- EQ DSP/MFT, spectrum FFT and processing fallback.
- Media Library catalog/search/sort/naming/playback and safe selective reset.

Skin-dependent tests use the bundled MIT-licensed Classic atlas directory. `missing_skin_startup` retains its existing return-77 skip when a complete skin is present; it is registered and is not disabled for CI.

## GitHub Actions

[windows-debug.yml](../.github/workflows/windows-debug.yml) runs on windows-2022 with the x64 Visual Studio generator, Debug configure/build and the full standard CTest suite. Checkout is pinned and token permissions are read-only. No package downloads or live-service validation are part of tests. A moving runner image/toolchain means this is deterministic fixture validation, not a promise of byte-identical binaries.

The entire registered suite runs without filters or continue-on-error. A hosted CI run must be confirmed after the public repository is created.

## Optional manual checks

Codec/device negotiation, audible EQ behavior, physical A/V sync, multi-monitor/DPI behavior and live YouTube availability need separate manual validation. They are not covered by offline success.

Developer harness flags such as `--preview`, `--preview-2x` and `--snapshots` may open windows or generate captures. `yinamp_playlist_unicode_tests --extractor-audit` performs a live network request and is deliberately outside registered CTest and CI. These are test flags, not application CLI features. Use only Debug executables, synthetic content and isolated storage; do not commit generated captures or real user data.
