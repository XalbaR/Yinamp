#pragma once
#include "playlist/PlaylistModel.h"
#include <set>

namespace yinamp::ui {
enum class LibrarySort {
    Title,
    Author,
    Duration,
    Size,
    Source,
    Location
};
struct LibraryAuthor {
    std::string name;
    std::size_t count{};
};
// Catalog and view state only. The playlist remains owned by PlaylistWindow.
class LibraryInteraction final {
  public:
    LibraryInteraction(playlist::PlaylistModel& playlist, std::optional<std::filesystem::path> root = {});
    void Refresh();
    void Filter(std::wstring search, std::optional<std::string> author = {});
    void Sort(LibrarySort column);
    void Select(std::size_t row, bool control = false, bool shift = false);
    void SelectAll();
    bool Selected(std::size_t row) const;
    std::vector<std::uint64_t> AddSelected();
    std::optional<std::uint64_t> PlaySelected();
    void Rename(std::string_view videoId, const std::optional<std::string>& title);
    const std::vector<media::LibraryEntry>& Entries() const {
        return entries_;
    }
    const std::vector<std::size_t>& Visible() const {
        return visible_;
    }
    const std::vector<LibraryAuthor>& Authors() const {
        return authors_;
    }
    const std::optional<std::string>& Author() const {
        return author_;
    }
    LibrarySort Column() const {
        return column_;
    }
    bool Ascending() const {
        return ascending_;
    }
    static std::string AuthorName(const media::LibraryEntry& entry);
    static std::wstring FormatSize(std::uintmax_t bytes);

  private:
    std::optional<std::uint64_t> PlaylistIdentity(const media::LibraryEntry& video);
    void Rebuild();
    playlist::PlaylistModel& playlist_;
    media::MediaStore store_;
    std::vector<media::LibraryEntry> entries_;
    std::vector<std::size_t> visible_;
    std::vector<LibraryAuthor> authors_;
    std::set<std::string> selected_;
    std::optional<std::string> anchor_;
    std::wstring search_;
    std::optional<std::string> author_;
    LibrarySort column_{LibrarySort::Title};
    bool ascending_{true};
};
}
