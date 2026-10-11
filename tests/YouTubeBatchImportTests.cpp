#include "TemporaryMediaRoot.h"
#include "playlist/YouTubeUrlList.h"
#include "playlist/PlaylistSessionStore.h"
#include "playlist/PlaylistOperations.h"
#include "skin/PlaylistLayout.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>

namespace {
using namespace yinamp;
using namespace playlist;
using namespace std::chrono_literals;
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void Write(const std::filesystem::path& path, std::string_view bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    Check(static_cast<bool>(file), "Temporary fixture write");
}
constexpr auto A = "https://youtu.be/AbCdEf012_-";
constexpr auto B = "https://music.youtube.com/watch?v=BcDeFg123_-&list=PLXYZ&index=4";
constexpr auto C = "https://m.youtube.com/live/CdEfGh234_-";
std::string Three() { return std::string(A) + "\n" + B + "\n" + C; }
void Parsing(const std::filesystem::path& root) {
    PlaylistModel model(root / L"media");
    const auto file = root / L"URLs 日本.txt";
    const auto prepare = [&](std::string_view bytes) { Write(file, bytes); return PrepareYouTubeUrlList(file, model); };
    for (const auto& bytes : {Three(), std::string("\xEF\xBB\xBF") + Three(),
             std::string("\n\r\n \t\n\t ") + A + " \t\r\n\n" + B + "\r" + C + "\n",
             std::string("\xC2\xA0") + A + "\xE3\x80\x80\n" + B + "\n" + C}) {
        const auto list = prepare(bytes);
        Check(list.accepted.size() == 3 && list.accepted[0].videoId == "AbCdEf012_-" &&
            list.accepted[1].videoId == "BcDeFg123_-" && list.accepted[2].videoId == "CdEfGh234_-" &&
            list.duplicates == 0 && list.invalid == 0, "UTF8 BOM whitespace CR LF order and watch context");
        Check(model.entries.empty(), "Preparation is mutation free");
    }
    auto list = prepare(std::string(A) + "\nhttps://youtube.com/watch?v=AbCdEf012_-\ninvalid\n" + B +
        "\nhttps://youtube.com/playlist?list=PLXYZ\nhttps://example.com/video\nhttps://youtu.be/abCdEf012_-");
    Check(list.accepted.size() == 3 && list.duplicates == 1 && list.invalid == 3 &&
        list.accepted[0].url == FromUtf8(A) && list.accepted[1].videoId == "BcDeFg123_-" &&
        list.accepted[2].videoId == "abCdEf012_-", "First occurrence wins; case preserved; invalid rows skipped");
    const auto summary = AppendYouTubeUrlList(list, model);
    Check(summary.added == 3 && summary.duplicates == 1 && summary.invalid == 3, "Mixed summary");
    list = prepare(std::string(A) + "\n" + B);
    Check(list.accepted.empty() && list.duplicates == 2 && list.invalid == 0, "Existing playlist duplicates");
    const auto unchanged = model.entries[0];
    Check(AppendYouTubeUrlList(list, model).added == 0 && model.entries[0].id == unchanged.id &&
        model.entries[0].source == unchanged.source, "Duplicate only import leaves entry intact");
    Check(prepare("invalid\nhttps://youtube.com/playlist?list=PL").invalid == 2, "All invalid summary");
    // Hazırlık ile ekleme arasındaki eklemeler de model sınırında tekrar denetlenir.
    model.Clear();
    list = prepare(A);
    Check(model.QueueYouTubeUrl(FromUtf8(A)), "Intervening individual submission");
    Check(AppendYouTubeUrlList(list, model).duplicates == 1 && model.entries.size() == 1, "Final duplicate safety");
    const auto rejects = [&](std::string_view bytes, std::string_view expected) {
        Write(file, bytes);
        bool rejected{};
        try { (void)PrepareYouTubeUrlList(file, model); }
        catch (const std::runtime_error& error) { rejected = std::string_view(error.what()).find(expected) != std::string_view::npos; }
        Check(rejected && model.entries.size() == 1 && model.entries[0].youtubeVideoId == "AbCdEf012_-",
            "Whole file rejection is atomic");
    };
    rejects("", "empty");
    rejects(" \t\r\n\n\v\f", "empty");
    rejects("\xEF\xBB\xBF", "empty");
    for (const auto bytes : {"\xC0\xAF", "\xED\xA0\x80", "\xF4\x90\x80\x80", "\xE2\x82"})
        rejects(std::string(A) + "\n" + bytes, "UTF-8");
    rejects(std::string(YouTubeUrlListMaximumBytes + 1, 'x'), "1 MiB");
    std::string hundred;
    for (int i = 0; i < 100; ++i)
        hundred += "\n \t\nhttps://youtu.be/Video" + std::to_string(100000 + i) + "\n";
    list = prepare(hundred);
    Check(list.accepted.size() == 100, "Exactly 100 nonempty lines accepted; blanks do not count");
    rejects(hundred + A, "Maximum 100");
    rejects(hundred + "invalid", "Maximum 100");
    bool missing{};
    try { (void)PrepareYouTubeUrlList(root / L"missing.txt", model); }
    catch (const std::runtime_error&) { missing = true; }
    Check(missing, "Missing file handled");
    Write(file, A);
    const auto locked = CreateFileW(file.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    Check(locked != INVALID_HANDLE_VALUE, "Lock fixture");
    bool unreadable{};
    try { (void)PrepareYouTubeUrlList(file, model); }
    catch (const std::runtime_error&) { unreadable = true; }
    CloseHandle(locked);
    Check(unreadable, "Unreadable file handled");
    model.Clear();
    list = prepare(hundred);
    Check(AppendYouTubeUrlList(list, model).added == 100 && model.entries.size() == 100, "100 inserted without workers");
    Check(!ParseYouTubeVideoUrl(FromUtf8(std::string(A) + "\n" + B)), "Single URL remains single video only");
    Check(skin::PlaylistMenuCount(skin::PlaylistControl::Add) == 4 &&
        skin::PlaylistMenuItem({275, 116}, skin::PlaylistControl::Add, 1).y == 50 &&
        skin::PlaylistMenuSource(skin::PlaylistControl::Add, 1, false).y == 111 &&
        skin::PlaylistMenuSource(skin::PlaylistControl::Add, 3, false).y == 149,
        "Fourth Classic tile preserves existing URL and FILE geometry");
}
std::vector<media::MediaRequest> Requests(const PlaylistModel& model);
void CacheAndPersistence(const std::filesystem::path& root) {
    const auto mediaRoot = root / L"store";
    media::MediaStore store(mediaRoot);
    const auto work = store.beginWork("BcDeFg123_-");
    Write(work / L"final.mp4.part", "fixture");
    Write(work / L"audio.m4a.part", "fixture");
    media::MediaMetadata metadata{};
    metadata.videoId = "BcDeFg123_-";
    metadata.title = "Cache title";
    metadata.durationSeconds = 123;
    store.complete(metadata);
    const auto partial = store.beginWork("CdEfGh234_-");
    Write(partial / L"video.mp4.part", "partial");
    const auto file = root / L"cache.txt";
    Write(file, Three() + "\nhttps://youtube.com/watch?v=AbCdEf012_-");
    PlaylistModel model(mediaRoot), individual(mediaRoot);
    PlaylistSessionStore session(root / L"session");
    int saves{};
    model.durableChanged = [&] { ++saves; Check(session.Save(model), "Existing durable session save"); };
    Check(AppendYouTubeUrlList(PrepareYouTubeUrlList(file, model), model).added == 3, "Cache batch insertion");
    for (const auto url : {A, B, C}) individual.QueueYouTubeUrl(FromUtf8(url));
    for (std::size_t i = 0; i < 3; ++i) {
        const auto& entry = model.entries[i];
        Check(entry.id != 0 && entry.sourceType == SourceType::YouTubeVideo &&
            entry.source == individual.entries[i].source && entry.youtubeVideoId == individual.entries[i].youtubeVideoId &&
            entry.downloadStatus == individual.entries[i].downloadStatus &&
            entry.downloadProgress == individual.entries[i].downloadProgress, "Same submission identity and eligibility as individual rows");
    }
    Check(model.entries[0].downloadStatus == DownloadStatus::Queued && model.entries[0].downloadProgress == 0 &&
        model.entries[1].downloadStatus == DownloadStatus::Ready && model.entries[1].downloadProgress == 100 &&
        model.entries[1].localMediaPath == store.findCompletedVideo(metadata.videoId)->mediaPath &&
        model.entries[2].downloadStatus == DownloadStatus::Queued, "Ordered uncached cached partial states");
    const auto requests = Requests(model);
    Check(requests.size() == 2 && requests[0].videoId == "AbCdEf012_-" && requests[1].videoId == "CdEfGh234_-",
        "Valid completed cache excluded from job requests; partial cache eligible");
    model.ApplyMediaUpdate({{model.entries[0].youtubeVideoId, model.entries[0].source, model.entries[0].id},
        DownloadStatus::Downloading, 20, "Türkçe title", 47, {}, {}});
    Check(saves == 4, "Learned metadata uses durable callback");
    PlaylistModel restored(mediaRoot);
    session.Restore(restored);
    Check(restored.entries.size() == 3 && restored.entries[0].title == "Türkçe title" &&
        restored.entries[0].durationSeconds == 47, "Metadata and duplicates persist");
    for (std::size_t i = 0; i < 3; ++i)
        Check(restored.entries[i].youtubeVideoId == model.entries[i].youtubeVideoId &&
            restored.entries[i].source == model.entries[i].source &&
            restored.entries[i].downloadStatus == individual.entries[i].downloadStatus,
            "Session order identity and completion rederived");
    Check(restored.entries[0].downloadProgress == 0, "Transient progress is not persisted");
    std::filesystem::remove(store.findCompletedVideo(metadata.videoId)->mediaPath);
    session.Restore(restored);
    Check(restored.entries[1].downloadStatus == DownloadStatus::Queued, "Missing completed cache never restores false Ready");
}
std::vector<media::MediaRequest> Requests(const PlaylistModel& model) {
    std::vector<media::MediaRequest> result;
    for (const auto& entry : model.entries)
        if (entry.downloadStatus == DownloadStatus::Queued)
            result.push_back({entry.youtubeVideoId, entry.source, entry.id});
    return result;
}
void Jobs(const std::filesystem::path& root) {
    const auto file = root / L"jobs.txt";
    Write(file, Three());
    PlaylistModel model(root / L"jobs-media");
    AppendYouTubeUrlList(PrepareYouTubeUrlList(file, model), model);
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<std::string> started;
    int active{}, maximum{};
    bool release{};
    media::MediaJobs jobs(root / L"jobs-media", [&](const auto& request, auto, const auto& emit) {
        {
            std::unique_lock lock(mutex);
            ++active; maximum = std::max(maximum, active);
            started.push_back(request.videoId);
            changed.notify_all();
            if (started.size() == 1) changed.wait(lock, [&] { return release; });
            --active;
        }
        if (request.videoId == "BcDeFg123_-") throw std::runtime_error("Injected failure");
        media::MediaUpdate update;
        update.request = request; update.state = media::JobState::Ready; update.progress = 100;
        emit(update);
    });
    jobs.Synchronize(Requests(model));
    {
        std::unique_lock lock(mutex);
        const bool first = changed.wait_for(lock, 2s, [&] { return started.size() == 1; });
        release = true; changed.notify_all();
        Check(first, "First batch job activation");
        Check(changed.wait_for(lock, 2s, [&] { return started.size() == 3; }), "Failed middle job continues to last");
    }
    jobs.Shutdown();
    Check(maximum == 1 && active == 0 && started == std::vector<std::string>{"AbCdEf012_-", "BcDeFg123_-", "CdEfGh234_-"},
        "Production worker is sequential in TXT order");
    const auto updates = jobs.Drain();
    Check(updates.size() == 3 && updates[1].state == DownloadStatus::Failed && updates[2].state == DownloadStatus::Ready,
        "Failure terminal state and later success");
    started.clear();
    media::MediaJobs cancellation(root / L"jobs-media", [&](const auto& request, std::stop_token stop, const auto&) {
        std::unique_lock lock(mutex);
        started.push_back(request.videoId); changed.notify_all();
        std::condition_variable_any wait;
        wait.wait(lock, stop, [] { return false; });
        throw std::runtime_error("Cancelled fixture");
    });
    cancellation.Synchronize(Requests(model));
    {
        std::unique_lock lock(mutex);
        Check(changed.wait_for(lock, 2s, [&] { return started.size() == 1; }), "Cancellation first active");
    }
    model.entries[0].selected = model.entries[2].selected = true;
    model.RemoveSelected();
    cancellation.Synchronize(Requests(model));
    {
        std::unique_lock lock(mutex);
        Check(changed.wait_for(lock, 2s, [&] { return started.size() == 2; }), "Removing active starts remaining row");
    }
    cancellation.Shutdown();
    Check(started == std::vector<std::string>{"AbCdEf012_-", "BcDeFg123_-"}, "Removed queued row never starts");
    const auto cancelled = cancellation.Drain();
    Check(cancelled.size() == 2 && cancelled[0].state == DownloadStatus::Cancelled, "Existing active cancellation path");
}
}
int main() {
    try {
        TemporaryMediaRoot temporary;
        Parsing(temporary.path);
        CacheAndPersistence(temporary.path);
        Jobs(temporary.path);
        std::cout << "UTF8 batch parsing, identity, cache, session, sequential failure and cancellation passed offline.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
