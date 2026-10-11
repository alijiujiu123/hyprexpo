#include "WorkspaceContents.hpp"
#include "AppIcons.hpp"
#include "HyprexpoLogic.hpp"
#include "OverviewInternal.hpp"
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/state/GlobalWindowController.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/layout/space/Space.hpp>
#include <hyprland/src/layout/target/Target.hpp>
#include <hyprland/src/layout/algorithm/Algorithm.hpp>
#include <hyprland/src/layout/algorithm/tiled/scrolling/ScrollingAlgorithm.hpp>
#include <hyprland/src/layout/supplementary/WorkspaceAlgoMatcher.hpp>
#include <hyprland/src/config/shared/workspace/WorkspaceRuleManager.hpp>
#include <hyprland/src/config/shared/actions/ConfigActions.hpp>
#include <hyprland/src/state/WorkspaceState.hpp>
#include <hyprland/src/state/MonitorState.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Hyprexpo::Contents {
// Card reorder keeps a workspace's tiling (issue #22). A window moved the plain way is a fresh insertion
// that splits whatever node the pointer happens to be over, along that node's longer side, so who ends
// up beside whom - and in which direction - depends on the order and on the residents of the target.
// Instead each window's rectangle in its workspace is recorded (as a fraction of the work area), and the
// windows are re-inserted in tiling order: each one beside the already-placed window it shared the
// longest edge with, on the side it was on (dwindle's `preselect`), by pointing the insertion at that
// window. Dwindle's own tree is private, so this rebuilds an equivalent tree rather than copying it.

void carryLayout(std::vector<PHLWINDOW>& windows, const PHLWORKSPACE& workspace, SCarriedBoxes& boxes) {
    if (!workspace || !workspace->m_space)
        return;

    // A fullscreen window has no tiled box while it covers the area (and the core takes it out of
    // fullscreen to move it anyway): read the layout without it. The caller restores the modes.
    for (const auto& window : windows) {
        if (window && Fullscreen::controller()->isFullscreen(window))
            Fullscreen::controller()->setFullscreenMode(window, Fullscreen::FSMODE_NONE);
    }
    const auto                              AREA = workspace->m_space->workArea();
    std::vector<std::pair<PHLWINDOW, CBox>> tiled;
    for (const auto& window : windows) {
        const auto TARGET = window ? window->layoutTarget() : nullptr;
        if (!TARGET || TARGET->floating() || AREA.w <= 0 || AREA.h <= 0)
            continue;
        const auto LOCAL = workspace->m_space->targetPositionLocal(TARGET);
        tiled.emplace_back(window, CBox{LOCAL.x / AREA.w, LOCAL.y / AREA.h, LOCAL.w / AREA.w, LOCAL.h / AREA.h});
    }
    // Tiling order, top-left first: every window after the first has a neighbour that came before it.
    std::stable_sort(tiled.begin(), tiled.end(), [](const auto& a, const auto& b) {
        if (std::abs(a.second.y - b.second.y) > 0.01)
            return a.second.y < b.second.y;
        return a.second.x < b.second.x;
    });
    std::vector<PHLWINDOW> ordered;
    for (const auto& [window, box] : tiled) {
        boxes[window] = box;
        ordered.push_back(window);
    }
    for (const auto& window : windows) {
        if (!boxes.contains(window))
            ordered.push_back(window);
    }
    windows = ordered;
}

