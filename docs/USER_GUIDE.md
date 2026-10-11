# User guide

Yinamp includes its MIT-licensed default Classic skin. See [Getting started](GETTING_STARTED.md).

## Main Window

The executable embeds the multi-resolution Yinamp application ICO (16, 20, 24, 32,
40, 48, 64 and 256 pixels). Main class and HWND big/small icons use that resource,
including DPI refresh, for its system icon, taskbar and Alt+Tab. Startup sets the
stable process AppUserModelID `Yinamp.Yinamp` before COM/shell use.

One system tray icon has the tooltip **Yinamp**. Double-click shows hidden Main,
restores minimized Main and requests foreground activation. Single left click has
no action. Right-click opens the full custom Winamp Classic-style root menu through
`MainWindowMenu` + `ClassicPopupMenu` and the same command execution path as the Main/auxiliary menus;
Exit posts the normal Main close path. Explorer's `TaskbarCreated` broadcast re-adds
the icon and negotiates version 4 again. RAII and normal close remove the tray icon.

There is no close-to-tray or minimize-to-tray feature: Main close exits and
EQ/Playlist/Library close hides. Root/Main/tray popup presentation is drawn by
Yinamp rather than native Windows 11 menus.
Normal Windows hover thumbnails/live previews and window ownership/styles are unchanged.

| Control / command | Current behavior |
| --- | --- |
| Previous | Restarts current media if position is greater than 3 seconds; otherwise traverses previous playlist/shuffle history. At the beginning it seeks to zero unless repeat permits wrapping. |
| Play | Resumes a paused source or restarts loaded stopped/ended media from zero; for a new selection tries selected rows, current row and then playlist candidates. Missing/unready local-mode sources cannot open. |
| Pause | Toggles native Playing/Paused; no effect in other controller states. |
| Stop | Stops and resets displayed position. During Opening, closes/cancels the source. Does not clear playlist/cache. |
| Next | Opens next eligible ordered/shuffled item. Without repeat, traversal can stop at the end. |
| Eject / Play > Open local media | Native multiple-file picker; append chosen local files, then activate the first added row. Cancel leaves the list unchanged. |
| Shuffle | Randomized traversal bag, previous history, repeat-aware refill; does not reorder the displayed list. |
| Repeat | Wrap/refill playlist traversal. No distinct repeat-one selection. |
| EQ / PL | Toggle Equalizer/Playlist visibility, bring newly opened auxiliary to foreground. Hiding retains session state. |
| Title menu button / ordinary right-click | Shared custom Classic root menu with About, Play, window toggles, fixed Skins, Options, Playback and Exit. Root checks reflect actual window visibility. |
| O / Options | Elapsed/remaining, Double Size, Repeat, Shuffle and next-source Download/Online preference. Right-click O also opens Options. |
| A / always-on-top | Toggle topmost Main/EQ/Playlist; session-only. No explicit shared Video Window topmost command. |
| I / Track Info | Read-only Classic modal for the playback current entry, falling back to the playlist current entry before playback. Shows available titles, canonical source/path, duration, mode/state, audio/video format and online itags; Escape/Enter/OK/Close dismiss. Only an empty current entry shows `No track is loaded.`. |
| D / Double Size | Explicit 1x/2x scaling of all three Classic windows, including hidden windows; not saved. |
| V / Video Window | Toggles the existing Video Window; selected only while visible. Audio-only/no source is inactive. Hiding does not stop playback. PCM spectrum runs independently. |
| Main logo / Yinamp... | About dialog identifying Classic interface and native A/V playback. |
| Minimize | Native Main minimize. Main is not a freely resizable/maximizable window. |
| Shade / title double-click | Full/compact toggle; compact Main retains Previous/Play/Pause/Stop/Next/Eject, seek and time click. |
| Close / Exit / Main Alt+F4 | Close application, save durable state, cancel/join owned jobs and shut down native playback. |
| Main Window menu toggle | Hide/show Main; available from shared auxiliary context menus even when Main is hidden. |
| Media Library menu toggle | Shows/hides the existing YINAMP LIBRARY window; closing Library does not stop playback. |
| Time click / Time elapsed / Time remaining | Choose display mode. Time follows native presentation position. Remaining requires known duration; stopped playback blanks Main time. |
| Seek slider | Captured absolute seek, clamped to duration; supports paused seek. Unknown/nonseekable sources reject duration-based seeks. |
| Volume slider / Main mouse wheel | Volume 0–255; each complete wheel notch changes by 13, clamped. Wheel remainder preserves partial notches. |
| Balance slider | -127 to +127, center zero; attenuates first left/right channels rather than mixing them. Mono has volume but no stereo pan; further channels keep overall volume. |
| Back 5 seconds / Fwd 5 seconds | Relative seek through same duration/seekability gate. |
| 10 tracks back / 10 tracks fwd | Disabled; no ten-track navigation implementation. |
| Repeat / Shuffle right-click | Specialized checked one-item popup for that toggle. |
| Skins / fixed Classic | Fixed/disabled entries, no selection/browser/archive loader. |

