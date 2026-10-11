#include "TemporaryMediaRoot.h"
#include "media/playback/PlaybackController.h"
#include "platform/windows/WindowsRuntime.h"
#include <fstream>
#include <iostream>
#include <set>
using namespace yinamp;
using namespace media::playback;
namespace {
void Check(bool value, const char* name) {
    if (!value)
        throw std::runtime_error(name);
}
struct Fake final : Session {
    Snapshot value;
    std::filesystem::path path;
    unsigned opens{}, closes{}, stops{};
    double seek{-1};
    std::set<std::filesystem::path> failures;
    void Open(const std::filesystem::path& p, std::uint64_t generation) override {
        path = p;
        ++opens;
        value = {};
        value.generation = generation;
        value.state = failures.contains(p) ? State::Failed : State::Playing;
        value.duration = 20;
        value.seekable = true;
    }
    void Close() override {
        ++closes;
        value = {};
    }
    void Play() override {
        value.state = State::Playing;
    }
    void Pause() override {
        value.state = State::Paused;
    }
    void Stop() override {
        ++stops;
        value.state = State::Stopped;
        value.position = 0;
    }
    void Seek(double seconds) override {
        seek = seconds;
        value.position = seconds;
    }
    void Audio(int, int) override {}
    Snapshot Read() override {
        return value;
    }
};
void Write(const std::filesystem::path& path) {
    std::ofstream(path, std::ios::binary) << "fixture";
}
void Sources(const std::filesystem::path& root) {
    media::MediaStore store(root / L"store");
    auto work = store.beginWork("AbCdEf012_-");
    Write(work / L"final.mp4.part");
    Write(work / L"audio.m4a.part");
    media::MediaMetadata metadata;
    metadata.videoId = "AbCdEf012_-";
    metadata.title = "Title";
    store.complete(metadata);
    playlist::PlaylistModel model(root / L"store");
    model.QueueYouTubeUrl(L"https://youtu.be/AbCdEf012_-");
    SourceResolver resolver(root / L"store");
    auto entry = model.entries[0];
    const auto source = resolver.Resolve(entry);
    Check(source && *source == work.parent_path() / L"final.mp4", "Ready resolves MediaStore final.mp4");
    Check(source->filename() != L"audio.m4a", "audio.m4a is never default");
    for (auto state : {media::JobState::Queued, media::JobState::Resolving, media::JobState::Downloading,
                       media::JobState::Failed, media::JobState::Cancelled}) {
        entry.downloadStatus = state;
        Check(!resolver.Resolve(entry), "non Ready rejected");
    }
    entry.downloadStatus = media::JobState::Ready;
    std::filesystem::remove(*source);
    Check(!resolver.Resolve(entry), "incomplete cache rejected");
    const auto path = root / L"original.wav";
    Write(path);
    model.Add("Local", {}, path.wstring());
    Check(resolver.Resolve(model.entries.back()) == path, "original local path");
    std::filesystem::remove(path);
    Check(!resolver.Resolve(model.entries.back()), "missing local rejected");
}
void Controller(const std::filesystem::path& root) {
    playlist::PlaylistModel model(root / L"unused");
    for (int i = 0; i < 4; ++i) {
        const auto path = root / (std::to_wstring(i) + L".wav");
        Write(path);
        model.Add(std::to_string(i), {}, path.wstring());
    }
    Fake fake;
    PlaybackController player(model, fake, root / L"unused", 42);
    Check(player.Value().state == State::Closed && !player.Current(), "initial closed");
    model.entries[1].selected = true;
    player.Play();
    player.Poll();
    Check(player.Current() == model.entries[1].id && fake.path == model.entries[1].source, "selected Play");
    fake.value.title = "Source metadata title";
    player.Poll();
    model.SetDisplayTitle(model.entries[1].id, "Visible custom title");
    fake.value.duration = 21;
    player.Poll();
    Check(model.entries[1].title == "Visible custom title" && model.entries[1].durationSeconds == 21,
          "Playback metadata preserves renamed title while updating duration");
    model.SetDisplayTitle(model.entries[1].id, std::nullopt);
    Check(model.entries[1].title == "Source metadata title", "Reset preserves original playback metadata title");
    fake.value.title.clear();
    fake.value.duration = 20;
    player.Poll();
    player.Pause();
    player.Poll();
    Check(player.Value().state == State::Paused, "pause does not report Playing");
    player.Seek(7);
    player.Poll();
    Check(player.Value().state == State::Paused && fake.seek == 7, "paused seek preserves state");
    fake.value.duration = 0;
    player.Poll();
    fake.seek = -1;
    player.Seek(3);
    Check(fake.seek == -1, "unknown duration cannot seek");
    fake.value.duration = 20;
    fake.value.seekable = false;
    player.Poll();
    player.Seek(3);
    Check(fake.seek == -1, "non seekable safely ignored");
    fake.value.seekable = true;
    player.Poll();
    player.Pause();
    player.Poll();
    Check(player.Value().state == State::Playing, "resume");
    fake.value.position = 5;
    player.Poll();
    Check(player.Value().position == 5 && player.Value().duration - player.Value().position == 15, "elapsed remaining");
    player.Previous();
    Check(fake.seek == 0 && player.Current() == model.entries[1].id, "previous >3 restarts");
    player.Poll();
    player.Previous();
    player.Poll();
    Check(player.Current() == model.entries[0].id, "previous <=3 selects previous");
    player.Previous();
    Check(player.Current() == model.entries[0].id, "beginning restarts");
    player.Seek(-20);
    Check(fake.seek == 0, "seek lower clamp");
    player.Seek(999);
    Check(fake.seek == 20, "seek upper clamp");
    player.Stop();
    player.Poll();
    Check(player.Value().position == 0 && player.Value().state == State::Stopped, "stop resets");
    fake.value.state = State::Ended;
    player.Poll();
    Check(player.Current() == model.entries[0].id, "late end cannot undo Stop");
    model.SelectAll(false);
    const auto opens = fake.opens;
    player.Play();
    player.Poll();
    Check(fake.opens == opens && player.Current() == model.entries[0].id, "Play loaded current");
    player.Next();
    player.Poll();
    Check(player.Current() == model.entries[1].id, "next");
    fake.value.state = State::Ended;
    player.Poll();
    player.Poll();
    Check(player.Current() == model.entries[2].id, "natural advance");
    player.PlayItem(model.entries[3].id);
    player.Poll();
    player.Next();
    player.Poll();
    Check(player.Value().state == State::Stopped, "end repeat off stops");
    player.SetModes(true, false);
    player.Next();
    player.Poll();
    Check(player.Current() == model.entries[0].id, "repeat wraps");
    player.Previous();
    player.Poll();
    Check(player.Current() == model.entries[3].id, "previous repeat wraps");
    fake.failures.insert(model.entries[1].source);
    player.PlayItem(model.entries[0].id);
    player.Poll();
    player.Next();
    player.Poll();
    player.Poll();
    Check(player.Current() == model.entries[2].id, "async open failure skipped");
    auto failed = fake.opens;
    for (const auto& row : model.entries)
        fake.failures.insert(row.source);
    player.Next();
    for (int i = 0; i < 12; ++i)
        player.Poll();
    Check(fake.opens - failed <= model.entries.size(), "bounded invalid traversal");
    fake.failures.clear();
    player.PlayItem(model.entries[0].id);
    player.Poll();
    model.entries.erase(model.entries.begin());
    player.Poll();
    Check(!player.Current() && player.Value().state == State::Closed && fake.closes == 1, "current removal closes");
    Fake fallback;
    PlaybackController first(model, fallback, root / L"unused", 42);
    model.entries[0].source = (root / L"missing").wstring();
    model.entries[0].selected = true;
    first.Play();
    first.Poll();
    Check(first.Current() == model.entries[1].id, "first eligible fallback");
    model.SelectAll(false);
    model.Activate(2);
    Fake existing;
    PlaybackController current(model, existing, root / L"unused", 42);
    current.Play();
    current.Poll();
    Check(current.Current() == model.entries[2].id, "existing Playlist current fallback");
}
std::vector<std::uint64_t> Shuffle(const std::filesystem::path& root) {
    playlist::PlaylistModel model(root / L"unused");
    for (int i = 0; i < 5; ++i) {
        const auto path = root / (L"shuffle" + std::to_wstring(i));
        Write(path);
        model.Add("Track", {}, path.wstring());
    }
    Fake fake;
    PlaybackController player(model, fake, root / L"unused", 19);
    player.SetModes(false, true);
    player.Play();
    player.Poll();
    std::vector<std::uint64_t> result{*player.Current()};
    for (int i = 0; i < 4; ++i) {
        player.Next();
        player.Poll();
        result.push_back(*player.Current());
    }
    Check(std::set(result.begin(), result.end()).size() == 5, "shuffle epoch visits each once");
    for (std::size_t i = 0; i < model.entries.size(); ++i)
        Check(model.entries[i].id == i + 1, "visible order unchanged");
    player.Next();
    player.Poll();
    Check(player.Value().state == State::Stopped, "shuffle end repeat off");
    player.Previous();
    player.Poll();
    Check(player.Current() == result[3], "shuffle previous follows history");
    player.SetModes(true, true);
    player.Next();
    player.Poll();
    Check(player.Value().state == State::Playing, "shuffle repeat refills");
    return result;
}
void Policy() {
    VideoPolicy policy;
    Snapshot value;
    policy.Toggle(value);
    Check(!policy.visible && !policy.userHidden, "no source is inactive");
    value.state = State::Playing;
    policy.Toggle(value);
    Check(!policy.visible && !policy.userHidden, "audio-only is inactive");
    value.video = true;
    value.generation = 1;
    value.position = 12;
    policy.Update(value);
    Check(policy.visible, "video auto shows");
    for (auto state : {State::Playing, State::Paused, State::Stopped, State::Ended}) {
        value.state = state;
        policy.Toggle(value);
        Check(!policy.visible && policy.userHidden && value.state == state && value.position == 12 &&
                  value.generation == 1, "hide preserves transport position and source");
        policy.Toggle(value);
        Check(policy.visible && !policy.userHidden && value.state == state && value.position == 12 &&
                  value.generation == 1, "reopen preserves transport position and source");
    }
    policy.Dismiss();
    policy.Update(value);
    Check(!policy.visible && policy.userHidden, "native dismiss unselects video");
    ++value.generation;
    policy.Update(value);
    Check(!policy.visible, "next video preserves dismissal");
    value.video = false;
    policy.Update(value);
    policy.Toggle(value);
    Check(!policy.visible && policy.userHidden, "audio transition keeps preference");
    value.video = true;
    policy.Update(value);
    Check(!policy.visible, "audio to video preserves dismissal");
    policy.Toggle(value);
    Check(policy.visible && !policy.userHidden, "V restores after native dismissal");
    ++value.generation;
    policy.Update(value);
    Check(policy.visible, "next video stays visible after restore");
    value.video = false;
    policy.Update(value);
    Check(!policy.visible, "video to audio hides");
    value.video = true;
    policy.Update(value);
    Check(policy.visible, "audio to video auto shows without dismissal");
    for (auto state : {State::Opening, State::Failed, State::Closed}) {
        value.state = state;
        policy.Update(value);
        policy.Toggle(value);
        Check(!policy.visible && !policy.userHidden, "unavailable source cannot show stale video");
    }
    Check(Gains(0, 0) == std::array<float, 2>{0, 0}, "volume min silent");
    Check(Gains(255, 0) == std::array<float, 2>{1, 1}, "max normal center");
    Check(Gains(255, -127) == std::array<float, 2>{1, 0}, "left");
    Check(Gains(255, 127) == std::array<float, 2>{0, 1}, "right");
}
}
int main() {
    try {
        platform::ComApartment apartment;
        TemporaryMediaRoot temp;
        Sources(temp.path);
        Controller(temp.path);
        Check(Shuffle(temp.path) == Shuffle(temp.path), "seeded deterministic shuffle");
        Policy();
        std::cout << "Playback source, controller, ordering, video policy, clock, seek, audio tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
