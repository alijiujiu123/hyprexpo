#!/usr/bin/env python3
"""Regression against a disposable two-output Lua hypr-sandbox; never a host session."""
import json
import subprocess
import sys
import time
from pathlib import Path

name = sys.argv[1] if len(sys.argv) == 2 else None
if not name:
    raise SystemExit("usage: test-workspace-insertion.py SANDBOX")

def hc(*args):
    result = subprocess.run(["hypr-sandbox", "hyprctl", name, *args], capture_output=True, text=True, check=True)
    if result.stdout.startswith("error") or "Lua error" in result.stdout:
        raise RuntimeError(result.stdout)
    return result.stdout

def ev(code):
    result = hc("eval", code)
    assert result.strip() == "ok", result
    return result

def query(kind): return json.loads(hc("-j", kind))
def clients(): return {c["title"]: c for c in query("clients") if c["class"] == "workspace-insertion-test"}
def focus(ws): ev(f'hl.dispatch(hl.dsp.focus({{workspace="{ws}", on_current_monitor=true}}))')
def spawn(title):
    subprocess.run(["hypr-sandbox", "exec", name, f'foot --app-id=workspace-insertion-test --title={title} sh -c "sleep 600"'], check=True, capture_output=True)
    for _ in range(60):
        if title in clients(): return
        time.sleep(0.1)
    raise RuntimeError("test client failed to map: " + title)

def native(ws):
    raw = hc("repl", f'return hl.plugin.hyprexpo.scrolling_snapshot("test-{ws} workspace:{ws}")')
    state = json.loads(raw)
    return {"direction": state["direction"], "offset": state["offsetBefore"], "columns": [
        {"width": c["width"], "targets": [(t["windowStableId"],t["proportion"]) for t in c["targets"]]} for c in state["columns"]]}

def settle(): time.sleep(0.8)
def screen2(): return next(m for m in query("monitors") if m["name"] == "HEADLESS-2")["activeWorkspace"]["id"]
def check_contents(before, expected):
    after = clients()
    for title, original in before.items():
        actual = after[title]
        assert actual["address"] == original["address"], (title, "identity")
        assert actual["workspace"]["id"] == expected.get(original["workspace"]["id"], original["workspace"]["id"]), (title, "membership")
        assert actual["monitor"] == original["monitor"], (title, "monitor")
        for key in ("at", "size"):
            assert all(abs(a-b) <= 1 for a,b in zip(actual[key], original[key])), (title, key, original[key], actual[key])
        assert actual["fullscreen"] == original["fullscreen"], (title, "fullscreen")
    return after

assert len(query("monitors")) == 2, "requires two headless outputs"
assert not clients(), "use a fresh sandbox for each run"
ev('hl.dispatch(hl.dsp.focus({monitor="HEADLESS-1"}))')
ev('hl.workspace_rule({workspace="100",layout="dwindle"}); hl.workspace_rule({workspace="101",layout="dwindle"}); hl.workspace_rule({workspace="102",layout="scrolling"})')
settle()
focus(100); spawn("origin-a"); spawn("origin-b")
focus(101); spawn("tail-a"); spawn("tail-b"); spawn("tail-c")
focus(102); spawn("scroll-a"); spawn("scroll-b"); spawn("scroll-c")
ev('hl.dispatch(hl.dsp.layout("consume_or_expel prev")); hl.dispatch(hl.dsp.layout("colresize 0.65"))')
settle()
focus(100); settle()
other = screen2()
before = clients()
scroll_before = native(102)
# Exercising the actual kit resolver also tests the green light's hyprctl eval entry point.
kit = Path.home() / "Projects/omarchy-setup-kit/modules/workspaces/files/hypr/workspaces-address.lua"
ev(f'dofile({json.dumps(str(kit))})')
ev('workspaces.new()'); settle()
assert query("activeworkspace")["id"] == 101
check_contents(before, {101: 102, 102: 103})
assert screen2() == other
assert native(103) == scroll_before, "native column order/widths/row proportions/camera changed"
assert next(w for w in query("workspaces") if w["id"] == 103)["tiledLayout"] == "scrolling"
print("PASS blank insertion: position, dwindle/scrolling geometry, membership, other screen")
# The transient inserted blank must disappear when abandoned.
focus(100); settle()
assert all(w["id"] != 101 for w in query("workspaces"))
print("PASS abandoned blank is removed")
# Make the original tail fullscreen. It must remain fullscreen after another shift.
focus(102)
ev('hl.dispatch(hl.dsp.window.fullscreen({mode="fullscreen",action="set"}))')
focus(100); settle()
before = clients()
ev('workspaces.move_to_new()'); settle()
assert query("activeworkspace")["id"] == 102
for title in ("origin-a", "origin-b"):
    if clients()[title]["workspace"]["id"] == 102: selected = title
remaining = "origin-a" if selected == "origin-b" else "origin-b"
assert clients()[remaining]["workspace"]["id"] == 100
check_contents({k:v for k,v in before.items() if not k.startswith("origin-")}, {102:103, 103:104})
assert screen2() == other
print("PASS green +: selected window follows insertion; fullscreen and scrolling tail retained")
before_reload = clients()
scroll_before_reload = native(104)
hc("reload"); settle()
check_contents(before_reload, {})
assert native(104) == scroll_before_reload
assert screen2() == other
assert not hc("configerrors").strip()
print("PASS reload: carried layouts and geometry retained; no config errors")
# Ordinary IDs on a different monitor are never reused; digits and step see the final order.
result = hc("repl", 'local out={}; for _,ws in ipairs(workspaces.list("HEADLESS-1")) do out[#out+1]=ws.id end; return table.concat(out,",")').strip()
assert result == "100,102,103,104", result
print("PASS resolver final order 100,102,103,104")
