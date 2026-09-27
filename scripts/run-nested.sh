#!/usr/bin/env bash
set -euo pipefail

# Launch a nested Hyprland session that loads the local hyprexpo.so,
# so you can test changes without restarting your main session.
#
# Parallel, background sandboxes: HYPREXPO_DEV_INSTANCE=<name> gives the session its own
# build, config and runtime directory, so any number can run side by side without sharing
# a .so (rebuilding one another's mapped plugin would crash them) or a plugin log. A named
# instance is headless by default: it renders to its own headless output and never opens a
# window on the host, so it takes no focus, no input and no screen space from whoever is
# using the machine. scripts/nested-ctl.sh starts, drives, screenshots and stops them.
#
#   HYPREXPO_DEV_INSTANCE=<name>   state in $XDG_CACHE_HOME/hyprexpo/nested/<name>,
#                                  runtime (sockets, plugin logs) in
#                                  $XDG_RUNTIME_DIR/hyprexpo-nested/<name>
#   HYPREXPO_DEV_HEADLESS=0|1      1 = no host window (default for a named instance)
#   HYPREXPO_DEV_NICE=<n>          CPU niceness of build + compositor (named default 10)
#
# Unnamed, the paths and the host window are what they always were (the validators rely on
# them); a second unnamed run with the same XDG_CACHE_HOME is refused instead of rebuilding
# the .so the first one has mapped.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CACHE_ROOT="${XDG_CACHE_HOME:-$HOME/.cache}"
HOST_RUNTIME="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
INSTANCE_NAME="${HYPREXPO_DEV_INSTANCE:-}"
if [[ -n "$INSTANCE_NAME" ]]; then
  if [[ ! "$INSTANCE_NAME" =~ ^[A-Za-z0-9_.-]+$ || "$INSTANCE_NAME" == .* ]]; then
    printf 'HYPREXPO_DEV_INSTANCE must be [A-Za-z0-9_.-]+ and not start with a dot, got: %s\n' "$INSTANCE_NAME" >&2
    exit 2
  fi
  STATE_DIR="$CACHE_ROOT/hyprexpo/nested/$INSTANCE_NAME"
  SO="${HYPREXPO_DEV_SO:-$STATE_DIR/hyprexpo.so}"
  CONF="$STATE_DIR/hyprexpo-dev.conf"
  NESTED_RUNTIME="$HOST_RUNTIME/hyprexpo-nested/$INSTANCE_NAME"
  HEADLESS="${HYPREXPO_DEV_HEADLESS:-1}"
  NICE="${HYPREXPO_DEV_NICE:-10}"
else
  BUILD_DIR="$CACHE_ROOT/hyprexpo"
  STATE_DIR="$BUILD_DIR"
  SO="${HYPREXPO_DEV_SO:-$BUILD_DIR/hyprexpo.so}"
  CONF="$CACHE_ROOT/hyprexpo-dev.conf"
  NESTED_RUNTIME=""
  HEADLESS="${HYPREXPO_DEV_HEADLESS:-0}"
  NICE="${HYPREXPO_DEV_NICE:-0}"
fi
DEV_LAYOUT="${HYPREXPO_DEV_LAYOUT:-grid}"

# Aquamarine picks the Wayland backend from WAYLAND_DISPLAY. Without one it would take the
# DRM backend and try to become the seat's compositor - never what a sandbox should do.
if [[ -z "${WAYLAND_DISPLAY:-}" ]]; then
  echo "[run-nested] WAYLAND_DISPLAY is not set: run this from inside a Wayland session" >&2
  exit 2
fi

case "$DEV_LAYOUT" in
    grid)
        LAYOUT_BLOCK=''
        FIXTURE_BLOCK=''
        SCROLLING_INPUT_DEBUG=0
        ;;
    scrolling)
        SCROLLING_INPUT_DEBUG=1
        read -r -d '' LAYOUT_BLOCK <<'EOF' || true
general {
  layout = scrolling
  border_size = 0
  gaps_in = 8
  gaps_out = 8
}

scrolling {
  direction = right
  column_width = 0.42
  fullscreen_on_one_column = 0
  follow_focus = 0
}