Transport mouse actions require release inside the captured control; drag-out, capture
loss, deactivation and cancellation clear transient pressed state. Menus use Yinamp's
GDI-rendered popup HWNDs: Tahoma at 11 px, 18 px rows, 7 px separators,
18 px check gutter and 16 px arrow gutter at 96 DPI, flat dark-blue selection, square
1 px border and no theme shadow/blur. Monitor DPI scales content; Double Size does not.
Submenus open after 200 ms hover or Right/Enter, retain a brief diagonal-travel grace,
flip left at the right work-area edge and clamp vertically. Up/Down/Home/End skip
disabled/separator rows; Left/Escape close the current submenu, root Escape/outside
click/capture or focus loss close the session. Mouse click/Enter returns the existing
command IDs. Popup HWNDs do not appear in taskbar/Alt+Tab and are destroyed on close.
This does not constitute a complete Winamp Preferences suite. No global
keyboard transport-hotkey system is implemented by the current Main handlers.

Main displays native playback status, actual negotiated MONO/STEREO, audio bitrate and
whole kHz (44.1 kHz appears as 44). Missing values are blank; audio-less/failed/closed
sources clear audio information. Bitrate/kHz wells are blank while stopped. ASCII Main
titles use bitmap glyphs; full non-ASCII titles switch to DirectWrite/Arial. Small bitmap
labels retain atlas limits. Title text clips; a scrolling title/marquee is not implemented.
The full Main four-digit time display saturates at 99:59; this display limit does not
shorten playback or the presentation clock.

## Equalizer

| Control | Current behavior |
| --- | --- |
| ON | Request actual preamp/ten-band decoded-PCM processing. OFF bypasses and retains gains. Processing can fall back when MF negotiation fails. |
| AUTO | Toggle artwork/state only; no per-track presets or automatic gain adjustment. |
| Preamp | -12 to +12 dB, separate from frequency-band filters. |
| Ten band sliders | 60/170/310/600/1000/3000/6000/12000/14000/16000 Hz, -12 to +12 dB, live parameter publication. |
| Horizontal band drag | Transfers captured adjustment across frequency bands; preamp remains separate. |
| +12 / 0 / -12 dB labels | Set all ten frequency bands together, without changing preamp. |
| Curve / preamp line | Render requested settings from Classic artwork/colors; not a measured acoustic response. |
| PRESETS click/right-click | Shared Classic popup: Flat, Save session preset, Load session preset. Load disabled until saved. |
| Flat | Set all eleven gains, including preamp, to zero; retain ON state. |
| Save / Load session preset | One in-memory gain-array slot. No disk persistence, file import/export or saved ON/AUTO state. |
| Shade / title double-click | Compact EQ. Compact volume/balance share Main transport settings. |
| Title drag / Close / Alt+F4 | Move/dock or hide EQ and clear Main EQ selection; do not stop playback. |
| Ordinary right-click | Shared Main root menu; PRESETS region opens its own preset menu. |

DSP is a synchronous float32 IMFTransform with one pending sample and preserved
sample timing/attributes, 8–192 kHz and 1–8 channels. Ten peaking biquads have per-channel
histories, coherent live parameter revisions and about 20 ms old/new-bank crossfades.
Bands at or above 45% of sample rate remain neutral. Numeric nonfinite values are
neutralized; float headroom is preserved rather than a limiter being added. Flush,
stream start, discontinuity and format/source/seek transitions reset history.