// Which placed window `box` sat beside, and on which side of it ('l' 'r' 'u' 'd').
std::pair<PHLWINDOW, char> carriedNeighbour(const CBox& box, const std::vector<PHLWINDOW>& placed, const SCarriedBoxes& boxes) {
    constexpr double EDGE = 0.02;
    PHLWINDOW        best;
    char             side  = 'r';
    double           score = 0.0;
    for (const auto& other : placed) {
        const auto IT = boxes.find(other);
        if (IT == boxes.end())
            continue;
        const auto&  O        = IT->second;
        const double OVERLAPY = std::min(box.y + box.h, O.y + O.h) - std::max(box.y, O.y);
        const double OVERLAPX = std::min(box.x + box.w, O.x + O.w) - std::max(box.x, O.x);
        char         found    = 0;
        double       length   = 0.0;
        if (OVERLAPY > EDGE && std::abs(box.x - (O.x + O.w)) < EDGE)
            found = 'r', length = OVERLAPY;
        else if (OVERLAPY > EDGE && std::abs(box.x + box.w - O.x) < EDGE)
            found = 'l', length = OVERLAPY;
        else if (OVERLAPX > EDGE && std::abs(box.y - (O.y + O.h)) < EDGE)
            found = 'd', length = OVERLAPX;
        else if (OVERLAPX > EDGE && std::abs(box.y + box.h - O.y) < EDGE)
            found = 'u', length = OVERLAPX;
        if (found && length > score - 1e-6) { // a tie goes to the later window: it is the one a split of the newer half belongs to
            best  = other;
            side  = found;
            score = length;
        }
    }
    return {best, side};
}

// moveWindowToWorkspace, but a tiled window enters the destination's tree where it stood. The core's own
// path ends in `assignToSpace(space)` without a focal point; this is the same call with one, plus the
// bookkeeping the core does around it. Floating, grouped and pinned windows keep the core's path.
void movePlacedInWorkspace(const PHLWINDOW& window, const PHLWORKSPACE& destination, const SCarriedBoxes& boxes, std::vector<PHLWINDOW>& placed) {
    const auto IT     = boxes.find(window);
    const auto TARGET = window ? window->layoutTarget() : nullptr;
    if (IT == boxes.end() || !TARGET || !destination || !destination->m_space || TARGET->floating() || window->m_group || window->m_pinned ||
        window->m_workspace == destination) {
        Desktop::globalWindowController()->moveWindowToWorkspace(window, destination);
        return;
    }

    const auto  SPACE = destination->m_space;
    const auto  AREA  = SPACE->workArea();
    const auto& BOX   = IT->second;
    Vector2D    focal = {AREA.x + (BOX.x + BOX.w / 2.0) * AREA.w, AREA.y + (BOX.y + BOX.h / 2.0) * AREA.h};
    bool        preselected = false;

    if (!placed.empty()) {
        const auto [NEIGHBOUR, SIDE] = carriedNeighbour(BOX, placed, boxes);
        if (NEIGHBOUR && NEIGHBOUR->layoutTarget() && NEIGHBOUR->layoutTarget()->space() == SPACE) {
            const auto NB = SPACE->targetPositionLocal(NEIGHBOUR->layoutTarget());
            focal         = {AREA.x + NB.x + NB.w / 2.0, AREA.y + NB.y + NB.h / 2.0};
            SPACE->layoutMsg(std::format("preselect {}", SIDE));
            preselected = true;
        }
    }

    const auto OLD = window->m_workspace;
    TARGET->assignToSpace(SPACE, focal);
    if (preselected)
        SPACE->layoutMsg("preselect reset"); // a window that did not take the direction must not leave it for the next
    placed.push_back(window);

    window->m_ruleApplicator->propertiesChanged(Desktop::Rule::RULE_PROP_ON_WORKSPACE);
    window->uncacheWindowDecos();
    destination->updateWindows();
    if (OLD)
        OLD->updateWindows();
    Desktop::globalWindowController()->updateSuspendedStates();
}


