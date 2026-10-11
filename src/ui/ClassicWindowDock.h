#pragma once
#include <array>
#include <optional>
#include <windows.h>

namespace yinamp::ui {
enum class ClassicWindowId : unsigned {
    Main,
    Equalizer,
    Playlist
};
enum class DockEdge {
    Top,
    Bottom,
    Left,
    Right
};
struct ClassicDockRelation {
    ClassicWindowId source;
    ClassicWindowId target;
    DockEdge sourceEdge;
    DockEdge targetEdge;
    int logicalOffset{};
    bool operator==(const ClassicDockRelation&) const = default;
};
using ClassicWindowBounds = std::array<RECT, 3>;
using ClassicWindowVisibility = std::array<bool, 3>;
inline constexpr UINT ClassicDockBeginMessage = WM_APP + 11;
inline constexpr UINT ClassicDockMoveMessage = WM_APP + 12;
inline constexpr UINT ClassicDockEndMessage = WM_APP + 13;
inline constexpr UINT ClassicDockPositionMessage = WM_APP + 14;

class ClassicWindowDock final {
  public:
    static constexpr int SnapThreshold = 12;
    static constexpr int DetachThreshold = 10;
    void InitializeStartup();
    void BeginDrag(ClassicWindowId source, RECT bounds, POINT cursor);
    bool MoveDrag(ClassicWindowId source, RECT& proposed, POINT cursor, const ClassicWindowBounds& bounds,
                  ClassicWindowVisibility visible, int scale);
    void EndDrag(ClassicWindowId source, RECT actual, const ClassicWindowBounds& bounds, int scale);
    void Disconnect(ClassicWindowId source);
    void ValidatePosition(ClassicWindowId source, const ClassicWindowBounds& bounds, int scale);
    [[nodiscard]] bool InMainGroup(ClassicWindowId source) const;
    [[nodiscard]] bool Dragging(ClassicWindowId source) const;
    [[nodiscard]] const std::optional<ClassicDockRelation>& Relation(ClassicWindowId source) const;
    [[nodiscard]] ClassicWindowBounds Reflow(ClassicWindowBounds bounds, int scale) const;
    [[nodiscard]] static RECT Place(const ClassicDockRelation& relation, RECT source, RECT target, int scale);

  private:
    struct DragSession {
        ClassicWindowId source;
        RECT start;
        POINT cursor;
        std::optional<ClassicDockRelation> initialRelation;
        bool detached{};
        bool snapRearmed{};
        std::optional<ClassicDockRelation> candidate;
    };
    [[nodiscard]] bool Connected(ClassicWindowId source, ClassicWindowId target) const;
    std::array<std::optional<ClassicDockRelation>, 3> relations_{};
    std::optional<DragSession> drag_;
};
} // namespace yinamp::ui