# Four native direction fixtures plus one mixed-layout fallback row.
workspace = 1, layout:scrolling, layoutopt:direction:right
workspace = 2, layout:scrolling, layoutopt:direction:left
workspace = 3, layout:scrolling, layoutopt:direction:down
workspace = 4, layout:scrolling, layoutopt:direction:up
workspace = 5, layout:dwindle
EOF
        read -r -d '' FIXTURE_BLOCK <<'EOF' || true
# The first workspace settles to three columns: C+D share a column, A and B are
# dedicated column, and D remains offscreen at the default 0.42 width.
# Through `hyprctl dispatch exec`, not a plain exec-once: in a nested 0.56 session every
# exec run during startup inherits the *host's* WAYLAND_DISPLAY (the sandbox's own socket
# only reaches children after a reload), so these windows used to open on the host desktop.
exec-once = hyprctl dispatch exec "[workspace 1 silent] kitty --class hyprexpo-scroll-fixture --title HYPREXPO-SCROLL-A"
exec-once = hyprctl dispatch exec "[workspace 1 silent] kitty --class hyprexpo-scroll-fixture --title HYPREXPO-SCROLL-B"
exec-once = hyprctl dispatch exec "[workspace 1 silent] kitty --class hyprexpo-scroll-fixture --title HYPREXPO-SCROLL-C"
exec-once = hyprctl dispatch exec "[workspace 1 silent] kitty --class hyprexpo-scroll-fixture --title HYPREXPO-SCROLL-D"
exec-once = hyprctl dispatch exec "[workspace 2 silent] kitty --class hyprexpo-scroll-fixture --title HYPREXPO-SCROLL-LEFT"
exec-once = hyprctl dispatch exec "[workspace 3 silent] kitty --class hyprexpo-scroll-fixture --title HYPREXPO-SCROLL-DOWN"
exec-once = hyprctl dispatch exec "[workspace 4 silent] kitty --class hyprexpo-scroll-fixture --title HYPREXPO-SCROLL-UP"
exec-once = hyprctl dispatch exec "[workspace 5 silent] kitty --class hyprexpo-scroll-fixture --title HYPREXPO-MIXED"
exec-once = hyprctl dispatch exec "[workspace 1 silent; float] kitty --class hyprexpo-scroll-fixture --title HYPREXPO-FLOATING"
exec-once = hyprctl dispatch exec "[workspace 1 silent; float] kitty --class hyprexpo-scroll-fixture --title HYPREXPO-PINNED"
exec-once = hyprctl dispatch exec "[workspace 5 silent] kitty --class hyprexpo-scroll-fixture --title HYPREXPO-GROUP"
exec-once = hyprctl dispatch exec "[workspace 5 silent] kitty --class hyprexpo-scroll-fixture --title HYPREXPO-FULLSCREEN"
exec-once = sh -c 'sleep 2; hyprctl dispatch focuswindow title:HYPREXPO-SCROLL-D; hyprctl dispatch layoutmsg consume; hyprctl dispatch focuswindow title:HYPREXPO-PINNED; hyprctl dispatch pin; hyprctl dispatch focuswindow title:HYPREXPO-GROUP; hyprctl dispatch togglegroup; hyprctl dispatch focuswindow title:HYPREXPO-FULLSCREEN; hyprctl dispatch fullscreen 1; hyprctl dispatch workspace 1'
EOF
        ;;
    *)
        printf 'HYPREXPO_DEV_LAYOUT must be grid or scrolling, got: %s\n' "$DEV_LAYOUT" >&2
        exit 2
        ;;
esac

# A nested Wayland output advertises no preferred mode, so "preferred" resolves
# to 0x0 and Hyprland refuses to render it. Always pin an explicit mode.
DEFAULT_MODE=1280x720@60
[[ "$DEV_LAYOUT" == scrolling ]] && DEFAULT_MODE=800x600@60
MODE="${HYPREXPO_DEV_MODE:-$DEFAULT_MODE}"
MODE_W="${MODE%%x*}"

# Number of nested outputs. Set to 2+ to exercise multi-monitor behavior; each
# extra output is created at runtime and appears as its own host window (headless:
# as another headless output, HEADLESS-1..N).
OUTPUTS="${HYPREXPO_DEV_OUTPUTS:-1}"

# Pick a terminal that actually exists on this machine instead of assuming one.
TERMINAL="${HYPREXPO_DEV_TERMINAL:-}"
if [[ -z "$TERMINAL" ]]; then
  for candidate in kitty ghostty alacritty foot wezterm; do
    if command -v "$candidate" >/dev/null 2>&1; then
      TERMINAL="$candidate"
      break
    fi
  done
