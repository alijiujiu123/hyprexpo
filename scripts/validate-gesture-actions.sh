#!/usr/bin/env bash
# What the gesture actions mean, checked in a nested sandbox instead of by hand.
#
#   expo    opens the overview; with one open it commits the hovered card
#   cancel  closes interactively without selecting anything
#   commit  only ever commits the hovered card, and does nothing without an overview
#
# The interesting claims are the last two lines of that table, and neither is observable
# without an overview that is already open with a card under the pointer, so this script
# drives the real gesture path with the synthetic swipe dispatcher (`hyprexpo:simswipe`,
# hyprlang only) and places the pointer with `vptr` (real motion events -- a warp or a
# zero-distance move does not refresh hover).
#
# Cases, all asserted:
#   A  swipe while the overview is closed     inert: no workspace change, no overview
#   B  overview open, pointer on a card       the `expo select` dispatcher (a different code
#                                             path that commits the hovered card) and the
#                                             gesture must land on the same workspace, the
#                                             drag must report closing, and the overview must
#                                             be gone afterwards -- twice
#   C  overview open, pointer off the cards   no workspace change
#   D  the same swipe, gesture_action=cancel  no workspace change
#   E  gesture_action=sideways                the swipe stays inert: the value registers
#                                             nothing
#
# Prerequisites: jq, python3, a terminal (kitty/ghostty/alacritty/foot/wezterm), and the
# Hyprland version this checkout targets (run-nested.sh builds the plugin against it). The
# first run pays for that build.
#
# `vptr` (from omarchy-setup-kit's tools module) is needed for the hover cases; without it,
# or when the nested surface is narrower than the card it would have to reach (the host
# tiles the sandbox window, so the surface can be smaller than the scene the nested
# compositor believes it has), B and C are skipped -- and by default that fails the run, so
# a CI machine cannot pass by skipping. Pass --allow-skips to accept a degraded run.
#
# It runs a sandbox window in this session, moves the pointer, and opens a terminal inside
# the sandbox. Do not run it while a trackpad gesture is in flight. The sandbox and its plugin
# build are cached in $XDG_CACHE_HOME/hyprexpo-gesture-check (a private path, so a dev build
# that happens to be loaded in the live session is never rewritten); delete it to build fresh.
#
# Usage: scripts/validate-gesture-actions.sh [--allow-skips] [--keep]
#   HYPREXPO_DEV_SO=<path>   plugin to test (default: run-nested.sh's dev build)
set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

ALLOW_SKIPS=false
KEEP=false
for arg in "$@"; do
    case "$arg" in
        --allow-skips) ALLOW_SKIPS=true ;;
        --keep) KEEP=true ;;
        *) printf 'unknown argument: %s\n' "$arg" >&2; exit 2 ;;
    esac
done

for command in jq python3 Hyprland hyprctl; do
    command -v "$command" >/dev/null || { printf 'FAIL: %s is required\n' "$command" >&2; exit 1; }
done
TERMINAL="${HYPREXPO_DEV_TERMINAL:-}"
if [[ -z $TERMINAL ]]; then
    for candidate in kitty ghostty alacritty foot wezterm; do
        command -v "$candidate" >/dev/null && { TERMINAL="$candidate"; break; }
    done
fi
[[ -n $TERMINAL ]] || { printf 'FAIL: no terminal found to keep a workspace alive\n' >&2; exit 1; }
HAVE_VPTR=false
command -v vptr >/dev/null && HAVE_VPTR=true

EVID="$(mktemp -d "${TMPDIR:-/tmp}/hyprexpo-gestures-XXXXXX")"
# A private, stable cache: run-nested.sh builds the plugin into $XDG_CACHE_HOME/hyprexpo, and
# rebuilding the standard dev path while it is loaded in a live session is what makes the
# compositor execute from a rewritten file. Keeping it here reuses the build between runs and
# never touches a .so that could be mapped.
CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/hyprexpo-gesture-check"
RESULTS="$EVID/results.txt"
: > "$RESULTS"
RT="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
SLOG="$RT/hyprexpo-sim.log"
CONF="$CACHE/hyprexpo-dev.conf"
INSTANCE=''; NESTED_PID=''; CLIENT_PID=''
FAILURES=0; SKIPS=0

hc() { env HYPRLAND_INSTANCE_SIGNATURE="$INSTANCE" hyprctl "$@"; }
verdict() { # verdict <PASS|FAIL|SKIP> <description>
    printf '%-4s %s\n' "$1" "$2" | tee -a "$RESULTS"
    [[ $1 == FAIL ]] && FAILURES=$((FAILURES + 1))
    [[ $1 == SKIP ]] && SKIPS=$((SKIPS + 1))
    return 0
}
check_eq() { # check_eq <description> <expected> <actual>
    if [[ $2 == "$3" ]]; then verdict PASS "$1 (=$3)"; else verdict FAIL "$1: expected '$2', got '$3'"; fi
}
skip_hover() { verdict SKIP "$1 -- $2"; }

