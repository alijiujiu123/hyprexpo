#!/usr/bin/env bash
# Show what plugin:hyprexpo:dirty_refresh does, with your own eyes.
#
# A grid tile is a snapshot taken when the overview opens. With dirty_refresh the plugin
# watches window commits: a workspace whose clients keep producing frames while they are
# hidden (a player on its own clock, a page with a running timer) gets its tile recaptured,
# and a workspace that is quiet is not rendered at all. This script puts a self-driving
# video on a hidden workspace so the difference is visible: its tile keeps playing while
# every other tile is frozen.
#
# The keys only exist in a build from this tree (the hyprpm checkout predates them):
#   hyprctl plugin unload /var/cache/hyprpm/$USER/hyprexpo/hyprexpo.so
#   make dev-build && hyprctl plugin load ~/.cache/hyprexpo/hyprexpo.so && hyprctl reload
#
# Usage: scripts/dirty-preview-demo.sh [status|start|stop|on|off|log]
#   WS=<workspace>  workspace to park the demo player on (default 3)
set -euo pipefail

WS="${WS:-3}"
VIDEO="${VIDEO:-/tmp/hyprexpo-dirty-demo.mp4}"
DEV_SO="${DEV_SO:-$HOME/.cache/hyprexpo/hyprexpo.so}"
INSTALLED_SO="${INSTALLED_SO:-/var/cache/hyprpm/$USER/hyprexpo/hyprexpo.so}"
LOG_FILE="${XDG_RUNTIME_DIR:-/tmp}/hyprexpo-dirty.log"

opt() {
    hyprctl -j getoption "plugin:hyprexpo:$1" 2>/dev/null | jq -r '.int' 2>/dev/null
}

loaded_version() {
    hyprctl plugin list | awk '/^Plugin hyprexpo/{seen = 1} seen && /Version:/{print $2; exit}'
}

require_dirty_keys() {
    local value
    value="$(opt dirty_refresh)"
    [ -n "${value}" ] && [ "${value}" != "null" ] && return 0

    printf 'The loaded hyprexpo has no dirty_refresh key (version %s).\n' "$(loaded_version)" >&2
    printf 'Load the build from this tree first:\n' >&2
    printf '  hyprctl plugin unload %s\n' "$INSTALLED_SO" >&2
    printf '  make dev-build && hyprctl plugin load %s && hyprctl reload\n' "$DEV_SO" >&2
    exit 1
}

cmd_status() {
    printf 'loaded hyprexpo: %s\n' "$(loaded_version)"
    if [ -z "$(opt dirty_refresh)" ] || [ "$(opt dirty_refresh)" = "null" ]; then
        printf 'dirty_refresh:   not present in this build\n'
        return 0
    fi
    printf 'dirty_refresh:        %s\n' "$(opt dirty_refresh)"
    printf 'dirty_cooldown_ms:    %s   (per-tile rate = 1000/this, fps)\n' "$(opt dirty_cooldown_ms)"
    printf 'dirty_max_per_frame:  %s\n' "$(opt dirty_max_per_frame)"
    printf 'dirty_max_per_second: %s   (shared by every tile; 0 = unlimited)\n' "$(opt dirty_max_per_second)"
    printf 'dirty_debug:          %s\n' "$(opt dirty_debug)"
    if pgrep -f "$VIDEO" >/dev/null; then
        printf 'demo player:          running on workspace %s\n' "$WS"
    else
        printf 'demo player:          not running\n'
    fi
}

cmd_start() {
    require_dirty_keys

    if [ ! -s "$VIDEO" ]; then
        command -v ffmpeg >/dev/null || { printf 'ffmpeg is required to generate %s\n' "$VIDEO" >&2; exit 1; }
        ffmpeg -y -f lavfi -i "testsrc=size=800x450:rate=30" -t 20 -pix_fmt yuv420p "$VIDEO" >/dev/null 2>&1
    fi

    pgrep -f "$VIDEO" >/dev/null && { printf 'demo player already running\n'; return 0; }

    command -v mpv >/dev/null || { printf 'mpv is required for the demo player\n' >&2; exit 1; }
    hyprctl dispatch "hl.dsp.exec_cmd(\"mpv --loop --no-audio --really-quiet $VIDEO\", { workspace = \"$WS silent\", no_focus = true, float = true, size = \"800 450\", move = \"300 250\" })" >/dev/null
    sleep 1

    printf 'demo player parked on workspace %s.\n' "$WS"
    printf 'Open the overview (three fingers up, or CTRL+UP) and watch tile %s: it keeps\n' "$WS"
    printf 'playing. Run `%s off` and open it again: that tile is frozen instead.\n' "$0"
}

cmd_stop() {
    pkill -f "$VIDEO" >/dev/null 2>&1 || true
    printf 'demo player stopped\n'
}

cmd_toggle() {
    require_dirty_keys
    hyprctl eval "hl.config({ plugin = { hyprexpo = { dirty_refresh = $1 } } })" >/dev/null
    printf 'dirty_refresh = %s\n' "$(opt dirty_refresh)"
}

# set <key> <value>: change one knob at runtime (until the next hyprctl reload).
cmd_set() {
    require_dirty_keys
    [ $# -eq 2 ] || { printf 'usage: %s set <dirty_* key> <value>\n' "$0" >&2; exit 2; }

    case "$1" in
        dirty_*) ;;
        *) printf 'only plugin:hyprexpo:dirty_* keys can be set from here\n' >&2; exit 2 ;;
    esac

    hyprctl eval "hl.config({ plugin = { hyprexpo = { $1 = $2 } } })" >/dev/null
    printf '%s = %s\n' "$1" "$(opt "$1")"
}

# rate <fps>: per-tile update rate (0 = every frame the client produces).
cmd_rate() {
    require_dirty_keys
    [ $# -eq 1 ] || { printf 'usage: %s rate <fps>   (0 = unlimited)\n' "$0" >&2; exit 2; }

    local cooldown
    if [ "$1" = "0" ]; then
        cooldown=0
    else
        cooldown=$((1000 / $1))
    fi

    hyprctl eval "hl.config({ plugin = { hyprexpo = { dirty_cooldown_ms = ${cooldown}, dirty_max_per_second = 0 } } })" >/dev/null
    printf 'per-tile rate: dirty_cooldown_ms = %s (~%s fps), dirty_max_per_second = 0 (unlimited)\n' "$(opt dirty_cooldown_ms)" "$1"
    printf 'With several tiles moving at once the shared cap is what protects the GPU; see `%s status`.\n' "$0"
}

cmd_log() {
    require_dirty_keys
    hyprctl eval 'hl.config({ plugin = { hyprexpo = { dirty_debug = 1 } } })' >/dev/null
    printf 'dirty_debug = 1; counters go to %s (commits / recaptures / pending tiles / per workspace)\n' "$LOG_FILE"
    printf 'Leave that running, open the overview, and the per_ws field names the workspaces that are producing frames.\n'
    while :; do
        sleep 1
        [ -s "$LOG_FILE" ] && tail -n 1 "$LOG_FILE"
    done
}

case "${1:-status}" in
    status) cmd_status ;;
    start) cmd_start ;;
    stop) cmd_stop ;;
    on) cmd_toggle 1 ;;
    off) cmd_toggle 0 ;;
    set) shift; cmd_set "$@" ;;
    rate) shift; cmd_rate "$@" ;;
    log) cmd_log ;;
    *)
        printf 'usage: %s [status|start|stop|on|off|set <key> <value>|rate <fps>|log]\n' "$0" >&2
        exit 2
        ;;
esac
