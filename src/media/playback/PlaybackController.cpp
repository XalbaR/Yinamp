#include "media/playback/PlaybackController.h"
namespace yinamp::media::playback {
PlaybackController::PlaybackController(playlist::PlaylistModel& model, Session& session,
                                       std::optional<std::filesystem::path> root, unsigned seed,
                                       StreamResolver onlineResolver)
    : model_(model), session_(session), resolver_(std::move(root)), onlineResolver_(std::move(onlineResolver)),
      random_(seed) {}
const playlist::Entry* PlaybackController::CurrentEntry() const {
    for (const auto& entry : model_.entries)
        if (current_ == entry.id)
            return &entry;
    return nullptr;
}
void PlaybackController::SetModes(bool repeat, bool shuffle) {
    repeat_ = repeat;
    if (shuffle_ != shuffle) {
        bag_.clear();
        history_.clear();
        epochStarted_ = false;
    }
    shuffle_ = shuffle;
}
void PlaybackController::StartCandidates(std::vector<std::uint64_t> ids, bool history) {
    candidates_ = std::move(ids);
    recordHistory_ = history;
    TryCandidate();
}
void PlaybackController::TryCandidate() {
    while (!candidates_.empty()) {
        const auto id = candidates_.front();
        candidates_.erase(candidates_.begin());
        for (const auto& entry : model_.entries) {
            if (entry.id != id)
                continue;
            const auto path = resolver_.ResolveSource(entry, mode_, onlineResolver_);
            if (!path)
                break;
            stopping_ = false;
            loaded_ = true;
            current_ = id;
            activeMode_ = path->Online() ? PlaybackMode::Online : PlaybackMode::Download;
            activePath_ = path->localPath;
            for (auto& row : model_.entries)
                row.current = row.id == id;
            value_ = {};
            value_.state = State::Opening;
            value_.generation = ++generation_;
            session_.OpenSource(*path, generation_);
            return;
        }
    }
    Stop();
}
void PlaybackController::PlayItem(std::uint64_t id) {
    for (const auto& entry : model_.entries)
        if (entry.id == id && resolver_.ResolveSource(entry, mode_, onlineResolver_)) {
            bag_.clear();
            epochStarted_ = false;
            StartCandidates({id});
            return;
        }
}
void PlaybackController::Play() {
    if (current_ && loaded_ &&
        (value_.state == State::Paused || value_.state == State::Stopped || value_.state == State::Ended)) {
        stopping_ = false;
        session_.Play();
        return;
    }
    if (value_.state == State::Playing || value_.state == State::Opening)
        return;
    std::vector<std::uint64_t> ids;
    for (const auto& entry : model_.entries)
        if (entry.selected)
            ids.push_back(entry.id);
    if (current_)
        ids.push_back(*current_);
    else
        for (const auto& entry : model_.entries)
            if (entry.current)
                ids.push_back(entry.id);
    for (const auto& entry : model_.entries)
        ids.push_back(entry.id);
    StartCandidates(std::move(ids));
}
void PlaybackController::Pause() {
    if (value_.state == State::Playing)
        session_.Pause();
    else if (value_.state == State::Paused)
        session_.Play();
}
void PlaybackController::Stop() {
    if (value_.state == State::Opening) {
        session_.Close();
        loaded_ = false;
    }
    stopping_ = true;
    candidates_.clear();
    session_.Stop();
    value_.state = State::Stopped;
    value_.position = 0;
}
std::vector<std::uint64_t> PlaybackController::Forward() {
    std::vector<std::uint64_t> ids;
    if (shuffle_) {
        std::erase_if(bag_, [&](auto id) {
            return id == current_ || std::ranges::none_of(model_.entries, [id](const auto& e) { return e.id == id; });
        });
        if (bag_.empty()) {
            // An exhausted epoch only refills when repeat is on (or this is the first traversal).
            if (!repeat_ && epochStarted_)
                return {};
            for (const auto& e : model_.entries)
                if (e.id != current_)
                    bag_.push_back(e.id);
            if (bag_.empty() && repeat_ && current_)
                bag_.push_back(*current_);
            std::shuffle(bag_.begin(), bag_.end(), random_);
            epochStarted_ = true;
        }
        ids = std::move(bag_);
        bag_.clear();
        // Candidates are consumed one at a time; retain the unvisited epoch on successful opening.
        return ids;
    }
    std::size_t start = 0;
    for (std::size_t i = 0; i < model_.entries.size(); ++i)
        if (model_.entries[i].id == current_)
            start = i + 1;
    for (std::size_t i = start; i < model_.entries.size(); ++i)
        ids.push_back(model_.entries[i].id);
    if (repeat_)
        for (std::size_t i = 0; i < start; ++i)
            ids.push_back(model_.entries[i].id);
    return ids;
}
void PlaybackController::Next() {
    StartCandidates(Forward());
}
void PlaybackController::Previous() {
    if (value_.position > 3) {
        Seek(0);
        return;
    }
    if (shuffle_) {
        while (!history_.empty()) {
            const auto id = history_.back();
            history_.pop_back();
            for (const auto& e : model_.entries)
                if (e.id == id && resolver_.ResolveSource(e, mode_, onlineResolver_)) {
                    StartCandidates({id}, false);
                    return;
                }
        }
        Seek(0);
        return;
    }
    std::vector<std::uint64_t> ids;
    std::size_t index{};
    for (std::size_t i = 0; i < model_.entries.size(); ++i)
        if (model_.entries[i].id == current_)
            index = i;
    for (auto i = index; i > 0; --i)
        ids.push_back(model_.entries[i - 1].id);
    if (repeat_)
        for (auto i = model_.entries.size(); i > index; --i)
            ids.push_back(model_.entries[i - 1].id);
    if (ids.empty()) {
        Seek(0);
        return;
    }
    StartCandidates(std::move(ids));
}
void PlaybackController::Seek(double seconds) {
    if (current_ && value_.seekable && value_.duration > 0)
        session_.Seek(ClampSeek(seconds, value_.duration));
}
void PlaybackController::Poll() {
    if (current_ && !CurrentEntry()) {
        session_.Close();
        current_.reset();
        lastPlayed_.reset();
        candidates_.clear();
        value_ = {};
        return;
    }
    auto next = session_.Read();
    if (next.generation != generation_)
        return;
    if (stopping_ && next.state != State::Failed) {
        next.state = State::Stopped;
        next.position = 0;
    }
    const auto old = value_.state;
    value_ = next;
    if (!next.title.empty() &&
        (next.state == State::Playing || next.state == State::Paused || next.state == State::Stopped)) {
        for (auto& entry : model_.entries) {
            if (entry.id != current_)
                continue;
            const auto duration =
                next.duration > 0 ? std::optional<int>{static_cast<int>(next.duration)} : entry.durationSeconds;
            if ((!entry.displayTitle && entry.title != next.title) || entry.durationSeconds != duration) {
                if (!entry.displayTitle) {
                    entry.title = next.title;
                    entry.originalTitle = next.title;
                }
                entry.durationSeconds = duration;
                model_.NotifyDurableChange();
            }
        }
    }
    if (next.state == State::Failed)
        loaded_ = false;
    if (next.state == State::Playing || next.state == State::Paused) {
        if (shuffle_ && recordHistory_ && lastPlayed_ && lastPlayed_ != current_)
            history_.push_back(*lastPlayed_);
        lastPlayed_ = current_;
        if (shuffle_ && !epochStarted_) {
            bag_.clear();
            for (const auto& row : model_.entries)
                if (row.id != current_)
                    bag_.push_back(row.id);
            std::shuffle(bag_.begin(), bag_.end(), random_);
            epochStarted_ = true;
        } else if (shuffle_ && !candidates_.empty())
            bag_ = candidates_;
        candidates_.clear();
    }
    if (next.state == State::Ended && old != State::Ended)
        Next();
    else if (next.state == State::Failed && old != State::Failed) {
        if (!candidates_.empty())
            TryCandidate();
    }
}
}