cleanup() {
    rc=$?
    set +e
    [[ -n $INSTANCE ]] && hc dispatch hyprexpo:simswipe end >/dev/null 2>&1
    [[ -n $CLIENT_PID ]] && kill "$CLIENT_PID" 2>/dev/null
    [[ -n $NESTED_PID ]] && kill "$NESTED_PID" 2>/dev/null
    if [[ $KEEP == true ]]; then printf 'evidence kept in %s\n' "$EVID"; else rm -rf "$EVID"; fi
    exit $rc
}
trap cleanup EXIT INT TERM

# ---- sandbox ------------------------------------------------------------------------------
mkdir -p "$CACHE"
cd "$REPO_ROOT"
XDG_CACHE_HOME="$CACHE" ./scripts/run-nested.sh >"$EVID/nested-stdout.log" 2>&1 &
NESTED_PID=$!
# Wait until run-nested.sh has *finished* writing the config: it writes it with one redirect,
# so appending while that write is still in flight loses the block (cat keeps writing at its
# own offset). The last line of its generated config is the submap reset.
for _ in $(seq 1 900); do
    kill -0 "$NESTED_PID" 2>/dev/null || { printf 'FAIL: the sandbox exited before it was configured; see %s\n' "$EVID/nested-stdout.log" >&2; exit 1; }
    grep -q '^submap = reset' "$CONF" 2>/dev/null && break
    sleep 0.1
done
grep -q '^submap = reset' "$CONF" 2>/dev/null || { printf 'FAIL: the sandbox never finished writing %s\n' "$CONF" >&2; exit 1; }


for _ in $(seq 1 900); do
    kill -0 "$NESTED_PID" 2>/dev/null || { printf 'FAIL: the sandbox died while starting up; see %s\n' "$EVID/nested-stdout.log" >&2; exit 1; }
    INSTANCE="$(hyprctl instances -j 2>/dev/null | jq -r --argjson pid "$NESTED_PID" '.[] | select(.pid == $pid) | .instance' || true)"
    [[ -n $INSTANCE ]] && break
    sleep 0.25
done
[[ -n $INSTANCE ]] || { printf 'FAIL: the nested instance was not discoverable\n' >&2; exit 1; }
for _ in $(seq 1 240); do
    kill -0 "$NESTED_PID" 2>/dev/null || { printf 'FAIL: the sandbox died before its control socket came up\n' >&2; exit 1; }
    hc monitors -j >/dev/null 2>&1 && break
    sleep 0.25
done
# Append the keys only once the sandbox is up: while run-nested.sh is still writing the config,
# an append lands under the writer's file offset and is overwritten. A reload after the append
# is what makes them live (it re-reads the same file the instance was started with).
# A down swipe drives the closing direction; no release momentum keeps the landing a pure
# position decision; the close animation is slowed down so the mid-drag state is observable.
cat >> "$CONF" <<'EOF'

plugin {
  hyprexpo {
    gesture_fingers = 3
    gesture_direction = down
    gesture_action = commit
    momentum_decel = 0
    dirty_debug = 0
    overview_anim_speed = 8
  }
}
EOF

RELOAD_OUT=''
for _ in 1 2 3; do
    RELOAD_OUT="$(hc reload 2>&1)"
    sleep 1
    [[ "$(hc getoption plugin:hyprexpo:gesture_action | head -1 | awk '{print $NF}')" == commit ]] && break
done

