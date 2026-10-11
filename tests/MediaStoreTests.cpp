#include "TemporaryMediaRoot.h"
#include "media/MediaStore.h"
#include "playlist/PlaylistModel.h"
#include "platform/windows/WindowsRuntime.h"
#include <fstream>
#include <iostream>

namespace {
void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
void Write(const std::filesystem::path& file, std::string_view text) {
    std::ofstream output(file, std::ios::binary);
    output << text;
    if (!output)
        throw std::runtime_error("Fixture write failed");
}
std::string Metadata(std::string_view id, std::string_view file = "video.mp4",
                     std::string_view state = "completed") {
    return "{\"videoId\":\"" + std::string(id) + "\",\"mediaFilename\":\"" + std::string(file) +
           "\",\"downloadState\":\"" + std::string(state) + "\",\"title\":\"A renamed title\"}";
}
}
int main() {
    try {
        yinamp::platform::ComApartment apartment;
        TemporaryMediaRoot temporary;
        const auto root = temporary.path / L"store";
        yinamp::media::MediaStore store(root);
        Check(!std::filesystem::exists(root), "Construction is lazy");
        Check(store.pathForVideo("ABC123") == root / L"ABC123", "ID path, not title");
        Check(std::filesystem::is_directory(root), "Root created on first use");
        Check(store.pathForVideo("AbC123") == root / L"AbC123", "Case preserved");
        Check(!std::filesystem::exists(root / L"ABC123"), "Path resolver creates no video directory");
        bool traversalRejected = false;
        try { (void)store.pathForVideo("../outside"); }
        catch (const std::invalid_argument&) { traversalRejected = true; }
        Check(traversalRejected, "Traversal rejected");
        const auto video = store.pathForVideo("dQw4w9WgXcQ");
        std::filesystem::create_directory(video);
        Check(!store.findCompletedVideo("dQw4w9WgXcQ"), "Empty directory incomplete");
        Write(video / L"thumbnail.jpg", "fixture");
        Check(!store.findCompletedVideo("dQw4w9WgXcQ"), "Thumbnail only incomplete");
        std::filesystem::remove(video / L"thumbnail.jpg");
        Write(video / L"metadata.json", Metadata("dQw4w9WgXcQ"));
        Check(!store.findCompletedVideo("dQw4w9WgXcQ"), "Metadata only incomplete");
        yinamp::playlist::PlaylistModel incomplete(root);
        Check(incomplete.QueueYouTubeUrl(L"https://youtu.be/dQw4w9WgXcQ") &&
                  incomplete.entries[0].downloadStatus == yinamp::playlist::DownloadStatus::Queued &&
                  incomplete.entries[0].downloadProgress == 0 && incomplete.entries[0].localMediaPath.empty(),
              "Incomplete physical item remains queued");
        Write(video / L"video.mp4", "");
        Check(!store.findCompletedVideo("dQw4w9WgXcQ"), "Zero bytes incomplete");
        std::filesystem::remove(video / L"video.mp4");
        Write(video / L"video.mp4.part", "interrupted fixture");
        Check(!store.findCompletedVideo("dQw4w9WgXcQ"), "Part file incomplete");
        Write(video / L"metadata.json", Metadata("dQw4w9WgXcQ", "video.mp4.part"));
        Check(!store.findCompletedVideo("dQw4w9WgXcQ"), "Metadata naming part file incomplete");
        Write(video / L"video.mp4.tmp", "temporary fixture");
        Write(video / L"metadata.json", Metadata("dQw4w9WgXcQ", "video.mp4.tmp"));
        Check(!store.findCompletedVideo("dQw4w9WgXcQ"), "Temporary media incomplete");
        Write(video / L"video.mp4", "nonempty completed media fixture");
        Write(video / L"metadata.json", Metadata("dQw4w9WgXcQ", "video.mp4", "downloading"));
        Check(!store.findCompletedVideo("dQw4w9WgXcQ"), "Incomplete state rejected");
        Write(video / L"metadata.json", Metadata("wrongVideoId"));
        Check(!store.findCompletedVideo("dQw4w9WgXcQ"), "Mismatched identity rejected");
        Write(video / L"metadata.json", "{bad json}");
        Check(!store.findCompletedVideo("dQw4w9WgXcQ"), "Malformed metadata rejected");
        Write(video / L"metadata.json", "{\"videoId\":\"dQw4w9WgXcQ\",\"mediaFilename\":\"video.mp4\"}");
        Check(!store.findCompletedVideo("dQw4w9WgXcQ"), "Missing completion state rejected");
        Write(video / L"metadata.json", Metadata("dQw4w9WgXcQ", "../video.mp4"));
        Check(!store.findCompletedVideo("dQw4w9WgXcQ"), "Metadata traversal rejected");
        Write(video / L"metadata.json", Metadata("dQw4w9WgXcQ", "A renamed title.mp4"));
        Check(!store.findCompletedVideo("dQw4w9WgXcQ"), "Title filename rejected");
        Write(video / L"video.exe", "unsupported fixture");
        Write(video / L"metadata.json", Metadata("dQw4w9WgXcQ", "video.exe"));
        Check(!store.findCompletedVideo("dQw4w9WgXcQ"), "Unsupported extension rejected");
        Write(video / L"metadata.json", Metadata("dQw4w9WgXcQ"));
        std::filesystem::rename(video / L"video.mp4", video / L"Video.mp4");
        Check(!store.findCompletedVideo("dQw4w9WgXcQ"), "Exact stable media filename required");
        std::filesystem::rename(video / L"Video.mp4", video / L"video.mp4");
        const auto completed = store.findCompletedVideo("dQw4w9WgXcQ");
        Check(completed && completed->mediaPath == video / L"video.mp4", "Valid completed contract found");
        Check(!store.findCompletedVideo("DQw4w9WgXcQ"), "Case-sensitive store identity on Windows");
        for (const auto filename : {"video.webm", "video.mkv"}) {
            Write(video / filename, "completed supported media fixture");
            Write(video / L"metadata.json", Metadata("dQw4w9WgXcQ", filename));
            Check(store.findCompletedVideo("dQw4w9WgXcQ").has_value(), "Supported completed video format");
        }
        Write(video / L"metadata.json", Metadata("dQw4w9WgXcQ"));
        yinamp::playlist::PlaylistModel model(root);
        using Result = yinamp::playlist::YouTubeSubmission;
        using Status = yinamp::playlist::DownloadStatus;
        Check(model.SubmitYouTubeUrl(L"https://youtu.be/dQw4w9WgXcQ") == Result::Added,
              "Completed video added");
        Check(model.entries.size() == 1 && model.entries[0].downloadStatus == Status::Ready &&
                  model.entries[0].downloadProgress == 100 && model.entries[0].localMediaPath == completed->mediaPath,
              "Ready 100 reuses existing path");
        Check(model.SubmitYouTubeUrl(L"https://youtube.com/watch?v=dQw4w9WgXcQ&t=15") == Result::AlreadyInPlaylist &&
                  model.entries.size() == 1, "URL variant duplicate rejected");
        model.SelectAll(true);
        model.RemoveSelected();
        Check(std::filesystem::exists(completed->mediaPath), "Removal preserves media");
        Check(model.SubmitYouTubeUrl(L"https://youtube.com/shorts/dQw4w9WgXcQ") == Result::Added &&
                  model.entries[0].downloadStatus == Status::Ready && model.entries[0].downloadProgress == 100 &&
                  model.entries[0].localMediaPath == completed->mediaPath, "Re-add URL variant reuses same directory");
        Check(model.SubmitYouTubeUrl(L"https://youtu.be/ABCdef12345") == Result::Added &&
                  model.entries.back().downloadStatus == Status::Queued && model.entries.back().downloadProgress == 0 &&
                  model.entries.back().localMediaPath.empty() && !std::filesystem::exists(root / L"ABCdef12345"),
              "New source queued without files or directory");
        // Prove duplicates return before touching an unavailable store root.
        const auto blocked = temporary.path / L"blocked";
        Write(blocked, "regular file");
        yinamp::playlist::PlaylistModel duplicate(blocked);
        duplicate.entries.push_back(model.entries[0]);
        Check(duplicate.SubmitYouTubeUrl(L"https://youtu.be/dQw4w9WgXcQ") == Result::AlreadyInPlaylist &&
                  duplicate.entries.size() == 1, "Playlist duplicate checked before store lookup");
        std::cout << "Media store contract and ingestion reuse passed; all fixtures used injected temporary roots.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
