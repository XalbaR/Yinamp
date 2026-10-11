#include "TemporaryMediaRoot.h"
#include "platform/windows/WindowsRuntime.h"
#include "playlist/PlaylistFile.h"
#include "playlist/PlaylistOperations.h"
#include "ui/PlaylistInteraction.h"
#include <fstream>
#include <iostream>
#include <set>

using namespace yinamp;
using namespace yinamp::playlist;
namespace {
void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
void Write(const std::filesystem::path& path, std::string_view text) {
    std::ofstream output(path, std::ios::binary);
    output << text;
    Check(static_cast<bool>(output), "Temporary fixture write");
}
std::size_t Selected(const PlaylistModel& model) {
    return static_cast<std::size_t>(
        std::ranges::count_if(model.entries, [](const auto& entry) { return entry.selected; }));
}
std::set<std::uint64_t> Ids(const PlaylistModel& model) {
    std::set<std::uint64_t> result;
    for (const auto& entry : model.entries)
        result.insert(entry.id);
    return result;
}
}
int main() {
    try {
        platform::ComApartment apartment;
        TemporaryMediaRoot temporary;
        const auto root = temporary.path / L"store";
        const auto existing = temporary.path / L"existing.mp3";
        const auto missing = temporary.path / L"missing.mp3";
        Write(existing, "media fixture");

        ui::PlaylistInteraction state;
        state.model = PlaylistModel(root);
        for (int i = 0; i < 12; ++i)
            state.model.Add("Track " + std::to_string(i));
        state.model.SelectAll(true);
        Check(Selected(state.model) == 12, "Select All");
        state.model.SelectAll(false);
        Check(Selected(state.model) == 0, "Select None");
        state.Select(5, false, false);
        state.model.InvertSelection();
        Check(Selected(state.model) == 11 && !state.model.entries[5].selected && state.focus == 5 && state.anchor == 5,
              "Invert Selection and focus/anchor");
        state.Select(5, false, false);
        const auto focused = state.FocusId();
        state.model.entries[0].selected = true;
        state.model.entries[5].selected = false;
        state.Delete();
        Check(state.model.entries.size() == 11 && state.FocusId() == focused && state.focus == 4 && !state.anchor,
              "REM SEL preserves retained focus");
        state.Select(4, false, false);
        state.model.Activate(4);
        state.Delete(true);
        Check(state.model.entries.size() == 1 && state.FocusId() == focused && state.model.entries[0].current,
              "CROP keeps selected identity");
        state.model.entries.clear();
        state.Changed();
        Check(!state.focus && !state.anchor && state.scroll == 0, "REM ALL navigation reset");

        PlaylistModel live(root);
        live.Add("Missing", 42, missing.wstring());
        live.Add("Existing unknown duration", {}, existing.wstring());
        Check(live.QueueYouTubeUrl(L"https://youtu.be/dQw4w9WgXcQ"), "Queued YouTube fixture");
        const auto queuedId = live.entries.back().id;
        Check(!IsDead(live.entries.back()), "Queued YouTube is never dead");
        Check(IsDead(live.entries[0]) && !IsDead(live.entries[1]), "Local liveness ignores duration");
        const auto video = root / L"AbCdEf012_-";
        std::filesystem::create_directories(video);
        Write(video / L"video.mp4", "completed media fixture");
        Write(video / L"metadata.json",
              "{\"videoId\":\"AbCdEf012_-\",\"mediaFilename\":\"video.mp4\",\"downloadState\":\"completed\"}");
        Check(live.QueueYouTubeUrl(L"https://youtu.be/AbCdEf012_-"), "Ready YouTube fixture");
        Check(live.entries.back().downloadStatus == DownloadStatus::Ready && !IsDead(live.entries.back()),
              "Ready nonempty media remains");
        auto deadReady = live.entries.back();
        deadReady.id = 900;
        deadReady.localMediaPath = missing;
        live.entries.push_back(deadReady);
        Check(IsDead(live.entries.back()), "Ready missing media is dead");
        auto emptyReady = deadReady;
        emptyReady.id = 901;
        emptyReady.localMediaPath = temporary.path / L"empty.mp4";
        Write(emptyReady.localMediaPath, "");
        live.entries.push_back(emptyReady);
        Check(IsDead(emptyReady), "Ready zero-byte media is dead");
        emptyReady.localMediaPath = video;
        Check(IsDead(emptyReady), "Ready directory is dead");
        RemoveDead(live);
        Check(live.entries.size() == 3 && live.entries[1].id == queuedId &&
                  live.entries[2].downloadStatus == DownloadStatus::Ready,
              "Dead cleanup preserves order and queued/ready/local entries");
        ui::PlaylistInteraction cleanup;
        cleanup.model = PlaylistModel(root);
        cleanup.model.Add("Dead before focus", {}, missing.wstring());
        cleanup.model.Add("Focused survivor", {}, existing.wstring());
        cleanup.Select(1, false, false);
        cleanup.model.Activate(1);
        const auto survivorId = cleanup.FocusId();
        RemoveDead(cleanup.model);
        cleanup.ChangedKeepingFocus(survivorId);
        Check(cleanup.focus == 0 && cleanup.FocusId() == survivorId && cleanup.model.entries[0].current &&
                  cleanup.model.entries[0].selected && !cleanup.anchor && cleanup.scroll == 0,
              "Dead cleanup preserves identity and clamps navigation");
        cleanup.model.Add("Duplicate before later focus", {}, existing.wstring());
        cleanup.model.Add("Later focus", {}, (temporary.path / L"later.mp3").wstring());
        cleanup.Select(2, false, false);
        cleanup.model.Activate(2);
        const auto laterId = cleanup.FocusId();
        RemoveDuplicates(cleanup.model);
        cleanup.ChangedKeepingFocus(laterId);
        Check(cleanup.focus == 1 && cleanup.FocusId() == laterId && cleanup.model.entries[1].current &&
                  cleanup.model.entries[1].selected && !cleanup.anchor,
              "Duplicate cleanup preserves later current/focus identity");
        cleanup.Delete();
        Check(cleanup.focus == 0 && !cleanup.anchor && cleanup.scroll == 0,
              "Removed focused item uses clamped neighbor");

        PlaylistModel duplicates(root);
        duplicates.Add("First", {}, (temporary.path / L"not-here.mp3").wstring());
        const auto firstLocal = duplicates.entries[0].id;
        duplicates.Add("Case duplicate", {}, (temporary.path / L"NOT-HERE.MP3").wstring());
        duplicates.Add("Normalized duplicate", {}, (temporary.path / L"folder" / L".." / L"not-here.mp3").wstring());
        Check(duplicates.QueueYouTubeUrl(L"https://youtu.be/dQw4w9WgXcQ"), "First YouTube duplicate fixture");
        const auto firstYoutube = duplicates.entries.back().id;
        auto copy = duplicates.entries.back();
        copy.id = 999;
        duplicates.entries.push_back(copy);
        Check(duplicates.QueueYouTubeUrl(L"https://youtu.be/DQw4w9WgXcQ"), "Case-sensitive distinct YouTube ID");
        Check(duplicates.QueueYouTubeUrl(L"https://youtu.be/AbCdEf012_-"), "Different YouTube ID");
        RemoveDuplicates(duplicates);
        Check(duplicates.entries.size() == 4 && duplicates.entries[0].id == firstLocal &&
                  duplicates.entries[1].id == firstYoutube,
              "Duplicate cleanup keeps first, normalized case-insensitive local paths, exact YouTube IDs");
        Check(duplicates.entries[2].youtubeVideoId == "DQw4w9WgXcQ" &&
                  duplicates.entries[3].youtubeVideoId == "AbCdEf012_-",
              "Different YouTube IDs remain in order");

        state.model = PlaylistModel(root);
        state.model.Add("z title", 9, (temporary.path / L"a" / L"b.mp3").wstring());
        state.model.Add("A title", {}, (temporary.path / L"z" / L"a.mp3").wstring());
        state.model.Add("a TITLE", {}, (temporary.path / L"b" / L"c.mp3").wstring());
        state.Select(0, false, false);
        state.model.Activate(0);
        const auto currentId = state.FocusId();
        const auto second = state.model.entries[1].id;
        const auto third = state.model.entries[2].id;
        Sort(state.model, SortOrder::Title);
        state.ChangedKeepingFocus(currentId);
        Check(state.model.entries[0].id == second && state.model.entries[1].id == third,
              "Stable case-insensitive title sort");
        Check(state.FocusId() == currentId && state.model.entries[2].current && state.model.entries[2].selected,
              "Sort preserves current, selection and logical focus");
        Sort(state.model, SortOrder::Filename);
        state.ChangedKeepingFocus(currentId);
        Check(state.model.entries[0].id == second && state.model.entries[1].id == currentId &&
                  state.model.entries[2].id == third,
              "Filename sort");
        Sort(state.model, SortOrder::Path);
        state.ChangedKeepingFocus(currentId);
        Check(state.model.entries[0].id == currentId && state.model.entries[1].id == third &&
                  state.model.entries[2].id == second,
              "Path and filename sort");
        Reverse(state.model);
        state.ChangedKeepingFocus(currentId);
        Check(state.model.entries[0].id == second && state.model.entries[2].id == currentId &&
                  state.FocusId() == currentId,
              "Reverse and identity");
        const auto before = Ids(state.model);
        std::mt19937 random(1234);
        Randomize(state.model, random);
        state.ChangedKeepingFocus(currentId);
        Check(Ids(state.model) == before && state.FocusId() == currentId && Selected(state.model) == 1,
              "Randomize preserves IDs and state");
        PlaylistModel fallback(root);
        fallback.Add("Local", {}, (temporary.path / L"z.mp3").wstring());
        Check(fallback.QueueYouTubeUrl(L"https://youtu.be/dQw4w9WgXcQ"), "Sort fallback fixture");
        fallback.entries.back().title = "A fallback";
        Sort(fallback, SortOrder::Filename);
        Check(fallback.entries.front().sourceType == SourceType::YouTubeVideo, "YouTube filename sort uses title");
        Sort(fallback, SortOrder::Path);
        Check(fallback.entries.back().source == L"https://youtu.be/dQw4w9WgXcQ", "YouTube path sort uses original URL");

        PlaylistModel portable(root);
        portable.Add("Türkçe & <ses> \"雪\"", 125, (temporary.path / L"Müzik 雪.mp3").wstring());
        portable.Add("Unknown", {}, missing.wstring());
        Check(portable.QueueYouTubeUrl(L"https://www.youtube.com/watch?v=AbCdEf012_-&t=12"), "Export YouTube fixture");
        const auto originalUrl = portable.entries.back().source;
        portable.entries.back().title = "YouTube & <title>";
        Check(portable.entries.back().downloadStatus == DownloadStatus::Ready, "Completed store reused before export");
        const auto m3u = SerializePlaylist(portable, PlaylistFormat::M3u8);
        Check(m3u.starts_with("#EXTM3U\n#EXTINF:125,") && m3u.find(ToUtf8(originalUrl)) != m3u.npos &&
                  m3u.find(ToUtf8(portable.entries.back().localMediaPath.wstring())) == m3u.npos,
              "M3U8 exports duration and portable YouTube URL");
        const auto playlistPath = temporary.path / L"round-trip.m3u8";
        WritePlaylistFile(playlistPath, m3u);
        PlaylistModel imported(root);
        const auto report =
            ImportPlaylist(imported, ParsePlaylist(ReadPlaylistFile(playlistPath), PlaylistFormat::M3u8, playlistPath));
        Check(report.added == 3 && imported.entries[0].title == portable.entries[0].title &&
                  imported.entries[0].source == portable.entries[0].source &&
                  imported.entries[0].durationSeconds == 125 && !imported.entries[1].durationSeconds,
              "M3U8 load and Unicode round trip");
        Check(imported.entries[2].source == originalUrl && imported.entries[2].youtubeVideoId == "AbCdEf012_-" &&
                  imported.entries[2].downloadStatus == DownloadStatus::Ready,
              "Imported YouTube uses safety boundary and completed MediaStore reuse");
        const auto relative =
            ParsePlaylist("#EXTM3U\n#EXTINF:61,Relative\nsub/../missing.mp3\n", PlaylistFormat::M3u8, playlistPath);
        Check(relative.size() == 1 && relative[0].source == missing.wstring() && relative[0].durationSeconds == 61,
              "Relative local path resolution without existence requirement");
        PlaylistModel urls(root);
        const auto parsedUrls =
            ParsePlaylist("#EXTM3U\nhttps://youtu.be/dQw4w9WgXcQ\nhttps://www.youtube.com/watch?v=dQw4w9WgXcQ\n"
                          "https://youtu.be/DQw4w9WgXcQ\nhttps://example.com/list.m3u8\nhttps://youtu.be/bad\n",
                          PlaylistFormat::M3u8, playlistPath);
        const auto urlReport = ImportPlaylist(urls, parsedUrls);
        Check(urlReport.added == 2 && urlReport.duplicates == 1 && urlReport.skipped == 2 &&
                  urls.entries[0].youtubeVideoId == "dQw4w9WgXcQ" && urls.entries[1].youtubeVideoId == "DQw4w9WgXcQ",
              "YouTube import duplicate suppression and no remote playlist fetching");
        const auto retainedId = imported.entries[0].id;
        imported.entries[0].selected = true;
        imported.Activate(0);
        ImportPlaylist(imported, relative);
        Check(imported.entries[0].id == retainedId && imported.entries[0].current && imported.entries[0].selected &&
                  imported.entries.back().source == missing.wstring(),
              "Load insertion preserves existing identity and appends order");
        const auto pls = SerializePlaylist(portable, PlaylistFormat::Pls);
        Check(pls.starts_with("[playlist]\nFile1=") && pls.find("Length2=-1") != pls.npos &&
                  pls.ends_with("NumberOfEntries=3\nVersion=2\n"),
              "PLS fields and unknown duration");
        PlaylistModel plsLoaded(root);
        const auto plsPath = temporary.path / L"playlist.pls";
        WritePlaylistFile(plsPath, pls);
        Check(ImportPlaylist(plsLoaded, ParsePlaylist(ReadPlaylistFile(plsPath), PlaylistFormat::Pls, plsPath)).added ==
                      3 &&
                  plsLoaded.entries[0].title == portable.entries[0].title && !plsLoaded.entries[1].durationSeconds &&
                  plsLoaded.entries[2].source == originalUrl,
              "PLS save/load and portable source round trip");
        const auto unorderedPls = ParsePlaylist("[Playlist]\nFile2=second.mp3\nTitle2=Second\nLength2=-1\nFile1=first."
                                                "mp3\nLength1=33\nFileBAD=no\nLength3=garbage\nVersion=2\n",
                                                PlaylistFormat::Pls, plsPath);
        Check(unorderedPls.size() == 2 && unorderedPls[0].source == (temporary.path / L"first.mp3").wstring() &&
                  unorderedPls[0].durationSeconds == 33 && !unorderedPls[1].durationSeconds,
              "PLS indexed order, relative paths and malformed individual fields");
        const auto bom = ParsePlaylist(std::string("\xEF\xBB\xBF") + m3u, PlaylistFormat::M3u8, playlistPath);
        Check(bom.size() == 3 && bom[0].title == portable.entries[0].title, "UTF-8 BOM accepted");
        const auto plainM3u = ParsePlaylist(SerializePlaylist(portable, PlaylistFormat::M3u), PlaylistFormat::M3u,
                                            temporary.path / L"list.m3u");
        Check(plainM3u.size() == 3 && plainM3u[0].title == portable.entries[0].title, "M3U uses UTF-8");
        for (const auto format : {PlaylistFormat::M3u8, PlaylistFormat::Pls}) {
            bool invalid = false;
            try {
                (void)ParsePlaylist("<html>not a playlist</html>", format, playlistPath);
            } catch (const std::runtime_error&) {
                invalid = true;
            }
            Check(invalid, "Severely invalid playlist rejected");
        }
        bool invalidUtf8 = false;
        try {
            (void)ParsePlaylist("#EXTM3U\n\xFF\n", PlaylistFormat::M3u8, playlistPath);
        } catch (const std::runtime_error&) {
            invalidUtf8 = true;
        }
        Check(invalidUtf8, "Invalid UTF-8 M3U8 rejected");
        Check(ParsePlaylist("#EXTM3U\n", PlaylistFormat::M3u8, playlistPath).empty() &&
                  ParsePlaylist("[playlist]\nNumberOfEntries=0\nVersion=2\n", PlaylistFormat::Pls, plsPath).empty(),
              "Empty saved lists load cleanly");
        const auto malformed =
            ParsePlaylist("#EXTM3U\n#EXTINF:not-a-number,Retained title\nmissing.mp3\n<html>bad line</html>\n"
                          "#EXTINF:12\nsecond.mp3\n",
                          PlaylistFormat::M3u8, playlistPath);
        Check(malformed.size() == 2 && !malformed[0].durationSeconds && malformed[0].title == "Retained title" &&
                  !malformed[1].durationSeconds,
              "Malformed individual M3U lines do not break remaining records");

        const auto html = GenerateHtml(portable);
        Check(html.starts_with("<!doctype html>") && html.find("charset=\"utf-8\"") != html.npos &&
                  html.find("Türkçe &amp; &lt;ses&gt; &quot;雪&quot;") != html.npos,
              "HTML UTF-8 and escapes ampersand, angle brackets and quotes");
        Check(html.find("Türkçe") < html.find("Unknown</td>") && html.find("Unknown</td>") < html.find("YouTube &amp;"),
              "HTML preserves playlist order");
        Check(html.find("href=\"https://www.youtube.com/watch?v=AbCdEf012_-&amp;t=12\"") != html.npos,
              "YouTube URL safely escaped as link");
        portable.entries.back().source = L"javascript:alert(\"x\")";
        const auto unsafeHtml = GenerateHtml(portable);
        Check(unsafeHtml.find("href=") == unsafeHtml.npos &&
                  unsafeHtml.find("javascript:alert(&quot;x&quot;)") != unsafeHtml.npos,
              "Invalid YouTube source never becomes executable link");
        WritePlaylistFile(temporary.path / L"playlist.html", html);
        Check(ReadPlaylistFile(temporary.path / L"playlist.html") == html, "HTML file export");
        Check(EntryInformation(live.entries[0]).find(L"Existence: Exists") != std::wstring::npos &&
                  EntryInformation(live.entries[1]).find(L"Download status: Queued") != std::wstring::npos &&
                  EntryInformation(live.entries[2]).find(L"Local media path:") != std::wstring::npos,
              "FILE INF uses available local/queued/ready information");

        const auto deletePath = temporary.path / L"delete-me.mp3";
        Write(deletePath, "disposable fixture");
        PlaylistModel deletion(root);
        deletion.Add("Disposable", {}, deletePath.wstring());
        deletion.Add("Duplicate file", {}, deletePath.wstring());
        deletion.Add("Missing file", {}, missing.wstring());
        deletion.Add("Directory must remain", {}, video.wstring());
        deletion.Add("Retain after failure", {}, existing.wstring());
        Check(deletion.QueueYouTubeUrl(L"https://youtu.be/AbCdEf012_-"), "Deletion YouTube fixture");
        deletion.SelectAll(true);
        int attempts = 0;
        const auto deleteTemporary = [&](const std::filesystem::path& path) {
            Check(path.is_absolute() && path.parent_path() == temporary.path, "Deletion confined to temporary root");
            ++attempts;
            if (path == existing)
                return false;
            return std::filesystem::remove(path);
        };
        Check(DeleteSelectedFiles(deletion, false, deleteTemporary).removed == 0 && attempts == 0 &&
                  std::filesystem::exists(deletePath),
              "No filesystem deletion without explicit confirmation");
        const auto deletionReport = DeleteSelectedFiles(deletion, true, deleteTemporary);
        Check(deletionReport.removed == 2 && deletionReport.failed == 3 && deletionReport.unsupported == 1 &&
                  attempts == 2 && !std::filesystem::exists(deletePath) && std::filesystem::exists(existing),
              "Only successful physical deletions remove entries; shared source deleted once");
        Check(deletion.entries.size() == 4 && std::filesystem::exists(video / L"video.mp4") &&
                  std::filesystem::exists(video / L"metadata.json"),
              "YouTube MediaStore directory and files never deleted");
        deletion.SelectAll(false);
        deletion.entries.back().selected = true;
        const auto youtubeOnly = DeleteSelectedFiles(deletion, true, deleteTemporary);
        Check(youtubeOnly.unsupported == 1 && attempts == 2, "YouTube-only selection makes no deletion callbacks");
        std::cout << "Playlist editor controls passed: removal, liveness, duplicates, selection, stable identity, "
                     "sort, import/export, HTML and temporary-only deletion.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
