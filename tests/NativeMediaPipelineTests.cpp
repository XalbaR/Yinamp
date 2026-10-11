#include "NativeMp4Fixture.h"
#include "TemporaryMediaRoot.h"
#include "media/MediaJobs.h"
#include "media/native/Cancellation.h"
#include "media/native/Fmp4Muxer.h"
#include "media/native/Json.h"
#include "media/native/MediaSelection.h"
#include "platform/windows/WindowsRuntime.h"
#include "playlist/PlaylistModel.h"
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>

namespace {
using namespace yinamp;
using namespace media;
using namespace media::native;
using namespace std::chrono_literals;
void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class F> void Fails(F&& function, const char* message) {
    bool failed{};
    try {
        function();
    } catch (const std::exception&) {
        failed = true;
    }
    Check(failed, message);
}
void Write(const std::filesystem::path& path, const fixture::Bytes& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out)
        throw std::runtime_error("Fixture write failed");
}
fixture::Bytes Read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return fixture::Bytes(std::istreambuf_iterator<char>(in), {});
}
void Selection() {
    ExtractionResult result;
    result.clients.resize(2);
    auto stream = [](int itag, int height, int fps, std::string mime, bool audio = false) {
        StreamInfo s;
        s.itag = itag;
        s.height = height;
        s.fps = fps;
        s.mimeType = std::move(mime);
        s.url = "https://transport.invalid/ephemeral";
        s.bitrate = itag * 100;
        if (audio)
            s.audioQuality = "AUDIO_QUALITY_MEDIUM";
        return s;
    };
    result.clients[0].streams = {
        stream(1, 1080, 60, "video/mp4; codecs=avc1"),  stream(2, 720, 60, "video/mp4; codecs=avc1"),
        stream(3, 720, 30, "video/mp4; codecs=avc1"),   stream(4, 2160, 60, "video/mp4; codecs=avc1"),
        stream(5, 1080, 120, "video/mp4; codecs=avc1"), stream(6, 1080, 60, "video/webm; codecs=vp9"),
        stream(7, 1080, 60, "video/mp4; codecs=avc1")};
    result.clients[0].streams.back().hdr = true;
    result.clients[1].streams = {stream(8, 0, 0, "audio/mp4; codecs=mp4a"),
                                 stream(9, 360, 30, "video/mp4; codecs=avc1,mp4a", true),
                                 stream(10, 720, 30, "video/mp4; codecs=avc1,mp4a", true)};
    std::vector<int> probed;
    auto selected = SelectMedia(result, [&](Candidate c) {
        probed.push_back(c.stream->itag);
        return c.stream->itag == 1 ? 403UL : 206UL;
    });
    Check(selected.adaptive && selected.video.stream->itag == 2 && selected.audio.stream->itag == 8,
          "Lower reachable adaptive resolution and cross-client AAC");
    Check(probed == std::vector<int>{1, 2, 8}, "Only supported candidates in best-first order");
    selected = SelectMedia(result, [](Candidate c) { return c.stream->IsMuxed() ? 200UL : 403UL; });
    Check(!selected.adaptive && selected.video.stream->itag == 10, "Best reachable progressive fallback");
    Fails([&] { SelectMedia(result, [](Candidate) { return 403UL; }); }, "Unavailable sources fail");
    result.clients[0].streams[0].url.clear();
    selected = SelectMedia(result, [](Candidate) { return 200UL; });
    Check(selected.video.stream->itag == 2, "Cipher-only source not selected");
}
void Containers(const std::filesystem::path& root) {
    const auto directory = root / L"Türkçe Arıca";
    std::filesystem::create_directory(directory);
    const auto video = directory / L"video.part";
    const auto audio = directory / L"audio.part";
    const auto combined = directory / L"final.part";
    const auto extracted = directory / L"audio.m4a.part";
    Write(video, fixture::Fragmented(true));
    Write(audio, fixture::Fragmented(false, true));
    const auto mux = MuxFmp4ToFlatMp4(video, audio, combined);
    ValidateAvcAacMp4(combined);
    Check(mux.videoSamples == 2 && mux.audioSamples == 2 && mux.finalBytes > 16, "Fragmented sample mux");
    Check(ExtractAacTrackToM4a(combined, extracted).samples == 2, "Progressive flat AAC extraction");
    const auto flatAudio = Read(extracted);
    Fails([&] { ValidateAvcAacMp4(extracted); }, "Audio-only source is not a completed normal video");
    const auto expected = fixture::Bytes{21, 22, 23, 24, 25, 26, 27, 28};
    Check(std::search(flatAudio.begin(), flatAudio.end(), expected.begin(), expected.end()) != flatAudio.end(),
          "AAC compressed samples preserved exactly");
    ExtractAacTrackToM4a(audio, directory / L"normalized.m4a");
    Check(Read(directory / L"normalized.m4a") == flatAudio, "Adaptive and progressive AAC normalize identically");
    ExtractAacTrackToM4a(extracted, directory / L"roundtrip.m4a");
    Check(Read(directory / L"roundtrip.m4a") == flatAudio, "Flat tables roundtrip");
    ExtractH264TrackToMp4(combined, directory / L"silent.mp4");
    ExtractH264TrackToMp4(directory / L"silent.mp4", directory / L"silent-again.mp4");
    Check(Read(directory / L"silent.mp4") == Read(directory / L"silent-again.mp4"), "H264 ctts/stss tables roundtrip");
    auto broken = fixture::Fragmented(false);
    broken.resize(broken.size() - 3);
    Write(audio, broken);
    Fails([&] { ExtractAacTrackToM4a(audio, extracted); }, "Truncated MP4 rejected");
    Write(audio, fixture::Box("moov", {}));
    Fails([&] { ExtractAacTrackToM4a(audio, extracted); }, "Missing track rejected");
    std::stop_source cancel;
    cancel.request_stop();
    Fails([&] { MuxFmp4ToFlatMp4(video, audio, combined, cancel.get_token()); }, "Mux cancellation before IO");
    HttpClient http(cancel.get_token());
    Fails([&] { http.Get(L"https://transport.invalid"); }, "HTTP cancellation before network");
}
void Store(const std::filesystem::path& root) {
    const std::string id = "AbCdEf012_-";
    MediaStore store(root / L"store");
    Check(!store.findCompletedVideo(id), "Missing store entry");
    Check(!std::filesystem::exists(store.pathForVideo(id)), "Lookup never creates video directory");
    const auto work = store.beginWork(id);
    MediaMetadata metadata;
    metadata.videoId = id;
    metadata.title = "Soner Arıca - Deniz Gözlüm (Official Video)";
    metadata.originalUrl = "https://youtu.be/" + id;
    metadata.durationSeconds = 42;
    metadata.strategy = "adaptive_mux";
    for (auto state : {"resolving", "downloading", "finalizing", "failed", "cancelled"}) {
        metadata.downloadState = state;
        store.writeMetadata(metadata);
        Check(!store.findCompletedVideo(id), "Incomplete state never Ready");
    }
    Write(work / L"final.mp4.part", {1, 2, 3});
    Fails([&] { store.complete(metadata); }, "One staged file cannot complete");
    Check(!std::filesystem::exists(work.parent_path() / L"final.mp4"), "Validation before publish");
    Write(work / L"audio.m4a.part", {4, 5, 6});
    metadata.downloadState = "completed";
    store.writeMetadata(metadata);
    Check(!store.findCompletedVideo(id), "Work-only files never satisfy completed metadata");
    metadata.downloadState = "cancelled";
    store.writeMetadata(metadata);
    std::stop_source cancel;
    cancel.request_stop();
    Fails([&] { store.complete(metadata, cancel.get_token()); }, "Cancelled transaction fails");
    Check(!store.findCompletedVideo(id), "Cancelled transaction not Ready");
    // İkinci rename başarısızsa metadata finalizing kalır.
    std::filesystem::create_directory(work.parent_path() / L"audio.m4a");
    Fails([&] { store.complete(metadata); }, "Second publish failure");
    Check(!store.findCompletedVideo(id) && std::filesystem::exists(work.parent_path() / L"final.mp4"),
          "Partially published pair cannot be Ready");
    std::filesystem::remove(work.parent_path() / L"audio.m4a");
    Write(work / L"final.mp4.part", {1, 2, 3});
    store.complete(metadata);
    const auto completed = store.findCompletedVideo(id);
    Check(completed && completed->title == metadata.title && completed->durationSeconds == 42 &&
              !completed->audioPath.empty() && !std::filesystem::exists(work),
          "Completed contract and UTF8 title");
    const auto bytes = Read(work.parent_path() / L"metadata.json");
    const std::string jsonText(bytes.begin(), bytes.end());
    Check(jsonText.find("completed") != std::string::npos && jsonText.find("ephemeral") == std::string::npos,
          "Stable metadata only");
    Check(*json::Parse(jsonText).Get("title")->AsString() == metadata.title, "UTF8 JSON preservation");
    Check(WideToUtf8(Utf8ToWide(metadata.title)) == metadata.title, "UTF16 boundary roundtrip");
    Check(*json::Parse("{\"title\":\"Ar\\u0131ca G\\u00f6zl\\u00fcm\"}").Get("title")->AsString() == "Arıca Gözlüm",
          "Escaped Unicode metadata");
    playlist::PlaylistModel model(root / L"store");
    Check(model.QueueYouTubeUrl(Utf8ToWide(metadata.originalUrl)) && model.entries[0].title == metadata.title &&
              model.entries[0].downloadStatus == JobState::Ready,
          "Re-add reuses metadata and media");
    std::filesystem::remove(completed->audioPath);
    Check(!store.findCompletedVideo(id), "Final video alone not Ready");
    Write(completed->audioPath, {});
    Check(!store.findCompletedVideo(id), "Empty M10 audio not Ready");
}
void Jobs(const std::filesystem::path& root) {
    std::mutex mutex;
    std::condition_variable_any changed;
    int started{}, running{}, maxRunning{};
    MediaJobs jobs(root, [&](const MediaRequest& request, std::stop_token stop, const UpdateSink& emit) {
        {
            std::lock_guard lock(mutex);
            ++started;
            ++running;
            maxRunning = std::max(maxRunning, running);
        }
        changed.notify_all();
        MediaUpdate update;
        update.request = request;
        update.state = JobState::Resolving;
        update.title = "Türkçe başlık";
        emit(update);
        {
            std::unique_lock lock(mutex);
            changed.wait(lock, stop, [] { return false; });
            --running;
        }
        CheckCancelled(stop);
    });
    const MediaRequest first{"AbCdEf012_-", L"https://youtu.be/AbCdEf012_-", 1};
    const MediaRequest second{"BcDeFg123_-", L"https://youtu.be/BcDeFg123_-", 2};
    const MediaRequest removedQueued{"CdEfGh234_-", L"https://youtu.be/CdEfGh234_-", 3};
    jobs.Synchronize({first, first, second, removedQueued});
    {
        std::unique_lock lock(mutex);
        Check(changed.wait_for(lock, 2s, [&] { return started == 1; }), "First job starts");
    }
    jobs.Synchronize({second});
    {
        std::unique_lock lock(mutex);
        Check(changed.wait_for(lock, 2s, [&] { return started == 2; }), "Next job after cancellation");
    }
    jobs.Shutdown();
    Check(maxRunning == 1 && running == 0 && started == 2, "One worker, deduplicated active jobs, clean shutdown");
    const auto updates = jobs.Drain();
    Check(updates.size() == 2 &&
              std::ranges::all_of(updates, [](const auto& u) { return u.state == JobState::Cancelled; }),
          "Cancellation terminal states");
    playlist::PlaylistModel model(root / L"model");
    model.QueueYouTubeUrl(first.url);
    MediaUpdate update;
    update.request = first;
    update.title = "Türkçe başlık";
    update.duration = 12;
    for (auto state :
         {JobState::Resolving, JobState::Downloading, JobState::Finalizing, JobState::Failed, JobState::Cancelled}) {
        update.state = state;
        model.ApplyMediaUpdate(update);
        Check(model.entries[0].downloadStatus == state && model.entries[0].localMediaPath.empty(),
              "UI state transition");
    }
    Check(model.entries[0].title == update.title && model.entries[0].durationSeconds == 12,
          "Metadata reflected in model");
    model.entries.clear();
    model.QueueYouTubeUrl(first.url);
    update.state = JobState::Ready;
    update.mediaPath = root / L"old.mp4";
    model.ApplyMediaUpdate(update);
    Check(model.entries[0].downloadStatus == JobState::Queued, "Stale update ignored after remove/re-add");
    Check(!std::filesystem::exists(root / L"model" / first.videoId), "Queue-only creates no video directory");
}
}
int main() {
    try {
        yinamp::platform::ComApartment apartment;
        TemporaryMediaRoot temporary;
        Selection();
        Containers(temporary.path);
        Store(temporary.path);
        Jobs(temporary.path);
        std::cout << "Native selection, MP4, store, UTF8, state and cancellation tests passed (offline).\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