Normal topology: decoder/converter -> EQ -> PCM tee -> SAR and spectrum tap.
Fallback order: EQ+spectrum, EQ only, spectrum only, plain native playback. ON is a
stored request, not an availability indicator. EQ controls/preset/ON/AUTO are session-only.

## Playlist

ADD/REM/SEL/MISC bottom stacks keep their separate skin/bitmap implementation and
keyboard behavior. Their secondary options and row naming popups use the shared
Classic HWND renderer. EQ presets and Library entry popups also use that renderer;
Library title/background right-click forwards the shared Main root menu.

| Surface / action | Current behavior |
| --- | --- |
| ADD FILE | Multiple local media files or a single UTF-8 .txt YouTube URL list (case-insensitive extension). TXT uses the existing batch importer, never a media entry; mixed/multiple TXT selections are rejected before mutation. Existing Classic dialogs show added/duplicate/invalid counts or errors. Media filenames are initial titles; file choice does not certify a Windows decoder. |
| ADD DIR | Nonrecursive regular MP3/WAV/FLAC/OGG/M4A files in chosen folder; no video extension list, recursive library scan or directory watcher. |
| ADD URL / ADD URL LIST | Both open the same single video/playlist URL dialog. TXT lists use ADD FILE. |
| REM SEL / Delete | Remove selected rows only. Media files remain. Current-row removal closes that playback source on controller polling. |
| REM ALL / NEW LIST | Clear playlist rows and save the empty durable session; not a cache purge. |
| CROP | Retain selected rows, remove unselected rows; no media deletion. |
| REM MISC > Remove dead files | Local existence failures establish deadness only without access errors. Ready YouTube rows are dead if recorded media is missing/not regular/empty. Pending Online/YouTube rows are retained; duration is not liveness. |
| Remove duplicate entries | Keep first local normalized path (case-insensitive ordinal comparison) or case-sensitive YouTube video ID. |
| Physically remove selected file(s) | Explicit confirmation, shell/Recycle Bin where supported. Only successfully removed local-file rows disappear. Directories/reparse points are skipped; YouTube MediaStore media is preserved. Results report removed/failed/skipped counts. |
| SEL ALL / SEL NONE / INVERT | Update transient selection; no source/cache modification. |
| SORT by title / filename / path and filename | Stable case-insensitive ordinal sorting. YouTube filename sort uses title. Current/selected rows retain identity. |
| Reverse / Randomize list | Change displayed/durable row order; separate from playback shuffle mode. |
| FILE INF | Read-only focused entry, otherwise first selected entry; empty list does nothing. Shows title/type/source/duration/ID, local existence/path or YouTube ID/download status/progress/path/error. Not editable tag metadata. |
| Row context menu / keyboard context on focused entry | Select entry and show Change display name / Restore original name. Restore is enabled only with an override. Background/title routes retain the shared root menu. |
| Change display name | Classic RENAME dialog at 1x/2x with preselected Unicode text, Enter/Save, Escape, Ctrl+A and shade. Trim end whitespace; reject empty/multiline/tab names. Model limit 16384 UTF-8 bytes; edit limit 4096 characters. |
| Restore original name | Clear override and restore retained original title. No file/URL/ID/embedded tag changes. Completed YouTube metadata write failures leave the explicit edit unapplied. |
| Generate HTML Playlist | Save escaped UTF-8 row/order/title/duration/source; only validated YouTube URLs become links. No automatic opening/upload. |
| Read extended info on selection | Disabled; no automatic extended tag extraction. |
| LIST OPTS > LOAD LIST | Parse and append M3U/M3U8/PLS; does not replace existing rows. Skip invalid/unsupported network sources and duplicate YouTube IDs. Local duplicates can remain until cleanup. |
| SAVE LIST | M3U/M3U8/PLS from current ordered rows; UTF-8 exports. Cancel/failure does not publish a partial replacement. |
| Plain click / Ctrl-click | Select row / toggle individual selection. Clicking existing selection without dragging collapses to that row on release. |
| Shift / Ctrl+Shift | Select range / additive range from anchor. |
| Drag selected row(s) | Move selected group by rows, preserving ID/current/focus/anchor identity. No drop-file import or cross-app drag/drop route. |
| Up/Down, PageUp/PageDown, Home/End | Move/select visible focus; Shift extends, Ctrl alone moves focus without altering selection. |
| Ctrl+A / Enter / double-click | Select all / play focused row / play clicked row. |
| Scrollbar / wheel / arrow buttons | Captured thumb, page clicks, three rows per wheel notch, four rows per arrow-button action. |
| Bitmap menu keyboard | Up/Down item selection, Enter execute, Escape dismiss; outside click/capture loss dismiss. |
| Bottom mini transport | Previous, Play, Pause, Stop, Next, Eject share Main commands. |
| Status / small time | Entry count and known-duration sum with `+` for unknown entries; active download replaces status with resolving/progress/finalizing. Small time is selected duration, not playhead. |
| Ready/progress presentation | Ready rows show normal title without READY prefix; pending/error download states can prefix row text. |
| Row text / compact title | DirectWrite with PLEDIT.TXT font face/colors and Unicode/case preservation. Current-row color and selection background are separate. |
| Resize / shade / close / right-click | 25x29 logical steps, minimum 275x116, tiled/cropped atlas; shaded width resize absent. Compact current title; close hides/clears PL selection; entry naming menu or background/title root menu. |

