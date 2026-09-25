#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Hyprexpo {

struct SColorRGBA {
    float r = 0.F;
    float g = 0.F;
    float b = 0.F;
    float a = 1.F;
};

struct SGradientSpec {
    SColorRGBA c1;
    SColorRGBA c2;
    float      angleDeg = 0.F;
    bool       valid    = false;
};

enum class EWorkspaceMethodMode {
    Center,
    First,
};

enum class ENumberKeyMode {
    Workspace,
    Index,
    Passthrough,
};

// Semantics one registered swipe can carry. Accepted by the Lua `gesture` helper as `action`
// and by the `gesture_action` config key, both parsed through parseGestureAction().
enum class EGestureAction {
    // Opens the overview; with one already open it selects the hovered workspace and
    // switches to it.
    Expo,
    // Closes interactively without selecting anything.
    Cancel,
    // Never opens an overview: with one open it selects the hovered workspace and switches
    // to it, and with none it is inert. This is what a swipe in the closing direction wants
    // -- `expo` there would summon an overview the user was swiping to dismiss.
    Commit,
};

struct SWorkspaceMethodSpec {
    bool                 valid = false;
    EWorkspaceMethodMode mode  = EWorkspaceMethodMode::Center;
    std::string          workspace;
    std::string          error;
};

// Result of stripping an "all monitors" qualifier off an expo dispatcher arg.
struct SExpoCommand {
    std::string command;            // the arg with the qualifier removed
    bool        allMonitors = false;
};

// Split "toggle all", "on all" or a bare "all" (which means "toggle all") into
// the underlying command plus the all-monitors flag. Args without the
// qualifier come back unchanged with allMonitors = false.
SExpoCommand parseExpoCommand(const std::string& arg);

struct SPoint {
    double x = 0.0;
    double y = 0.0;
};

struct SSize {
    double w = 0.0;
    double h = 0.0;
};

struct SRect {
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;
};

enum class EDirection {
    Left,
    Right,
    Up,
    Down,
};

struct SGlobalTile {
    uint64_t overviewKey = 0;
    int      tileIndex   = -1;
    SRect    overviewGlobal;
    SRect    tileGlobal;
};

struct STileTarget {
    uint64_t overviewKey = 0;
    int      tileIndex   = -1;
};

struct STileHit {
    uint64_t overviewKey = 0;
    int      tileIndex   = -1;
    SPoint   pointLocal;
};

enum class EOverviewDragEventType {
    Press,
    Move,
    Target,
    Release,
    Cancel,
    MonitorDestroyed,
    AllClose,
};

struct SOverviewDragState {
    bool                  active             = false;
    bool                  moved              = false;
    uint64_t              sourceMonitorKey   = 0;
    int                   sourceTileIndex    = -1;
    uint64_t              targetMonitorKey   = 0;
    int                   targetTileIndex    = -1;
    uint64_t              windowKey          = 0;
    std::vector<uint64_t> affectedMonitorKeys;
};

struct SOverviewDragEvent {
    EOverviewDragEventType type       = EOverviewDragEventType::Move;
    uint64_t               monitorKey = 0;
    int                    tileIndex  = -1;
    uint64_t               windowKey  = 0;
};

struct SOverviewDropIntent {
    uint64_t sourceMonitorKey = 0;
    int      sourceTileIndex  = -1;
    uint64_t targetMonitorKey = 0;
    int      targetTileIndex  = -1;
    uint64_t windowKey        = 0;
};

struct SOverviewDragTransition {
    SOverviewDragState                next;
    std::optional<SOverviewDropIntent> drop;
    std::vector<uint64_t>             cleanupMonitorKeys;
    bool                              accepted = false;
    bool                              cleanup  = false;
};

struct SGridShape {
    int cols = 1;
    int rows = 1;
};

struct STileLayout {
    SRect box;
    int   row = -1;
    int   col = -1;
};

struct SDropIntentInput {
    bool   targetValid     = false;
    SPoint pointerLocal    = {};
    SRect  targetTileLocal = {};
    SSize  workspaceSize   = {};
    SSize  windowSize      = {};
    SPoint grabOffset      = {};
    double minProxySize    = 24.0;
};

struct SDropIntentGeometry {
    bool   valid               = false;
    SPoint targetWorkspacePoint = {};
    SRect  targetProxyLocal     = {};
};

struct SGestureConfig {
    int         fingers = 0;
    std::string direction;
    bool        directionValid = false;
    // Already parsed by the caller so the error can name the string that failed; the default
    // matches the `gesture_action` default, which is what an unset config produces.
    std::optional<EGestureAction> action = EGestureAction::Expo;
    std::string                   actionRaw = "expo";
};

struct SGestureSyncDecision {
    bool        registerGesture = false;
    std::string error;
};

std::string trimString(std::string value);
std::string lowerString(std::string value);
std::vector<std::string> splitCommaList(const std::string& value);

