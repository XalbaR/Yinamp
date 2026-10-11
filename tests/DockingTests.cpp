#include "ui/ClassicWindowDock.h"
#include <iostream>
#include <stdexcept>

namespace {
using namespace yinamp::ui;
constexpr auto Main = ClassicWindowId::Main;
constexpr auto Eq = ClassicWindowId::Equalizer;
constexpr auto Pl = ClassicWindowId::Playlist;
constexpr ClassicWindowVisibility Visible{true, true, true};
void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
bool Same(RECT a, RECT b) {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}
ClassicWindowBounds Initial(int scale) {
    return {RECT{500, 500, 500 + 275 * scale, 500 + 116 * scale},
            RECT{500, 500 + 116 * scale, 500 + 275 * scale, 500 + 232 * scale},
            RECT{500 + 275 * scale, 500, 500 + 550 * scale, 500 + 464 * scale}};
}
void DetachAndRedock(int scale, ClassicWindowId id) {
    ClassicWindowDock dock;
    dock.InitializeStartup();
    auto bounds = Initial(scale);
    const auto index = static_cast<std::size_t>(id);
    const auto original = bounds[index];
    const auto relationship = dock.Relation(id);
    const POINT cursor{620, 610};
    dock.BeginDrag(id, original, cursor);
    auto proposed = original;
    const int shortPull = (ClassicWindowDock::DetachThreshold - 1) * scale;
    OffsetRect(&proposed, shortPull, 0);
    Check(dock.MoveDrag(id, proposed, {cursor.x + shortPull, cursor.y}, bounds, Visible, scale) &&
              Same(proposed, original) && dock.Relation(id) == relationship,
          "Below detach threshold remains attached");
    // Windows hizalanmış dikdörtgeni geri yollasa bile gerçek fare hareketi kopmayı başlatır.
    proposed = original;
    const int pull = ClassicWindowDock::DetachThreshold * scale;
    Check(!dock.MoveDrag(id, proposed, {cursor.x + pull, cursor.y}, bounds, Visible, scale) &&
              proposed.left == original.left + pull && !dock.InMainGroup(id),
          "Pointer alone detaches at dedicated inclusive threshold");
    Check(dock.InMainGroup(id == Eq ? Pl : Eq), "Detaching one preserves the other Main relationship");
    for (auto delta : {1, 5, 0, -2, 1}) {
        proposed = original;
        OffsetRect(&proposed, delta * scale, 0);
        Check(!dock.MoveDrag(id, proposed, {cursor.x + delta * scale, cursor.y}, bounds, Visible, scale) &&
                  proposed.left == original.left + delta * scale && !dock.Relation(id),
              "Initial detach stays free until leaving the magnetic zone");
    }
    bounds[index] = proposed;
    dock.EndDrag(id, proposed, bounds, scale);
    Check(!dock.Relation(id), "Mouse release after detach does not create a dock");
    dock.BeginDrag(id, proposed, cursor);
    proposed = original;
    OffsetRect(&proposed, 3 * scale, 0);
    Check(dock.MoveDrag(id, proposed, cursor, bounds, Visible, scale) && Same(proposed, original),
          "New drag can magnetically re-dock");
    Check(!dock.Relation(id), "Candidate is provisional until mouse release");
    bounds[index] = proposed;
    dock.EndDrag(id, proposed, bounds, scale);
    Check(dock.Relation(id) == relationship && dock.InMainGroup(id), "Mouse release commits explicit dock");
    const auto graphEq = dock.Relation(Eq), graphPl = dock.Relation(Pl);
    OffsetRect(&bounds[0], 37, 23);
    const auto moved = dock.Reflow(bounds, scale);
    Check(moved[index].left == original.left + 37 && moved[index].top == original.top + 23,
          "Redocked group follows Main anchor");
    Check(dock.Relation(Eq) == graphEq && dock.Relation(Pl) == graphPl, "Movement does not infer a new graph");
}
void ContinuousReturnAndMain(int scale) {
    for (auto id : {Eq, Pl}) {
        ClassicWindowDock dock;
        dock.InitializeStartup();
        auto bounds = Initial(scale);
        const auto index = static_cast<std::size_t>(id);
        const auto original = bounds[index];
        dock.BeginDrag(id, original, {0, 0});
        auto proposed = original;
        OffsetRect(&proposed, 1500 * scale, 1500 * scale);
        Check(!dock.MoveDrag(id, proposed, {0, 0}, bounds, Visible, scale), "Pull out of magnetic zone");
        bounds[index] = proposed;
        proposed = original;
        OffsetRect(&proposed, 3 * scale, 0);
        Check(dock.MoveDrag(id, proposed, {0, 0}, bounds, Visible, scale) && Same(proposed, original),
              "Same held drag snaps again after leaving the magnetic zone");
        bounds[index] = proposed;
        dock.EndDrag(id, proposed, bounds, scale);
        Check(dock.InMainGroup(id), "Same drag release restores group");

        dock.Disconnect(id);
        OffsetRect(&bounds[0], -1500 * scale, 0);
        bounds = dock.Reflow(bounds, scale);
        dock.BeginDrag(Main, bounds[0], {0, 0});
        proposed = Initial(scale)[0];
        OffsetRect(&proposed, -3 * scale, 0);
        Check(dock.MoveDrag(Main, proposed, {0, 0}, bounds, Visible, scale), "Main snaps to detached auxiliary");
        const int dx = proposed.left - bounds[0].left, dy = proposed.top - bounds[0].top;
        for (std::size_t member = 1; member < bounds.size(); ++member)
            if (dock.InMainGroup(static_cast<ClassicWindowId>(member)))
                OffsetRect(&bounds[member], dx, dy);
        bounds[0] = proposed;
        dock.EndDrag(Main, proposed, bounds, scale);
        Check(!dock.Relation(Main) && dock.InMainGroup(Eq) && dock.InMainGroup(Pl),
              "Main redock keeps Main as root and joins all three windows");
        Check(Same(dock.Reflow(bounds, scale)[index], bounds[index]), "Main redock reflow preserves contact");
        auto before = bounds;
        OffsetRect(&bounds[0], 37, 23);
        bounds = dock.Reflow(bounds, scale);
        for (std::size_t member = 1; member < bounds.size(); ++member) {
            OffsetRect(&before[member], 37, 23);
            Check(Same(bounds[member], before[member]), "Main redocked graph follows anchor");
        }
    }
}
void MainJoinsChain(int scale) {
    ClassicWindowDock dock;
    auto bounds = Initial(scale);
    bounds[2] = {bounds[1].left, bounds[1].bottom, bounds[1].right, bounds[1].bottom + 464 * scale};
    dock.BeginDrag(Pl, bounds[2], {0, 0});
    auto proposed = bounds[2];
    Check(dock.MoveDrag(Pl, proposed, {0, 0}, bounds, Visible, scale), "Independent Playlist docks to EQ");
    bounds[2] = proposed;
    dock.EndDrag(Pl, proposed, bounds, scale);
    Check(dock.Relation(Pl)->target == Eq, "Independent chain has EQ root");
    OffsetRect(&bounds[0], -1500 * scale, 0);
    dock.BeginDrag(Main, bounds[0], {0, 0});
    proposed = {bounds[2].left - 278 * scale, bounds[2].top + 40 * scale,
                bounds[2].left - 3 * scale, bounds[2].top + 156 * scale};
    Check(dock.MoveDrag(Main, proposed, {0, 0}, bounds, Visible, scale), "Main snaps to independent chain leaf");
    bounds[0] = proposed;
    dock.EndDrag(Main, proposed, bounds, scale);
    Check(dock.Relation(Pl)->target == Main && dock.Relation(Pl)->logicalOffset == -40 &&
              dock.Relation(Eq)->target == Pl && dock.InMainGroup(Eq),
          "Main reverses the chain and tangential offset without a cycle");
    const auto reflowed = dock.Reflow(bounds, scale);
    Check(Same(bounds[1], reflowed[1]) && Same(bounds[2], reflowed[2]),
          "Joining an independent chain preserves both auxiliary origins");
}
void Candidates(int scale) {
    for (auto id : {Eq, Pl}) {
        for (auto edge : {DockEdge::Top, DockEdge::Bottom, DockEdge::Left, DockEdge::Right}) {
            auto bounds = Initial(scale);
            OffsetRect(&bounds[1], 1600, 1600);
            OffsetRect(&bounds[2], 2600, 2600);
            ClassicWindowDock dock;
            const auto index = static_cast<std::size_t>(id);
            DockEdge sourceEdge = edge == DockEdge::Top      ? DockEdge::Bottom
                                  : edge == DockEdge::Bottom ? DockEdge::Top
                                  : edge == DockEdge::Left   ? DockEdge::Right
                                                             : DockEdge::Left;
            const ClassicDockRelation expected{id, Main, sourceEdge, edge, 0};
            const auto exact = ClassicWindowDock::Place(expected, bounds[index], bounds[0], scale);
            const bool vertical = edge == DockEdge::Top || edge == DockEdge::Bottom;
            const int snap = ClassicWindowDock::SnapThreshold * scale;
            for (auto distance : {snap + 1, snap}) {
                dock.BeginDrag(id, bounds[index], {0, 0});
                auto proposed = exact;
                OffsetRect(&proposed, vertical ? 0 : distance, vertical ? distance : 0);
                const auto before = proposed;
                const bool snapped = dock.MoveDrag(id, proposed, {0, 0}, bounds, Visible, scale);
                Check(snapped == (distance == snap), "Independent snap threshold is inclusive, scaled and bounded");
                if (snapped) {
                    Check(Same(proposed, exact), "All opposing edge candidates touch without changing dimensions");
                    bounds[index] = proposed;
                    dock.EndDrag(id, proposed, bounds, scale);
                    Check(dock.Relation(id) == expected, "Correct explicit source/target edges and alignment");
                    const auto topology = dock.Relation(id);
                    auto doubled = bounds;
                    const int nextScale = scale == 1 ? 2 : 1;
                    for (auto& rect : doubled) {
                        rect.right = rect.left + (rect.right - rect.left) / scale * nextScale;
                        rect.bottom = rect.top + (rect.bottom - rect.top) / scale * nextScale;
                    }
                    doubled = dock.Reflow(doubled, nextScale);
                    Check(Same(doubled[index],
                               ClassicWindowDock::Place(expected, doubled[index], doubled[0], nextScale)) &&
                              dock.Relation(id) == topology,
                          "All dock edges and topology survive scale changes");
                } else {
                    Check(Same(proposed, before), "Outside snap threshold is unchanged");
                    dock.EndDrag(id, proposed, bounds, scale);
                    Check(!dock.Relation(id), "No accidental dock after outside-threshold drag");
                }
            }
        }
    }
}
void ChainsAndOffsets(int scale) {
    ClassicWindowDock dock;
    dock.InitializeStartup();
    dock.Disconnect(Pl);
    auto bounds = Initial(scale);
    OffsetRect(&bounds[2], 1200, 1200);
    dock.BeginDrag(Pl, bounds[2], {0, 0});
    auto proposed = bounds[2];
    OffsetRect(&proposed, bounds[1].left - proposed.left, bounds[1].bottom - proposed.top + 4 * scale);
    Check(dock.MoveDrag(Pl, proposed, {0, 0}, bounds, Visible, scale), "Playlist can snap below Equalizer");
    bounds[2] = proposed;
    dock.EndDrag(Pl, proposed, bounds, scale);
    Check(dock.Relation(Pl)->target == Eq && dock.InMainGroup(Pl), "Indirect Main group membership");
    const auto eqBefore = bounds[1], plBefore = bounds[2];
    OffsetRect(&bounds[0], 43, 27);
    bounds = dock.Reflow(bounds, scale);
    Check(bounds[1].left == eqBefore.left + 43 && bounds[1].top == eqBefore.top + 27 &&
              bounds[2].left == plBefore.left + 43 && bounds[2].top == plBefore.top + 27,
          "Graph traversal moves indirect children by same delta");
    dock.BeginDrag(Eq, bounds[1], {0, 0});
    proposed = bounds[1];
    OffsetRect(&proposed, 30 * scale, 0);
    const auto stationaryMain = bounds[0], stationaryPl = bounds[2];
    dock.MoveDrag(Eq, proposed, {0, 0}, bounds, Visible, scale);
    Check(Same(bounds[0], stationaryMain) && Same(bounds[2], stationaryPl) && !dock.InMainGroup(Eq) &&
              !dock.Relation(Pl),
          "Dragging an intermediate auxiliary isolates it without pulling neighbors");
    dock.EndDrag(Eq, proposed, bounds, scale);

    dock.Disconnect(Pl);
    bounds = Initial(scale);
    OffsetRect(&bounds[1], 2000, 2000);
    OffsetRect(&bounds[2], 3000, 3000);
    dock.BeginDrag(Eq, bounds[1], {0, 0});
    proposed = {500 + 40 * scale, bounds[0].bottom + 3 * scale, 500 + 315 * scale, bounds[0].bottom + 119 * scale};
    Check(dock.MoveDrag(Eq, proposed, {0, 0}, bounds, Visible, scale), "Partial edge overlap with logical offset");
    bounds[1] = proposed;
    dock.EndDrag(Eq, proposed, bounds, scale);
    Check(dock.Relation(Eq)->logicalOffset == 40, "Tangential offset is stored in logical coordinates");
    dock.Disconnect(Eq);
    auto exactButIndependent = Initial(scale);
    const auto independent = dock.Reflow(exactButIndependent, scale);
    Check(!dock.InMainGroup(Eq) && !dock.InMainGroup(Pl) && Same(independent[1], exactButIndependent[1]),
          "Proximity and scale reflow never create docks");

    bounds = Initial(scale);
    bounds[1] = {500, 1000, 500 + 275 * scale, 1000 + 116 * scale};
    bounds[2] = {500, bounds[0].bottom + 8 * scale, 500 + 275 * scale, bounds[0].bottom + 124 * scale};
    dock.BeginDrag(Pl, bounds[2], {0, 0});
    proposed = bounds[2];
    Check(dock.MoveDrag(Pl, proposed, {0, 0}, bounds, Visible, scale), "Closest valid candidate available");
    bounds[2] = proposed;
    dock.EndDrag(Pl, proposed, bounds, scale);
    Check(dock.Relation(Pl)->target == Main, "Closest target wins");
    dock.Disconnect(Pl);
    auto competing = Initial(scale);
    competing[2] = {500, competing[0].bottom + 124 * scale, 500 + 275 * scale, competing[0].bottom + 588 * scale};
    competing[1] = {500, competing[0].bottom + 8 * scale, 500 + 275 * scale, competing[0].bottom + 124 * scale};
    dock.BeginDrag(Eq, competing[1], {0, 0});
    auto closest = competing[1];
    Check(dock.MoveDrag(Eq, closest, {0, 0}, competing, Visible, scale), "Multiple valid candidates available");
    competing[1] = closest;
    dock.EndDrag(Eq, closest, competing, scale);
    Check(dock.Relation(Eq)->target == Pl && dock.Relation(Eq)->targetEdge == DockEdge::Top,
          "Closest target wins among simultaneous Main and Playlist candidates");
    dock.Disconnect(Eq);
    dock.BeginDrag(Pl, bounds[2], {0, 0});
    proposed = bounds[2];
    Check(!dock.MoveDrag(Pl, proposed, {0, 0}, bounds, {false, false, true}, scale), "Hidden targets cannot attract");
    dock.EndDrag(Pl, proposed, bounds, scale);
    dock.BeginDrag(Pl, bounds[2], {0, 0});
    proposed = {bounds[0].right + 3 * scale, bounds[0].bottom + 3 * scale, bounds[0].right + 278 * scale,
                bounds[0].bottom + 119 * scale};
    Check(!dock.MoveDrag(Pl, proposed, {0, 0}, bounds, {true, false, true}, scale),
          "Diagonal nonoverlapping corners do not dock");
    dock.EndDrag(Pl, proposed, bounds, scale);
    auto blocked = Initial(scale);
    blocked[2] = blocked[1];
    OffsetRect(&blocked[1], 1500, 1500);
    dock.BeginDrag(Eq, blocked[1], {0, 0});
    proposed = blocked[2];
    OffsetRect(&proposed, 0, 2 * scale);
    Check(!dock.MoveDrag(Eq, proposed, {0, 0}, blocked, Visible, scale),
          "Candidate overlapping a third visible window is rejected");
    dock.EndDrag(Eq, proposed, blocked, scale);
    blocked[2] = Initial(scale)[2];
    dock.BeginDrag(Eq, blocked[1], {0, 0});
    proposed = Initial(scale)[1];
    OffsetRect(&proposed, 0, 2 * scale);
    Check(dock.MoveDrag(Eq, proposed, {0, 0}, blocked, Visible, scale), "Provisional snap candidate exists");
    OffsetRect(&proposed, 600, 600);
    Check(!dock.MoveDrag(Eq, proposed, {0, 0}, blocked, Visible, scale), "Dragging away clears provisional candidate");
    blocked[1] = proposed;
    dock.EndDrag(Eq, proposed, blocked, scale);
    Check(!dock.Relation(Eq), "Release away from a candidate does not commit its relationship");
}
}
int main() {
    try {
        for (int scale : {1, 2}) {
            DetachAndRedock(scale, Eq);
            DetachAndRedock(scale, Pl);
            Candidates(scale);
            ChainsAndOffsets(scale);
            ContinuousReturnAndMain(scale);
            MainJoinsChain(scale);
        }
        std::cout << "Explicit Classic dock graph, drag hysteresis, release/redock, candidates and scale passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
