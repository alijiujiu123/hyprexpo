#!/usr/bin/env bash
# What the gesture actions mean, checked in a nested sandbox instead of by hand.
#
#   expo    opens the overview; with one open it commits the hovered card
#   cancel  closes interactively without selecting anything
#   commit  only ever commits the hovered card, and does nothing without an overview
#
# None of that is observable without an overview that is already open with a card under the
# pointer, so this script drives the real gesture path with the synthetic swipe dispatcher
# (`hyprexpo:simswipe`, hyprlang only) and reads the plugin's own state from its diagnostic
# line (`debugGeometry()`, which reports hovered/focus/opened along with the geometry).
#
# Cases, all asserted:
#   A  swipe while the overview is closed     inert: no workspace change, no overview
#   B  overview open, pointer on a card       the `expo select` dispatcher (a different code
#                                             path that commits the hovered card) and the
#                                             gesture must land on the same workspace, the
#                                             drag must report closing, and the overview must
#                                             be gone afterwards -- twice
#   C  overview open, pointer off the cards   no card is hovered, and the swipe does not switch
#   D  the same swipe, gesture_action=cancel  no workspace change
#   E  gesture_action=sideways                the swipe stays inert: the value registers nothing
#   F  opened from a non-first workspace      nothing is marked until the pointer moves: the
#      with the pointer untouched             diagnostic line must report hovered=-1 and
#                                             focus=-1, `expo select` must not switch, and a
#                                             commit swipe must not switch either
#   G  release momentum alone                 with momentum_decel > 0 a tiny brisk swipe in the
#                                             closing direction used to switch workspaces
#                                             without travelling: with commit_min_travel set it
#                                             closes without switching, while a deliberate drag
#                                             still commits
#   F2 the same on the swipe path             a gesture-opened overview starts from the zoomed
#                                             layout, so the pre-fix construction-time hit test
#                                             mapped any pointer position to tile 0: every
#                                             swipe-opened overview had the first card marked
#
# The pointer is placed with `hyprctl dispatch movecursor`, which does refresh the hover (it
# reaches the compositor's own pointer position); a real motion event is what updates it, so
# nothing here needs a host-side injector.
#
# Prerequisites: jq, python3, a terminal (kitty/ghostty/alacritty/foot/wezterm), and the
# Hyprland version this checkout targets (run-nested.sh builds the plugin against it). The
# first run pays for that build.
#
# It runs a sandbox window in this session and opens a terminal inside the sandbox. Do not
# run it while a trackpad gesture is in flight. The sandbox and its plugin build are cached in
# $XDG_CACHE_HOME/hyprexpo-gesture-check (a private path, so a dev build that happens to be
# loaded in the live session is never rewritten); delete it to build fresh.
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
skip_case() { verdict SKIP "$1 -- $2"; }

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
    commit_min_travel = 0
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
geometry() { # one debugGeometry() line for the current overview, or empty without one
    : > "$SLOG"
    hc dispatch hyprexpo:simswipe begin >/dev/null
    hc dispatch hyprexpo:simswipe end >/dev/null
    sleep 0.2
    grep -v no-overview "$SLOG" | tail -1
}
overview_open() { [[ -n "$(geometry)" ]]; }
marks() { # "hovered focus" from the diagnostic line, "-1 -1" when nothing is marked
    local line
    line="$(geometry)"
    python3 - "$line" <<'PY'
import re, sys
hovered = re.search(r"hovered=(-?\d+)", sys.argv[1]) if sys.argv[1] else None
focus = re.search(r"focus=(-?\d+)", sys.argv[1]) if sys.argv[1] else None
print(f"{hovered.group(1) if hovered else '?'} {focus.group(1) if focus else '?'}")
PY
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
move_pointer() { # move_pointer <nested x> <nested y>; movecursor refreshes the hover
    local tries=0
    while :; do
        hc dispatch movecursor "$1" "$2" >/dev/null
        sleep 0.3
        local now_x now_y
        read -r now_x now_y <<<"$(hc cursorpos | tr -d ',')"
        now_x=${now_x%%.*}; now_y=${now_y%%.*}
        [[ $now_x == "$1" && $now_y == "$2" ]] && return 0
        tries=$((tries + 1))
        if [[ $tries -ge 3 ]]; then
            printf 'the pointer did not reach %s,%s (it is at %s,%s)\n' "$1" "$2" "$now_x" "$now_y" >&2
            return 1
        fi
    done
}
CARD1_X=0; CARD1_Y=0; CARD2_X=0; CARD2_Y=0
read_cards() { # card centres from the plugin's own geometry
    local line tile_x tile_y tile_w tile_h gap
    line="$(geometry)"
    [[ -n $line ]] || return 1
    read -r tile_x tile_y tile_w tile_h gap < <(python3 - "$line" <<'PY'
import re, sys
line = sys.argv[1]
tile = re.search(r"tile=\(x([\d.]+) y([\d.]+) w([\d.]+) h([\d.]+)\)", line)
gap = re.search(r"gap=([\d.]+)", line)
print(f"{tile.group(1)} {tile.group(2)} {tile.group(3)} {tile.group(4)} {gap.group(1)}")
PY
)
    CARD1_X="$(python3 -c "print(int($tile_x + $tile_w/2))")"
    CARD1_Y="$(python3 -c "print(int($tile_y + $tile_h/2))")"
    CARD2_X="$(python3 -c "print(int($tile_x + $tile_w + $gap + $tile_w/2))")"
    CARD2_Y="$CARD1_Y"
}
hover_card() { # move off the cards first, then onto the second card
    read_cards || return 1
    move_pointer 2 2 && move_pointer "$CARD2_X" "$CARD2_Y"
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

# ---- B: overview open with a card hovered: oracle and gesture must agree ------------------
for round in 1 2; do
    open_overview
    if ! hover_card; then skip_case "B round $round: hover" "the pointer did not reach the card"; continue; fi
    oracle_ws="$(hc dispatch hyprexpo:expo select >/dev/null; sleep 1.2; active_ws)"
    if [[ $oracle_ws == 1 ]]; then
        verdict FAIL "B round $round: the oracle did not select a card other than the current one (hover did not take)"
        continue
    fi
    verdict PASS "B round $round: oracle committed the hovered card (ws $oracle_ws)"

    open_overview
    if ! hover_card; then skip_case "B round $round: hover" "the pointer did not reach the card"; continue; fi
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

# ---- C: pointer off the cards -- nothing hovered, so nothing to commit --------------------
open_overview
if move_pointer 2 2; then
    read -r hovered focus <<<"$(marks)"
    check_eq "C: no card is hovered off the cards" "-1" "$hovered"
    before="$(active_ws)"
    swipe_down 200 2
    check_eq "C: commit with no card hovered does not switch" "$before" "$(active_ws)"
else
    skip_case "C: hover" "the pointer did not move off the cards"
fi
hc dispatch hyprexpo:expo off >/dev/null; sleep 0.5

# ---- G: release momentum alone must not commit a swipe that never travelled ----------------
# The report: "three fingers down usually switches workspaces" -- with momentum_decel > 0 and
# no floor, the projected landing of a short fast brush (20 units of travel) is already far past
# the halfway threshold, so the swipe commits without the user dragging anywhere.
hc dispatch hyprexpo:expo off >/dev/null; sleep 0.5
sed -i 's/momentum_decel = 0/momentum_decel = 25/' "$CONF"
hc reload >/dev/null; sleep 0.9
check_eq "G: momentum is enabled for this case" "25" "$(option momentum_decel)"
check_eq "G: the travel floor starts disabled" "0" "$(option commit_min_travel)"

open_overview
if hover_card; then
    before="$(active_ws)"
    hc dispatch hyprexpo:simswipe begin >/dev/null
    hc dispatch hyprexpo:simswipe update 20 2 >/dev/null      # 20 units: a brush, not a drag
    sleep 0.3
    hc dispatch hyprexpo:simswipe end >/dev/null
    sleep 1.2
    after="$(active_ws)"
    if [[ $after != "$before" ]]; then
        verdict PASS "G: momentum alone switches on a 20-unit brush without a floor (ws $before -> $after) -- this is the reported behaviour"
    else
        verdict FAIL "G: the 20-unit brush did not switch even without a floor; the case is not reproducing the report"
    fi
else
    skip_case "G: hover" "the pointer did not reach the card"
fi

# the same brush with a floor: no selection, just a close
sed -i 's/commit_min_travel = 0/commit_min_travel = 100/' "$CONF"
hc reload >/dev/null; sleep 0.9
check_eq "G: the travel floor is live" "100" "$(option commit_min_travel)"
open_overview
if hover_card; then
    before="$(active_ws)"
    hc dispatch hyprexpo:simswipe begin >/dev/null
    hc dispatch hyprexpo:simswipe update 20 2 >/dev/null
    sleep 0.3
    hc dispatch hyprexpo:simswipe end >/dev/null
    sleep 1.2
    check_eq "G: a 20-unit brush no longer switches with a 100-unit floor" "$before" "$(active_ws)"
    if overview_open; then verdict FAIL "G: the below-floor swipe must close the overview"; else verdict PASS "G: the below-floor swipe closed without switching"; fi

    # and a deliberate drag still commits
    open_overview
    if hover_card; then
        oracle_ws="$(hc dispatch hyprexpo:expo select >/dev/null; sleep 1.2; active_ws)"
        open_overview
        hover_card || skip_case "G: hover" "the pointer did not reach the card"
        hc dispatch hyprexpo:simswipe begin >/dev/null
        hc dispatch hyprexpo:simswipe update 200 2 >/dev/null   # 400 units, well past the floor
        sleep 0.3
        hc dispatch hyprexpo:simswipe end >/dev/null
        sleep 1.2
        check_eq "G: a deliberate drag still commits past the floor" "$oracle_ws" "$(active_ws)"
    fi
else
    skip_case "G: hover" "the pointer did not reach the card"
fi

# back to the neutral setup for the remaining cases
sed -i 's/commit_min_travel = 100/commit_min_travel = 0/' "$CONF"
sed -i 's/momentum_decel = 25/momentum_decel = 0/' "$CONF"
hc reload >/dev/null; sleep 0.9
hc dispatch hyprexpo:expo off >/dev/null; sleep 0.4

# ---- F: nothing is marked before the pointer moves ----------------------------------------
# Opened from workspace 2 with the pointer untouched: the marks must stay unset (they used to be
# pre-set at construction, with the hit test running against the opening animation's layout,
# which put the pointer inside tile 0 -- so every overview opened with the first card hovered).
hc dispatch workspace 2 >/dev/null; sleep 0.6
move_pointer 2 2 || skip_case "F: pointer" "the pointer did not move off the cards"
hc dispatch hyprexpo:expo on >/dev/null; sleep 1.2
read -r hovered focus <<<"$(marks)"
check_eq "F: nothing is hovered before the pointer moves" "-1" "$hovered"
check_eq "F: nothing is keyboard-focused before a key is pressed" "-1" "$focus"
before="$(active_ws)"
hc dispatch hyprexpo:expo select >/dev/null; sleep 1.2
check_eq "F: select with an untouched pointer does not switch" "$before" "$(active_ws)"
hc dispatch hyprexpo:expo off >/dev/null; sleep 0.5
hc dispatch hyprexpo:expo on >/dev/null; sleep 1.2
before="$(active_ws)"
swipe_down 200 2
check_eq "F: commit with an untouched pointer does not switch" "$before" "$(active_ws)"
if overview_open; then verdict FAIL "F: a commit swipe still closes the overview"; else verdict PASS "F: the overview closed without switching"; fi
hc dispatch workspace 1 >/dev/null; sleep 0.4

# ---- D: the same swipe under the cancel action must not switch ---------------------------
hc dispatch hyprexpo:expo off >/dev/null; sleep 0.5
sed -i 's/gesture_action = commit/gesture_action = cancel/' "$CONF"
hc reload >/dev/null; sleep 0.9
check_eq "config key gesture_action follows the config" "cancel" "$(option gesture_action)"
open_overview
hover_card || skip_case "D: hover" "the pointer did not reach the card"
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

# ---- F2: the same claims on the swipe path (the one the report came from) ------------------
# Registered up = expo for this phase, i.e. the configuration a real session uses to open the
# overview with three fingers.
hc dispatch hyprexpo:expo off >/dev/null; sleep 0.5
sed -i 's/gesture_direction = .*/gesture_direction = up/' "$CONF"
sed -i 's/gesture_action = .*/gesture_action = expo/' "$CONF"
hc reload >/dev/null; sleep 0.9
check_eq "F2: the up direction is registered as expo" "up" "$(option gesture_direction)"
check_eq "F2: gesture_action is expo for the swipe path" "expo" "$(option gesture_action)"
hc dispatch workspace 1 >/dev/null; sleep 0.4
if move_pointer 2 2; then
    hc dispatch hyprexpo:simswipe begin >/dev/null
    hc dispatch hyprexpo:simswipe update -50 20 >/dev/null    # fingers up, well past full open
    hc dispatch hyprexpo:simswipe end >/dev/null
    sleep 1.2
    if overview_open; then verdict PASS "F2: an up swipe with an untouched pointer opened the overview"; else verdict FAIL "F2: an up swipe did not open the overview"; fi
    read -r hovered focus <<<"$(marks)"
    check_eq "F2: nothing is hovered after a swipe-open with an untouched pointer" "-1" "$hovered"
    check_eq "F2: nothing is keyboard-focused after a swipe-open" "-1" "$focus"
    before="$(active_ws)"
    hc dispatch hyprexpo:expo select >/dev/null; sleep 1.2
    check_eq "F2: select after a swipe-open does not switch" "$before" "$(active_ws)"
else
    skip_case "F2: hover" "the pointer did not move off the cards"
fi

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