SGridShape               computeDynamicGridShape(int visibleCount);
SGridShape               computeFixedGridShape(int64_t columns, int64_t rows);
std::optional<std::vector<int64_t>> expandDynamicWorkspaceIDs(const std::vector<int64_t>& workspaceIDs, bool fillGaps, std::size_t maxExpandedWorkspaces);
SSize                    aspectCorrectTileSize(double screenW, double screenH, int cols, int rows, double gap);
STileLayout              computeTileLayout(int index, int visibleCount, SGridShape shape, SSize total, double gap, bool centerPartialRows);
int                      tileIndexAtPoint(double x, double y, int visibleCount, SGridShape shape, SSize total, double gap, bool centerPartialRows);
std::optional<STileTarget> selectDirectionalTile(const SRect& source, EDirection direction, const std::vector<SGlobalTile>& candidates);
std::optional<STileHit>    hitTestGlobalTile(const SPoint& point, const std::vector<SGlobalTile>& tiles);
SOverviewDragTransition    transitionOverviewDrag(const SOverviewDragState& state, const SOverviewDragEvent& event, const std::vector<uint64_t>& liveMonitorKeys);

int                      clampGridColumns(int64_t columns);
int                      gridColumnsToIncludeWorkspace(int configuredColumns, int firstWorkspaceID, int activeWorkspaceID, int maxColumns, int fixedRows = 0);
std::size_t              centeredWorkspaceBacktrack(std::size_t tileCount, int64_t activeWorkspaceID, std::optional<int64_t> lowestExistingID,
                                                    std::optional<int64_t> highestExistingID);
int                      tileIndexFromPoint(double x, double y, double width, double height, int sideLength);
int                      numberKeyToVisibleIndex(int number);
ENumberKeyMode           numberKeyModeFromString(const std::string& mode);
bool                     shouldAbortOverviewCloseForWorkspaceMove(bool windowPinned, bool movedOnOverviewMonitor);
SDropIntentGeometry      computeDropIntentGeometry(const SDropIntentInput& input);

SGestureSyncDecision     evaluateGestureSync(const SGestureConfig& config);
// Action names are compared exactly, never normalized: the config value and the Lua argument
// both surface verbatim in the error, so a typo cannot silently pick a different semantic.
std::optional<EGestureAction> parseGestureAction(std::string_view action);

std::string              decodeConfigString(const void* dataptr, bool underlyingIsStdString, const std::string& fallback);

std::string              fallbackTokenForVisibleIndex(int visibleIndex);
int                      fallbackTokenToVisibleIndex(const std::string& token);

bool                     parseHexRGBA8(const std::string& value, SColorRGBA& out);
bool                     parseSolidColorSpec(const std::string& value, SColorRGBA& out);
SGradientSpec            parseGradientSpec(const std::string& value);
bool                     isGradientBorderSpec(const std::string& value);
bool                     shouldShowWorkspaceLabel(bool labelEnabled, const std::string& labelShow, bool isHovered, bool isFocused, bool isCurrent);
std::string              resolveBorderSpec(const std::string& modernSpec, const std::string& legacySpec);
std::string              resolveLabelPosition(const std::string& modernValue, bool modernSetByUser, const std::string& legacyValue, bool legacySetByUser);
int                      resolveLabelFontSize(int modernValue, bool modernSetByUser, int legacyValue, bool legacySetByUser);

// One mapped window of a workspace, as the app-icon badge sees it. `id` is the compositor's
// stable id, which grows with every window created, so a smaller id is an older window.
struct SPrimaryWindowCandidate {
    uint64_t id      = 0;
    double   area    = 0.0;
    bool     visible = true;
};

// The window whose app a workspace card shows: the workspace's first-opened window (`anchor`)
// while it is still there, otherwise the one with the largest on-screen area (older wins a tie;
// hidden windows only when nothing is visible). 0 = the workspace has no window.
uint64_t                 choosePrimaryWindow(std::optional<uint64_t> anchor, const std::vector<SPrimaryWindowCandidate>& candidates);

// Dragging card `from` onto slot `to` reorders the cards: the dragged card's contents land in slot
// `to` and every card in between shifts one slot toward `from`. Slots are workspaces, whose ids never
// change - their *contents* (windows) move. Returns the moves in execution order as
// {source slot, destination slot}, one per slot whose contents change. The first move (the dragged
// contents into `to`) is the only one whose destination is still occupied when it runs; every later
// destination has just been vacated by the move before it. Empty for from == to or out of range.
struct SSlotMove {
    size_t source      = 0;
    size_t destination = 0;
};
std::vector<SSlotMove>   planCardReorder(size_t count, size_t from, size_t to);

// The Wayland app id Chromium gives a `--app=<url>` window (Omarchy's web apps):
// "https://x.com/" -> "chrome-x.com__-Default". Empty when the text holds no http(s) URL.
std::string              webAppClassFromUrl(std::string_view url);

SWorkspaceMethodSpec     parseWorkspaceMethodSpec(const std::string& method);
SWorkspaceMethodSpec     resolveWorkspaceMethodForMonitor(const std::string& config, const std::string& monitorName);

}
