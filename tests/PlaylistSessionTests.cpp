#include "WindowFixture.h"
#include "TemporaryMediaRoot.h"
#include "playlist/PlaylistSessionStore.h"
#include "playlist/PlaylistFile.h"
#include "playlist/PlaylistOperations.h"
#include "platform/windows/WindowsRuntime.h"
#include "ui/PlaylistInteraction.h"
#include "ui/MainWindow.h"
#include "app/BuildPaths.h"
#include "skin/SkinLoader.h"
#include <fstream>
#include <iostream>

namespace {
using namespace yinamp;
using namespace playlist;
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void Write(const std::filesystem::path& path, std::string_view bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << bytes;
    Check(static_cast<bool>(out), "Fixture write");
}
}
int main() {
    try {
        yinamp::test::WindowFixture windowFixture;
        platform::ComApartment apartment;
        TemporaryMediaRoot temporary;
        const auto mediaRoot = temporary.path / L"media";
        const auto sessionRoot = temporary.path / L"session";
        PlaylistSessionStore session(sessionRoot);
        PlaylistModel model(mediaRoot);
        session.Restore(model);
        Check(model.entries.empty() && !std::filesystem::exists(sessionRoot), "A missing session starts empty without writes");
        int notifications{};
        model.durableChanged = [&] { ++notifications; Check(session.Save(model), "Mutation save"); };
        Check(model.QueueYouTubeUrl(L"https://youtu.be/dQw4w9WgXcQ?t=15"), "Add YouTube");
        const auto id = model.entries[0].id;
        model.ApplyMediaUpdate({{"dQw4w9WgXcQ", model.entries[0].source, id}, DownloadStatus::Downloading,
                                12, "Ferdi Tayfur - Koparma Gülleri", 245, {}, {}});
        const auto metadataNotifications = notifications;
        for (int progress = 13; progress < 100; ++progress)
            model.ApplyMediaUpdate({{"dQw4w9WgXcQ", model.entries[0].source, id}, DownloadStatus::Downloading,
                                    progress, "Ferdi Tayfur - Koparma Gülleri", 245, {}, {}});
        Check(notifications == metadataNotifications, "Progress alone never notifies persistence");
        PlaylistModel restored(mediaRoot);
        PlaylistSessionStore reopened(sessionRoot);
        reopened.Restore(restored);
        Check(restored.entries.size() == 1 && restored.entries[0].youtubeVideoId == "dQw4w9WgXcQ" &&
              restored.entries[0].source == model.entries[0].source && restored.entries[0].title == model.entries[0].title &&
              restored.entries[0].durationSeconds == 245, "B durable YouTube metadata round trip");
        Check(restored.entries[0].downloadStatus == DownloadStatus::Queued && restored.entries[0].downloadProgress == 0 &&
              restored.entries[0].localMediaPath.empty(), "H interrupted transport returns queued");
        Check(restored.SubmitYouTubeUrl(L"https://www.youtube.com/watch?v=dQw4w9WgXcQ") ==
              YouTubeSubmission::AlreadyInPlaylist, "J duplicate URL after restore");
        const auto serialized = ReadPlaylistFile(session.FilePath());
        Check(serialized.find("downloadStatus") == serialized.npos && serialized.find("Progress") == serialized.npos &&
              serialized.find("localMediaPath") == serialized.npos && serialized.find("Ferdi Tayfur - Koparma Gülleri") != serialized.npos,
              "Only durable UTF-8 fields serialized");
        auto stale = serialized;
        stale.insert(stale.find("\"sourceType\""), "\"downloadStatus\":\"Ready\",\"downloadProgress\":100,\"localMediaPath\":\"bogus.mp4\",");
        Write(session.FilePath(), stale);
        media::MediaStore mediaStore(mediaRoot);
        const auto work = mediaStore.beginWork("dQw4w9WgXcQ");
        Write(work / L"final.mp4.part", "incomplete");
        reopened.Restore(restored);
        Check(restored.entries[0].downloadStatus == DownloadStatus::Queued, "I stale Ready and partial media rejected");
        Write(work / L"audio.m4a.part", "fixture");
        media::MediaMetadata metadata{};
        metadata.videoId = "dQw4w9WgXcQ";
        metadata.title = "Cache title";
        mediaStore.complete(metadata);
        reopened.Restore(restored);
        Check(restored.entries[0].downloadStatus == DownloadStatus::Ready && restored.entries[0].downloadProgress == 100 &&
              restored.entries[0].localMediaPath == mediaStore.findCompletedVideo(metadata.videoId)->mediaPath,
              "G completed M10 cache Ready with no download request");
        Check(restored.entries[0].title == model.entries[0].title, "Session visible metadata preserved");
        model.Clear();
        const auto missing = temporary.path / L"missing 宇多田ヒカル.mp3";
        for (const auto* title : {"Ferdi Tayfur - Koparma Gülleri", "Björk - Jóga", "Виктор Цой", "宇多田ヒカル", "아이유", "周杰倫"})
            model.Add(title, 42, missing.wstring());
        reopened.Restore(restored);
        Check(restored.entries.size() == model.entries.size(), "F Unicode rows restored");
        for (std::size_t i = 0; i < model.entries.size(); ++i)
            Check(restored.entries[i].title == model.entries[i].title && restored.entries[i].source == missing.wstring() &&
                  restored.entries[i].durationSeconds == 42, "C F O exact title path duration and ordering");
        model.entries[0].selected = true;
        model.RemoveSelected();
        reopened.Restore(restored);
        Check(restored.entries.size() == 5 && restored.entries[0].title == "Björk - Jóga", "D removal saved before shutdown");
        Reverse(model);
        reopened.Restore(restored);
        Check(restored.entries[0].title == "周杰倫", "Reverse persists");
        Sort(model, SortOrder::Title);
        reopened.Restore(restored);
        Check(restored.entries[0].title == model.entries[0].title, "Sort persists");
        ui::PlaylistInteraction interaction;
        interaction.model = PlaylistModel(mediaRoot);
        session.Restore(interaction.model);
        interaction.model.durableChanged = [&] { Check(session.Save(interaction.model), "Drag save"); };
        interaction.model.entries[0].selected = true;
        const auto movedTitle = interaction.model.entries[0].title;
        interaction.MoveSelected(2);
        reopened.Restore(restored);
        Check(restored.entries[2].title == movedTitle, "Drag reorder persists");
        const std::vector<FileEntry> imported{{L"https://youtu.be/ABCdef12345", "Imported Unicode Björk", 18}};
        ImportPlaylist(interaction.model, imported);
        reopened.Restore(restored);
        Check(restored.entries.back().title == imported[0].title && restored.entries.back().durationSeconds == 18, "Import metadata saved");
        model.Clear();
        reopened.Restore(restored);
        Check(restored.entries.empty(), "E clear saves empty session immediately");
        for (const auto* corrupt : {"", "{bad", "[]", "{\"schemaVersion\":2,\"items\":[]}", "{\"schemaVersion\":1}"}) {
            Write(session.FilePath(), corrupt);
            reopened.Restore(restored);
            Check(restored.entries.empty(), "K L corrupt unsupported document fails safely");
        }
        Write(session.FilePath(), R"({"schemaVersion":1,"items":[{},
            {"sourceType":"LocalFile","source":"missing.mp3","title":"valid","durationSeconds":null},
            {"sourceType":"YouTubeVideo","source":"https://youtu.be/dQw4w9WgXcQ","videoId":"wrong","title":"bad","durationSeconds":0},
            {"sourceType":"LocalFile","source":"other.mp3","title":"sibling","durationSeconds":23}]})");
        reopened.Restore(restored);
        Check(restored.entries.size() == 2 && restored.entries[1].title == "sibling", "M malformed items skipped individually");
        Check(session.Save(restored, true), "Save valid baseline");
        const auto baseline = ReadPlaylistFile(session.FilePath());
        const auto locked = CreateFileW(session.FilePath().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        Check(locked != INVALID_HANDLE_VALUE, "Lock canonical file");
        restored.Add("unsaved", {}, L"no.mp3");
        const bool failed = !session.Save(restored);
        CloseHandle(locked);
        Check(failed && ReadPlaylistFile(session.FilePath()) == baseline, "N failed atomic replacement preserves previous session");
        reopened.Restore(restored);
        Check(restored.entries.size() == 2, "Previous session remains usable");
        for (const auto& file : std::filesystem::directory_iterator(sessionRoot))
            Check(file.path() == session.FilePath(), "Failed write cleans temporary file");
        // Exercise the production window-owned store, independent of the real user profile.
        graphics::BitmapLoader loader;
        const auto assets = skin::SkinLoader{}.Load(app::DefaultSkinDirectory, loader);
        graphics::Renderer renderer;
        model.Clear();
        model.QueueYouTubeUrl(L"https://youtu.be/dQw4w9WgXcQ");
        model.QueueYouTubeUrl(L"https://youtu.be/ABCdef12345");
        model.Add("Björk - Jóga", 60, missing.wstring());
        const auto click = [](HWND window, int x, int y) {
            SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
            SendMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
        };
        {
            ui::MainWindow window(GetModuleHandleW(nullptr), assets, renderer, core::IntegerScale{1},
                                  mediaRoot, false, {}, sessionRoot);
            const auto& rows = window.Playlist().State().model.entries;
            Check(rows.size() == 3 && rows[0].downloadStatus == DownloadStatus::Ready &&
                  rows[1].downloadStatus == DownloadStatus::Queued && rows[2].title == "Björk - Jóga",
                  "Window startup restores order, cache truth and Unicode");
            window.Playlist().AddFiles({missing});
            click(window.Playlist().Handle(), 20, 25);
            SendMessageW(window.Playlist().Handle(), WM_KEYDOWN, VK_DELETE, 0);
            Check(window.Playlist().State().model.entries.size() == 3, "Native removal");
        }
        {
            ui::MainWindow window(GetModuleHandleW(nullptr), assets, renderer, core::IntegerScale{1},
                                  mediaRoot, false, {}, sessionRoot);
            Check(window.Playlist().State().model.entries.size() == 3 &&
                  window.Playlist().State().model.entries[0].youtubeVideoId == "ABCdef12345",
                  "Window destruction and recreation retains removal and added local row");
            const auto pl = window.Playlist().Handle();
            const auto size = window.Playlist().State().size;
            click(pl, size.width - 36, size.height - 22);
            click(pl, size.width - 36, size.height - 60);
            Check(window.Playlist().State().model.entries.empty(), "Native NEW LIST clears");
        }
        {
            ui::MainWindow window(GetModuleHandleW(nullptr), assets, renderer, core::IntegerScale{1},
                                  mediaRoot, false, {}, sessionRoot);
            Check(window.Playlist().State().model.entries.empty(), "NEW LIST survives window restart");
        }
        std::cout << "Playlist session A-P passed using injected temporary session/media roots.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
