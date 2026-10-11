#include "ui/ClassicWindowDock.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace yinamp::ui {
namespace {
std::size_t Index(ClassicWindowId id) {
    return static_cast<std::size_t>(id);
}
bool SameOrigin(RECT a, RECT b) {
    return a.left == b.left && a.top == b.top;
}
bool Overlap(RECT a, RECT b) {
    return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
}
}
void ClassicWindowDock::InitializeStartup() {
    relations_ = {};
    relations_[Index(ClassicWindowId::Equalizer)] =
        ClassicDockRelation{ClassicWindowId::Equalizer, ClassicWindowId::Main, DockEdge::Top, DockEdge::Bottom, 0};
    relations_[Index(ClassicWindowId::Playlist)] =
        ClassicDockRelation{ClassicWindowId::Playlist, ClassicWindowId::Main, DockEdge::Left, DockEdge::Right, 0};
    drag_.reset();
}
const std::optional<ClassicDockRelation>& ClassicWindowDock::Relation(ClassicWindowId source) const {
    return relations_[Index(source)];
}
bool ClassicWindowDock::Connected(ClassicWindowId source, ClassicWindowId target) const {
    std::array<bool, 3> reached{};
    reached[Index(source)] = true;
    for (int pass = 0; pass < 3; ++pass)
        for (const auto& relation : relations_)
            if (relation && (reached[Index(relation->source)] || reached[Index(relation->target)])) {
                reached[Index(relation->source)] = true;
                reached[Index(relation->target)] = true;
            }
    return reached[Index(target)];
}
bool ClassicWindowDock::InMainGroup(ClassicWindowId source) const {
    return Connected(source, ClassicWindowId::Main);
}
bool ClassicWindowDock::Dragging(ClassicWindowId source) const {
    return drag_ && drag_->source == source;
}
void ClassicWindowDock::Disconnect(ClassicWindowId source) {
    for (auto& relation : relations_)
        if (relation && (relation->source == source || relation->target == source))
            relation.reset();
}
RECT ClassicWindowDock::Place(const ClassicDockRelation& relation, RECT source, RECT target, int scale) {
    const auto width = source.right - source.left, height = source.bottom - source.top;
    const int offset = relation.logicalOffset * scale;
    LONG x{}, y{};
    switch (relation.targetEdge) {
    case DockEdge::Bottom:
        x = target.left + offset;
        y = target.bottom;
        break;
    case DockEdge::Top:
        x = target.left + offset;
        y = target.top - height;
        break;
    case DockEdge::Right:
        x = target.right;
        y = target.top + offset;
        break;
    case DockEdge::Left:
        x = target.left - width;
        y = target.top + offset;
        break;
    }
    return {x, y, x + width, y + height};
}
void ClassicWindowDock::BeginDrag(ClassicWindowId source, RECT bounds, POINT cursor) {
    const auto attached = std::find_if(relations_.begin(), relations_.end(), [source](const auto& relation) {
        return relation && (relation->source == source || relation->target == source);
    });
    drag_ = DragSession{source, bounds, cursor,
                        source == ClassicWindowId::Main || attached == relations_.end() ? std::nullopt : *attached,
                        false, false, {}};
}
bool ClassicWindowDock::MoveDrag(ClassicWindowId source, RECT& proposed, POINT cursor,
                                 const ClassicWindowBounds& bounds, ClassicWindowVisibility visible, int scale) {
    if (!Dragging(source))
        return false;
    auto& drag = *drag_;
    if (drag.initialRelation && !drag.detached) {
        const auto distance =
            std::max({std::abs(cursor.x - drag.cursor.x), std::abs(cursor.y - drag.cursor.y),
                      std::abs(proposed.left - drag.start.left), std::abs(proposed.top - drag.start.top)});
        if (distance < DetachThreshold * scale) {
            proposed = drag.start;
            return true;
        }
        // Avoid pulling the window straight back into its old dock while detaching.
        Disconnect(source);
        drag.detached = true;
    }
    if (drag.detached) {
        if (cursor.x != drag.cursor.x || cursor.y != drag.cursor.y)
            OffsetRect(&proposed, drag.start.left + cursor.x - drag.cursor.x - proposed.left,
                       drag.start.top + cursor.y - drag.cursor.y - proposed.top);
    }
    drag.candidate.reset();
    std::optional<RECT> best;
    long long bestDistance = std::numeric_limits<long long>::max();
    const int threshold = SnapThreshold * scale;
    const auto width = proposed.right - proposed.left, height = proposed.bottom - proposed.top;
    for (auto target : {ClassicWindowId::Main, ClassicWindowId::Equalizer, ClassicWindowId::Playlist}) {
        if (target == source || !visible[Index(target)] || Connected(source, target))
            continue;
        const auto targetBounds = bounds[Index(target)];
        for (auto targetEdge : {DockEdge::Bottom, DockEdge::Right, DockEdge::Top, DockEdge::Left}) {
            const bool vertical = targetEdge == DockEdge::Top || targetEdge == DockEdge::Bottom;
            const auto leading = vertical ? proposed.left : proposed.top;
            const auto targetLeading = vertical ? targetBounds.left : targetBounds.top;
            const auto trailing = leading + (vertical ? width : height);
            const auto targetTrailing = vertical ? targetBounds.right : targetBounds.bottom;
            if (leading >= targetTrailing || trailing <= targetLeading)
                continue;
            auto offset = leading - targetLeading;
            const auto endOffset = targetTrailing - targetLeading - (vertical ? width : height);
            if (std::abs(offset - endOffset) <= threshold && std::abs(offset - endOffset) < std::abs(offset))
                offset = endOffset;
            else if (std::abs(offset) <= threshold)
                offset = 0;
            const int logicalOffset = static_cast<int>(std::lround(static_cast<double>(offset) / scale));
            DockEdge sourceEdge = DockEdge::Top;
            switch (targetEdge) {
            case DockEdge::Bottom:
                sourceEdge = DockEdge::Top;
                break;
            case DockEdge::Top:
                sourceEdge = DockEdge::Bottom;
                break;
            case DockEdge::Right:
                sourceEdge = DockEdge::Left;
                break;
            case DockEdge::Left:
                sourceEdge = DockEdge::Right;
                break;
            }
            ClassicDockRelation candidate{source, target, sourceEdge, targetEdge, logicalOffset};
            const auto snapped = Place(candidate, proposed, targetBounds, scale);
            const auto dx = snapped.left - proposed.left, dy = snapped.top - proposed.top;
            if (std::abs(dx) > threshold || std::abs(dy) > threshold)
                continue;
            bool overlaps = false;
            for (std::size_t other = 0; other < bounds.size(); ++other)
                if (other != Index(source) && visible[other]) {
                    auto otherBounds = bounds[other];
                    if (source == ClassicWindowId::Main && InMainGroup(static_cast<ClassicWindowId>(other)))
                        OffsetRect(&otherBounds, snapped.left - bounds[0].left, snapped.top - bounds[0].top);
                    if (Overlap(snapped, otherBounds))
                        overlaps = true;
                    if (source == ClassicWindowId::Main && !InMainGroup(static_cast<ClassicWindowId>(other)))
                        for (std::size_t member = 1; member < bounds.size(); ++member)
                            if (visible[member] && InMainGroup(static_cast<ClassicWindowId>(member))) {
                                auto moved = bounds[member];
                                OffsetRect(&moved, snapped.left - bounds[0].left, snapped.top - bounds[0].top);
                                if (Overlap(moved, otherBounds))
                                    overlaps = true;
                            }
                }
            if (overlaps)
                continue;
            const long long distance = static_cast<long long>(dx) * dx + static_cast<long long>(dy) * dy;
            if (distance < bestDistance) {
                bestDistance = distance;
                best = snapped;
                drag.candidate = candidate;
            }
        }
    }
    if (drag.detached && !drag.snapRearmed) {
        drag.candidate.reset();
        // Leaving the magnetic zone rearms snapping for a return in this same drag.
        if (!best)
            drag.snapRearmed = true;
        return false;
    }
    if (!best)
        return false;
    proposed = *best;
    return true;
}
void ClassicWindowDock::EndDrag(ClassicWindowId source, RECT actual, const ClassicWindowBounds& bounds, int scale) {
    if (!Dragging(source))
        return;
    // İlişki fare bırakıldığında, gerçekten uygulanmış konum üzerinde kesinleşir.
    if (drag_->candidate &&
        SameOrigin(actual, Place(*drag_->candidate, actual, bounds[Index(drag_->candidate->target)], scale))) {
        relations_[Index(source)] = drag_->candidate;
        if (source == ClassicWindowId::Main) {
            // Orient the joined component away from Main so Main stays the group anchor.
            const auto edges = relations_;
            std::array<bool, 3> reached{true, false, false};
            relations_[0].reset();
            for (int pass = 0; pass < 3; ++pass)
                for (const auto& edge : edges) {
                    if (!edge)
                        continue;
                    auto relation = *edge;
                    if (reached[Index(relation.source)] && !reached[Index(relation.target)]) {
                        std::swap(relation.source, relation.target);
                        std::swap(relation.sourceEdge, relation.targetEdge);
                        relation.logicalOffset = -relation.logicalOffset;
                    }
                    if (reached[Index(relation.target)] && !reached[Index(relation.source)]) {
                        relations_[Index(relation.source)] = relation;
                        reached[Index(relation.source)] = true;
                    }
                }
        }
    }
    drag_.reset();
}
void ClassicWindowDock::ValidatePosition(ClassicWindowId source, const ClassicWindowBounds& bounds, int scale) {
    if (Dragging(source))
        return;
    for (const auto& relation : relations_)
        if (relation && (relation->source == source || relation->target == source) &&
            !SameOrigin(bounds[Index(relation->source)],
                        Place(*relation, bounds[Index(relation->source)], bounds[Index(relation->target)], scale))) {
            Disconnect(source);
            return;
        }
}
ClassicWindowBounds ClassicWindowDock::Reflow(ClassicWindowBounds bounds, int scale) const {
    std::array<bool, 3> placed{};
    for (std::size_t i = 0; i < placed.size(); ++i)
        placed[i] = !relations_[i];
    for (int pass = 0; pass < 3; ++pass)
        for (const auto& relation : relations_)
            if (relation && !placed[Index(relation->source)] && placed[Index(relation->target)]) {
                bounds[Index(relation->source)] =
                    Place(*relation, bounds[Index(relation->source)], bounds[Index(relation->target)], scale);
                placed[Index(relation->source)] = true;
            }
    return bounds;
}
} // namespace yinamp::ui