M3U8 requires valid UTF-8; M3U/PLS try UTF-8 then Windows ANSI. Relative paths use the
playlist file's directory. Unknown durations remain unknown. Network list rows support
validated single-video YouTube input only, not radio/general HTTP streaming or playlist
expansion. HTML/list exports use staged replacement, not per-row output writes.

## Media Library

Open **Media Library** from the main Yinamp root/context menu. The same resizable,
owned native window is shown or hidden on subsequent uses; its title is
**YINAMP LIBRARY**. Closing this window only hides it.

The window uses Direct2D/DirectWrite, the existing Playlist palette and font, a
Classic bitmap title strip from Main/Video with a centered bitmap label and borders.
Text uses the same Playlist font face and 10-unit font size. The search field uses the same styled
native EDIT approach as the existing Classic dialogs. No skin atlas is stretched.
The row context menu includes **Go to YouTube**, which opens the selected video's
canonical YouTube URL in the default browser without submitting it or changing playback.
The left pane contains ALL and alphabetical author/channel groups with full
catalog counts; missing authors appear as Unknown. Both panes support mouse-wheel
scrolling. The right pane also has a Classic scroll rail and keyboard navigation.

### Catalog and view

The Library view merges completed MediaStore records with the existing playlist's
valid YouTube entries, including online streams. It does not create a second
playlist or resolve/download anything while refreshing. YouTube ID deduplicates
the union; a validated completed file takes precedence. Online rows follow the
playlist session's lifetime; arbitrary local-file playlist rows are not cataloged.

`MediaStore::listCompletedVideos()` reads only the immediate MediaStore root.
It does not create a missing root. `findCompletedVideo()` and enumeration share
the same `ReadCompleted` helper and identity validation. Reparse roots are rejected;
unsafe/reparse entries, malformed or oversized metadata, mismatched identities,
incomplete downloads and unsafe/nonexistent/empty media files are skipped.
Schema-v2 records require the existing final.mp4 and audio.m4a completion contract;
legacy records retain the lookup's existing validation rules.

The table shows effective Title (displayTitle, then original title), Author,
Time, stored video Size, Source and Location. Source is the persisted YouTube
playlist title, **Single entry** for a new single-URL submission, or **Unknown source**
for older records without provenance. Playlist imports carry source identity/title
through the session and media job into metadata.json; repeated imports preserve
known original provenance. No network lookup or guessed backfill runs for old media.
TXT filename/path provenance is not persisted; single videos from TXT use the same
Single entry label as manual single-URL submissions.
Location is **Local** for validated completed MediaStore files and **Online** for
YouTube playlist rows without a completed file. Online size is unknown (`--`). Source and
Location headers also support sorting. Stored size uses B/KB/MB/GB/TB and excludes the
audio sidecar. Each read-model entry also retains video ID, canonical URL,
original source URL, original/display titles, duration, media/audio/metadata
paths, strategy and available codecs, itags, height and fps.

Online PLAY reuses the same playlist identity and MainWindow playback pipeline,
respecting the user's Download/Online preference. Online display names reuse
PlaylistModel's existing pending-entry rename and session persistence; local names
continue to use metadata.json. Track Info presents known online metadata without
starting a resolver. Refresh turns an online row into a local row when completion
is validated, preserving selection and leaving a currently playing stream alone.

