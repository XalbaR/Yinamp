#include "TemporaryMediaRoot.h"
#include "playlist/YouTubePlaylistImport.h"
#include "playlist/YouTubePlaylistUrl.h"
#include "playlist/PlaylistSessionStore.h"
#include "media/native/Json.h"
#include "media/native/Cancellation.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>

namespace {
using namespace yinamp;
using namespace playlist;
using namespace media::native;
using namespace std::chrono_literals;
constexpr wchar_t Url[] = L"https://www.youtube.com/playlist?list=PLAbC_-123";
constexpr auto A = "AbCdEf012_-";
constexpr auto B = "BcDeFg123_-";
constexpr auto C = "CdEfGh234_-";
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::string Old(std::string id, std::string title = "Müslüm } [ \\\" Gürses", std::string extra = {}) {
    return "{\"playlistVideoRenderer\":{\"videoId\":\"" + id + "\",\"title\":{\"runs\":[{\"text\":\"" +
        json::Escape(title) + "\"}]},\"lengthText\":{\"simpleText\":\"5:52\"}" + extra + "}}";
}
std::string New(std::string id, std::string type = "LOCKUP_CONTENT_TYPE_VIDEO") {
    return "{\"lockupViewModel\":{\"contentType\":\"" + type + "\",\"contentId\":\"" + id +
        R"(","metadata":{"lockupMetadataViewModel":{"title":{"content":"İbrahim 日本"}}},"contentImage":{"thumbnailViewModel":{"overlays":[{}, {"arbitrary":{"thumbnailBadgeViewModel":{"text":"LIVE"}}},{"arbitrary":{"thumbnailBadgeViewModel":{"text":"1:24"}}}]}}}})";
}
std::string Token(std::string value, int shape = 0) {
    if (shape == 1) return "{\"continuationItemViewModel\":{\"continuationCommand\":{\"innertubeCommand\":{\"continuationCommand\":{\"token\":\"" + value + "\"}}}}}";
    if (shape == 2) return "{\"nextContinuationData\":{\"continuation\":\"" + value + "\"}}";
    return "{\"continuationItemRenderer\":{\"continuationEndpoint\":{\"continuationCommand\":{\"token\":\"" + value + "\"}}}}";
}
std::string Page(std::string rows) { return "{\"playlistVideoListRenderer\":{\"contents\":[" + rows + "]}}"; }
std::string Config() { return R"("INNERTUBE_API_KEY":"fixture-key","INNERTUBE_CLIENT_VERSION":"fixture-version","VISITOR_DATA":"fixture-visitor")"; }
std::string Html(std::string rows, std::string marker = "var ytInitialData = ") { return "<script>" + marker + Page(rows) + ";</script>" + Config(); }
struct Fixture {
    std::vector<HttpResponse> responses;
    std::vector<PlaylistHttpRequest> requests;
    YouTubePlaylistResult Resolve(std::stop_token stop = {}) {
        return YouTubePlaylistResolver([&](const PlaylistHttpRequest& request, std::stop_token) {
            Check(requests.size() < responses.size(), "Unexpected network request");
            requests.push_back(request);
            return responses[requests.size() - 1];
        }).Resolve(Url, stop);
    }
};
void Reject(Fixture& fixture) {
    bool rejected{};
    try { (void)fixture.Resolve(); } catch (const std::runtime_error&) { rejected = true; }
    Check(rejected, "Expected safe resolution failure");
}
void Parsers() {
    Fixture named{{{200, "var ytInitialData={\"metadata\":{\"playlistMetadataRenderer\":{\"title\":\"My music\"}},\"contents\":[" + Old(A) + "]};", {}}}, {}};
    const auto namedResult = named.Resolve();
    Check(namedResult.title == "My music" && namedResult.playlistId == "PLAbC_-123", "Playlist title and source identity");
    Check(ParseYouTubePlaylistUrl(Url)->playlistId == "PLAbC_-123", "Playlist ID preserved");
    Check(ParseYouTubePlaylistUrl(L" HTTP://M.YouTube.com/playlist?list=xY_-&index=2#fragment ")->playlistId == "xY_-", "Playlist identity has no eleven-character rule");
    const auto watch = L"https://youtube.com/watch?v=AbCdEf012_-&list=PLAbC";
    Check(ParseYouTubeVideoUrl(watch)->videoId == A && !ParseYouTubePlaylistUrl(watch), "Watch context is single video");
    for (const auto url : {L"https://youtube.com/playlist", L"https://youtube.com/playlist?list=", L"https://youtu.be/playlist?list=x",
        L"https://youtube.com.evil/playlist?list=x", L"https://user@youtube.com/playlist?list=x", L"https://youtube.com:443/playlist?list=x",
        L"https://youtube.com/playlist?list=x&list=y", L"https://youtube.com/playlist?list=x&v=AbCdEf012_-",
        L"https://youtube.com/playlist?list=x%20y", L"https://youtube.com/playlist?list=x&", L"https://youtube.com/playlist?list=x y",
        L"https://youtube.com/playlist?list=x\nhttps://youtube.com/playlist?list=y", L"https://youtube.com/playlist?list=x/evil"})
        Check(!ParseYouTubePlaylistUrl(url), "Malformed playlist rejected");
    for (const auto marker : {"var ytInitialData = ", "ytInitialData = ", "window[\"ytInitialData\"] = ", "var\n ytInitialData\t=\n"}) {
        Fixture fixture{{{200, Html(Old(A) + "," + New(B) + "," + New(C, "LOCKUP_CONTENT_TYPE_PLAYLIST") +
            "," + Old("") + "," + Old(C, "Private", ",\"isPlayable\":false") +
            "," + Old(C, "[Deleted video]"), marker), {}}}, {}};
        const auto result = fixture.Resolve();
        Check(result.items.size() == 2 && result.items[0].videoId == A && result.items[1].videoId == B &&
            result.items[0].title == "Müslüm } [ \\\" Gürses" && result.items[0].duration == "5:52" &&
            result.items[1].title == "İbrahim 日本" && result.items[1].duration == "1:24", "Both shapes order unicode nested braces and badge duration");
        Check(fixture.requests.size() == 1 && fixture.requests[0].body.empty() && fixture.requests[0].headers.contains(L"User-Agent"), "Public HTML GET only when complete");
    }
}
void Continuations() {
    // The reference PoC requires only the row shape, not playlistVideoListRenderer/richGridRenderer.
    const auto wrapped = [&](std::string rows) {
        return "{\"contents\":{\"sectionListRenderer\":{\"contents\":[{\"itemSectionRenderer\":{\"contents\":[" + rows + "]}}]}}}";
    };
    Fixture unknownWrapper{{{200, "var ytInitialData=" + wrapped(New(A) + "," + Old(B)) + ";", {}}}, {}};
    const auto wrapperResult = unknownWrapper.Resolve();
    Check(wrapperResult.items.size() == 2 && wrapperResult.items[0].videoId == A && wrapperResult.items[1].videoId == B &&
        unknownWrapper.requests.size() == 1, "Known row shapes work without hardcoded outer wrappers or web configuration");
    Fixture newContinuationWrapper{{{200, "var ytInitialData=" + wrapped(New(A) + "," + Token("next", 1)) + ";" + Config(), {}},
        {200, "{\"continuationContents\":{\"newWrapper\":{\"items\":[" + Old(B) + "," + New(C) + "]}}}", {}}}, {}};
    const auto wrapperPages = newContinuationWrapper.Resolve();
    Check(wrapperPages.items.size() == 3 && wrapperPages.items[2].videoId == C && newContinuationWrapper.requests.size() == 2,
        "Continuation row/token discovery independent of outer wrapper");
    Fixture filteredFallback{{{200, "var ytInitialData={\"sidebar\":" + New(C) + ",\"contents\":" + wrapped(New(A)) +
        ",\"tabs\":[{\"tabRenderer\":{\"selected\":false,\"content\":" + wrapped(Old(B)) + "}}]};", {}}}, {}};
    Check(filteredFallback.Resolve().items.size() == 1, "Fallback still excludes sidebar and unselected tabs");
    Fixture scoped{{{200, "var ytInitialData={\"sidebar\":" + New(C) +
        ",\"contents\":{\"richGridRenderer\":{\"contents\":[" + New(A) + "," + Old(B) + "]}}};", {}}}, {}};
    const auto scopedResult = scoped.Resolve();
    Check(scopedResult.items.size() == 2 && scopedResult.items[0].videoId == A && scopedResult.items[1].videoId == B,
        "Current grid source order excludes sidebar recommendations");
    Fixture fixture{{{200, Html(Old(A) + "," + Token("first")), {}},
        {200, Page(New(B) + "," + Old(A) + "," + Token("second", 1)), {}},
        {200, Page(New(C) + "," + Token("third", 2)), {}}, {200, Page(""), {}}}, {}};
    const auto result = fixture.Resolve();
    Check(result.items.size() == 3 && result.items[0].videoId == A && result.items[1].videoId == B &&
        result.items[2].videoId == C && result.sourceDuplicates == 1 && fixture.requests.size() == 4,
        "Initial populated page follows all three continuation forms in order and deduplicates");
    for (std::size_t i = 1; i < fixture.requests.size(); ++i) {
        const auto body = json::Parse(fixture.requests[i].body);
        Check(body.Get("continuation") && body.Get("context") && !body.Get("browseId"), "Continuation WEB request");
        Check(fixture.requests[i].url.starts_with(L"https://www.youtube.com/youtubei/v1/browse?key="), "Web browse endpoint");
    }
    Fixture fallback{{{200, "<html>" + Config() + "</html>", {}}, {200, Page(Old(A) + "," + Token("next", 1)), {}},
        {200, Page(New(B)), {}}}, {}};
    Check(fallback.Resolve().items.size() == 2 && *json::Parse(fallback.requests[1].body).Get("browseId")->AsString() == "VLPLAbC_-123",
        "Browse fallback and continuation");
    Fixture escaped{{{200, R"(\"INNERTUBE_API_KEY\":\"fixture-key\",\"INNERTUBE_CLIENT_VERSION\":\"fixture-version\")", {}},
        {200, Page(Old(A)), {}}}, {}};
    Check(escaped.Resolve().items.size() == 1, "Escaped runtime config");
    Fixture loop{{{200, Html(Old(A) + "," + Token("again")), {}}, {200, Page(New(B) + "," + Token("again")), {}}}, {}};
    Reject(loop); Check(loop.requests.size() == 2, "Repeated tokens fail without looping");
    Fixture duplicateTokens{{{200, Html(Old(A) + "," + Token("next") + "," + Token("next", 2)), {}},
        {200, Page(New(B)), {}}}, {}};
    Check(duplicateTokens.Resolve().items.size() == 2 && duplicateTokens.requests.size() == 2, "Pending token dedupe");
    Fixture failure{{{200, Html(Old(A) + "," + Token("next")), {}}, {503, "secret response", {}}}, {}};
    Reject(failure);
    bool safeDiagnostic{};
    try {
        (void)YouTubePlaylistResolver([](const auto&, auto) -> HttpResponse {
            throw std::runtime_error("sensitive body fixture-key https://youtubei/key");
        }).Resolve(Url);
    } catch (const YouTubePlaylistError& error) { safeDiagnostic = std::string_view(error.what()) == "Playlist page request failed."; }
    Check(safeDiagnostic, "Controlled stage diagnostic never forwards transport secrets");
    for (const auto& html : {std::string("var ytInitialData = {bad}"), std::string("var ytInitialData = {\"x\":{\"bad\":1}"),
        std::string("<html>consent page</html>"), std::string("var ytInitialData={\"x\":1};")}) {
        Fixture bad{{{200, html, {}}}, {}}; Reject(bad);
    }
    for (const auto& response : {HttpResponse{403, "private", {}}, HttpResponse{302, "redirect", {}},
        HttpResponse{200, "\xC0\xAF", {}}, HttpResponse{200, std::string(YouTubePlaylistMaximumResponseBytes + 1, 'x'), {}}}) {
        Fixture bad{{response}, {}}; Reject(bad);
    }
    Fixture badJson{{{200, Config(), {}}, {200, "{bad}", {}}}, {}}; Reject(badJson);
    Fixture unsupported{{{200, Config(), {}}, {200, "{\"unknown\":[]}", {}}}, {}}; Reject(unsupported);
    Fixture emptyContinuation{{{200, Html(Old(A) + "," + Token("empty")), {}}, {200, "{}", {}}}, {}};
    Reject(emptyContinuation);
    Fixture missingConfig{{{200, "var ytInitialData=" + Page(Old(A) + "," + Token("need-config")) + ";", {}}}, {}};
    Reject(missingConfig);
    Fixture empty{{{200, Html(""), {}}, {200, Page(Old("")), {}}}, {}};
    Check(empty.Resolve().items.empty(), "Unavailable-only and empty produce no rows");
    std::stop_source cancelled; cancelled.request_stop();
    Fixture stop{{}, {}};
    bool stopped{}; try { (void)stop.Resolve(cancelled.get_token()); } catch (const Cancelled&) { stopped = true; }
    Check(stopped && stop.requests.empty(), "Pre-request explicit cancellation");
    std::stop_source during;
    bool interrupted{};
    try {
        (void)YouTubePlaylistResolver([&](const auto&, auto) { during.request_stop(); return HttpResponse{200, Html(Old(A)), {}}; }).Resolve(Url, during.get_token());
    } catch (const Cancelled&) { interrupted = true; }
    Check(interrupted, "Cancellation after response discards result");
}
std::string VideoRows(int count) {
    std::string rows;
    for (int i = 0; i < count; ++i) { if (i) rows += ','; rows += Old("Video" + std::to_string(100000 + i)); }
    return rows;
}
void Limits(const std::filesystem::path& root) {
    Fixture exact{{{200, Html(VideoRows(500)), {}}}, {}};
    const auto fiveHundred = exact.Resolve();
    Check(fiveHundred.items.size() == 500 && !fiveHundred.truncated && exact.requests.size() == 1, "Exactly 500 no truncation");
    Fixture over{{{200, Html(VideoRows(501)), {}}}, {}};
    const auto capped = over.Resolve();
    Check(capped.items.size() == 500 && capped.truncated && capped.items.back().videoId == "Video100499", "501st entry never returned");
    Fixture pending{{{200, Html(VideoRows(500) + "," + Token("never-fetch")), {}}}, {}};
    Check(pending.Resolve().truncated && pending.requests.size() == 1, "Stop fetching at cap with continuation notice");
    Fixture paged{{{200, Html(Old(A) + "," + Token("next")), {}}, {200, Page(VideoRows(500)), {}}}, {}};
    Check(paged.Resolve().items.size() == 500 && paged.requests.size() == 2, "Cap during continuation");
    PlaylistModel model(root / L"limit-media");
    Check(AppendYouTubePlaylist(capped, model).added == 500 && model.entries.size() == 500, "No 501st queued item");
    Check(YouTubePlaylistSummaryMessage({500, 500, 0, true}).find(L"Only the first 500") != std::wstring::npos, "Compact truncation result");
}
void Write(const std::filesystem::path& path, std::string_view text) {
    std::ofstream file(path, std::ios::binary); file << text; Check(static_cast<bool>(file), "Temporary file write");
}
YouTubePlaylistResult Three() {
    Fixture fixture{{{200, Html(Old(A) + "," + New(B) + "," + Old(A) + "," + Old(C)), {}}}, {}};
    return fixture.Resolve();
}
std::vector<media::MediaRequest> Requests(const PlaylistModel& model) {
    std::vector<media::MediaRequest> result;
    for (const auto& entry : model.entries) if (entry.downloadStatus == DownloadStatus::Queued)
        result.push_back({entry.youtubeVideoId, entry.source, entry.id});
    return result;
}
void Integration(const std::filesystem::path& root) {
    const auto mediaRoot = root / L"integration-media";
    media::MediaStore store(mediaRoot);
    const auto work = store.beginWork(B);
    Write(work / L"final.mp4.part", "fixture"); Write(work / L"audio.m4a.part", "fixture");
    media::MediaMetadata metadata{}; metadata.videoId = B; metadata.title = "Authoritative cache"; metadata.durationSeconds = 123;
    store.complete(metadata);
    Write(store.beginWork(C) / L"video.mp4.part", "partial");
    PlaylistModel model(mediaRoot);
    PlaylistSessionStore session(root / L"session");
    int saves{};
    model.durableChanged = [&] { ++saves; Check(model.entries.size() == 3, "One ordered publication"); Check(session.Save(model), "Session save"); };
    const auto result = Three();
    const auto summary = AppendYouTubePlaylist(result, model);
    Check(summary.found == 3 && summary.added == 3 && summary.duplicates == 1 && saves == 1, "Single import operation/source duplicates");
    Check(model.entries[0].youtubeVideoId == A && model.entries[1].youtubeVideoId == B && model.entries[2].youtubeVideoId == C,
        "Exact source order");
    for (const auto& entry : model.entries) Check(entry.source == L"https://www.youtube.com/watch?v=" + Utf8ToWide(entry.youtubeVideoId), "Canonical source URL");
    Check(model.entries[0].downloadStatus == DownloadStatus::Queued && model.entries[0].downloadProgress == 0 &&
        model.entries[1].downloadStatus == DownloadStatus::Ready && model.entries[1].downloadProgress == 100 &&
        model.entries[1].title == "Authoritative cache" && model.entries[2].downloadStatus == DownloadStatus::Queued &&
        model.entries[2].downloadProgress == 0 && Requests(model).size() == 2, "Cache reused; provisional data never establishes Ready");
    Check(AppendYouTubePlaylist(result, model).added == 0 && AppendYouTubePlaylist(result, model).duplicates == 4 && saves == 1,
        "Existing identities win / duplicate only result");
    auto invalid = result; invalid.items[2].videoId = "bad";
    PlaylistModel fresh(mediaRoot);
    bool freshRejected{}; try { (void)AppendYouTubePlaylist(invalid, fresh); } catch (const std::runtime_error&) { freshRejected = true; }
    Check(freshRejected && fresh.entries.empty(), "Invalid final prepared item rolls back earlier staged items");
    PlaylistModel existing(mediaRoot);
    existing.QueueYouTubeUrl(L"https://youtu.be/BcDeFg123_-");
    const auto existingSummary = AppendYouTubePlaylist(result, existing);
    Check(existingSummary.added == 2 && existingSummary.duplicates == 2 && existing.entries[0].youtubeVideoId == B &&
        existing.entries[1].youtubeVideoId == A && existing.entries[2].youtubeVideoId == C,
        "Existing duplicate wins; accepted append order remains source order");
    bool rejected{}; try { (void)AppendYouTubePlaylist(invalid, model); } catch (const std::runtime_error&) { rejected = true; }
    Check(rejected && model.entries.size() == 3 && saves == 1, "Prepared invalid result publishes nothing");
    Fixture networkFailure{{{200, Html(Old(A) + "," + Token("failed")), {}}, {500, "private", {}}}, {}};
    try { AppendYouTubePlaylist(networkFailure.Resolve(), model); } catch (const std::runtime_error&) {}
    Check(model.entries.size() == 3 && saves == 1, "Continuation failure is atomic");
    PlaylistModel restored(mediaRoot); session.Restore(restored);
    Check(restored.entries.size() == 3, "Session rows restore");
    for (std::size_t i = 0; i < 3; ++i) Check(restored.entries[i].source == model.entries[i].source &&
        restored.entries[i].youtubeVideoId == model.entries[i].youtubeVideoId && restored.entries[i].downloadStatus == model.entries[i].downloadStatus,
        "Restored identity/order/status rederived");
    const auto sessionPath = root / L"session" / L"session.json";
    std::ifstream file(sessionPath, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(file)), {});
    Check(!bytes.empty() && bytes.find("fixture-key") == bytes.npos && bytes.find("fixture-version") == bytes.npos &&
        bytes.find("fixture-visitor") == bytes.npos && bytes.find("continuation") == bytes.npos && bytes.find("youtubei") == bytes.npos,
        "No runtime web configuration persisted");
    std::filesystem::remove(store.findCompletedVideo(B)->mediaPath);
    session.Restore(restored);
    Check(restored.entries[1].downloadStatus == DownloadStatus::Queued && restored.entries[1].downloadProgress == 0, "Broken cache rederived");
}
void InfoCache(const std::filesystem::path& root) {
    const auto directory = root / L"youtube-info";
    using Cache = media::YouTubeInfoCache;
    const auto now = Cache::Clock::now();
    Cache cache(directory, now);
    int requests{};
    bool changed{}, failContinuation{};
    const auto transport = [&](const PlaylistHttpRequest& request, std::stop_token) {
        ++requests;
        if (failContinuation) {
            if (request.body.empty()) return HttpResponse{200, Html(Old(A) + "," + Token("refresh-failure")), {}};
            return HttpResponse{500, "failed", {}};
        }
        if (changed) return HttpResponse{200, Html(New(B) + "," + Old(C)), {}};
        return HttpResponse{200, Html(Old(A) + "," + New(B)), {}};
    };
    const auto first = YouTubePlaylistResolver(transport, directory).Resolve(Url);
    Check(requests == 1 && first.items.size() == 2, "First playlist fetch populates info cache");
    changed = true;
    const auto second = YouTubePlaylistResolver(transport, directory).Resolve(
        L"http://m.youtube.com/playlist?index=2&list=PLAbC_-123");
    Check(requests == 2 && second.items.size() == 2 && second.items[0].videoId == B && second.items[1].videoId == C,
          "Reimport fetches changed remote membership despite a fresh cache and URL variant");
    const auto refreshed = Cache(directory).Playlist("PLAbC_-123");
    Check(refreshed && refreshed->items.size() == 2 && refreshed->items[0].videoId == B && refreshed->items[1].videoId == C,
          "Successful refresh replaces cached membership");
    failContinuation = true;
    bool failed{};
    try { (void)YouTubePlaylistResolver(transport, directory).Resolve(Url); }
    catch (const YouTubePlaylistError&) { failed = true; }
    const auto afterFailure = Cache(directory).Playlist("PLAbC_-123");
    Check(failed && requests == 4 && afterFailure && afterFailure->items.size() == 2 && afterFailure->items[1].videoId == C,
          "Continuation failure does not return stale membership or overwrite the successful snapshot");
    failContinuation = false;
    Check(Cache(directory).Video(A)->duration == 352, "Playlist seeds video identity cache");
    media::MediaRequest request{A, L"https://youtu.be/AbCdEf012_-", 77};
    int updates{};
    media::RunCachedMetadataJob(directory, request, {}, [&](const media::MediaUpdate& update) {
        ++updates;
        Check(update.request.entryId == 77 && update.title == first.items[0].title &&
              update.duration == 352 && update.state == media::JobState::Ready,
              "Cached metadata delivered to new row without network");
    });
    Check(updates == 1, "Single cache completion");
    PlaylistModel model(root / L"info-import-media");
    AppendYouTubePlaylist(first, model);
    Check(model.entries[0].title == first.items[0].title && model.entries[0].durationSeconds == 352 &&
          model.entries[0].metadataStatus == media::JobState::Ready &&
          model.entries[0].downloadStatus == DownloadStatus::Queued,
          "Import retains playlist metadata independently of download state");
    const auto summary = AppendYouTubePlaylist(second, model);
    Check(summary.found == 2 && summary.added == 1 && summary.duplicates == 1 && model.entries.size() == 3 &&
          model.entries[0].youtubeVideoId == A && model.entries[1].youtubeVideoId == B && model.entries[2].youtubeVideoId == C,
          "Refresh appends only missing videos and preserves existing rows and order");
    Check(AppendYouTubePlaylist(second, model).added == 0 && model.entries.size() == 3,
          "Repeated refreshed import never duplicates existing videos");
    cache.SaveVideo("aBCdef012_-", {"Different case", 84});
    Check(Cache(directory).Video("aBCdef012_-")->title == "Different case" &&
          Cache(directory).Video(A)->title == first.items[0].title, "Case-sensitive video identities remain distinct");
    Check(!Cache(directory, now + 25h).Playlist("PLAbC_-123") &&
          Cache(directory, now + 25h).Video(A) && !Cache(directory, now + 24h * 31).Video(A),
          "Playlist and metadata expiry are independent");
    const auto old = now - 25h;
    Cache(directory, old).SavePlaylist("PLAbC_-123", first);
    (void)YouTubePlaylistResolver(transport, directory).Resolve(Url);
    Check(requests == 5, "Expired playlist is refetched");
    std::stop_source stopped; stopped.request_stop();
    bool cancelled{};
    try { (void)YouTubePlaylistResolver(transport, directory).Resolve(Url, stopped.get_token()); }
    catch (const Cancelled&) { cancelled = true; }
    Check(cancelled && requests == 5, "Cancelled refresh makes no network request even with a cached snapshot");
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
        if (entry.is_regular_file()) { std::ofstream output(entry.path(), std::ios::trunc); output << "corrupt"; }
    }
    Check(!Cache(directory).Video(A), "Corrupt video cache is a miss");
    (void)YouTubePlaylistResolver(transport, directory).Resolve(Url);
    Check(requests == 6 && Cache(directory).Video(B) && Cache(directory).Playlist("PLAbC_-123"),
          "Corrupt cache repaired by successful fetch");
    Check(Cache::Duration("1:02:03") == 3723 && !Cache::Duration("LIVE") &&
          !Cache::Duration("1:99") && !Cache::Duration("999999999999:00"), "Duration parsing is bounded");
    const auto blocked = root / L"blocked-cache";
    { std::ofstream output(blocked); output << "file"; }
    Check(YouTubePlaylistResolver(transport, blocked).Resolve(Url).items.size() == 2 && requests == 7,
          "Unwritable cache does not prevent successful import");
}
void Jobs(const std::filesystem::path& root) {
    PlaylistModel model(root / L"jobs-media"); AppendYouTubePlaylist(Three(), model);
    std::mutex mutex; std::condition_variable changed;
    std::vector<std::string> started; int active{}, maximum{}; bool release{};
    media::MediaJobs jobs(root / L"jobs-media", [&](const auto& request, auto, const auto& emit) {
        { std::unique_lock lock(mutex); ++active; maximum = std::max(maximum, active); started.push_back(request.videoId); changed.notify_all();
          if (started.size() == 1) changed.wait(lock, [&] { return release; }); --active; }
        if (request.videoId == B) throw std::runtime_error("Injected media failure");
        media::MediaUpdate update; update.request = request; update.state = media::JobState::Ready; update.progress = 100; emit(update);
    });
    jobs.Synchronize(Requests(model));
    { std::unique_lock lock(mutex); const bool first = changed.wait_for(lock, 2s, [&] { return started.size() == 1; });
      release = true; changed.notify_all(); Check(first, "First media job");
      Check(changed.wait_for(lock, 2s, [&] { return started.size() == 3; }), "Later media proceeds after failure"); }
    jobs.Shutdown(); const auto updates = jobs.Drain();
    Check(maximum == 1 && active == 0 && started == std::vector<std::string>{A, B, C} && updates.size() == 3 &&
        updates[1].state == DownloadStatus::Failed && updates[2].state == DownloadStatus::Ready, "Playlist media strictly sequential");
    started.clear();
    media::MediaJobs cancellation(root / L"jobs-media", [&](const auto& request, std::stop_token stop, const auto&) {
        std::unique_lock lock(mutex); started.push_back(request.videoId); changed.notify_all();
        std::condition_variable_any wait; wait.wait(lock, stop, [] { return false; }); throw Cancelled{};
    });
    cancellation.Synchronize(Requests(model));
    { std::unique_lock lock(mutex); Check(changed.wait_for(lock, 2s, [&] { return started.size() == 1; }), "Removal first active"); }
    model.entries[0].selected = model.entries[2].selected = true; model.RemoveSelected(); cancellation.Synchronize(Requests(model));
    { std::unique_lock lock(mutex); Check(changed.wait_for(lock, 2s, [&] { return started.size() == 2; }), "Removal cancels active"); }
    cancellation.Shutdown();
    Check(started == std::vector<std::string>{A, B}, "Removed queued item never starts");
    Check(cancellation.Drain().front().state == DownloadStatus::Cancelled, "Active removal cancellation");
}
}
int main() {
    try { TemporaryMediaRoot temporary; Parsers(); Continuations(); Limits(temporary.path); Integration(temporary.path); InfoCache(temporary.path); Jobs(temporary.path);
        std::cout << "Native playlist parsing, continuation, cap, atomic import, cache, persistence and sequential jobs passed offline.\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