namespace {
struct SLayoutOverride {
    PHLWORKSPACEREF workspace;
    std::string layout;
    std::map<std::string, std::string> options;
    SP<Config::CWorkspaceRule> rule;
};
std::map<int64_t, SLayoutOverride> overrides;
CHyprSignalListener reloadListener;
CHyprSignalListener removedListener;

void installRule(int64_t id, SLayoutOverride& value) {
    if (value.rule) value.rule->setEnabled(false);
    Config::CWorkspaceRule rule;
    rule.m_workspaceString = std::to_string(id);
    rule.m_workspaceId = id;
    rule.m_layout = value.layout;
    rule.m_layoutopts = value.options;
    // Layout follows contents. Monitor/default/persistence rules still belong to the ID slot.
    value.rule = Config::workspaceRuleMgr()->add(std::move(rule));
}

void restoreScrolling(const Scrolling::SWorkspaceSnapshot& saved, const PHLWORKSPACE& workspace) {
    if (saved.columns.empty()) return;
    auto* algo = dynamic_cast<Layout::Tiled::CScrollingAlgorithm*>(workspace->m_space->algorithm()->tiledAlgo().get());
    if (!algo) throw std::runtime_error("destination did not retain scrolling layout");
    const auto seed = saved.columns.front().targets.front().targetRef.lock();
    const auto targetData = seed ? algo->dataFor(seed) : nullptr;
    const auto column = targetData ? targetData->column.lock() : nullptr;
    const auto data = column ? column->scrollingData.lock() : nullptr;
    if (!data || !data->controller) throw std::runtime_error("destination scrolling data expired");
    const auto oldColumns = data->columns;
    for (const auto& col : oldColumns) {
        const auto targets = col->targetDatas;
        for (const auto& target : targets)
            if (const auto live = target->target.lock()) col->remove(live);
    }
    for (size_t i = 0; i < saved.columns.size(); ++i) {
        const auto& old = saved.columns[i];
        const auto col = data->add(static_cast<int>(i) - 1, static_cast<float>(old.width));
        for (const auto& savedTarget : old.targets) {
            const auto target = savedTarget.targetRef.lock();
            if (!target || target->space() != workspace->m_space)
                throw std::runtime_error("scrolling target did not reach destination");
            col->add(target);
        }
        col->setColumnWidth(static_cast<float>(old.width));
        for (size_t j = 0; j < old.targets.size(); ++j)
            col->setTargetSize(j, old.targets[j].proportion);
    }
    using namespace Layout::Tiled;
    const auto direction = saved.direction == "left" ? SCROLL_DIR_LEFT : saved.direction == "up" ? SCROLL_DIR_UP :
                           saved.direction == "down" ? SCROLL_DIR_DOWN : SCROLL_DIR_RIGHT;
    data->controller->setDirection(direction);
    data->controller->setOffset(saved.offset);
    data->recalculate(true);
}
}

std::expected<SSnapshot, std::string> snapshot(const PHLWORKSPACE& workspace) {
    if (!workspace || workspace->inert() || !workspace->m_space || !workspace->m_space->algorithm())
        return std::unexpected("workspace has no live layout");
    SSnapshot saved;
    saved.workspace = workspace;
    saved.anchor = AppIcons::anchorWindow(workspace->m_id);
    saved.lastFocused = workspace->m_lastFocusedWindow.lock();
    saved.layout = Layout::Supplementary::algoMatcher()->getNameForTiledAlgo(workspace->m_space->algorithm()->tiledAlgo().get());
    if (const auto rule = Config::workspaceRuleMgr()->getWorkspaceRuleFor(workspace)) saved.layoutopts = rule->m_layoutopts;
    if (saved.layout != "dwindle" && saved.layout != "scrolling")
        return std::unexpected("workspace insertion supports dwindle and scrolling layouts");
    for (const auto& window : Desktop::windowState()->windows()) {
        // Include grouped/hidden members; pinned windows stay attached to their screen, as in the card reorder.
        if (!window || !window->m_isMapped || window->m_workspace != workspace || window->m_pinned) continue;
        saved.windows.push_back(window);
        saved.fullscreen.emplace_back(window, Fullscreen::controller()->getFullscreenModes(window));
    }
    if (saved.layout == "scrolling" && workspace->getWindowCount(true) > 0) {
        const auto native = Scrolling::snapshotWorkspace(workspace);
        if (!native.success()) return std::unexpected(native.error);
        saved.scrolling = native.snapshot;
    }
    // Read dwindle's underlying rectangles with fullscreen removed, then restore it before returning.
    carryLayout(saved.windows, workspace, saved.boxes);
    for (const auto& [window, modes] : saved.fullscreen)
        if (modes.internal != Fullscreen::FSMODE_NONE || modes.client != Fullscreen::FSMODE_NONE)
            Fullscreen::controller()->setFullscreenMode(window, modes.internal, modes.client);
    return saved;
}

