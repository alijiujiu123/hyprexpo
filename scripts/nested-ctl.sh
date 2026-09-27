#!/usr/bin/env bash
set -euo pipefail

# hyprexpo's entry into the kit's hypr-sandbox: parallel, headless, background sandboxes that
# load every plugin this machine runs, with hyprexpo built from this checkout.
#
#   nested-ctl.sh start <name> [--fixture] [hypr-sandbox options...]
#       default:   the live Lua config (the check that matters before a live load)
#       --fixture: run-nested.sh's hyprlang fixture instead (F10 binds, labels, keynav;
#                  HYPREXPO_DEV_LAYOUT=scrolling for the scrolling rows) - hyprlang is also
#                  where the hyprexpo:* dispatchers work
#   nested-ctl.sh <anything else>   passed to hypr-sandbox (list, hyprctl, eval, exec, shot,
#                                   env, logs, restart, stop)
#
# hypr-sandbox is installed by omarchy-setup-kit (modules/tools); see `hypr-sandbox --help`.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SANDBOX="${HYPR_SANDBOX:-$(command -v hypr-sandbox || true)}"
[[ -x "$SANDBOX" ]] || { echo "nested-ctl: hypr-sandbox not found (omarchy-setup-kit: ./install.sh --only tools)" >&2; exit 1; }

if [[ "${1:-}" != start ]]; then
  exec "$SANDBOX" "$@"
fi
shift
name=${1:?usage: nested-ctl.sh start <name> [--fixture] [hypr-sandbox options...]}
shift
args=()
fixture=0
for arg in "$@"; do
  if [[ "$arg" == --fixture ]]; then fixture=1; else args+=("$arg"); fi
done
if ((fixture)); then
  conf="${XDG_CACHE_HOME:-$HOME/.cache}/hyprexpo/fixture-$name.conf"
  mkdir -p "$(dirname "$conf")"
  "$SCRIPT_DIR/run-nested.sh" --print-config >"$conf"
  args+=(--config "$conf")
fi
exec "$SANDBOX" start "$name" --dev "hyprexpo=$REPO_ROOT" "${args[@]}"