# ---- helpers ------------------------------------------------------------------------------
active_ws() { hc activeworkspace -j | jq -r .id; }
option() { hc getoption "plugin:hyprexpo:$1" | head -1 | awk '{print $NF}'; }
overview_open() { # a begin logs the geometry; "no-overview" means there is none
    : > "$SLOG"
    hc dispatch hyprexpo:simswipe begin >/dev/null
    hc dispatch hyprexpo:simswipe end >/dev/null
    sleep 0.2
    ! grep -q no-overview "$SLOG"
}
swipe_down() { # swipe_down <units> <events>  (the first event is consumed by the gesture)
    hc dispatch hyprexpo:simswipe begin >/dev/null
    hc dispatch hyprexpo:simswipe update "$1" "$2" >/dev/null
    sleep 0.3
    hc dispatch hyprexpo:simswipe end >/dev/null
    sleep 1.2
}
open_overview() {
    hc dispatch workspace 1 >/dev/null; sleep 0.4
    hc dispatch hyprexpo:expo on >/dev/null; sleep 1.2
}
nested_window() { # the sandbox window in this session: host position and real surface size
    hyprctl -j clients | jq -r --argjson pid "$NESTED_PID" '.[] | select(.pid == $pid) | "\(.at[0]) \(.at[1]) \(.size[0]) \(.size[1])"' | head -1
}
# Lands the pointer anywhere inside a box, not on an exact pixel: which card it ends up on is
# decided by the oracle, and asking for an exact point fails whenever the host screen edge
# clamps the motion. The move has to be nonzero (a zero delta produces no motion event).
point_inside() { # point_inside <x0> <x1> <y0> <y1>
    local mid_x=$(( ($1 + $2) / 2 )) mid_y=$(( ($3 + $4) / 2 )) tries=0
    while :; do
        local win_x win_y surf_w surf_h cur_x cur_y new_x new_y
        read -r win_x win_y surf_w surf_h <<<"$(nested_window)"
        read -r cur_x cur_y <<<"$(hyprctl cursorpos | tr -d ',')"
        vptr move "$((win_x + mid_x - cur_x))" "$((win_y + mid_y - cur_y))" 10 6 >/dev/null 2>&1 || true
        sleep 0.3
        read -r new_x new_y <<<"$(hc cursorpos | tr -d ',')"
        new_x=${new_x%%.*}; new_y=${new_y%%.*}
        if (( new_x >= $1 && new_x <= $2 && new_y >= $3 && new_y <= $4 )); then
            return 0
        fi
        tries=$((tries + 1))
        if [[ $tries -ge 5 ]]; then
            printf 'pointer did not reach the card: box %s..%s x %s..%s, nested pointer %s,%s, sandbox window %s,%s surface %sx%s, host pointer %s\n' \
                "$1" "$2" "$3" "$4" "$new_x" "$new_y" "$win_x" "$win_y" "$surf_w" "$surf_h" "$(hyprctl cursorpos | tr -d ',')" >&2
            return 1
        fi
    done
}
CARD1_BOX=''; CARD2_BOX=''; SURFACE_W=0
read_cards() { # card centres from the plugin's own geometry (the layout follows the surface)
    : > "$SLOG"
    hc dispatch hyprexpo:simswipe begin >/dev/null
    hc dispatch hyprexpo:simswipe end >/dev/null
    sleep 0.2
    local geo tile_x tile_y tile_w tile_h gap
    geo="$(grep -v no-overview "$SLOG" | tail -1)"
    read -r tile_x tile_y tile_w tile_h gap < <(python3 - "$geo" <<'PY'
import re, sys
line = sys.argv[1]
tile = re.search(r"tile=\(x([\d.]+) y([\d.]+) w([\d.]+) h([\d.]+)\)", line)
gap = re.search(r"gap=([\d.]+)", line)
print(f"{tile.group(1)} {tile.group(2)} {tile.group(3)} {tile.group(4)} {gap.group(1)}")
PY
)
    CARD1_BOX="$(python3 -c "print(int($tile_x), int($tile_x + $tile_w), int($tile_y), int($tile_y + $tile_h))")"
    CARD2_BOX="$(python3 -c "print(int($tile_x + $tile_w + $gap), int($tile_x + 2*$tile_w + $gap), int($tile_y), int($tile_y + $tile_h))")"
    SURFACE_W="$(nested_window | awk '{print $3}')"
    CARD2_X="${CARD2_BOX%% *}"
}
# Two nonzero moves: card 1 first, then card 2, so the hover is rebuilt from motion events.
hover_card() { # two nonzero moves, so hover is rebuilt from real motion events
    read_cards
    point_inside $CARD1_BOX && point_inside $CARD2_BOX
}

# ---- fixture ------------------------------------------------------------------------------
# A client on workspace 2, so the workspace exists and the grid has a second card to hover.
hc dispatch workspace 1 >/dev/null
hc dispatch exec "[workspace 2 silent] $TERMINAL" >/dev/null
for _ in $(seq 1 150); do
    CLIENT_PID="$(hc clients -j | jq -r '[.[] | select(.workspace.id == 2)] | first | .pid // empty' 2>/dev/null)"
    [[ -n $CLIENT_PID ]] && break
    sleep 0.2
done
[[ -n $CLIENT_PID ]] || { printf 'FAIL: no client mapped on workspace 2\n' >&2; exit 1; }
sleep 0.8
printf 'instance=%s pid=%s client=%s plugin=%s\n' "$INSTANCE" "$NESTED_PID" "$CLIENT_PID" \
    "$(hc plugin list | grep -A1 'Plugin hyprexpo' | tail -1 | awk '{print $2}')" > "$EVID/setup.txt"
cat "$EVID/setup.txt"

{
    printf 'config: %s (present: %s)\n' "$CONF" "$([[ -s $CONF ]] && echo yes || echo no)"
    printf 'config tail:\n'; tail -12 "$CONF"
    printf 'reload output: %s\n' "$RELOAD_OUT"
    printf 'gesture_fingers=%s gesture_direction=%s gesture_action=%s\n' \
        "$(option gesture_fingers)" "$(option gesture_direction)" "$(option gesture_action)"
} > "$EVID/config.txt" 2>&1

