#pragma once

#include <cstddef>

namespace HyprexpoConfig {
inline constexpr int         LEGACY_DYNAMIC_GRID_DEFAULT         = 0;
inline constexpr int         LEGACY_FILL_GAPS_DEFAULT            = 0;
inline constexpr int         LEGACY_MRU_SORT_DEFAULT             = 0;
inline constexpr unsigned    LEGACY_ACTIVE_HIGHLIGHT_COL_DEFAULT = 0xFF3584E4;
inline constexpr int         LEGACY_ACTIVE_HIGHLIGHT_BORDER_DEFAULT = 2;
inline constexpr unsigned    LEGACY_HOVER_HIGHLIGHT_COL_DEFAULT  = 0x80FFFFFF;
inline constexpr int         LEGACY_HOVER_HIGHLIGHT_BORDER_DEFAULT = 2;
inline constexpr const char* LEGACY_LABEL_POS_DEFAULT            = "top_right";
inline constexpr int         LEGACY_LABEL_SIZE_DEFAULT           = 36;
inline constexpr unsigned    LEGACY_LABEL_COL_DEFAULT            = 0xFFFFFFFF;
inline constexpr int         LEGACY_SHOW_WORKSPACE_NAMES_DEFAULT = 0;
inline constexpr int         LEGACY_ENABLE_KEYBOARD_NAV_DEFAULT  = 1;
inline constexpr int         LEGACY_ENABLE_DRAG_MOVE_DEFAULT     = 0;
inline constexpr int         LEGACY_ANIMATE_ENTRY_DEFAULT        = 0;
inline constexpr int         WALLPAPER_BG_DEFAULT                = 0;
inline constexpr std::size_t DYNAMIC_GRID_MAX_TILES              = 64;

inline constexpr int         COLUMNS_DEFAULT                 = 3;
inline constexpr int         ROWS_DEFAULT                    = 0;
inline constexpr int         COLUMNS_MIN                     = 1;
inline constexpr int         COLUMNS_MAX                     = 7;
inline constexpr int         GAPS_IN_DEFAULT                 = 5;
inline constexpr int         GAPS_OUT_DEFAULT                = 0;
inline constexpr unsigned    BG_COL_DEFAULT                  = 0xFF111111;
inline constexpr const char* WORKSPACE_METHOD_DEFAULT        = "center current";
inline constexpr int         SKIP_EMPTY_DEFAULT              = 0;
inline constexpr int         MAX_WORKSPACE_DEFAULT           = 0;
inline constexpr int         SHOW_WORKSPACE_NUMBERS_DEFAULT  = 0;
inline constexpr unsigned    WORKSPACE_NUMBER_COLOR_DEFAULT  = 0xFFFFFFFF;
inline constexpr int         GESTURE_DISTANCE_DEFAULT        = 200;
// Release momentum: the swipe delta a released finger would still cover is v^2 / (2a),
// so the deceleration `a` sets how brisk a flick has to be before it commits the overview
// on its own. Units are gesture-delta units per second^2 — the delta is the registered
// gesture `scale` times the raw libinput travel, so with scale 0.4 and
// gesture_distance 200 the values below mean:
//   800  only a hard flick (~2000 raw units/s) commits without travel
//   2000 a deliberate swipe (~1600 raw units/s) commits without travel
//   4000 the 50 % distance rule dominates; flicks only help when they are fast
// 0 disables the momentum term entirely (the commit test becomes pure position again).
inline constexpr int         MOMENTUM_DECEL_DEFAULT          = 2000;
// Time constant of the release-velocity estimate, in ms: longer follows the whole swipe,
// shorter only its tail (and lets a flick that stops dead before release read as slow).
inline constexpr int         MOMENTUM_WINDOW_MS_DEFAULT      = 80;
// Log one HYPREXPO_SWIPE_RELEASE line per gesture release (delta / velocity / projection),
// so the momentum feel can be calibrated from numbers instead of guesswork.
inline constexpr int         MOMENTUM_DEBUG_DEFAULT          = 0;
// Overview open/close animation duration in 100 ms steps (0 = inherit the compositor's
// `windowsMove` leaf, which also animates window moves).
inline constexpr int         OVERVIEW_ANIM_SPEED_DEFAULT     = 0;
// Recapture a tile when its workspace produced surface damage while the overview is open.
// Hyprland renders only the visible workspace, so a hidden workspace's clients commit
// exactly when they have something new to show (its commits carry damage or they are not
// commits at all: a commit without damage leaves SSurfaceState::updateState clearing
// damage, see `CWindow::commitWindow`). Recapturing only those tiles keeps the workspaces
// that are actually moving live at a bounded cost. 0 restores the old behaviour, where
// only the opened workspace's tile is refreshed.
inline constexpr int         DIRTY_REFRESH_DEFAULT        = 1;
// Minimum interval between two recaptures of the same tile, in ms. 33 keeps a moving tile
// at roughly 30 updated frames per second; the client's own commit rate is the ceiling, so
// going far below that mostly repeats frames it has not replaced yet.
inline constexpr int         DIRTY_COOLDOWN_MS_DEFAULT    = 33;
// Upper bound on how many changed tiles are recaptured within one frame.
inline constexpr int         DIRTY_MAX_PER_FRAME_DEFAULT  = 2;
// Upper bound on how many recaptures of changed tiles run within one second, shared by every
// tile. Measured on a 2880x1800 screen (Radeon 780M, full-screen windows in the tile): ~1.8 ms
// of GPU per recapture is pixel work and ~1.7 ms is the compositor frame the recapture itself
// forces, so one tile at the full ~128/s costs roughly +20 GPU points. This cap, not the
// cooldown, is what bounds "many workspaces moving at once". 0 disables it.
inline constexpr int         DIRTY_MAX_PER_SECOND_DEFAULT = 60;
// Hand wl_surface.frame callbacks to the workspaces shown in the grid, at the tile refresh
// cadence. Hyprland only sends them to the visible workspace: a client already drawing on its
// own clock keeps going without them, but one that was started while hidden (a player opened
// on a background workspace, a page that paused) waits for a callback that never comes and
// freezes on its first frame. Measured: such a player starts producing frames again as soon as
// the grid drives it. Cost: those clients really do render at that cadence.
// On by default: draws the workspace label of every tile that is being recaptured in red, and
// logs a per-second HYPREXPO_DIRTY line (commits/recaptures/pending/per workspace) so a tile
// that keeps being rendered while nothing in it moves is visible and countable.
inline constexpr int         DIRTY_DEBUG_DEFAULT          = 1;
inline constexpr int         GESTURE_FINGERS_DEFAULT         = 0;
inline constexpr const char* GESTURE_DIRECTION_DEFAULT       = "up";
// What the config-registered swipe *does*: `expo` opens the overview (and, when one is open,
// switches to the hovered workspace), `cancel` closes it without selecting, `commit` only ever
// acts on an open overview and switches to the hovered workspace — the semantics a swipe in the
// closing direction wants, since it must not summon an overview it is asking to dismiss.
inline constexpr const char* GESTURE_ACTION_DEFAULT          = "expo";
// Minimum accumulated finger travel (gesture delta, i.e. raw delta x scale) before a `commit`
// swipe is allowed to switch at all. 0 keeps the historical behavior, where the release
// momentum alone can commit a flick that never travelled: with momentum_decel > 0 a brisk
// three-finger brush in the closing direction switches workspaces without the user dragging.
inline constexpr int         COMMIT_MIN_TRAVEL_DEFAULT       = 0;
inline constexpr const char* CANCEL_KEY_DEFAULT              = "escape";
inline constexpr int         SHOW_CURSOR_DEFAULT             = 1;
inline constexpr int         SHOW_PINNED_WINDOWS_DEFAULT     = 0;
inline constexpr int         DRAG_DROP_ENABLE_DEFAULT        = 1;
inline constexpr int         SCROLLING_THUMBNAIL_BUDGET_DEFAULT = 4;
inline constexpr int         SCROLLING_THUMBNAIL_BUDGET_MIN     = 1;
inline constexpr int         SCROLLING_THUMBNAIL_BUDGET_MAX     = 16;
inline constexpr int         SCROLLING_INPUT_DEBUG_DEFAULT      = 0;
inline constexpr int         KEYNAV_ENABLE_DEFAULT           = 1;
inline constexpr const char* NUMBER_KEY_MODE_DEFAULT         = "workspace";
inline constexpr int         KEYNAV_WRAP_H_DEFAULT           = 1;
inline constexpr int         KEYNAV_WRAP_V_DEFAULT           = 1;
inline constexpr int         KEYNAV_READING_ORDER_DEFAULT    = 0;
inline constexpr int         BORDER_WIDTH_DEFAULT            = 2;
inline constexpr const char* BORDER_COLOR_DEFAULT            = "";
inline constexpr const char* BORDER_COLOR_CURRENT_DEFAULT    = "rgb(66ccff)";
inline constexpr const char* BORDER_COLOR_FOCUS_DEFAULT      = "rgb(ffcc66)";
inline constexpr const char* BORDER_COLOR_HOVER_DEFAULT      = "rgb(aabbcc)";
inline constexpr const char* BORDER_STYLE_DEFAULT            = "simple";
inline constexpr const char* BORDER_GRAD_CURRENT_DEFAULT     = "";
inline constexpr const char* BORDER_GRAD_FOCUS_DEFAULT       = "";
inline constexpr const char* BORDER_GRAD_HOVER_DEFAULT       = "";
inline constexpr unsigned    DRAG_DROP_PROXY_COLOR_DEFAULT        = 0x24EDB342;
inline constexpr unsigned    DRAG_DROP_PROXY_ACTIVE_COLOR_DEFAULT = 0x3DEDB342;
inline constexpr const char* DRAG_DROP_PROXY_BORDER_COLOR_DEFAULT = "";
inline constexpr int         DRAG_DROP_PROXY_BORDER_WIDTH_DEFAULT = -1;
inline constexpr int         DRAG_DROP_PROXY_ROUNDING_DEFAULT     = -1;
inline constexpr const char* DRAG_DROP_SOURCE_BORDER_COLOR_DEFAULT = "";
inline constexpr int         DRAG_DROP_SOURCE_BORDER_WIDTH_DEFAULT = -1;
inline constexpr int         LABEL_ENABLE_DEFAULT            = 1;
inline constexpr unsigned    LABEL_COLOR_DEFAULT_LEGACY      = 0xFFFFFFFF;
inline constexpr int         LABEL_FONT_SIZE_DEFAULT         = 16;
inline constexpr const char* LABEL_TEXT_MODE_DEFAULT         = "token";
inline constexpr const char* LABEL_TOKEN_MAP_DEFAULT         = "";
inline constexpr const char* LABEL_POSITION_DEFAULT          = "center";
inline constexpr int         LABEL_OFFSET_X_DEFAULT          = 0;
inline constexpr int         LABEL_OFFSET_Y_DEFAULT          = 0;
inline constexpr const char* LABEL_SHOW_DEFAULT              = "always";
inline constexpr unsigned    LABEL_COLOR_DEFAULT             = 0xFFFFFFFF;
inline constexpr unsigned    LABEL_COLOR_HOVER_DEFAULT       = 0xFFEEEEEE;
inline constexpr unsigned    LABEL_COLOR_FOCUS_DEFAULT       = 0xFFFFCC66;
inline constexpr unsigned    LABEL_COLOR_CURRENT_DEFAULT     = 0xFF66CCFF;
inline constexpr float       LABEL_SCALE_HOVER_DEFAULT       = 1.0F;
inline constexpr float       LABEL_SCALE_FOCUS_DEFAULT       = 1.0F;
inline constexpr int         LABEL_BG_ENABLE_DEFAULT         = 1;
inline constexpr unsigned    LABEL_BG_COLOR_DEFAULT          = 0x88000000;
inline constexpr int         LABEL_BG_ROUNDING_DEFAULT       = 8;
inline constexpr const char* LABEL_BG_SHAPE_DEFAULT          = "circle";
inline constexpr int         LABEL_PADDING_DEFAULT           = 8;
inline constexpr const char* LABEL_FONT_FAMILY_DEFAULT       = "sans";
inline constexpr int         LABEL_FONT_BOLD_DEFAULT         = 0;
inline constexpr int         LABEL_FONT_ITALIC_DEFAULT       = 0;
inline constexpr int         LABEL_TEXT_UNDERLINE_DEFAULT    = 0;
inline constexpr int         LABEL_TEXT_STRIKETHROUGH_DEFAULT = 0;
inline constexpr int         LABEL_PIXEL_SNAP_DEFAULT        = 1;
inline constexpr int         LABEL_CENTER_ADJUST_X_DEFAULT   = 0;
inline constexpr int         LABEL_CENTER_ADJUST_Y_DEFAULT   = 0;
inline constexpr int         TILE_ROUNDING_DEFAULT           = 0;
inline constexpr float       TILE_ROUNDING_POWER_DEFAULT     = 2.0F;
inline constexpr int         TILE_ROUNDING_FOCUS_DEFAULT     = -1;
inline constexpr int         TILE_ROUNDING_CURRENT_DEFAULT   = -1;
inline constexpr int         TILE_ROUNDING_HOVER_DEFAULT     = -1;
inline constexpr int         SELECTION_LABEL_ENABLE_DEFAULT  = 0;
inline constexpr const char* SELECTION_LABEL_TOKEN_MAP_DEFAULT = "a,s,d,f,g,q,w,e,r,t,z,x,c,v,b";
inline constexpr const char* SELECTION_LABEL_POSITION_DEFAULT  = "top-right";
inline constexpr int         SELECTION_LABEL_OFFSET_X_DEFAULT  = 6;
inline constexpr int         SELECTION_LABEL_OFFSET_Y_DEFAULT  = 6;
inline constexpr unsigned    SELECTION_LABEL_COLOR_DEFAULT     = 0xFFFFCC66;
}
