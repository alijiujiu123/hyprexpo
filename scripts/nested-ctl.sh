#!/usr/bin/env bash
set -euo pipefail

# Start, drive and stop named nested sandboxes (scripts/run-nested.sh with
# HYPREXPO_DEV_INSTANCE=<name>), several at once, in the background.
#
#   nested-ctl.sh start <name> [--window]  build + launch detached, wait until it answers;
#                                          headless unless --window (a host window)
#   nested-ctl.sh list                     every named instance, running or not
#   nested-ctl.sh hyprctl <name> <args...> hyprctl against that sandbox only
#   nested-ctl.sh exec <name> <command>    run a client inside the sandbox
#   nested-ctl.sh shot <name> [file] [output]
#                                          screenshot (headless: grim on the sandbox's own
#                                          output, default HEADLESS-1; window: host grim)
#   nested-ctl.sh env <name>               eval-able exports for the sandbox's socket
#                                          (clients started with them open inside it)
#   nested-ctl.sh logs <name>              where its compositor and plugin logs are
#   nested-ctl.sh stop <name>|--all
#
# run-nested.sh's other knobs pass through the environment, e.g.
#   HYPREXPO_DEV_LAYOUT=scrolling HYPREXPO_DEV_OUTPUTS=2 nested-ctl.sh start b
#
# A headless instance never maps a window on the host, so it takes no focus or input from
# the person using the machine; it runs at nice 10 (HYPREXPO_DEV_NICE) so it also yields
# the CPU. It still shares the GPU.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NESTED_ROOT="${XDG_CACHE_HOME:-$HOME/.cache}/hyprexpo/nested"
START_TIMEOUT="${HYPREXPO_DEV_START_TIMEOUT:-600}"

die() { printf 'nested-ctl: %s\n' "$1" >&2; exit 1; }

