#pragma once

#include "ScrollingLayoutAdapter.hpp"
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <expected>
#include <map>
#include <string>
#include <vector>

namespace Hyprexpo::Contents {
using SCarriedBoxes = std::map<PHLWINDOW, CBox>;
struct SSnapshot {
    PHLWORKSPACE workspace;
    std::vector<PHLWINDOW> windows;
    SCarriedBoxes boxes;
    std::vector<std::pair<PHLWINDOW, Fullscreen::SFullscreenMode>> fullscreen;
    PHLWINDOW anchor;
    PHLWINDOW lastFocused;
    std::string layout;
    std::map<std::string, std::string> layoutopts;
    std::optional<Scrolling::SWorkspaceSnapshot> scrolling;
};
// Shared by card reorder and insertion. Snapshot before moving any participant.
void carryLayout(std::vector<PHLWINDOW>& windows, const PHLWORKSPACE& workspace, SCarriedBoxes& boxes);
void movePlacedInWorkspace(const PHLWINDOW& window, const PHLWORKSPACE& destination, const SCarriedBoxes& boxes, std::vector<PHLWINDOW>& placed);
std::expected<SSnapshot, std::string> snapshot(const PHLWORKSPACE& workspace);
void setLayout(const PHLWORKSPACE& workspace, const std::string& layout, const std::map<std::string, std::string>& options);
void move(SSnapshot& snapshot, const PHLWORKSPACE& destination);
void restore(const SSnapshot& snapshot, const PHLWORKSPACE& destination);
std::expected<int64_t, std::string> insertAfterCurrent(bool moveWindow, const std::string& monitor = {});
void init();
void shutdown();
}