void setLayout(const PHLWORKSPACE& workspace, const std::string& layout, const std::map<std::string, std::string>& options) {
    auto& value = overrides[workspace->m_id];
    value.workspace = workspace;
    value.layout = layout;
    value.options = options;
    installRule(workspace->m_id, value);
    Layout::Supplementary::algoMatcher()->updateWorkspaceLayouts();
}

void move(SSnapshot& saved, const PHLWORKSPACE& destination) {
    // A direct layout-target reassignment bypasses the controller's fullscreen bookkeeping.
    // Clear fullscreen on the source before detaching; restore the captured modes after arrival.
    for (const auto& window : saved.windows)
        if (window && window->m_isMapped && window->m_workspace != destination && Fullscreen::controller()->isFullscreen(window))
            Fullscreen::controller()->setFullscreenMode(window, Fullscreen::FSMODE_NONE);
    setLayout(destination, saved.layout, saved.layoutopts);
    std::vector<PHLWINDOW> placed;
    for (const auto& window : saved.windows) {
        if (!window || !window->m_isMapped || window->m_workspace == destination) continue;
        if (saved.scrolling) Desktop::globalWindowController()->moveWindowToWorkspace(window, destination);
        else movePlacedInWorkspace(window, destination, saved.boxes, placed);
        settleWorkspaceMoveAnimation(window);
    }
    AppIcons::setAnchorWindow(destination->m_id, saved.anchor);
    destination->m_lastFocusedWindow = saved.lastFocused;
}

void restore(const SSnapshot& saved, const PHLWORKSPACE& destination) {
    if (saved.scrolling) restoreScrolling(*saved.scrolling, destination);
    for (const auto& [window, modes] : saved.fullscreen) {
        if (!window || !window->m_isMapped) continue;
        Fullscreen::controller()->setFullscreenMode(window, modes.internal, modes.client);
        const auto target = window->layoutTarget();
        if (target && !target->floating()) target->warpPositionSize();
    }
}