check_eq "config key gesture_action is live" "commit" "$(option gesture_action)"

# ---- A: the closing direction is inert while the overview is closed ----------------------
before="$(active_ws)"
swipe_down 200 2
check_eq "A: swipe with no overview does not switch" "$before" "$(active_ws)"
if overview_open; then verdict FAIL "A: swipe with no overview must not create one"; else verdict PASS "A: no overview was created"; fi

# ---- B/C: hover cases (need vptr and a card that fits in the real surface) ---------------
open_overview
read_cards
HOVER_OK=true
if [[ $HAVE_VPTR != true ]]; then
    HOVER_OK=false
    skip_hover "B/C: hover" "vptr is not installed (real pointer motion is required)"
elif (( CARD2_X > SURFACE_W )); then
    HOVER_OK=false
    skip_hover "B/C: hover" "card centre x=$CARD2_X is outside the ${SURFACE_W}px nested surface"
fi
hc dispatch hyprexpo:expo off >/dev/null; sleep 0.5

if [[ $HOVER_OK == true ]]; then
    for round in 1 2; do
        # the oracle: the `expo select` dispatcher commits the hovered card by another path
        open_overview
        if ! hover_card; then
            skip_hover "B round $round: hover" "the pointer did not settle on the card"
            continue
        fi
        oracle_ws="$(hc dispatch hyprexpo:expo select >/dev/null; sleep 1.2; active_ws)"
        if [[ $oracle_ws == 1 ]]; then
            verdict FAIL "B round $round: the oracle did not select a card other than the current one (hover did not take)"
            continue
        fi
        verdict PASS "B round $round: oracle committed the hovered card (ws $oracle_ws)"

        # the gesture, from the same pointer
        open_overview
        if ! hover_card; then
            skip_hover "B round $round: hover" "the pointer did not settle on the card"
            continue
        fi
        : > "$SLOG"
        hc dispatch hyprexpo:simswipe begin >/dev/null
        hc dispatch hyprexpo:simswipe update 200 2 >/dev/null
        sleep 0.3
        if grep -q 'closing=1' "$SLOG"; then verdict PASS "B round $round: the drag reports closing"; else verdict FAIL "B round $round: the drag did not enter the closing state"; fi
        hc dispatch hyprexpo:simswipe end >/dev/null
        sleep 1.2
        check_eq "B round $round: the commit gesture lands where the oracle did" "$oracle_ws" "$(active_ws)"
        if overview_open; then verdict FAIL "B round $round: the overview must be gone after a commit"; else verdict PASS "B round $round: the overview closed"; fi
    done

    # C: pointer off the cards -- nothing to select, so the overview just closes
    open_overview
    point_inside 0 4 0 4 || skip_hover "C: hover" "the pointer did not settle off the cards"
    before="$(active_ws)"
    swipe_down 200 2
    check_eq "C: commit with the pointer off the cards does not switch" "$before" "$(active_ws)"
fi

# ---- D: the same swipe under the cancel action must not switch ---------------------------
hc dispatch hyprexpo:expo off >/dev/null; sleep 0.5
sed -i 's/gesture_action = commit/gesture_action = cancel/' "$CONF"
hc reload >/dev/null; sleep 0.9
check_eq "config key gesture_action follows the config" "cancel" "$(option gesture_action)"
open_overview
if [[ $HOVER_OK == true ]]; then hover_card || skip_hover "D: hover" "the pointer did not settle on the card"; fi
before="$(active_ws)"
swipe_down 200 2
check_eq "D: cancel does not commit the hovered card" "$before" "$(active_ws)"

# ---- E: an unknown action registers nothing ----------------------------------------------
hc dispatch hyprexpo:expo off >/dev/null; sleep 0.5
sed -i 's/gesture_action = cancel/gesture_action = sideways/' "$CONF"
hc reload >/dev/null; sleep 0.9
before="$(active_ws)"
swipe_down 200 2
check_eq "E: an invalid action leaves the swipe inert" "$before" "$(active_ws)"
if overview_open; then verdict FAIL "E: an invalid action must register nothing"; else verdict PASS "E: no overview appeared"; fi

# ---- verdict ------------------------------------------------------------------------------
printf '\n%s\n' "----- $(basename "$0") -----"
cat "$RESULTS"
if (( FAILURES > 0 )); then
    printf '%d check(s) failed\n' "$FAILURES" >&2
    exit 1
fi
if (( SKIPS > 0 )) && [[ $ALLOW_SKIPS != true ]]; then
    printf '%d check(s) skipped; rerun with --allow-skips to accept a degraded run\n' "$SKIPS" >&2
    exit 1
fi
printf 'all gesture-action checks passed\n'
