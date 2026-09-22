#include "IOverviewSession.hpp"

#include "Overview.hpp"
#include "ScrollingLayoutAdapter.hpp"
#include "ScrollingOverview.hpp"
#include "globals.hpp"

#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include <atomic>
#include <exception>
#include <format>

namespace {

void notifyScrollingFailure(const std::string& message) {
    Log::logger->log(Log::ERR, "[hyprexpo] {}", message);
    HyprlandAPI::addNotification(PHANDLE, "[hyprexpo] " + message, CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
}

}

std::unique_ptr<IOverviewSession> createOverviewSession(const PHLWORKSPACE& startedOn, const PHLMONITOR& monitor, bool swipe) {
    if (!startedOn || !monitor || startedOn->m_monitor != monitor)
        return nullptr;
    static std::atomic<uint64_t> nextGeneration = 1;
    const uint64_t generation = nextGeneration.fetch_add(1, std::memory_order_relaxed);

    // The *scrolling* overview is not used at all any more (2026-09-22).
    //
    // A workspace whose tiled algorithm is `scrolling` used to get that session instead of the grid. On
    // this machine it renders nothing the user can see, so three-finger-up on such a workspace (eDP-1's
    // fourth slot, id 10, is one) looked like the gesture doing nothing at all — the long-standing
    // "some workspaces cannot be swiped up into" report. The grid shows a scrolling workspace's windows
    // perfectly well, so it is the only overview now.
    //
    // A config key was tried for this and crashed the compositor on every swipe: `getConfigValue` on a
    // key that is not registered returns null, and dereferencing it aborts. Nothing here reads config.
    const bool detectedScrolling = false;
    if (detectedScrolling) {
        const auto snapshot = Hyprexpo::Scrolling::snapshotWorkspace(startedOn);
        const bool emptyScrolling = startedOn && startedOn->getWindowCount() <= 0 && snapshot.failure == Hyprexpo::Scrolling::ESnapshotFailure::MissingScrollingData;
        if (!snapshot.success() && !emptyScrolling) {
            notifyScrollingFailure(std::format("native scrolling snapshot failed ({}): {}", snapshotFailureName(snapshot.failure), snapshot.error));
            return nullptr;
        }
        try {
            auto scrolling = std::make_unique<CScrollingOverview>(startedOn, monitor, swipe, generation, snapshot.snapshot);
            if (scrolling->valid())
                return scrolling;
            notifyScrollingFailure("native scrolling session initialization failed");
        } catch (const std::exception& error) {
            notifyScrollingFailure(std::format("native scrolling session threw during initialization: {}", error.what()));
        } catch (...) {
            notifyScrollingFailure("native scrolling session threw an unknown exception");
        }
        return nullptr;
    }

    return std::make_unique<COverview>(startedOn, monitor, swipe, generation);
}
