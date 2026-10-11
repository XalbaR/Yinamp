# Storage and privacy

## Production locations

Paths are resolved using Windows known folders; Music and LocalAppData may be redirected by Windows. Data is outside the executable folder.

| Location | Contents | Lifetime |
| --- | --- | --- |
| `%LOCALAPPDATA%/Yinamp/session.json` | Ordered local/YouTube sources, names, duration, optional display/original names and import provenance | Durable playlist changes and shutdown. |
| `%LOCALAPPDATA%/Yinamp/window-position.txt` | Last normal Main origin | Restored and clamped to available work area. |
| `%LOCALAPPDATA%/Yinamp/playback-mode.txt` | Download/Online preference | Next source opening. |
| `%LOCALAPPDATA%/Yinamp/youtube-info/videos/.../info.json` | ID-keyed source title/duration metadata | Reader TTL 30 days. |
| `%LOCALAPPDATA%/Yinamp/youtube-info/playlists/.../info.json` | Playlist membership/metadata/flags | Reader TTL 24 hours; explicit imports always fetch current membership. |
| Music known folder `/YINAMP/<videoId>` | `final.mp4`, `audio.m4a`, `metadata.json` and temporary `.work` files | No automatic media eviction or TTL. |

Info-cache IDs are hex encoded to preserve case on Windows. The application does not rename download directories to titles. Artwork/thumb files are not downloaded by the UI. Online mode writes no media bytes to disk but retains metadata and session state.

## Persistence contracts

Session version 1 validates Unicode, types and identities, bounds input to 16 MiB, skips invalid rows, deduplicates restored YouTube IDs and assigns fresh runtime IDs. Missing/corrupt root data starts an empty playlist. Selection/current/playhead/progress/errors/jobs are not restored; playback starts stopped. Completion is re-derived from validated MediaStore files.

Custom names survive restore and asynchronous metadata updates. Completed YouTube labels are staged into metadata.json and recover on re-add; pending labels are copied on completion. Local labels affect the session, not the original media/tags. Source titles remain separate. Playlist identity/title provenance persists; old unknown provenance and TXT filename/path provenance cannot be reconstructed.

Session, mode, position and info-cache writes use staged replacement; identical session writes are avoided and failed session saves preserve the previous file. Info-cache records are bounded to 16 MiB, MediaStore metadata to 64 KiB. Info writes are best-effort; expired/corrupt video metadata is a miss.

MediaStore publishes files individually and completed metadata last, rather than one three-file transaction. Reuse checks identity, exact expected names, metadata contract and nonempty regular files, rejects unsafe/reparse paths and does not deeply decode every file. Schema-v2 completion requires final.mp4 and audio.m4a. Partial/incomplete/malformed records are not Library downloads.

Scale, auxiliary layout/docking/visibility, EQ/AUTO/preset/gains, volume/balance, repeat/shuffle, time mode and topmost remain session-only.

## Privacy

Sessions, exported playlists/HTML and metadata can disclose local media paths, source URLs, titles and viewing/download information. Do not attach them to public issues without sanitizing them. Runtime cookies/API keys, continuation tokens and signed CDN addresses are excluded from persisted metadata; playback resolves fresh addresses. Online requests still reveal access to the remote service.

## Removal and reset

Removing playlist rows, REM ALL and NEW LIST preserve downloaded media. Explicit local-file physical deletion uses confirmation and the Windows shell/Recycle Bin where supported; YouTube media, directories and reparse points are excluded. Library has no per-entry disk deletion command.

Hold Ctrl, then press N, C, T in order to open the selective reset dialog. All categories default to NO. RESET requires a YES choice and deletes only selected dedicated session, playlist-cache, video-cache or Music/YINAMP targets after releasing playback and joining workers. CANCEL/Escape/close makes no changes. Dedicated-path and reparse checks precede deletion; partial removal failure is reported. Yinamp cannot undo deleted files.

Yinamp remains open after reset. A preserved session is restored. A deleted session stays absent until another durable playlist change; closing an untouched empty list does not recreate it. Settings, skins, original TXT files and music outside Music/YINAMP are preserved. See [User guide](USER_GUIDE.md) for exact interaction.
