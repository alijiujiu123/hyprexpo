#!/usr/bin/env python3
"""Exercise multi-output swipe teardown in an existing headless hypr-sandbox.

Start it with scripts/nested-ctl.sh start <name> --outputs 3, then pass <name>.
The immediate self-destruction case is covered by make test under ASan/UBSan.
"""
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

name = sys.argv[1] if len(sys.argv) == 2 else ""
if not re.fullmatch(r"[A-Za-z0-9_-]+", name):
    sys.exit("usage: validate-swipe-lifetime.py <headless-sandbox-name>")
directory = Path(os.environ.get("XDG_CACHE_HOME", str(Path.home() / ".cache"))) / "hypr-sandbox" / name
state = dict(line.split("=", 1) for line in (directory / "instance.env").read_text().splitlines() if "=" in line)
if state.get("headless") != "1" or state.get("lang") != "lua":
    sys.exit("refusing: requires a headless Lua sandbox")
pid = state["pid"]
if pid == os.environ.get("HYPRLAND_PID"):
    sys.exit("refusing: sandbox PID is the host compositor")


def run(*args):
    result = subprocess.run(["hypr-sandbox", *args], text=True, capture_output=True, timeout=15)
    if result.returncode or result.stdout.startswith("error:"):
        raise RuntimeError(result.stdout + result.stderr)
    return result.stdout


def lua(code):
    return run("eval", name, code)


def monitors():
    return json.loads(run("hyprctl", name, "monitors", "-j"))


def wait_for(predicate):
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.05)
    raise RuntimeError("sandbox did not reach the expected state")


def sessions():
    lua("return hl.plugin.hyprexpo.sessions()")
    log = (directory / "stdout.log").read_text()
    match = re.findall(r"HYPREXPO_SESSIONS (\d+) session\(s\)", log)
    if not match:
        raise RuntimeError("missing overview session diagnostic")
    return int(match[-1])


outputs = monitors()
if len(outputs) != 3 or any(not output["name"].startswith("HEADLESS-") for output in outputs):
    sys.exit("refusing: requires exactly three headless outputs")
follower = next(output for output in outputs if output["name"] == "HEADLESS-2")
output_name = json.dumps(follower["name"])
restore = f'hl.monitor({{ output = {output_name}, disabled = false, mode = "{follower["width"]}x{follower["height"]}@60", position = "auto", scale = {follower["scale"]} }})'
held = False
try:
    for round_number in range(20):
        lua('return hl.plugin.hyprexpo.expo("off")')
        wait_for(lambda: sessions() == 0)
        lua('return hl.plugin.hyprexpo.simswipe("begin")')
        held = True
        lua('return hl.plugin.hyprexpo.simswipe("update -40 12")')
        assert sessions() == 3, "all three outputs must follow the gesture"
        if round_number % 2:
            lua(f'hl.monitor({{ output = {output_name}, disabled = true }})')
            wait_for(lambda: len(monitors()) == 2)
        lua('return hl.plugin.hyprexpo.simswipe("end")')
        held = False
        lua('return hl.plugin.hyprexpo.expo("off")')
        wait_for(lambda: sessions() == 0)
        if round_number % 2:
            lua(restore)
            wait_for(lambda: len(monitors()) == 3)
        assert f"pid={pid}" in run("status", name), "compositor must keep the same PID"
    assert not run("hyprctl", name, "configerrors").strip(), "sandbox config must remain valid"
    print(f"PASS: 20 three-output swipes, 10 follower disconnect/reconnect cycles; compositor PID {pid} unchanged")
finally:
    if held:
        lua('return hl.plugin.hyprexpo.simswipe("end")')
    lua(restore)