fi
if [[ -z "$TERMINAL" ]]; then
  echo "[run-nested] warning: no terminal found; SUPER+Return will do nothing" >&2
  TERMINAL="kitty"
fi

EXTRA_OUTPUTS=""
if [[ "$HEADLESS" == 1 ]]; then
  # The host-facing output is switched off, so the session never maps a window on the host;
  # it renders to headless outputs instead. That also keeps the frame loop alive: a nested
  # window the host is not compositing never gets a frame back, and grim then hangs.
  MONITOR_BLOCK="monitor=WAYLAND-1,disable"$'\n'"monitor=,$MODE,auto,1"
  for ((i = 1; i <= OUTPUTS; i++)); do
    EXTRA_OUTPUTS+="exec-once = hyprctl output create headless HEADLESS-$i"$'\n'
  done
else
  MONITOR_BLOCK="monitor=WAYLAND-1,$MODE,0x0,1"$'\n'"monitor=WAYLAND-2,$MODE,${MODE_W}x0,1"$'\n'"monitor=,$MODE,auto,1"
  for ((i = 2; i <= OUTPUTS; i++)); do
    EXTRA_OUTPUTS+="exec-once = hyprctl output create auto"$'\n'
  done
fi

mkdir -p "$STATE_DIR" "$(dirname "$CONF")" "$(dirname "$SO")"
# One session per state directory, held until the compositor exits (the fd survives the exec
# below). Taken before the build: rebuilding a .so another session has mapped crashes it.
exec 9>"$STATE_DIR/nested.lock"
if ! flock -n 9; then
  echo "[run-nested] a sandbox is already running from $STATE_DIR" >&2
  echo "[run-nested] stop it, or start another alongside: HYPREXPO_DEV_INSTANCE=<name> (see scripts/nested-ctl.sh)" >&2
  exit 1
fi

echo "[run-nested] Building local plugin at $SO"
nice -n "$NICE" make -C "$REPO_ROOT" all TARGET="$SO"

cat > "$CONF" <<EOF
$MONITOR_BLOCK

$EXTRA_OUTPUTS

$LAYOUT_BLOCK

debug {
  disable_logs = false
}

cursor {
  no_hardware_cursors = true
}

# load local build
plugin = $SO

plugin {
  hyprexpo {
    # layout + visuals
    columns = 3
    gaps_in = 20
    bg_col = rgb(101010)
    workspace_method = center current
    skip_empty = 0
    show_pinned_windows = 0
    scrolling_thumbnail_budget = 4
    scrolling_input_debug = $SCROLLING_INPUT_DEBUG

    # borders (hypr-style gradient, thicker to showcase)
    border_style = hyprland
    border_width = 4
    border_color_current = rgb(66ccff)
    border_color_focus   = rgb(ffcc66)

    # keyboard nav
    keynav_enable = 1
    keynav_wrap_h = 1
    keynav_wrap_v = 1
    keynav_reading_order = 0

    # labels (numbers): smaller font, rounded background bubble
    label_enable = 1
    label_font_size = 12
    label_position = bottom-right
    label_offset_x = 8
    label_offset_y = 8
    label_show = hover+focus
    label_color_default = rgb(ffffff)
    label_color_hover   = rgb(72ff7a)
    label_color_focus   = rgb(ffcc66)
    label_color_current = rgb(66ccff)
    label_scale_hover = 1.0
    label_scale_focus = 1.2
    label_bg_enable = 1
    label_bg_color = rgba(000000cc)
    label_bg_rounding = 999  # fully rounded bubble
    label_padding = 6

    # outer margin around the grid to demo spacing from screen edge
    gaps_out = 20

    # demo hyprland style gradient borders
    border_grad_current = rgba(33ccffee) rgba(00ff99ee) 45deg
    border_grad_focus   = rgba(ffdd44ee) rgba(22aaffee) 30deg
  }
}

# toggle with an unmodified function key to avoid host grabs
bind = , F10, hyprexpo:expo, toggle
bind = , F11, hyprexpo:expo, toggle all