Search applies an invariant Unicode lowercase comparison to effective title,
original title and author. Search and author filters are combined. Clicking
Title, Author, Time or Size changes ascending/descending order. Video ID breaks
ties deterministically. Filtering/sorting never modifies storage or playlist order.
Selection is retained by video ID; actions use only selected visible rows.
Ctrl toggles selection, Shift selects a range and Ctrl+A selects visible rows.
Arrow/Home/End/Page keys navigate; Ctrl+F/Tab moves to search, Enter plays, F5 refreshes.

Opening the Library refreshes it. REFRESH/F5 performs a controlled rescan, and a
three-second timer refreshes only while the window is visible and no Library
context menu/dialog is open. There is no directory watcher.

### Playlist, playback and naming

ADD TO PLAYLIST sends selected rows in visible order through the existing
`PlaylistModel::SubmitYouTubeUrls` boundary. Its YouTube-ID duplicate policy and
completed MediaStore reuse apply unchanged. A stale row whose completed files
are no longer available cannot enqueue another download.

Double-click, Enter, context Play and PLAY submit/reuse the first selected visible
row, then call PlaylistWindow's existing `playItem` callback, which invokes the
MainWindow-owned PlaybackController. Library owns neither a playlist copy nor a
playback controller/session. The existing Download/Online preference continues
to determine explicit PLAY behavior.

The right-click menu includes Play, Add to Playlist, Change display name,
Restore original name and Track Info. Rename reuses the Classic rename dialog
via a small title/save callback adapter. Completed playlist and Library renames
share `PlaylistModel::SetStoredDisplayTitle`, which persists through the existing
`MediaStore::setDisplayTitle` atomic metadata update and immediately updates
matching open playlist rows. Restore removes displayTitle and restores the
metadata's original title in both views. Playlist-originated changes appear in
Library on its next controlled refresh.

Track Info uses the existing presentation formatter and Classic Track Info
dialog, fed by the Library disk read model. It does not resolve network metadata.

Open/close, search/filter/sort, refresh and naming do not call playback. They do
not reopen a source, change its generation or mutate transport, video policy,
EQ, volume/balance, repeat/shuffle or playback preference. Only explicit PLAY
crosses into playback.

## Selective content reset

With a Yinamp window focused, hold **Ctrl**, then press **N, C, T in order**.
Release each letter before the next; only Ctrl needs to stay held. There is no
normal menu entry. A Classic skin dialog lists the four exact target paths with
independent `YES [  ] / NO [ x ]` choices. All four default to NO. Holding the
last key does not repeatedly open confirmations. Releasing Ctrl or typing a
different key cancels an unfinished sequence. Keyboard events are handled before
dispatch for all owned Yinamp windows, including the Library search field.

Clicking a choice only changes its `[ x ]` marker. CANCEL, Escape and closing the
dialog leave playback, downloads, playlist and files unchanged. RESET requires
at least one YES selection. It closes playback, cancels and joins workers,
temporarily suspends session saving and clears the in-memory playlist, then deletes only
the selected categories:

- `%LOCALAPPDATA%\Yinamp\session.json`
- `%LOCALAPPDATA%\Yinamp\youtube-info\playlists` (including any nested session.json)
- `%LOCALAPPDATA%\Yinamp\youtube-info\videos`
- The Windows Music known folder's `YINAMP` directory, including partial downloads

The actual session file is in the Yinamp root, not normally beneath playlists.
Only dedicated Yinamp paths are accepted. Preflight rejects reparse points,
traversal and inaccessible paths before deleting anything. Each path is checked
again during removal. Failure after removal begins is reported as an incomplete
reset; removed files cannot be recovered by Yinamp.

Yinamp stays open after RESET. Selecting all four categories leaves the playlist
and Library empty immediately. Playback and downloads can be used again without
restarting. A preserved session is restored when session.json is set to NO.
After deleting the session, it stays absent until a new durable playlist change;
closing an untouched empty playlist does not recreate old content.
Settings, skins, original input TXT files and music outside Music\YINAMP are
preserved.

For acquisition details see [YouTube](YOUTUBE.md); for stored files see [Storage](STORAGE.md).
