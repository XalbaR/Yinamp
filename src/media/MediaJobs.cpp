#include "media/MediaJobs.h"
#include "media/native/Cancellation.h"
#include <algorithm>

namespace yinamp::media {
std::string JobStatusText(JobState state, int progress) {
    switch (state) {
    case JobState::Queued:
        return "QUEUED";
    case JobState::Resolving:
        return "RESOLVING";
    case JobState::Downloading:
        return progress < 0 ? "DOWNLOADING" : std::to_string(progress) + "%";
    case JobState::Finalizing:
        return "FINALIZING";
    case JobState::Ready:
        return "READY";
    case JobState::Failed:
        return "ERR";
    case JobState::Cancelled:
        return "CANCELLED";
    default:
        return "";
    }
}
MediaJobs::MediaJobs(std::optional<std::filesystem::path> root, MediaRunner runner)
    : runner_(runner ? std::move(runner) : MediaRunner{[root](const auto& request, auto stop, const auto& sink) {
          RunMediaJob(root, request, stop, sink);
      }}),
      worker_([this](std::stop_token stop) { Work(stop); }) {}
MediaJobs::~MediaJobs() {
    Shutdown();
}
void MediaJobs::Shutdown() {
    worker_.request_stop();
    std::stop_source cancel;
    {
        std::lock_guard lock(mutex_);
        pending_.clear();
        cancel = activeStop_;
    }
    cancel.request_stop();
    wake_.notify_all();
    if (worker_.joinable())
        worker_.join();
}
void MediaJobs::Restart() {
    Shutdown();
    {
        std::lock_guard lock(mutex_);
        pending_.clear();
        updates_.clear();
        active_.reset();
        activeStop_ = std::stop_source{};
    }
    worker_ = std::jthread([this](std::stop_token stop) { Work(stop); });
}
void MediaJobs::Synchronize(const std::vector<MediaRequest>& wanted) {
    std::stop_source cancel;
    bool shouldCancel{};
    {
        std::lock_guard lock(mutex_);
        const auto needed = [&](const MediaRequest& request) {
            return std::ranges::any_of(wanted, [&](const auto& item) {
                return item.videoId == request.videoId && item.entryId == request.entryId;
            });
        };
        std::erase_if(pending_, [&](const auto& request) { return !needed(request); });
        if (active_) {
            const auto remaining =
                std::ranges::find_if(wanted, [&](const auto& item) { return item.videoId == active_->videoId; });
            if (remaining == wanted.end()) {
                cancel = activeStop_;
                shouldCancel = true;
            } else if (!activeStop_.stop_requested()) {
                active_ = *remaining;
            }
        }
        for (const auto& request : wanted) {
            if (active_ && active_->videoId == request.videoId)
                continue;
            if (std::ranges::any_of(pending_, [&](const auto& item) { return item.videoId == request.videoId; }))
                continue;
            pending_.push_back(request);
        }
    }
    if (shouldCancel)
        cancel.request_stop();
    wake_.notify_all();
}
std::vector<MediaUpdate> MediaJobs::Drain() {
    std::lock_guard lock(mutex_);
    auto updates = std::move(updates_);
    updates_.clear();
    return updates;
}
void MediaJobs::Work(std::stop_token stop) {
    while (!stop.stop_requested()) {
        MediaRequest request;
        std::stop_token jobStop;
        {
            std::unique_lock lock(mutex_);
            if (!wake_.wait(lock, stop, [&] { return !pending_.empty(); }))
                return;
            if (stop.stop_requested())
                return;
            request = pending_.front();
            pending_.pop_front();
            active_ = request;
            activeStop_ = std::stop_source{};
            jobStop = activeStop_.get_token();
        }
        const auto emit = [&](MediaUpdate update) {
            std::lock_guard lock(mutex_);
            if (active_ && active_->videoId == update.request.videoId)
                update.request = *active_;
            const auto old = std::ranges::find_if(
                updates_, [&](const auto& item) { return item.request.entryId == update.request.entryId; });
            if (old != updates_.end()) {
                if (update.title.empty())
                    update.title = old->title;
                if (!update.duration)
                    update.duration = old->duration;
            }
            // İlerleme birikmez; her istek için son güncelleme yeterlidir.
            std::erase_if(updates_, [&](const auto& old) { return old.request.entryId == update.request.entryId; });
            updates_.push_back(std::move(update));
        };
        try {
            native::CheckCancelled(jobStop);
            runner_(request, jobStop, emit);
        } catch (const std::exception& error) {
            MediaUpdate update;
            update.request = request;
            update.state = jobStop.stop_requested() ? JobState::Cancelled : JobState::Failed;
            update.error = error.what();
            emit(std::move(update));
        } catch (...) {
            MediaUpdate update;
            update.request = request;
            update.state = JobState::Failed;
            update.error = "Unexpected media job failure";
            emit(std::move(update));
        }
        {
            std::lock_guard lock(mutex_);
            active_.reset();
        }
    }
}
}
