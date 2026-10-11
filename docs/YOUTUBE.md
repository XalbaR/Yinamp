# YouTube

## Input and import

Playlist ADD URL and ADD URL LIST open the same single URL dialog. It accepts supported YouTube video URLs or playlist-only `/playlist?list=id` URLs. A watch URL containing both `v` and `list` adds the video only. The Unicode edit is bounded to 4096 characters; pasted line breaks/tabs become spaces so multiple URLs cannot silently add the first alone.

For batch video URLs, use ADD FILE with one UTF-8 TXT file, optional BOM, one URL per non-empty line. Limits are 1 MiB and 100 non-empty lines. First-occurrence deduplication and invalid-line counts appear in the result dialog. Multiple TXT files or TXT mixed with media are rejected before playlist mutation.

Public playlist enumeration runs on an owned cancellable worker. It recognizes supported page/browse containers, skips recognized private/deleted/unplayable rows, deduplicates in source order and caps output at 500 accessible videos, 100 continuations and 32 MiB responses. Unsupported/failed continuation responses fail preparation without partially changing the playlist. Result dialogs report found, added, duplicate and truncation counts.

Every explicit import fetches current membership, even when a snapshot exists. Re-import appends missing IDs while preserving local order, existing rows, user names and rows absent remotely; it does not synchronize removals/reordering. Success refreshes the snapshot and seeds video metadata. Failure never substitutes stale membership or overwrites a good snapshot. Duplicate-only import does not overwrite existing user-visible metadata.

## Modes

**Download/Local** is the default. It queues serial cancellable acquisition and waits for validated completion before playing YouTube media. Completed downloads are reused.

**Online/Stream** cancels the download queue and opens remote media without creating media/mux/cache files. Known title/duration can be reused, but every source opening resolves fresh direct signed media URLs. An existing local download does not change an Online source into local playback. Container buffers remain in memory; metadata/session state may still be persisted.

The mode is saved and affects the next source opening. Switching back to Download requeues eligible rows. Local files always remain local. Online metadata resolution has its own queue/error state, uses cached metadata or watch-page/client fallback, and avoids repeated automatic retries of failed rows. User display names survive updates. Track Info does not trigger resolution.

## Supported formats and pipeline

Native WinHTTP watch-page/InnerTube extraction discovers metadata and direct formats. Selection probes MP4 and prefers adaptive SDR H.264 up to 1080p/60 fps with AAC; progressive H.264/AAC is fallback. There is no signature JavaScript decipherer, authenticated/private-content path, live/SABR manifest support, WebM/AV1/VP9/Opus path, transcoding or quality picker. Native AAC extraction creates M4A, not MP3.

Download stages are Queued → Resolving → Downloading → Finalizing → Ready, with Failed/Cancelled states. Adaptive fMP4 compressed samples are remuxed into flat A/V MP4 without decoding/re-encoding. Progressive MP4 is validated and AAC extracted to `audio.m4a`. The store publishes `final.mp4` and `audio.m4a`, then completed `metadata.json`. Interrupted transfers restart rather than resume partial files. Selected access/expiry errors permit one fresh resolution. Removed rows, clear and mode switches cancel unnecessary work; old results are discarded.

Online uses native readable/remote/seekable IMFByteStream Range reads of at most 256 KiB per request/cache chunk. HTTP 206, matching Content-Range/length and byte counts are mandatory; ignored ranges fail instead of downloading a full response. Adaptive audio/video sources share a Media Session clock. Selected access/not-found/network/timeout errors allow one bounded fresh resolution, preserving position and paused/playing intent. Repeated failure enters Failed. WinHTTP stages use 10-second timeouts; Opening has a 30-second watchdog.

## Privacy and service boundaries

YouTube extraction and playback contact YouTube/Google media services. Source URLs, titles and import provenance can be stored locally. Runtime API keys, cookies, signed media addresses and continuation tokens are not serialized. See [Storage](STORAGE.md).

Availability depends on the service and direct-format access; offline tests do not prove live compatibility. Only stream/download content for which you have applicable rights and comply with platform terms. No external FFmpeg/yt-dlp, browser, WebView or JavaScript engine is launched.

Implementation: [extractor](../src/media/native/YouTubeExtractor.cpp), [playlist resolver](../src/media/native/YouTubePlaylistResolver.cpp), [selection](../src/media/native/MediaSelection.cpp), [muxer](../src/media/native/Fmp4Muxer.cpp), [online byte stream](../src/media/playback/HttpByteStream.cpp).