std::expected<int64_t, std::string> insertAfterCurrent(bool moveWindow, const std::string& monitor) {
    if (!g_overviews.empty()) return std::unexpected("close the overview before creating a workspace");
    const auto mon = monitor.empty() ? Desktop::focusState()->monitor() : State::monitorState()->query().name(monitor).run();
    const auto current = mon ? mon->m_activeWorkspace : PHLWORKSPACE{};
    const auto selected = moveWindow ? Desktop::focusState()->window() : PHLWINDOW{};
    if (!current || current->m_isSpecialWorkspace || current->inert()) return std::unexpected("no ordinary current workspace");
    if (moveWindow && (!selected || selected->m_workspace != current || selected->m_pinned))
        return std::unexpected("no movable current window on this screen");
    std::vector<int64_t> slots, used;
    std::map<int64_t, PHLWORKSPACE> workspaces;
    for (const auto& ws : State::workspaceState()->workspacesCopy()) {
        if (!ws || ws->inert() || ws->m_isSpecialWorkspace || ws->m_id <= 0) continue;
        used.push_back(ws->m_id);
        if (ws->m_monitor != mon || (ws != current && ws->getWindowCount() == 0)) continue;
        slots.push_back(ws->m_id);
        workspaces[ws->m_id] = ws;
    }
    std::ranges::sort(slots);
    const auto plan = planWorkspaceInsertion(slots, used, current->m_id);
    if (!plan.valid) return std::unexpected("no free workspace ID for insertion");
    // All snapshots are validated before any content moves. Strong refs keep emptied slots alive until focus lands.
    std::map<int64_t, SSnapshot> snapshots;
    for (const auto& step : plan.moves) {
        auto saved = snapshot(workspaces.at(step.source));
        if (!saved) return std::unexpected(saved.error());
        snapshots.emplace(step.source, std::move(*saved));
    }
    const auto fresh = CWorkspace::create(plan.newID, mon, std::to_string(plan.newID), false, false);
    workspaces[plan.newID] = fresh;
    try {
        // Empty destinations, tail first: no resident window can steal another workspace's fullscreen mode.
        for (const auto& step : plan.moves) {
            auto& saved = snapshots.at(step.source);
            const auto destination = workspaces.at(step.destination);
            move(saved, destination);
            restore(saved, destination);
        }
        const auto destination = workspaces.at(plan.targetID);
        {
            // The inserted workspace inherits the current layout; displaced contents keep their own.
            const auto owner = current->m_space->algorithm();
            const auto layout = Layout::Supplementary::algoMatcher()->getNameForTiledAlgo(owner->tiledAlgo().get());
            const auto rule = Config::workspaceRuleMgr()->getWorkspaceRuleFor(current);
            setLayout(destination, layout, rule ? rule->m_layoutopts : std::map<std::string, std::string>{});
        }
        if (moveWindow) {
            const auto modes = Fullscreen::controller()->getFullscreenModes(selected);
            Desktop::globalWindowController()->moveWindowToWorkspace(selected, destination);
            settleWorkspaceMoveAnimation(selected);
            Fullscreen::controller()->setFullscreenMode(selected, modes.internal, modes.client);
            AppIcons::setAnchorWindow(destination->m_id, selected);
        }
        if (Desktop::focusState()->monitor() != mon) Desktop::focusState()->rawMonitorFocus(mon);
        const auto focused = Config::Actions::changeWorkspaceOnCurrentMonitor(destination);
        if (!focused) throw std::runtime_error(focused.error().message);
        if (selected) Desktop::focusState()->fullWindowFocus(selected, Desktop::FOCUS_REASON_KEYBIND);
        return plan.targetID;
    } catch (const std::exception& error) {
        const auto message = std::string(error.what());
        // Restore original memberships and layouts on failure; no partial insertion is reported as success.
        try {
            if (selected && selected->m_workspace != current)
                Desktop::globalWindowController()->moveWindowToWorkspace(selected, current);
            for (auto it = plan.moves.rbegin(); it != plan.moves.rend(); ++it) {
                auto& saved = snapshots.at(it->source);
                move(saved, workspaces.at(it->source));
                restore(saved, workspaces.at(it->source));
            }
            (void)Config::Actions::changeWorkspaceOnCurrentMonitor(current);
        } catch (const std::exception& rollback) {
            return std::unexpected(message + "; rollback failed: " + rollback.what());
        }
        return std::unexpected(message + "; original contents restored");
    }
}

void init() {
    reloadListener = Event::bus()->m_events.config.reloaded.listen([] {
        for (auto it = overrides.begin(); it != overrides.end();) {
            const auto workspace = it->second.workspace.lock();
            if (!workspace || workspace->inert()) { it = overrides.erase(it); continue; }
            installRule(it->first, it->second);
            ++it;
        }
    });
    removedListener = Event::bus()->m_events.workspace.removed.listen([](PHLWORKSPACEREF ws) {
        for (auto it = overrides.begin(); it != overrides.end();) {
            if (!it->second.workspace || it->second.workspace == ws) {
                if (it->second.rule) it->second.rule->setEnabled(false);
                it = overrides.erase(it);
            } else ++it;
        }
    });
}
void shutdown() {
    reloadListener.reset();
    removedListener.reset();
    // The rules contain host data, not plugin callbacks. Keep the session's layout on unload.
    overrides.clear();
}
}