# nested-session test controls
bind = SUPER, Return, exec, $TERMINAL
bind = SUPER, Q, killactive
bind = SUPER SHIFT, Q, exit
bind = SUPER, 1, workspace, 1
bind = SUPER, 2, workspace, 2
bind = SUPER, 3, workspace, 3
bind = SUPER, 4, workspace, 4
bind = SUPER, 5, workspace, 5
bind = SUPER, 6, workspace, 6
bind = SUPER, 7, workspace, 7
bind = SUPER, 8, workspace, 8
bind = SUPER, 9, workspace, 9
bind = SUPER SHIFT, 1, movetoworkspace, 1
bind = SUPER SHIFT, 2, movetoworkspace, 2
bind = SUPER SHIFT, 3, movetoworkspace, 3
bind = SUPER SHIFT, 4, movetoworkspace, 4
bind = SUPER SHIFT, 5, movetoworkspace, 5
bind = SUPER SHIFT, 6, movetoworkspace, 6
bind = SUPER SHIFT, 7, movetoworkspace, 7
bind = SUPER SHIFT, 8, movetoworkspace, 8
bind = SUPER SHIFT, 9, movetoworkspace, 9

# Native scrolling layout controls used by the scrolling fixture and validator.
bind = SUPER ALT, left, layoutmsg, move -200
bind = SUPER ALT, right, layoutmsg, move +200
bind = SUPER ALT, C, layoutmsg, consume
bind = SUPER ALT, E, layoutmsg, expel
bind = SUPER ALT, F, layoutmsg, fit visible

# submap for keyboard nav (the plugin auto-enters this when open)
submap = hyprexpo
  bind = , left, hyprexpo:kb_focus, left
  bind = , right, hyprexpo:kb_focus, right
  bind = , up, hyprexpo:kb_focus, up
  bind = , down, hyprexpo:kb_focus, down
  bind = , return, hyprexpo:kb_confirm
  bind = , 1, hyprexpo:kb_selectn, 1
  bind = , 2, hyprexpo:kb_selectn, 2
  bind = , 3, hyprexpo:kb_selectn, 3
  bind = , 4, hyprexpo:kb_selectn, 4
  bind = , 5, hyprexpo:kb_selectn, 5
  bind = , 6, hyprexpo:kb_selectn, 6
  bind = , 7, hyprexpo:kb_selectn, 7
  bind = , 8, hyprexpo:kb_selectn, 8
  bind = , 9, hyprexpo:kb_selectn, 9
  bind = , 0, hyprexpo:kb_selectn, 0
submap = reset

$FIXTURE_BLOCK
EOF

RUNTIME_ENV=()
if [[ -n "$NESTED_RUNTIME" ]]; then
  # A private runtime directory: the session's sockets and the plugin's
  # $XDG_RUNTIME_DIR/hyprexpo-*.log files are this instance's alone. The host display then
  # has to be named by its absolute path. Cleared first - the lock above is ours, so
  # nothing else is using it.
  [[ "$NESTED_RUNTIME" == "$HOST_RUNTIME/hyprexpo-nested/"?* ]] || exit 2
  rm -rf -- "$NESTED_RUNTIME"
  mkdir -p -m 700 "$NESTED_RUNTIME"
  HOST_DISPLAY="$WAYLAND_DISPLAY"
  [[ "$HOST_DISPLAY" == /* ]] || HOST_DISPLAY="$HOST_RUNTIME/$HOST_DISPLAY"
  RUNTIME_ENV=(XDG_RUNTIME_DIR="$NESTED_RUNTIME" WAYLAND_DISPLAY="$HOST_DISPLAY")
fi

# What nested-ctl.sh reads to find this session; the pid is the compositor's (exec below).
cat > "$STATE_DIR/instance.env" <<EOF
pid=$$
runtime=${NESTED_RUNTIME:-$HOST_RUNTIME}
headless=$HEADLESS
outputs=$OUTPUTS
layout=$DEV_LAYOUT
conf=$CONF
so=$SO
EOF

echo "[run-nested] Launching nested Hyprland with $CONF (pid $$, $( [[ "$HEADLESS" == 1 ]] && echo headless || echo 'host window'))"
# Hyprland 0.56 uses aquamarine, not wlroots: the old WLR_* variables are
# inert. Aquamarine selects its Wayland backend from WAYLAND_DISPLAY.
exec nice -n "$NICE" env "${RUNTIME_ENV[@]}" HYPRLAND_NO_LOGO=1 Hyprland -c "$CONF"
