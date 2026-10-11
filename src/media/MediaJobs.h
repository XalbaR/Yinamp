#pragma once
#include "media/MediaStore.h"
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace yinamp::media {
enum class JobState {
    None,
    Queued,
    Resolving,
    Downloading,
    Finalizing,
    Ready,
    Failed,
    Cancelled
};
std::string JobStatusText(JobState state, int progress);
struct MediaRequest {
    std::string videoId;
    std::wstring url;
    std::uint64_t entryId{};
    ImportSource importSource;
};
struct MediaUpdate {
    MediaRequest request;
    JobState state{JobState::Queued};
    int progress{-1};
    std::string title;
    std::optional<int> duration;
    std::filesystem::path mediaPath;
    std::string error;
};
using UpdateSink = std::function<void(MediaUpdate)>;
using MediaRunner = std::function<void(const MediaRequest&, std::stop_token, const UpdateSink&)>;
// Sahip UI thread'inde istekleri eşitler; worker yalnızca değer kopyaları üretir.
class MediaJobs final {
  public:
    explicit MediaJobs(std::optional<std::filesystem::path> root = {}, MediaRunner runner = {});
    ~MediaJobs();
    void Synchronize(const std::vector<MediaRequest>& wanted);
    std::vector<MediaUpdate> Drain();
    void Shutdown();
    void Restart();

  private:
    void Work(std::stop_token stop);
    MediaRunner runner_;
    std::mutex mutex_;
    std::condition_variable_any wake_;
    std::deque<MediaRequest> pending_;
    std::vector<MediaUpdate> updates_;
    std::optional<MediaRequest> active_;
    std::stop_source activeStop_;
    std::jthread worker_;
};
void RunMediaJob(std::optional<std::filesystem::path> root, const MediaRequest& request, std::stop_token stop,
                 const UpdateSink& sink);
void RunMetadataJob(const MediaRequest& request, std::stop_token stop, const UpdateSink& sink);
void RunCachedMetadataJob(std::optional<std::filesystem::path> cacheRoot, const MediaRequest& request,
                          std::stop_token stop, const UpdateSink& sink);
}