usage() { sed -n '4,25p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-1}"; }

state_dir() {
  [[ "${1:-}" =~ ^[A-Za-z0-9_.-]+$ && "$1" != .* ]] || die "bad instance name: '${1:-}'"
  printf '%s/%s\n' "$NESTED_ROOT" "$1"
}

# field <name> <key>: one value from the instance.env run-nested.sh wrote.
field() {
  local file
  file="$(state_dir "$1")/instance.env"
  [[ -f "$file" ]] || return 1
  sed -n "s/^$2=//p" "$file" | head -1
}

alive() {
  local pid
  pid="$(field "$1" pid 2>/dev/null)" || return 1
  [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null && [[ "$(cat "/proc/$pid/comm" 2>/dev/null)" == Hyprland ]]
}

# The instance entry (hyprctl instances) whose pid is this sandbox's compositor.
instance_json() {
  local pid runtime
  pid="$(field "$1" pid)" && runtime="$(field "$1" runtime)" || return 1
  XDG_RUNTIME_DIR="$runtime" hyprctl instances -j 2>/dev/null |
    jq -c --argjson pid "$pid" '.[] | select(.pid == $pid)' | head -1
}

signature() {
  local sig
  alive "$1" || die "instance '$1' is not running (nested-ctl.sh start $1)"
  sig="$(instance_json "$1" | jq -r '.instance // empty')"
  [[ -n "$sig" ]] || die "instance '$1' is running but has no control socket yet"
  printf '%s\n' "$sig"
}

hc() {
  local name=$1 sig
  shift
  sig="$(signature "$name")"
  XDG_RUNTIME_DIR="$(field "$name" runtime)" hyprctl -i "$sig" "$@"
}

cmd_start() {
  local name=${1:-} headless=1 dir pid
  [[ -n "$name" ]] || usage
  [[ "${2:-}" == --window ]] && headless=0
  dir="$(state_dir "$name")"
  if alive "$name"; then
    echo "[nested-ctl] $name already running (pid $(field "$name" pid))"
    return 0
  fi
  mkdir -p "$dir"
  rm -f "$dir/instance.env"
  # Its own session, so it outlives the shell that started it and is not hit by that
  # shell's signals; the compositor keeps this pid (run-nested.sh and nice both exec).
  HYPREXPO_DEV_INSTANCE="$name" HYPREXPO_DEV_HEADLESS="${HYPREXPO_DEV_HEADLESS:-$headless}" \
    setsid "$SCRIPT_DIR/run-nested.sh" >"$dir/stdout.log" 2>&1 </dev/null &
  pid=$!
  printf '[nested-ctl] %s: building + launching (log %s)\n' "$name" "$dir/stdout.log"
  local deadline=$((SECONDS + START_TIMEOUT))
  while ((SECONDS < deadline)); do
    if ! kill -0 "$pid" 2>/dev/null; then
      tail -20 "$dir/stdout.log" >&2 || true
      die "$name exited during startup"
    fi
    if alive "$name" && hc "$name" monitors -j >/dev/null 2>&1; then
      # Headless outputs are created by exec-once; wait for the first one.
      if [[ "$(field "$name" headless)" != 1 ]] || hc "$name" monitors -j | jq -e 'any(.[]; .name | startswith("HEADLESS-"))' >/dev/null; then
        break
      fi
    fi
    sleep 0.25
  done
  ((SECONDS < deadline)) || die "$name did not come up within ${START_TIMEOUT}s (see $dir/stdout.log)"
  cmd_status_line "$name"
}

cmd_status_line() {
  local name=$1 state mode ws plugin
  if alive "$name"; then
    state="running pid=$(field "$name" pid)"
    ws="$(hc "$name" activeworkspace -j 2>/dev/null | jq -r '.id' || echo '?')"
    plugin="$(hc "$name" plugin list -j 2>/dev/null | jq -r 'map(.name + " " + .version) | join(", ")' || echo '?')"
    state+=" ws=$ws plugins=[${plugin:-none}]"
  else
    state="stopped"
  fi
  mode="$( [[ "$(field "$name" headless 2>/dev/null)" == 1 ]] && echo headless || echo window)"
  printf '%-16s %-8s layout=%-9s %s\n' "$name" "$mode" "$(field "$name" layout 2>/dev/null || echo '?')" "$state"
}

cmd_list() {
  local dir found=0
  for dir in "$NESTED_ROOT"/*/; do
    [[ -f "$dir/instance.env" ]] || continue
    found=1
    cmd_status_line "$(basename "$dir")"
  done
  ((found)) || echo "[nested-ctl] no named instances under $NESTED_ROOT"
}

cmd_exec() {
  local name=${1:-}
  shift || true
  [[ -n "$name" && $# -gt 0 ]] || usage
  # dispatch exec starts the client from the sandbox itself, with its display and signature.
  hc "$name" dispatch exec "$*"
}

cmd_shot() {
  local name=${1:-} out output geometry pid
  [[ -n "$name" ]] || usage
  out="$(realpath -m "${2:-$(state_dir "$name")/shot.png}")"
  output="${3:-HEADLESS-1}"
  alive "$name" || die "instance '$name' is not running"
  rm -f "$out"
  if [[ "$(field "$name" headless)" == 1 ]]; then
    # grim inside the sandbox, on its own output: the host session is never captured.
    # grim damages the output it copies, so an idle session still produces a frame.
    cmd_exec "$name" "grim -o $output '$out.part' && mv '$out.part' '$out'" >/dev/null
    for _ in $(seq 1 50); do
      [[ -s "$out" ]] && break
      sleep 0.1
    done
    [[ -s "$out" ]] || die "grim produced nothing (is output $output there? nested-ctl.sh hyprctl $name monitors)"
  else
    pid="$(field "$name" pid)"
    geometry="$(hyprctl clients -j 2>/dev/null |
      jq -r --argjson pid "$pid" '.[] | select(.pid == $pid) | "\(.at[0]),\(.at[1]) \(.size[0])x\(.size[1])"' | head -1)"
    [[ -n "$geometry" ]] || die "no host window found for '$name'"
    grim -g "$geometry" "$out" || die "grim failed for $geometry"
  fi
  echo "$out"
}

cmd_env() {
  local name=${1:-} json runtime
  [[ -n "$name" ]] || usage
  alive "$name" || die "instance '$name' is not running"
  json="$(instance_json "$name")"
  runtime="$(field "$name" runtime)"
  printf 'export XDG_RUNTIME_DIR=%q\n' "$runtime"
  printf 'export HYPRLAND_INSTANCE_SIGNATURE=%q\n' "$(jq -r .instance <<<"$json")"
  printf 'export WAYLAND_DISPLAY=%q\n' "$(jq -r .wl_socket <<<"$json")"
}

cmd_logs() {
  local name=${1:-} runtime sig
  [[ -n "$name" ]] || usage
  runtime="$(field "$name" runtime)" || die "no instance '$name'"
  echo "launcher:   $(state_dir "$name")/stdout.log"
  if alive "$name"; then
    sig="$(signature "$name")"
    echo "compositor: $runtime/hypr/$sig/hyprland.log"
  fi
  local f
  for f in "$runtime"/hyprexpo-*.log; do
    [[ -e "$f" ]] && echo "plugin:     $f"
  done
  return 0
}

cmd_stop() {
  local name=${1:-} pid
  [[ -n "$name" ]] || usage
  if [[ "$name" == --all ]]; then
    local dir
    for dir in "$NESTED_ROOT"/*/; do
      [[ -f "$dir/instance.env" ]] && cmd_stop "$(basename "$dir")"
    done
    return 0
  fi
  if alive "$name"; then
    pid="$(field "$name" pid)"
    hc "$name" dispatch exit >/dev/null 2>&1 || true
    for _ in $(seq 1 40); do
      alive "$name" || break
      sleep 0.25
    done
    alive "$name" && kill "$pid" 2>/dev/null || true
  fi
  echo "[nested-ctl] $name stopped"
}

case "${1:-}" in
  start) shift; cmd_start "$@" ;;
  list) cmd_list ;;
  hyprctl) shift; [[ $# -ge 2 ]] || usage; hc "$@" ;;
  exec) shift; cmd_exec "$@" ;;
  shot) shift; cmd_shot "$@" ;;
  env) shift; cmd_env "$@" ;;
  logs) shift; cmd_logs "$@" ;;
  stop) shift; cmd_stop "$@" ;;
  -h|--help) usage 0 ;;
  *) usage ;;
esac
