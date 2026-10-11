#include "HttpRangeFixture.h"
#include "TemporaryMediaRoot.h"
#include "media/playback/HttpByteStream.h"
#include "media/playback/PlaybackController.h"
#include "media/playback/PlaybackSettings.h"
#include "platform/windows/WindowsRuntime.h"
#include <fstream>
#include <iostream>
#include <mfapi.h>
#include <mferror.h>
using namespace yinamp;
using namespace media::playback;
namespace {
void Check(bool value, const char* name) {
    if (!value)
        throw std::runtime_error(name);
}
struct Fake final : Session {
    Snapshot value;
    std::vector<PlaybackSource> opened;
    void Open(const std::filesystem::path&, std::uint64_t) override {
        throw std::runtime_error("Old Open unexpected");
    }
    void OpenSource(const PlaybackSource& source, std::uint64_t generation) override {
        if (source.Online())
            source.resolve(source.videoId, {});
        opened.push_back(source);
        value = {};
        value.generation = generation;
        value.state = State::Playing;
    }
    void Close() override {
        value = {};
    }
    void Play() override {
        value.state = State::Playing;
    }
    void Pause() override {
        value.state = State::Paused;
    }
    void Stop() override {
        value.state = State::Stopped;
    }
    void Seek(double) override {}
    void Audio(int, int) override {}
    Snapshot Read() override {
        return value;
    }
};
}
int main() {
    try {
        platform::ComApartment apartment;
        TemporaryMediaRoot temp;
        PlaybackSettings settings(temp.path);
        Check(settings.Mode() == PlaybackMode::Download, "default Download");
        Check(settings.SetMode(PlaybackMode::Online), "save Online");
        Check(PlaybackSettings(temp.path).Mode() == PlaybackMode::Online, "Online survives restart");
        std::ofstream(temp.path / L"playback-mode.txt") << "invalid";
        Check(PlaybackSettings(temp.path).Mode() == PlaybackMode::Download, "invalid configuration default");
        playlist::PlaylistModel model(temp.path / L"store");
        model.QueueYouTubeUrl(L"https://youtu.be/AbCdEf012_-");
        model.QueueYouTubeUrl(L"https://youtu.be/ZbCdEf012_-");
        Fake fake;
        std::vector<std::string> resolvedIds;
        PlaybackController controller(model, fake, temp.path / L"store", 1,
                                      [&](const std::string& id, std::stop_token) {
                                          resolvedIds.push_back(id);
                                          return ResolvedStreams{};
                                      });
        controller.Play();
        Check(fake.opened.empty(), "Download keeps pending-media gate");
        controller.SetPlaybackMode(PlaybackMode::Online);
        controller.Play();
        controller.Poll();
        Check(fake.opened.size() == 1 && fake.opened[0].Online() && fake.opened[0].localPath.empty(),
              "Online bypasses download/cache gate");
        Check(fake.opened[0].videoId == model.entries[0].youtubeVideoId, "shared resolver receives video identity");
        fake.value.video = true;
        fake.value.position = 2.5;
        controller.Poll();
        VideoPolicy videoPolicy;
        videoPolicy.Update(controller.Value());
        for (int i = 0; i < 4; ++i) {
            videoPolicy.Toggle(controller.Value());
            Check(controller.Value().position == 2.5 && controller.Value().state == State::Playing &&
                      fake.opened.size() == 1 && resolvedIds.size() == 1,
                  "Online visibility never resolves or opens a source or changes playback");
        }
        controller.Next();
        controller.Poll();
        Check(fake.opened.size() == 2 && fake.opened.back().videoId == model.entries[1].youtubeVideoId,
              "next creates fresh Online source");
        Check(resolvedIds == std::vector<std::string>{model.entries[0].youtubeVideoId, model.entries[1].youtubeVideoId},
              "playlist next invokes resolver for the new item");
        controller.SetPlaybackMode(PlaybackMode::Download);
        Check(fake.value.state == State::Playing, "mode change keeps current transport");
        const auto local = temp.path / L"local.wav";
        std::ofstream(local) << "local";
        model.Add("local", {}, local.wstring());
        controller.PlayItem(model.entries.back().id);
        Check(!fake.opened.back().Online() && fake.opened.back().localPath == local, "local routing retained");
        controller.SetPlaybackMode(PlaybackMode::Online);
        controller.PlayItem(model.entries.back().id);
        Check(!fake.opened.back().Online(), "local file in Online mode remains local");
        SourceResolver resolver(temp.path / L"store");
        unsigned resolves{};
        auto source = resolver.ResolveSource(model.entries[0], PlaybackMode::Online, [&](const auto&, auto) {
            ++resolves;
            return ResolvedStreams{};
        });
        Check(source && resolves == 0, "prepare has no networking on UI thread");
        source->resolve(source->videoId, {});
        Check(resolves == 1, "Online invokes resolver lazily");
        media::MediaUpdate metadata;
        metadata.request = {model.entries[0].youtubeVideoId, model.entries[0].source, model.entries[0].id};
        metadata.state = media::JobState::Ready;
        metadata.title = "Resolved metadata";
        metadata.duration = 42;
        const auto downloadState = model.entries[0].downloadStatus;
        model.ApplyMetadataUpdate(metadata);
        Check(model.entries[0].title == metadata.title && model.entries[0].durationSeconds == 42 &&
                  model.entries[0].downloadStatus == downloadState && model.entries[0].localMediaPath.empty(),
              "metadata updates preserve playback download state");
        metadata.request.entryId += 999;
        metadata.title = "Stale metadata";
        model.ApplyMetadataUpdate(metadata);
        Check(model.entries[0].title == "Resolved metadata", "stale metadata update is ignored");
        for (const auto& file : std::filesystem::recursive_directory_iterator(temp.path / L"store"))
            Check(!file.is_regular_file(), "Online creates no media files");
        Check(SUCCEEDED(MFStartup(MF_VERSION)), "MFStartup");
        {
            std::string bytes(1024U * 1024U, 'x');
            bytes[700000] = 'z';
            HttpRangeFixture server({{"/media", bytes}});
            auto stream = CreateHttpByteStream(server.Url(L"/media"), {}, {});
            QWORD length{};
            Check(SUCCEEDED(stream->GetLength(&length)) && length == bytes.size(), "range length");
            BYTE buffer[4]{};
            ULONG read{};
            Check(SUCCEEDED(stream->Read(buffer, 4, &read)) && read == 4 && buffer[0] == 'x', "stream read");
            Check(SUCCEEDED(stream->SetCurrentPosition(700000)), "stream seek");
            Check(SUCCEEDED(stream->Read(buffer, 4, &read)) && buffer[0] == 'z', "seek requests correct range");
            Check(server.largestRange <= 256U * 1024U && server.transferred < bytes.size(),
                  "bounded streaming before complete transfer");
            stream->Close();
            Check(stream->Read(buffer, 4, &read) == MF_E_SHUTDOWN, "closed stream fails");
        }
        MFShutdown();
        std::cout << "Online settings, source routing, next, resolver, HTTP range streaming passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
